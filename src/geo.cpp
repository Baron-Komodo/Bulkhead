#include "geo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace sea {
namespace {

constexpr double kPi = 3.14159265358979323846;

double toRadians(double degrees) { return degrees * kPi / 180.0; }
double toDegrees(double radians) { return radians * 180.0 / kPi; }
double clampd(double value, double lo, double hi) { return std::min(std::max(value, lo), hi); }

std::array<double, 3> toVector(double lat, double lng) {
  const double phi = toRadians(lat);
  const double lambda = toRadians(lng);
  const double cosPhi = std::cos(phi);
  return {cosPhi * std::cos(lambda), cosPhi * std::sin(lambda), std::sin(phi)};
}

LatLng fromVector(const std::array<double, 3>& v) {
  const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  const double l = length > 0 ? length : 1.0;
  return {toDegrees(std::asin(clampd(v[2] / l, -1.0, 1.0))),
          toDegrees(std::atan2(v[1] / l, v[0] / l))};
}

/// Spherical linear interpolation between two unit vectors. Returns false when the two
/// points coincide or are antipodal, where the interpolated path is undefined.
bool slerpVector(const std::array<double, 3>& from, const std::array<double, 3>& to,
                 double ratio, std::array<double, 3>& out) {
  const double dot = clampd(from[0] * to[0] + from[1] * to[1] + from[2] * to[2], -1.0, 1.0);
  const double omega = std::acos(dot);
  const double sinOmega = std::sin(omega);

  if (!std::isfinite(omega) || std::fabs(sinOmega) < 1e-9) return false;

  const double fromWeight = std::sin((1 - ratio) * omega) / sinOmega;
  const double toWeight = std::sin(ratio * omega) / sinOmega;

  out = {from[0] * fromWeight + to[0] * toWeight,
         from[1] * fromWeight + to[1] * toWeight,
         from[2] * fromWeight + to[2] * toWeight};
  return true;
}

}  // namespace

double parseCoordinate(const std::string& value) {
  std::string normalised = value;
  // Trim whitespace.
  const auto first = normalised.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return std::nan("");
  const auto last = normalised.find_last_not_of(" \t\r\n");
  normalised = normalised.substr(first, last - first + 1);

  // European decimal notation.
  std::replace(normalised.begin(), normalised.end(), ',', '.');

  char* end = nullptr;
  const double parsed = std::strtod(normalised.c_str(), &end);
  if (end == normalised.c_str()) return std::nan("");  // nothing consumed
  return std::isfinite(parsed) ? parsed : std::nan("");
}

double parseCoordinate(double value) { return std::isfinite(value) ? value : std::nan(""); }

double haversineKm(double fromLat, double fromLng, double toLat, double toLng) {
  const double deltaLat = toRadians(toLat - fromLat);
  const double deltaLng = toRadians(toLng - fromLng);
  const double fromLatRad = toRadians(fromLat);
  const double toLatRad = toRadians(toLat);

  const double a = std::sin(deltaLat / 2) * std::sin(deltaLat / 2) +
                   std::sin(deltaLng / 2) * std::sin(deltaLng / 2) * std::cos(fromLatRad) *
                       std::cos(toLatRad);

  return kEarthRadiusKm * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

double haversineKm(const LatLng& a, const LatLng& b) {
  return haversineKm(a.lat, a.lng, b.lat, b.lng);
}

std::vector<LatLng> greatCirclePoints(const LatLng& from, const LatLng& to, int steps) {
  const int count = std::max(2, steps);
  std::vector<LatLng> points;
  points.reserve(static_cast<size_t>(count) + 1);

  if (!std::isfinite(from.lat) || !std::isfinite(from.lng) || !std::isfinite(to.lat) ||
      !std::isfinite(to.lng)) {
    return points;
  }

  const auto fromVec = toVector(from.lat, from.lng);
  const auto toVec = toVector(to.lat, to.lng);

  for (int index = 0; index <= count; ++index) {
    const double ratio = static_cast<double>(index) / count;
    std::array<double, 3> interpolated;
    if (slerpVector(fromVec, toVec, ratio, interpolated)) {
      points.push_back(fromVector(interpolated));
    } else {
      // Fallback straight interpolation for degenerate paths.
      points.push_back({from.lat + (to.lat - from.lat) * ratio,
                        from.lng + (to.lng - from.lng) * ratio});
    }
  }

  points.front() = from;
  points.back() = to;
  return points;
}

double wrapLongitude(double lng) {
  double value = std::fmod(lng + 540.0, 360.0);
  if (value < 0) value += 360.0;
  return value - 180.0;
}

bool interpolateRoutePosition(const std::vector<LatLng>& points, double progress, LatLng& out) {
  if (points.empty()) return false;
  if (points.size() == 1) {
    out = points.front();
    return true;
  }

  const double ratio = clampd(std::isfinite(progress) ? progress : 0.0, 0.0, 100.0) / 100.0;
  if (ratio <= 0.0) {
    out = points.front();
    return true;
  }
  if (ratio >= 1.0) {
    out = points.back();
    return true;
  }

  std::vector<double> legs(points.size() - 1);
  double total = 0.0;
  for (size_t index = 1; index < points.size(); ++index) {
    const double length = haversineKm(points[index - 1], points[index]);
    legs[index - 1] = length;
    total += length;
  }

  if (!(total > 0.0)) {
    out = points.front();
    return true;
  }

  double remaining = total * ratio;
  for (size_t index = 0; index < legs.size(); ++index) {
    const bool last = index == legs.size() - 1;
    if (remaining > legs[index] && !last) {
      remaining -= legs[index];
      continue;
    }

    const LatLng& a = points[index];
    const LatLng& b = points[index + 1];
    const double along = legs[index] > 0 ? clampd(remaining / legs[index], 0.0, 1.0) : 0.0;
    double deltaLng = b.lng - a.lng;
    if (deltaLng > 180.0) deltaLng -= 360.0;
    if (deltaLng < -180.0) deltaLng += 360.0;
    const double longitude = a.lng + deltaLng * along;

    out.lat = a.lat + (b.lat - a.lat) * along;
    out.lng = (longitude > 180.0 || longitude < -180.0) ? wrapLongitude(longitude) : longitude;
    return true;
  }

  out = points.back();
  return true;
}

bool mapHeadingDegrees(const LatLng& from, const LatLng& to, double& out) {
  if (!std::isfinite(from.lat) || !std::isfinite(from.lng) || !std::isfinite(to.lat) ||
      !std::isfinite(to.lng)) {
    return false;
  }

  auto mercatorY = [](double latitude) {
    return std::log(std::tan(kPi / 4 + toRadians(latitude) / 2));
  };

  const double deltaY = mercatorY(to.lat) - mercatorY(from.lat);
  double deltaX = toRadians(to.lng) - toRadians(from.lng);
  if (deltaX > kPi) deltaX -= 2 * kPi;
  if (deltaX < -kPi) deltaX += 2 * kPi;

  if (std::fabs(deltaX) < 1e-12 && std::fabs(deltaY) < 1e-12) return false;

  out = toDegrees(std::atan2(deltaX, deltaY));
  return true;
}

double voyageDurationMs(double distanceKm, double multiplier) {
  const double distance = std::isfinite(distanceKm) && distanceKm > 0 ? distanceKm : 0.0;
  const double factor = std::isfinite(multiplier) && multiplier > 0 ? multiplier : 1.0;
  const double span = std::min(1.0, std::sqrt(distance / kVoyageFullDistanceKm));
  const double curved = kVoyageMinDurationMs + (kVoyageMaxDurationMs - kVoyageMinDurationMs) * span;

  return std::min(kVoyageMaxDurationMs,
                  std::max(kVoyageMinDurationMs, std::round(curved * factor)));
}

std::string formatDuration(double ms) {
  const double value = std::isfinite(ms) ? ms : 0.0;
  const long long seconds = std::max<long long>(0, static_cast<long long>(std::llround(value / 1000.0)));
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", seconds / 60, seconds % 60);
  return std::string(buffer);
}

}  // namespace sea

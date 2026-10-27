#include "map_view.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "json.hpp"
#include "rlgl.h"

namespace sea {
namespace {

constexpr Color kOcean = {8, 30, 44, 255};
constexpr Color kLand = {206, 197, 168, 255};
constexpr Color kCoast = {18, 63, 82, 160};
constexpr float kMinScale = 0.9f;
constexpr float kMaxScale = 90.0f;

double wrapDelta(double degrees) {
  double value = std::fmod(degrees + 540.0, 360.0);
  if (value < 0) value += 360.0;
  return value - 180.0;
}

double cross2(const Vector2& a, const Vector2& b, const Vector2& c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointInTriangle(const Vector2& p, const Vector2& a, const Vector2& b, const Vector2& c) {
  const double d1 = cross2(a, b, p);
  const double d2 = cross2(b, c, p);
  const double d3 = cross2(c, a, p);
  const bool hasNegative = d1 < 0 || d2 < 0 || d3 < 0;
  const bool hasPositive = d1 > 0 || d2 > 0 || d3 > 0;
  return !(hasNegative && hasPositive);
}

/// Ear-clipping triangulation of a simple polygon. Good enough for coastlines, and small
/// enough to run once at load on all 10.6k Natural Earth vertices.
void triangulateRing(const std::vector<Vector2>& ring, std::vector<Vector2>& out) {
  if (ring.size() < 3) return;

  std::vector<int> indices(ring.size());
  for (size_t i = 0; i < ring.size(); ++i) indices[i] = static_cast<int>(i);

  // Make the winding counter-clockwise so "convex" is a positive cross product.
  double area = 0.0;
  for (size_t i = 0; i < ring.size(); ++i) {
    const Vector2& a = ring[i];
    const Vector2& b = ring[(i + 1) % ring.size()];
    area += a.x * b.y - b.x * a.y;
  }
  if (area < 0) std::reverse(indices.begin(), indices.end());

  const size_t guardLimit = ring.size() * ring.size() + 16;
  size_t guard = 0;

  while (indices.size() > 3 && guard++ < guardLimit) {
    bool clipped = false;

    for (size_t i = 0; i < indices.size(); ++i) {
      const Vector2& prev = ring[indices[(i + indices.size() - 1) % indices.size()]];
      const Vector2& cur = ring[indices[i]];
      const Vector2& next = ring[indices[(i + 1) % indices.size()]];

      if (cross2(prev, cur, next) <= 0) continue;  // reflex vertex

      bool containsOther = false;
      for (size_t j = 0; j < indices.size(); ++j) {
        if (j == i || j == (i + indices.size() - 1) % indices.size() ||
            j == (i + 1) % indices.size()) {
          continue;
        }
        if (pointInTriangle(ring[indices[j]], prev, cur, next)) {
          containsOther = true;
          break;
        }
      }
      if (containsOther) continue;

      out.push_back(prev);
      out.push_back(cur);
      out.push_back(next);
      indices.erase(indices.begin() + static_cast<long>(i));
      clipped = true;
      break;
    }

    if (!clipped) break;  // degenerate ring; stop rather than spin
  }

  if (indices.size() == 3) {
    out.push_back(ring[indices[0]]);
    out.push_back(ring[indices[1]]);
    out.push_back(ring[indices[2]]);
  }
}

/// Reads a ring, unwrapping longitudes so a shape crossing the antimeridian stays continuous.
std::vector<Vector2> readRing(const nlohmann::json& coordinates) {
  std::vector<Vector2> ring;
  ring.reserve(coordinates.size());

  double previous = 0.0;
  bool first = true;

  for (const auto& point : coordinates) {
    if (!point.is_array() || point.size() < 2) continue;
    double lng = point[0].get<double>();
    const double lat = point[1].get<double>();

    if (!first) {
      while (lng - previous > 180.0) lng -= 360.0;
      while (lng - previous < -180.0) lng += 360.0;
    }

    ring.push_back({static_cast<float>(lng), static_cast<float>(lat)});
    previous = lng;
    first = false;
  }

  if (ring.size() > 1 && std::fabs(ring.front().x - ring.back().x) < 1e-9 &&
      std::fabs(ring.front().y - ring.back().y) < 1e-9) {
    ring.pop_back();
  }

  return ring;
}

/// Adds every polygon of a geometry to the triangle soup and the ring list.
void addGeometry(const nlohmann::json& geometry, std::vector<Vector2>& triangles,
                 std::vector<std::vector<Vector2>>& rings, bool fill) {
  if (!geometry.is_object() || !geometry.contains("type") || !geometry.contains("coordinates")) {
    return;
  }

  const std::string type = geometry.value("type", std::string());
  const auto& coordinates = geometry["coordinates"];

  auto addPolygon = [&](const nlohmann::json& polygon) {
    if (!polygon.is_array() || polygon.empty()) return;
    for (size_t index = 0; index < polygon.size(); ++index) {
      std::vector<Vector2> ring = readRing(polygon[index]);
      if (ring.size() < 3) continue;
      // Only the exterior ring is filled; holes are restored by the lakes layer.
      if (fill && index == 0) triangulateRing(ring, triangles);
      rings.push_back(std::move(ring));
    }
  };

  if (type == "Polygon") {
    addPolygon(coordinates);
  } else if (type == "MultiPolygon") {
    for (const auto& polygon : coordinates) addPolygon(polygon);
  }
}

bool readGeoJson(const std::string& path, std::vector<Vector2>& triangles,
                 std::vector<std::vector<Vector2>>& rings, bool fill) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  nlohmann::json root;
  try {
    file >> root;
  } catch (const std::exception&) {
    return false;
  }

  const std::string type = root.value("type", std::string());
  if (type == "FeatureCollection" && root.contains("features")) {
    for (const auto& feature : root["features"]) {
      if (feature.contains("geometry")) addGeometry(feature["geometry"], triangles, rings, fill);
    }
    return true;
  }
  if (type == "Feature" && root.contains("geometry")) {
    addGeometry(root["geometry"], triangles, rings, fill);
    return true;
  }
  addGeometry(root, triangles, rings, fill);
  return true;
}

}  // namespace

bool MapView::load(const std::string& countriesGeoJson, const std::string& lakesGeoJson) {
  landTriangles_.clear();
  coastRings_.clear();
  lakeTriangles_.clear();
  lakeRings_.clear();

  const bool countriesOk =
      readGeoJson(countriesGeoJson, landTriangles_, coastRings_, /*fill=*/true);
  // Lakes are drawn as water on top of the land fill, restoring the big holes the country
  // outlines ignore.
  readGeoJson(lakesGeoJson, lakeTriangles_, lakeRings_, /*fill=*/true);

  return countriesOk;
}

void MapView::setViewport(Rectangle viewport) { viewport_ = viewport; }

Vector2 MapView::rawProject(double lng, double lat) const {
  const float cx = viewport_.x + viewport_.width * 0.5f;
  const float cy = viewport_.y + viewport_.height * 0.5f;
  return {cx + static_cast<float>((lng - center_.lng) * scale_),
          cy - static_cast<float>((lat - center_.lat) * scale_)};
}

Vector2 MapView::project(const LatLng& point) const {
  const double delta = wrapDelta(point.lng - center_.lng);
  const float cx = viewport_.x + viewport_.width * 0.5f;
  const float cy = viewport_.y + viewport_.height * 0.5f;
  return {cx + static_cast<float>(delta * scale_),
          cy - static_cast<float>((point.lat - center_.lat) * scale_)};
}

LatLng MapView::unproject(Vector2 screen) const {
  const float cx = viewport_.x + viewport_.width * 0.5f;
  const float cy = viewport_.y + viewport_.height * 0.5f;
  LatLng point;
  point.lng = center_.lng + (screen.x - cx) / scale_;
  point.lat = center_.lat - (screen.y - cy) / scale_;
  point.lng = wrapDelta(point.lng);
  point.lat = std::max(-85.0, std::min(85.0, point.lat));
  return point;
}

void MapView::pan(Vector2 delta) {
  center_.lng -= delta.x / scale_;
  center_.lat += delta.y / scale_;
  center_.lng = wrapDelta(center_.lng);
  center_.lat = std::max(-82.0, std::min(82.0, center_.lat));
}

void MapView::zoomAt(Vector2 screenPoint, float wheel) {
  const LatLng before = unproject(screenPoint);
  const float next = std::max(kMinScale, std::min(kMaxScale, scale_ * std::pow(1.15f, wheel)));
  if (next == scale_) return;
  scale_ = next;

  const float cx = viewport_.x + viewport_.width * 0.5f;
  const float cy = viewport_.y + viewport_.height * 0.5f;
  center_.lng = before.lng - (screenPoint.x - cx) / scale_;
  center_.lat = before.lat + (screenPoint.y - cy) / scale_;
  center_.lng = wrapDelta(center_.lng);
  center_.lat = std::max(-82.0, std::min(82.0, center_.lat));
}

void MapView::centerOn(const LatLng& point, float minScale) {
  center_ = point;
  if (scale_ < minScale) scale_ = minScale;
}

void MapView::resetView(float scale) {
  center_ = {20.0, 0.0};
  scale_ = scale;
}

const Port* MapView::portAtScreen(const std::vector<Port>& ports, Vector2 screen,
                                  float radiusPixels) const {
  const Port* best = nullptr;
  float bestDistance = radiusPixels;

  for (const Port& port : ports) {
    if (!port.hasPosition()) continue;
    const Vector2 position = project(port.position);
    const float distance = std::hypot(position.x - screen.x, position.y - screen.y);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = &port;
    }
  }

  return best;
}

void MapView::drawTriangles(const std::vector<Vector2>& triangles) const {
  if (triangles.empty()) return;

  const float left = viewport_.x;
  const float right = viewport_.x + viewport_.width;
  const float top = viewport_.y;
  const float bottom = viewport_.y + viewport_.height;

  rlBegin(RL_TRIANGLES);
  for (size_t index = 0; index + 2 < triangles.size(); index += 3) {
    const Vector2& a = triangles[index];
    const Vector2& b = triangles[index + 1];
    const Vector2& c = triangles[index + 2];

    // Draw the world copy whose centroid is nearest the view centre.
    const double centroid = (a.x + b.x + c.x) / 3.0;
    const double shift = std::round((center_.lng - centroid) / 360.0) * 360.0;

    const Vector2 sa = rawProject(a.x + shift, a.y);
    const Vector2 sb = rawProject(b.x + shift, b.y);
    const Vector2 sc = rawProject(c.x + shift, c.y);

    if ((sa.x < left && sb.x < left && sc.x < left) ||
        (sa.x > right && sb.x > right && sc.x > right) ||
        (sa.y < top && sb.y < top && sc.y < top) ||
        (sa.y > bottom && sb.y > bottom && sc.y > bottom)) {
      continue;
    }

    rlVertex2f(sa.x, sa.y);
    rlVertex2f(sb.x, sb.y);
    rlVertex2f(sc.x, sc.y);
  }
  rlEnd();
}

void MapView::drawRings(const std::vector<std::vector<Vector2>>& rings) const {
  const float left = viewport_.x - 8.0f;
  const float right = viewport_.x + viewport_.width + 8.0f;
  const float top = viewport_.y - 8.0f;
  const float bottom = viewport_.y + viewport_.height + 8.0f;

  rlBegin(RL_LINES);
  for (const std::vector<Vector2>& ring : rings) {
    if (ring.size() < 2) continue;

    const double centroid = [&] {
      double sum = 0.0;
      for (const Vector2& point : ring) sum += point.x;
      return sum / static_cast<double>(ring.size());
    }();
    const double shift = std::round((center_.lng - centroid) / 360.0) * 360.0;

    for (size_t index = 0; index < ring.size(); ++index) {
      const Vector2& a = ring[index];
      const Vector2& b = ring[(index + 1) % ring.size()];
      const Vector2 sa = rawProject(a.x + shift, a.y);
      const Vector2 sb = rawProject(b.x + shift, b.y);

      if ((sa.x < left && sb.x < left) || (sa.x > right && sb.x > right) ||
          (sa.y < top && sb.y < top) || (sa.y > bottom && sb.y > bottom)) {
        continue;
      }

      rlVertex2f(sa.x, sa.y);
      rlVertex2f(sb.x, sb.y);
    }
  }
  rlEnd();
}

void MapView::drawWorld() const {
  DrawRectangleRec(viewport_, kOcean);

  rlColor4ub(kLand.r, kLand.g, kLand.b, kLand.a);
  drawTriangles(landTriangles_);

  rlColor4ub(kOcean.r, kOcean.g, kOcean.b, kOcean.a);
  drawTriangles(lakeTriangles_);

  rlColor4ub(kCoast.r, kCoast.g, kCoast.b, kCoast.a);
  drawRings(coastRings_);
  drawRings(lakeRings_);
}

void MapView::drawRoute(const std::vector<LatLng>& points, Color color, float thickness,
                        bool dashed) const {
  if (points.size() < 2) return;

  const std::vector<std::vector<LatLng>> legs = splitAtAntimeridian(points);

  for (const std::vector<LatLng>& leg : legs) {
    if (leg.size() < 2) continue;

    if (!dashed) {
      for (size_t index = 1; index < leg.size(); ++index) {
        DrawLineEx(project(leg[index - 1]), project(leg[index]), thickness, color);
      }
      continue;
    }

    // Dashes flow along the leg, evenly spaced, so the course reads as a direction.
    constexpr float kDash = 12.0f;
    constexpr float kGap = 9.0f;
    float travelled = 0.0f;

    for (size_t index = 1; index < leg.size(); ++index) {
      const Vector2 from = project(leg[index - 1]);
      const Vector2 to = project(leg[index]);
      const float length = std::hypot(to.x - from.x, to.y - from.y);
      if (length <= 0.0f) continue;

      float position = 0.0f;
      while (position < length) {
        const float phase = std::fmod(travelled + position, kDash + kGap);
        const float remainingInDash = (kDash + kGap) - phase;
        if (remainingInDash >= kDash) {
          const float start = position;
          const float end = std::min(length, position + std::min(kDash, remainingInDash));
          const float t0 = start / length;
          const float t1 = end / length;
          DrawLineEx({from.x + (to.x - from.x) * t0, from.y + (to.y - from.y) * t0},
                     {from.x + (to.x - from.x) * t1, from.y + (to.y - from.y) * t1}, thickness,
                     color);
          position = end;
        } else {
          position += remainingInDash;
        }
      }
      travelled += length;
    }
  }
}

std::vector<std::vector<LatLng>> splitAtAntimeridian(const std::vector<LatLng>& points) {
  std::vector<std::vector<LatLng>> segments;
  if (points.empty()) return segments;

  std::vector<LatLng> current{points[0]};

  for (size_t index = 1; index < points.size(); ++index) {
    const LatLng& previous = points[index - 1];
    const LatLng& point = points[index];
    const double rawDelta = point.lng - previous.lng;

    if (std::fabs(rawDelta) <= 180.0) {
      current.push_back(point);
      continue;
    }

    // The leg crosses the antimeridian. Split it into two legs that meet at the map
    // edge, otherwise the connection - and often the whole ocean crossing - is lost.
    // The boundary points sit just inside +/-180 so the projection keeps each half on
    // the correct side of the map instead of wrapping it back across the world.
    const double delta = wrapLongitude(rawDelta);
    const double target = previous.lng + delta;
    const double trueBoundary = target > 0.0 ? 180.0 : -180.0;
    const double edge = target > 0.0 ? 179.999999 : -179.999999;
    const double ratio = std::fabs(delta) > 1e-9 ? (trueBoundary - previous.lng) / delta : 0.0;
    const double latitude = previous.lat + (point.lat - previous.lat) * ratio;

    current.push_back({latitude, edge});
    segments.push_back(current);
    current.clear();
    current.push_back({latitude, -edge});
    current.push_back(point);
  }

  segments.push_back(current);

  segments.erase(std::remove_if(segments.begin(), segments.end(),
                                [](const std::vector<LatLng>& s) { return s.size() < 2; }),
                 segments.end());
  return segments;
}

}  // namespace sea

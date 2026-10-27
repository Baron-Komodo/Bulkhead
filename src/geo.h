#pragma once

// Pure map/game maths, ported from sea-builder/src/geo.js.
// Nothing here touches a window or a renderer, so it is unit-testable headless.

#include <string>
#include <utility>
#include <vector>

namespace sea {

constexpr double kEarthRadiusKm = 6371.0;

/// A latitude / longitude in degrees.
struct LatLng {
  double lat = 0.0;
  double lng = 0.0;
};

/// Ports store coordinates as strings using European decimal notation ("43,866667").
/// This normalises either a string or a number into a finite double, or NaN.
double parseCoordinate(const std::string& value);
double parseCoordinate(double value);

/// Distance between two points in kilometres.
double haversineKm(double fromLat, double fromLng, double toLat, double toLng);
double haversineKm(const LatLng& a, const LatLng& b);

/// The great circle between two points, sampled into `steps` + 1 points.
std::vector<LatLng> greatCirclePoints(const LatLng& from, const LatLng& to, int steps = 96);

/// Position `progress` percent of the way along a polyline, measured by distance.
/// Returns false for an empty path.
bool interpolateRoutePosition(const std::vector<LatLng>& points, double progress, LatLng& out);

/// Direction of a course as drawn on a Web Mercator map, degrees clockwise from north.
/// Returns false when there is no direction to speak of.
bool mapHeadingDegrees(const LatLng& from, const LatLng& to, double& out);

// ---------------------------------------------------------------------------
// Voyage timing (see geo.js). These are the whole speed dial.
// ---------------------------------------------------------------------------

constexpr double kVoyageMinDurationMs = 30000.0;
constexpr double kVoyageMaxDurationMs = 75000.0;
constexpr double kVoyageFullDistanceKm = 12000.0;

/// How long a voyage of the given distance takes to animate, in milliseconds.
double voyageDurationMs(double distanceKm, double multiplier = 1.0);

/// A duration as mm:ss.
std::string formatDuration(double ms);

/// Normalises a longitude into [-180, 180).
double wrapLongitude(double lng);

}  // namespace sea

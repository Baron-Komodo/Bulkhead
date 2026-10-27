#pragma once

// The world map: an equirectangular projection with pan and zoom, country outlines
// triangulated from Natural Earth, and helpers to project ports, routes and vessels.
//
// Everything here is presentation. The game rules never look at it.

#include <string>
#include <vector>

#include "raylib.h"

#include "data.h"
#include "geo.h"

namespace sea {

class MapView {
 public:
  /// Loads the country outlines and lakes. Either file may be missing; the map still draws.
  bool load(const std::string& countriesGeoJson, const std::string& lakesGeoJson);

  void setViewport(Rectangle viewport);
  Rectangle viewport() const { return viewport_; }

  void pan(Vector2 delta);
  void zoomAt(Vector2 screenPoint, float wheel);
  void centerOn(const LatLng& point, float minScale);
  void resetView(float scale = 3.0f);

  /// Screen position of a point, choosing the world copy nearest the view centre.
  Vector2 project(const LatLng& point) const;

  /// Inverse of project: the latitude / longitude under a screen position.
  LatLng unproject(Vector2 screen) const;

  float scale() const { return scale_; }
  LatLng center() const { return center_; }

  /// The port whose marker is under a screen position, or nullptr.
  const Port* portAtScreen(const std::vector<Port>& ports, Vector2 screen,
                           float radiusPixels = 12.0f) const;

  /// Draws the ocean, the landmasses and the coastline strokes.
  void drawWorld() const;

  /// Draws a route polyline, cut at the antimeridian so it never runs back across the map.
  void drawRoute(const std::vector<LatLng>& points, Color color, float thickness,
                 bool dashed) const;

 private:
  /// Projection with no wrapping: x and y are degrees, and x may sit outside [-180, 180].
  Vector2 rawProject(double lng, double lat) const;

  void drawTriangles(const std::vector<Vector2>& triangles) const;
  void drawRings(const std::vector<std::vector<Vector2>>& rings) const;

  Rectangle viewport_{0.0f, 0.0f, 1280.0f, 720.0f};
  LatLng center_{20.0, 0.0};
  float scale_ = 3.0f;  // pixels per degree

  std::vector<Vector2> landTriangles_;   // three vertices per triangle, x = lng, y = lat
  std::vector<std::vector<Vector2>> coastRings_;
  std::vector<Vector2> lakeTriangles_;
  std::vector<std::vector<Vector2>> lakeRings_;
};

/// Splits a polyline where it crosses the antimeridian, so it can be drawn without a sawtooth
/// back across the whole map.
std::vector<std::vector<LatLng>> splitAtAntimeridian(const std::vector<LatLng>& points);

}  // namespace sea

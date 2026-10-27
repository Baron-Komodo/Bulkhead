#pragma once

// Sea routing, ported from sea-builder/src/seaRoute.js.
//
// A cheapest-first search over the ocean grid built from the map's own country outlines.
// The grid is water only, so a course runs around the continents instead of across them.
// One flood from a departure fills a distance *field* for every cell the sea can reach,
// which is what lets the navigation panel price sixty destinations at once.

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "geo.h"

namespace sea {

class SeaRouter;

/// A sea route between two points: the polyline to draw and how far it really is.
struct RouteResult {
  double distanceKm = 0.0;
  bool ok = false;  // true when the course is a real sea route, false for a straight line
  std::vector<LatLng> points;
};

/// The distance field produced by one flood from a departure: the sea distance to any
/// point, and the path back out of it. Shared, because a field is expensive and reused.
class SeaField {
 public:
  /// The sea distance to a point in kilometres, or false when no sea route exists.
  bool distanceKmTo(const LatLng& point, double& outKm) const;

  /// The route to a point: the field's path, straightened, departure to point.
  bool routeTo(const LatLng& point, RouteResult& out) const;

  const LatLng& from() const { return from_; }

 private:
  friend class SeaRouter;

  /// Where a point joins the field's water, if it does at all.
  bool berth(const LatLng& point, int& cell, double& distanceKm) const;

  const SeaRouter* owner_ = nullptr;
  LatLng from_;
  LatLng startCenter_;
  int startCell_ = -1;
  double startOffsetKm_ = 0.0;
  std::vector<float> gScore_;
  std::vector<int32_t> cameFrom_;
};

/// Owns the decoded grid and the search caches.
class SeaRouter {
 public:
  SeaRouter();

  /// Loads the packed grid. Returns false if the file cannot be read.
  bool loadGrid(const std::string& path);

  /// Whether the grid is loaded.
  bool ready() const { return ready_; }

  /// The distance field from a departure, cached a few deep.
  std::shared_ptr<const SeaField> field(const LatLng& from);

  /// The sea route between two points: the path to draw and how far it is.
  RouteResult route(const LatLng& from, const LatLng& to);

  /// Whether a coordinate falls on land (or polar ice) according to the grid.
  bool isLandAt(double latitude, double longitude) const;

  int rows() const { return rows_; }
  int columns() const { return columns_; }
  double cellDegrees() const { return cellDegrees_; }
  double latStart() const { return latStart_; }
  double lonStart() const { return lonStart_; }

 private:
  friend class SeaField;

  int rowOf(double latitude) const;
  int columnOf(double longitude) const;
  int wrapColumn(long long column) const;
  LatLng cellLatLng(int cell) const;
  int snapToWater(const LatLng& target) const;
  int waterCellFor(const LatLng& point) const;

  void floodFrom(int startCell, std::vector<float>& gScore, std::vector<int32_t>& cameFrom);
  bool lineIsClear(int fromCell, int toCell) const;
  std::vector<int> pullStraight(const std::vector<int>& cells, int limit) const;
  double cellDistanceKm(int fromCell, int toCell) const;

  bool ready_ = false;
  int cellDegreesInt_ = 1;
  double cellDegrees_ = 0.5;
  double latStart_ = -90.0;
  double lonStart_ = -180.0;
  int rows_ = 360;
  int columns_ = 720;
  int cellCount_ = 0;

  std::vector<uint8_t> land_;
  std::vector<double> costEast_;
  std::vector<double> costNorth_;
  std::vector<double> costNorthEast_;

  // Reused between searches.
  std::vector<uint8_t> closed_;
  std::vector<int32_t> heapNode_;
  std::vector<double> heapScore_;
  size_t heapSize_ = 0;

  mutable std::unordered_map<std::string, int> snapped_;
  std::list<std::string> fieldOrder_;
  std::unordered_map<std::string, std::shared_ptr<const SeaField>> fields_;
};

}  // namespace sea

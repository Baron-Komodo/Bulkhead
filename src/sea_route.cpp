#include "sea_route.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

#include "json.hpp"

namespace sea {
namespace {

constexpr int kMaxSnapCells = 4;
constexpr int kMaxPullStep = 48;
constexpr size_t kFieldCacheLimit = 3;

const std::string kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// The eight steps a route may take, as row / column offsets.
const int kDRow[8] = {0, 0, 1, -1, 1, 1, -1, -1};
const int kDColumn[8] = {1, -1, 0, 0, 1, -1, 1, -1};

/// Decodes the packed land bitfield the builder emits: six bits per character, cells
/// filled most-significant-bit first.
std::vector<uint8_t> decodeLand(const std::string& packed, size_t cellCount) {
  int8_t values[128];
  std::fill(values, values + 128, static_cast<int8_t>(-1));
  for (size_t i = 0; i < kBase64Alphabet.size(); ++i) {
    values[static_cast<unsigned char>(kBase64Alphabet[i])] = static_cast<int8_t>(i);
  }

  std::vector<uint8_t> land(cellCount, 0);
  size_t bit = 0;
  uint32_t accumulator = 0;
  int bitsInAccumulator = 0;

  for (char ch : packed) {
    const unsigned char code = static_cast<unsigned char>(ch);
    if (code >= 128) continue;
    const int value = values[code];
    if (value < 0) continue;

    accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
    bitsInAccumulator += 6;

    while (bitsInAccumulator >= 8 && bit < cellCount) {
      bitsInAccumulator -= 8;
      const uint8_t byte = (accumulator >> bitsInAccumulator) & 0xff;
      for (int offset = 0; offset < 8 && bit < cellCount; ++offset) {
        land[bit] = (byte >> (7 - offset)) & 1;
        bit += 1;
      }
    }

    if (bitsInAccumulator < 8) accumulator &= (1u << bitsInAccumulator) - 1u;
  }

  return land;
}

std::string coordKey(double latitude, double longitude) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.4f,%.4f", latitude, longitude);
  return std::string(buffer);
}

}  // namespace

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

SeaRouter::SeaRouter() = default;

bool SeaRouter::loadGrid(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  nlohmann::json grid;
  try {
    file >> grid;
  } catch (const std::exception&) {
    return false;
  }

  cellDegrees_ = grid.value("cellDegrees", 0.5);
  latStart_ = grid.value("latStart", -90.0);
  lonStart_ = grid.value("lonStart", -180.0);
  rows_ = grid.value("rows", 360);
  columns_ = grid.value("columns", 720);
  cellCount_ = rows_ * columns_;

  const std::string packed = grid.value("land", std::string());
  if (packed.empty() || cellCount_ <= 0) return false;

  land_ = decodeLand(packed, static_cast<size_t>(cellCount_));

  costEast_.assign(rows_, 0.0);
  costNorth_.assign(rows_, 0.0);
  costNorthEast_.assign(rows_, 0.0);

  for (int row = 0; row < rows_; ++row) {
    const double latitude = latStart_ + (row + 0.5) * cellDegrees_;
    const double nextLatitude = std::min(90.0, latStart_ + (row + 1.5) * cellDegrees_);
    costEast_[row] = haversineKm(latitude, 0, latitude, cellDegrees_);
    costNorth_[row] = haversineKm(latitude, 0, nextLatitude, 0);
    costNorthEast_[row] = haversineKm(latitude, 0, nextLatitude, cellDegrees_);
  }

  closed_.assign(static_cast<size_t>(cellCount_), 0);
  heapNode_.assign(1 << 16, 0);
  heapScore_.assign(1 << 16, 0.0);
  heapSize_ = 0;

  ready_ = true;
  return true;
}

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------

int SeaRouter::rowOf(double latitude) const {
  const int row = static_cast<int>(std::floor((latitude - latStart_) / cellDegrees_));
  return std::min(rows_ - 1, std::max(0, row));
}

int SeaRouter::columnOf(double longitude) const {
  return wrapColumn(static_cast<long long>(std::floor((longitude - lonStart_) / cellDegrees_)));
}

int SeaRouter::wrapColumn(long long column) const {
  return static_cast<int>(((column % columns_) + columns_) % columns_);
}

LatLng SeaRouter::cellLatLng(int cell) const {
  const int row = cell / columns_;
  const int column = cell % columns_;
  return {latStart_ + (row + 0.5) * cellDegrees_, lonStart_ + (column + 0.5) * cellDegrees_};
}

double SeaRouter::cellDistanceKm(int fromCell, int toCell) const {
  const LatLng a = cellLatLng(fromCell);
  const LatLng b = cellLatLng(toCell);
  return haversineKm(a, b);
}

bool SeaRouter::isLandAt(double latitude, double longitude) const {
  if (!ready_) return false;
  return land_[static_cast<size_t>(rowOf(latitude)) * columns_ + columnOf(longitude)] == 1;
}

int SeaRouter::snapToWater(const LatLng& target) const {
  const int row = rowOf(target.lat);
  const int column = columnOf(target.lng);

  if (!land_[static_cast<size_t>(row) * columns_ + column]) return row * columns_ + column;

  int best = -1;
  double bestDistance = std::numeric_limits<double>::infinity();

  for (int radius = 1; radius <= kMaxSnapCells; ++radius) {
    for (int dRow = -radius; dRow <= radius; ++dRow) {
      const int nextRow = row + dRow;
      if (nextRow < 0 || nextRow >= rows_) continue;

      const bool edge = std::abs(dRow) == radius;

      for (int dColumn = -radius; dColumn <= radius; ++dColumn) {
        if (!edge && std::abs(dColumn) != radius) continue;

        const int cell = nextRow * columns_ + wrapColumn(column + dColumn);
        if (land_[cell]) continue;

        const LatLng cellPoint = cellLatLng(cell);
        const double distance = haversineKm(target, cellPoint);

        if (distance < bestDistance) {
          bestDistance = distance;
          best = cell;
        }
      }
    }

    if (best != -1) return best;
  }

  return -1;
}

int SeaRouter::waterCellFor(const LatLng& point) const {
  const std::string key = coordKey(point.lat, point.lng);
  const auto found = snapped_.find(key);
  if (found != snapped_.end()) return found->second;

  const int cell = snapToWater(point);
  snapped_[key] = cell;
  return cell;
}

// ---------------------------------------------------------------------------
// Minimum-heap of (cell, distance), parallel arrays.
// ---------------------------------------------------------------------------

namespace {

void heapPush(std::vector<int32_t>& nodes, std::vector<double>& scores, size_t& size,
              int32_t node, double score) {
  if (size == nodes.size()) {
    const size_t grown = nodes.size() * 2;
    nodes.resize(grown);
    scores.resize(grown);
  }

  size_t child = size;
  nodes[child] = node;
  scores[child] = score;
  size += 1;

  while (child > 0) {
    const size_t parent = (child - 1) >> 1;
    if (scores[parent] <= scores[child]) break;

    const int32_t nodeSwap = nodes[parent];
    const double scoreSwap = scores[parent];
    nodes[parent] = nodes[child];
    scores[parent] = scores[child];
    nodes[child] = nodeSwap;
    scores[child] = scoreSwap;
    child = parent;
  }
}

int32_t heapPop(std::vector<int32_t>& nodes, std::vector<double>& scores, size_t& size) {
  const int32_t top = nodes[0];
  size -= 1;

  if (size > 0) {
    nodes[0] = nodes[size];
    scores[0] = scores[size];

    size_t parent = 0;
    for (;;) {
      const size_t left = parent * 2 + 1;
      if (left >= size) break;

      const size_t right = left + 1;
      const size_t smallest =
          (right < size && scores[right] < scores[left]) ? right : left;

      if (scores[parent] <= scores[smallest]) break;

      const int32_t nodeSwap = nodes[parent];
      const double scoreSwap = scores[parent];
      nodes[parent] = nodes[smallest];
      scores[parent] = scores[smallest];
      nodes[smallest] = nodeSwap;
      scores[smallest] = scoreSwap;
      parent = smallest;
    }
  }

  return top;
}

}  // namespace

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

void SeaRouter::floodFrom(int start, std::vector<float>& gScore,
                          std::vector<int32_t>& cameFrom) {
  gScore.assign(static_cast<size_t>(cellCount_),
                std::numeric_limits<float>::infinity());
  cameFrom.assign(static_cast<size_t>(cellCount_), -1);

  std::fill(closed_.begin(), closed_.end(), static_cast<uint8_t>(0));
  heapSize_ = 0;

  gScore[start] = 0.0f;
  heapPush(heapNode_, heapScore_, heapSize_, start, 0.0);

  while (heapSize_ > 0) {
    const int node = heapPop(heapNode_, heapScore_, heapSize_);
    if (closed_[node]) continue;
    closed_[node] = 1;

    const int row = node / columns_;
    const int column = node - row * columns_;
    const double base = gScore[node];

    for (int side = 0; side < 8; ++side) {
      const int dRow = kDRow[side];
      const int nextRow = row + dRow;
      if (nextRow < 0 || nextRow >= rows_) continue;

      const int dColumn = kDColumn[side];
      const int shifted = column + dColumn;
      const int nextColumn = shifted < 0 ? columns_ - 1 : shifted >= columns_ ? 0 : shifted;
      const int next = nextRow * columns_ + nextColumn;

      if (land_[next] || closed_[next]) continue;

      double cost;
      if (dRow == 0) {
        cost = base + costEast_[row];
      } else if (dColumn == 0) {
        cost = base + costNorth_[dRow > 0 ? row : row - 1];
      } else {
        // No corner cutting: a diagonal is only navigable when both cells sharing that
        // corner are navigable too.
        if (land_[row * columns_ + nextColumn] || land_[nextRow * columns_ + column]) continue;
        cost = base + costNorthEast_[dRow > 0 ? row : row - 1];
      }

      if (cost >= gScore[next]) continue;

      gScore[next] = static_cast<float>(cost);
      cameFrom[next] = node;
      heapPush(heapNode_, heapScore_, heapSize_, next, cost);
    }
  }
}

bool SeaRouter::lineIsClear(int from, int to) const {
  const int fromRow = from / columns_;
  const int fromColumn = from - fromRow * columns_;
  int dRow = (to / columns_) - fromRow;
  int dColumn = (to % columns_) - fromColumn;

  if (dColumn > columns_ / 2) dColumn -= columns_;
  if (dColumn < -columns_ / 2) dColumn += columns_;

  const int steps = 4 * std::max(std::abs(dRow), std::abs(dColumn)) + 1;
  bool havePrevious = false;
  int previousRow = 0;
  int previousColumn = 0;

  for (int step = 0; step <= steps; ++step) {
    const double ratio = static_cast<double>(step) / steps;
    const int row = static_cast<int>(std::llround(fromRow + dRow * ratio));
    const int column = wrapColumn(std::llround(fromColumn + dColumn * ratio));

    if (row < 0 || row >= rows_ || land_[row * columns_ + column]) return false;

    if (havePrevious && previousRow != row && previousColumn != column) {
      if (land_[previousRow * columns_ + column] || land_[row * columns_ + previousColumn]) {
        return false;
      }
    }

    previousRow = row;
    previousColumn = column;
    havePrevious = true;
  }

  return true;
}

std::vector<int> SeaRouter::pullStraight(const std::vector<int>& cells, int limit) const {
  if (cells.size() <= 2) return cells;

  std::vector<int> pulled;
  pulled.push_back(cells[0]);
  size_t anchor = 0;

  while (anchor < cells.size() - 1) {
    size_t next = anchor + 1;
    const size_t reach = std::min(cells.size() - 1, anchor + static_cast<size_t>(limit));

    for (size_t probe = reach; probe > anchor + 1; --probe) {
      if (lineIsClear(cells[anchor], cells[probe])) {
        next = probe;
        break;
      }
    }

    pulled.push_back(cells[next]);
    anchor = next;
  }

  return pulled;
}

// ---------------------------------------------------------------------------
// Fields
// ---------------------------------------------------------------------------

std::shared_ptr<const SeaField> SeaRouter::field(const LatLng& from) {
  if (!ready_) return nullptr;
  if (!std::isfinite(from.lat) || !std::isfinite(from.lng)) return nullptr;

  const std::string key = coordKey(from.lat, from.lng);

  const auto cached = fields_.find(key);
  if (cached != fields_.end()) {
    fieldOrder_.remove(key);
    fieldOrder_.push_back(key);
    return cached->second;
  }

  const int startCell = waterCellFor(from);
  if (startCell == -1) return nullptr;

  auto fieldPtr = std::make_shared<SeaField>();
  fieldPtr->owner_ = this;
  fieldPtr->from_ = from;
  floodFrom(startCell, fieldPtr->gScore_, fieldPtr->cameFrom_);
  fieldPtr->startCell_ = startCell;
  fieldPtr->startCenter_ = cellLatLng(startCell);
  fieldPtr->startOffsetKm_ = haversineKm(from, fieldPtr->startCenter_);

  if (fieldOrder_.size() >= kFieldCacheLimit) {
    const std::string oldest = fieldOrder_.front();
    fieldOrder_.pop_front();
    fields_.erase(oldest);
  }
  fieldOrder_.push_back(key);
  fields_[key] = fieldPtr;

  return fieldPtr;
}

bool SeaField::berth(const LatLng& point, int& cell, double& distanceKm) const {
  if (!std::isfinite(point.lat) || !std::isfinite(point.lng) || owner_ == nullptr) return false;

  const int water = owner_->waterCellFor(point);
  if (water == -1) return false;

  const float score = gScore_[water];
  if (!std::isfinite(score)) return false;

  const LatLng center = owner_->cellLatLng(water);
  cell = water;
  distanceKm = startOffsetKm_ + score + haversineKm(center, point);
  return true;
}

bool SeaField::distanceKmTo(const LatLng& point, double& outKm) const {
  int cell = -1;
  return berth(point, cell, outKm);
}

bool SeaField::routeTo(const LatLng& point, RouteResult& out) const {
  int cell = -1;
  double distanceKm = 0.0;
  if (!berth(point, cell, distanceKm)) return false;

  std::vector<int> cells;
  for (int current = cell; current != -1; current = cameFrom_[current]) {
    cells.push_back(current);
  }
  std::reverse(cells.begin(), cells.end());

  const std::vector<int> corners =
      owner_->pullStraight(owner_->pullStraight(cells, kMaxPullStep), static_cast<int>(cells.size()));

  out.distanceKm = distanceKm;
  out.ok = true;
  out.points.clear();
  out.points.reserve(corners.size() + 2);
  out.points.push_back(from_);
  for (int corner : corners) out.points.push_back(owner_->cellLatLng(corner));
  out.points.push_back(point);
  return true;
}

RouteResult SeaRouter::route(const LatLng& from, const LatLng& to) {
  RouteResult result;

  if (!ready_ || !std::isfinite(from.lat) || !std::isfinite(from.lng) ||
      !std::isfinite(to.lat) || !std::isfinite(to.lng)) {
    return result;
  }

  if (from.lat == to.lat && from.lng == to.lng) {
    result.distanceKm = 0.0;
    result.ok = true;
    result.points = {from, from};
    return result;
  }

  auto fieldPtr = field(from);
  if (fieldPtr && fieldPtr->routeTo(to, result)) return result;

  // Straight-line stand-in for water the search cannot reach.
  result.points = greatCirclePoints(from, to);
  if (result.points.size() < 2) {
    result.distanceKm = 0.0;
    result.ok = false;
    result.points.clear();
    return result;
  }

  result.distanceKm = haversineKm(from, to);
  result.ok = false;
  return result;
}

}  // namespace sea

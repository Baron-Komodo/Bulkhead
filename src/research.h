#pragma once

// Port research, ported from sea-builder/src/Research.js: four technologies, what they
// cost, what they do, and the berth/hull rules that depend on them.

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "data.h"

namespace sea {

constexpr int kMaxTechLevel = 3;

/// A fresh game: every technology at level 0. The four original technologies live in their
/// own fields; every later one is kept in `levels`, keyed by id.
struct Research {
  int cranes = 0;
  int dredging = 0;
  int security = 0;
  int warehouses = 0;
  std::unordered_map<std::string, int> levels;
};

/// A technology's level, clamped into 0..kMaxTechLevel.
int techLevel(const Research& research, const std::string& id);

/// Sets a technology's level, clamped into 0..kMaxTechLevel. Unknown ids are stored as well,
/// so the tree can carry technologies the economy does not read yet.
void setTechLevel(Research& research, const std::string& id, int level);

/// Whether the technology has been researched at least once. A child technology unlocks once
/// every one of its parents is acquired.
bool techAcquired(const Research& research, const std::string& id);

/// Whether every parent of the technology has been acquired, so it may be researched.
bool techUnlocked(const Research& research, const std::string& id);

struct Technology {
  std::string id;
  std::string name;
  std::string summary;
  std::array<int, kMaxTechLevel> costs;
  std::vector<std::string> parentTechRequired; // id of the parent
};

const std::vector<Technology>& technologies();

/// The price of the next level, or -1 when fully researched / unknown.
int nextLevelCost(const Research& research, const std::string& id);

/// Everything already spent across all technologies.
int investedPoints(const Research& research);

/// The parent ids of a technology that are still missing, for the "Research for ..." gate.
std::vector<std::string> missingParents(const Research& research, const std::string& id);

/// What a technology's effect reads as at a given level.
std::string techEffectText(const std::string& id, int level);

// Technology effects ---------------------------------------------------------

/// Automated Cranes: voyages take 10% less time per level.
double voyageDurationMultiplier(const Research& research);

/// Warehouse Expansion: freight pays 12% more per level.
double rewardMultiplier(const Research& research);

/// Security Systems: 15% base loss risk, minus 5 points per level, floored at 0.
double lossRiskPercent(const Research& research);

/// Research points earned for a voyage of the given length, floored and capped.
int researchPointsForVoyage(double distanceKm);

// Berths and hulls -----------------------------------------------------------

/// The class a hull of the given length belongs to, or '\0' for unknown lengths.
char vesselClass(double length);

/// The class of hull a berth can take once Deepwater Dredging is factored in.
char berthClass(const Port& port, const Research& research);

/// Whether a berth can take a hull of the given length.
bool canBerth(const Port& port, double length, const Research& research);

/// The label for a harbor size band. Empty for an unknown band.
std::string harborSizeLabel(const std::string& band);

}  // namespace sea

#pragma once

// Fleet organisation and the admiralty. A fleet is a named group of the player's naval
// units; an admiral is a flag officer that can be engaged and assigned to one fleet at a
// time, boosting every hull in it. How many fleets, ships per fleet and admirals the player
// may keep is decided by the research tree, so the caps grow as the navy is modernised.

#include <string>
#include <vector>

#include "data.h"
#include "research.h"

namespace sea {

// The technologies that raise the fleet caps.
constexpr const char* kFleetCommandTech = "fleet_command";
constexpr const char* kFleetOrganizationTech = "fleet_organization";
constexpr const char* kAdmiralCorpsTech = "admiral_corps";

// A fresh navy may raise two fleets of four ships each and keep two admirals on the payroll.
constexpr int kBaseFleetLimit = 2;
constexpr int kBaseShipsPerFleet = 4;
constexpr int kBaseAdmiralLimit = 2;

/// The stat an admiral's presence improves across the fleet. The ids in the admiral roster
/// map onto these, so the effect a card advertises is the effect combat actually reads.
enum class AdmiralBuff { Range, Damage, Repair, Speed, Defense, TorpedoSpeed, Magazine, SpeedDestroyerFrigatePatrol };

/// A flag officer the player can engage and put in command of a fleet.
struct Admiral {
  int id = 0;
  std::string name;
  std::string surname;
  std::string pathPhoto;
  std::string descriptionBuff;
  std::string generalDescription;
  std::string nationality;
  int buyCost = 0;
  int salary = 0;
  int level = 1;
  int experience = 0;
  bool hired = false;
  int fleetId = 0;  // the fleet this admiral commands, 0 when ashore

  std::string fullName() const { return name + " " + surname; }

  /// Which stat the admiral boosts. The roster is hand-written, so the buff follows the id
  /// rather than a stored enum: that keeps old saves readable.
  AdmiralBuff buffKind() const;

  /// The fractional bonus at the admiral's current level: 10% at level 1, +3% per level.
  float bonus() const;
};

/// A named group of the player's naval units. The ids refer to NavalUnit::id; a unit belongs
/// to at most one fleet, which the game state enforces when assigning.
struct Fleet {
  int id = 0;
  std::string name;
  std::vector<int> unitIds;
  int admiralId = 0;  // 0 when no admiral is embarked

  bool contains(int unitId) const;
  bool empty() const { return unitIds.empty(); }
};

/// The caps the player's current research allows.
struct FleetLimits {
  int maxFleets = kBaseFleetLimit;
  int maxShipsPerFleet = kBaseShipsPerFleet;
  int maxAdmirals = kBaseAdmiralLimit;
};

FleetLimits fleetLimits(const Research& research);

/// The roster of flag officers available in this game. They start ashore and unhired; the
/// player engages them from the admiralty screen.
std::vector<Admiral> generateAllAdmirals();

/// A short label for the buff, e.g. "Weapon range".
std::string admiralBuffLabel(AdmiralBuff buff);

}  // namespace sea

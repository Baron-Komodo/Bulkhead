#pragma once

// The static world data: the port and vessel catalogs, ported from the web game's
// assets. Loaded once at startup from the data/ directory next to the executable.

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "geo.h"



namespace sea {



/// Resolves a file name inside the data directory. Checks the SEA_BUILDER_DATA
/// environment variable, then a few locations relative to the working directory and the
/// executable, so the game runs from the build tree, an install tree, or the source tree.
std::string resolveDataFile(const std::string& name);

/// A World Port Index row, as the game consumes it.

const int MAX_PORT_LEVEL = 4;

/// One of the port modules the player can fit in an owned port's slots.
struct PortModules {
  int module_id = 0;
  std::string moduleName;
  std::string moduleDescription;
  std::string pathIconModule;
  int moduleCost = 0;
};

/// How many modules a port of a given level can hold. A fresh port opens with two slots and
/// every upgrade adds one, up to MAX_PORT_LEVEL.
inline int portSlotCountForLevel(int level) {
  return 2 + (std::max(1, std::min(level, MAX_PORT_LEVEL)) - 1);
}

/// How many gun mounts a port of a given level can arm. A fresh port carries one mount and
/// every upgrade adds another, the same way the module slots grow.
inline int portArmamentSlotCountForLevel(int level) {
  return std::max(1, std::min(level, MAX_PORT_LEVEL));
}

enum FireCategory {light, medium, heavy};


struct Armament {
  int id = 0;
  std::string name;
  std::string type; // machine gun, torpedos, rocket, ... base yourself in research type if not in research propose to add it in research -> it need to have a balance not too much research weapon but not enough not to reduce wepon to small categories
  FireCategory category; //global category of the armement
  std::string description;
  float damage; //take in count critical damage
  float power;
  // Seconds the crew needs to change a magazine once it is dry. The time between two rounds
  // comes from the mount's calibre and kind instead (see fireRateRoundsPerMinute in combat.h).
  float reload_time;
  // Rounds (or rockets, torpedoes) in one magazine. A mount starts full, fires at its cadence
  // until this reaches zero, spends reload_time changing the magazine, and refills.
  float ammo;
  bool is_efficient_against_ships = false;
  bool is_efficient_against_submarine = false;
  float radius;
  int cost; // cost of one ammo only

  std::string getCategory(int id) const {
    switch (id)
    {
    case 0:
      return "light";
    case 1:
      return "medium";
    case 2:
      return "heavy";
    default:
      return "unknown";
    }
  }
};

struct Port {
  int id = 0;
  std::string name;
  std::string country;  // ISO2, e.g. "NL"
  LatLng position;
  std::string maxVessel;   // S / M / L / ""
  std::string harborSize;  // V / S / M / L
  std::string repairCode;
  std::string drydock;
  std::string craneFixed;
  std::string fuelOil;
  std::string diesel;
  std::string commAir;
  std::string commRail;
  int health;
  int def;
  int attack;
  double buyPrice = 0.0;
  int level = 1;
  std::vector<Armament> equippedArmement;
  int totalAvailableSlotArmement;

  //modules
  std::vector<PortModules> modules;
  int totalSlotAvailable = 0;

  bool hasModule(int id) const {
    for(PortModules fitModule:  modules){
      if(fitModule.module_id == id){
        return true;
      }
    }
    return false;
  }

  bool hasPosition() const { return std::isfinite(position.lat) && std::isfinite(position.lng); }

  void statUpgrade(){
    attack *= 1.15;
    def *= 1.15;
    health *= 1.15;
  }
};

/// A vessel catalog row.
struct Vessel {
  int id = 0;
  std::string companyName;
  std::string shipName;
  int builtYear = 0;
  double gt = 0.0;
  double dwt = 0.0;
  double length = 0.0;
  double width = 0.0;
  std::string fleetNation;
};

/// A naval (military) vessel class, scraped from globalmilitary.net. These are not part of
/// the player's tradable fleet; the fleet screen lists them as reference naval fleets.
struct NavalVessel {
  int id = 0;
  std::string shipName;
  std::string type;          // e.g. "Patrol vessel", "Submarine"
  std::string manufacturer;
  std::string origin;        // operating country of origin
  std::string commissioned;
  std::string displacement;
  std::string maxSpeed;
  std::string summary;
  std::string navies;        // comma-separated operator names
  float health = 0.0f;
  float defense = 0.0f;
  float power = 0.0f;
  float reparationTime = 10.0f;
  std::vector<Armament> armament;

  /// The hull's own power plus every mounted weapon's power: the single "power" number the
  /// shipyard and the map readouts show for a class.
  float totalPower() const {
    float total = power;
    for (const Armament& weapon : armament) total += weapon.power;
    return total;
  }

  /// The combined damage of every mounted weapon, the class's attack rating.
  float totalDamage() const {
    float total = 0.0f;
    for (const Armament& weapon : armament) total += weapon.damage;
    return total;
  }
};
// Firing - which mounts a firing order may use and how fast they kill - lives in combat.h:
// it is a rule about the weapon's numbers plus the target's, not a property of the class,
// and keeping it out of the catalog lets the tests exercise it without a game state.

/// One good a country trades, with its annual value in thousands of USD.
struct TradeGood {
  std::string productCode;
  std::string description;
  double valueK = 0.0;
};

/// A country's top exports and imports.
struct CountryTrade {
  std::vector<TradeGood> exports;
  std::vector<TradeGood> imports;
};
/// Everything loaded from disk, immutable after startup.
class Catalog {
 public:
  bool loadPorts(const std::string& path);
  bool loadVessels(const std::string& path);
  bool loadNavalShips(const std::string& path);
  bool loadCountryCodes(const std::string& path);
  bool loadTrade(const std::string& path);

  const std::vector<Port>& ports() const { return ports_; }
  const std::vector<Vessel>& vessels() const { return vessels_; }
  const std::vector<NavalVessel>& navalVessels() const { return navalVessels_; }

  const Port* portById(int id) const;
  Port* portByIdM(int id);
  const Vessel* vesselById(int id) const;
  const NavalVessel* navalVesselById(int id) const;
  const CountryTrade* tradeFor(const std::string& countryCode) const;

  /// The country name for an ISO2 code ("NL" -> "Netherlands"), or an empty string when
  /// the code is unknown. Used to compare a port's nation with a naval class's origin.
  std::string countryNameFor(const std::string& iso2) const;

  /// Every good a country exports, biggest first. Empty when the country is unknown.
  std::vector<TradeGood> exportedGoods(const std::string& countryCode) const;
  std::vector<TradeGood> importedGoods(const std::string& countryCode) const;

  /// The world's biggest exports by product code, used when a country has no recorded
  /// trade so the player is never left with nothing to carry.
  const std::vector<TradeGood>& worldExports() const { return worldExports_; }

 private:
  std::vector<Port> ports_;
  std::vector<Vessel> vessels_;
  std::vector<NavalVessel> navalVessels_;
  std::unordered_map<int, size_t> portIndex_;
  std::unordered_map<int, size_t> vesselIndex_;
  std::unordered_map<int, size_t> navalIndex_;
  std::unordered_map<std::string, std::string> countryNames_;
  std::unordered_map<std::string, CountryTrade> trade_;
  std::vector<TradeGood> worldExports_;
};

}  // namespace sea

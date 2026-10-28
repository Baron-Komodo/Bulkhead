#include "game.h"
#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <iostream>
#include "json.hpp"
#include <vector>
#include <iostream>


namespace sea {
namespace {

std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool typeHas(const std::string& type, const char* needle) {
  return lowerCopy(type).find(needle) != std::string::npos;
}

std::string trimmed(const std::string& value) {
  const size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

/// "1st", "2nd", "3rd", "4th" ... for the automatic fleet names.
std::string ordinal(int value) {
  const int lastTwo = value % 100;
  if (lastTwo >= 11 && lastTwo <= 13) return std::to_string(value) + "th";
  switch (value % 10) {
    case 1:
      return std::to_string(value) + "st";
    case 2:
      return std::to_string(value) + "nd";
    case 3:
      return std::to_string(value) + "rd";
    default:
      return std::to_string(value) + "th";
  }
}

/// One fixed mount for a bot hull, in the same shape the catalog serves.
Armament makeWeapon(int id, const char* name, const char* type, FireCategory category,
                    float damage, float power, float reload, float ammo, float radius,
                    int cost) {
  Armament weapon;
  weapon.id = id;
  weapon.name = name;
  weapon.type = type;
  weapon.category = category;
  weapon.description = name;
  weapon.damage = damage;
  weapon.power = power;
  weapon.reload_time = reload;
  weapon.ammo = ammo;
  weapon.is_efficient_against_ships = true;
  weapon.is_efficient_against_submarine = false;
  weapon.radius = radius;
  weapon.cost = cost;
  return weapon;
}

/// How far an aggressive ship will look for a bot boat to chase. It is well beyond any mount's
/// reach: the ship closes the distance under its posture rather than waiting for the target to
/// wander into gun range.
constexpr double kAggroRadiusKm = 1800.0;

/// How far a bot hull looks for one of the player's ships to hunt, as a multiple of its own
/// longest reach: a corsair reacts to the traffic it could actually fight, not to a hull on
/// the far side of the sea. The old flat 1200 km reached from Gibraltar to Cyprus, so a boat
/// off Algiers set off after ships in the Adriatic. A patrol boat's 30 mm mount reaches 167 km
/// and a corsair frigate's 76 mm 389 km, so the floor keeps a small craft watching a usable
/// patch of water and the ceiling stops a long-legged hull (a submarine's torpedoes) from
/// clearing a whole basin.
constexpr double kBotHuntReachMultiple = 1.25;
constexpr double kBotHuntFloorKm = 180.0;
constexpr double kBotHuntCeilingKm = 400.0;

/// A hunt is carried a little past the distance that started it, so a fight does not flicker
/// on and off while the quarry hovers at the edge. Beyond that the boat gives up rather than
/// trailing a fleeing ship across the whole map.
constexpr double kBotBreakOffMultiple = 1.6;

/// A boat that has been shot at answers the shooter however far off it is, because being
/// attacked is its own trigger. The answer is still bounded, so a long-range strike does not
/// send a corsair to the far side of the world.
constexpr double kBotRetaliationLeashKm = 1500.0;
/// Minimum seconds between course plots while closing: a moving target would otherwise force a
/// fresh sea route on every frame.
constexpr double kBotRepathSeconds = 1.5;

/// A ship lying within this distance of its home yard can take on ammunition again.
constexpr double kRearmRadiusKm = 90.0;
/// Rounds a dry mount takes on per second while rearming. It is deliberately quick: the point
/// is that a fleet has to go home between fights, not that the player waits at a dock.
constexpr float kRearmRoundsPerSecond = 35.0f;

/// A repair alongside a quay is worked in steps rather than applied in one go, so the hull
/// visibly welds back together over a few seconds the way a refit would.
constexpr double kHealIntervalSeconds = 0.5;
/// Share of the hull's full hit points put back by each step. Ten steps bring a wreck back to
/// full, which is five seconds alongside - long enough to read as work, short enough to play.
constexpr float kHealStepFraction = 0.1f;

/// How fast a ship comes about to face what it is shooting at, in degrees per second. A hull
/// at a dead stop still trains its bow on the target, which reads as an RTS unit committing
/// to a fight rather than a static icon that happens to have guns.
constexpr float kTurnRateDegPerSecond = 75.0f;

/// The shortest signed turn from one heading to another, in degrees.
double headingDelta(double fromDeg, double toDeg) {
  double delta = std::fmod(toDeg - fromDeg, 360.0);
  if (delta > 180.0) delta -= 360.0;
  if (delta < -180.0) delta += 360.0;
  return delta;
}

/// The clearance a hull keeps when it is told to engage: it stops just inside its longest
/// usable mount's reach instead of sailing into the target's hull.
constexpr double kEngagementStandoff = 0.85;
/// A bot that finds the enemy inside this fraction of its reach backs off to reopen the range
/// instead of letting a faster hull run it down at point-blank.
constexpr double kBotEvadeFraction = 0.55;

/// Spacing between neighbouring hulls when a fleet is given a destination. It is deliberately
/// wider than any hull: the point of the block is that an arriving fleet reads as a squadron
/// drawn up for battle, not a pile of overlapping sprites.
constexpr double kFormationSpacingKm = 130.0;

/// A point `km` from `origin` on the given compass bearing. Good for the short hops a hull
/// uses to hold its station; long legs go through the sea router.
LatLng pointOnBearing(const LatLng& origin, double bearingDeg, double km) {
  constexpr double kPi = 3.14159265358979323846;
  const double radians = bearingDeg * kPi / 180.0;
  const double latKm = km / 111.32;
  const double lngKm = km / (111.32 * std::max(0.05, std::cos(origin.lat * kPi / 180.0)));
  return {origin.lat + std::cos(radians) * latKm,
          wrapLongitude(origin.lng + std::sin(radians) * lngKm)};
}

/// Advances a hull along its waypoints for one step of its speed budget, turning it to face
/// the way it actually moved. Shared by the player's ships and the bot hulls, so a directed
/// boat and a hunting one sail by exactly the same rule.
void advanceAlongPath(LatLng& position, std::vector<LatLng>& path, double budget,
                      double& headingDeg) {
  while (budget > 0.0 && !path.empty()) {
    const LatLng from = position;
    const LatLng next = path.front();
    const double legKm = haversineKm(from, next);

    if (legKm <= 1e-6) {
      position = next;
      path.erase(path.begin());
      continue;
    }
    if (budget >= legKm) {
      budget -= legKm;
      position = next;
      path.erase(path.begin());
    } else {
      const double t = budget / legKm;
      position = {from.lat + (next.lat - from.lat) * t, from.lng + (next.lng - from.lng) * t};
      budget = 0.0;
    }

    double heading = 0.0;
    if (mapHeadingDegrees(from, position, heading)) headingDeg = heading;
  }
}

/// How fast a round crosses to its target. The speed is chosen so the flight is *readable*
/// rather than literal: past about a second a shell stops reading as a shot and starts reading
/// as a bug, and under a fifth of a second the eye never sees it at all. Guided weapons are
/// allowed a slightly longer flight because their trail is the point.
float flightSpeedKmPerSecond(FireCategory category, Delivery delivery, double distanceKm) {
  double nominal = 900.0;  // a light gun's shell
  if (category == FireCategory::medium) nominal = 620.0;
  else if (category == FireCategory::heavy) nominal = 420.0;

  double minSeconds = 0.18;
  double maxSeconds = 0.9;
  switch (delivery) {
    case Delivery::MachineGun:
      nominal = 1100.0;
      minSeconds = 0.12;
      maxSeconds = 0.45;
      break;
    case Delivery::Rocket:
      nominal = 480.0;
      minSeconds = 0.30;
      maxSeconds = 1.2;
      break;
    case Delivery::Missile:
      nominal = 560.0;
      minSeconds = 0.28;
      maxSeconds = 1.0;
      break;
    case Delivery::Torpedo:
      nominal = 220.0;
      minSeconds = 0.45;
      maxSeconds = 1.8;
      break;
    case Delivery::Gun:
      break;
  }

  const double seconds = std::min(maxSeconds, std::max(minSeconds, distanceKm / nominal));
  return static_cast<float>(distanceKm / std::max(0.05, seconds));
}

std::string moneyText(double value) {
  // Group the integer part with commas, the way the web UI reads.
  const long long rounded = static_cast<long long>(std::llround(value));
  std::string digits = std::to_string(rounded < 0 ? -rounded : rounded);
  std::string grouped;
  int count = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (count == 3) {
      grouped.push_back(',');
      count = 0;
    }
    grouped.push_back(*it);
    ++count;
  }
  std::reverse(grouped.begin(), grouped.end());
  return "$" + grouped;
}

}  // namespace

double botHuntRadiusKm(double reachKm) {
  return std::min(kBotHuntCeilingKm, std::max(kBotHuntFloorKm, reachKm * kBotHuntReachMultiple));
}

NavalModelKind navalModelKindFor(const NavalVessel& vessel) {
  const std::string type = lowerCopy(vessel.type + " " + vessel.shipName);
  if (type.find("submarine") != std::string::npos) return NavalModelKind::submarine;
  if (type.find("cruiser") != std::string::npos) return NavalModelKind::cruiser;
  if (type.find("corvette") != std::string::npos) return NavalModelKind::corvette;
  if (type.find("aircraft") != std::string::npos) return NavalModelKind::aircraft_carrier;
  if (type.find("carrier") != std::string::npos) return NavalModelKind::aircraft_carrier;
  if (type.find("frigate") != std::string::npos) return NavalModelKind::frigate;
  if (type.find("patrol") != std::string::npos) return NavalModelKind::patrol;
  if (type.find("destroyer") != std::string::npos) return NavalModelKind::destroyer;
  if (type.find("battleship") != std::string::npos) return NavalModelKind::other;



  // Big surface combatants draw with the full hull sprite; the small craft and auxiliaries
  // use the shorter one so a patrol boat does not read as a cruiser at a glance.
  static const char* kBig[] = {"carrier", "battleship", "cruiser", "destroyer", "frigate",
                               "corvette"};
  for (const char* needle : kBig) {
    if (type.find(needle) != std::string::npos) return NavalModelKind::other;
  }

  static const char* kSmall[] = {"patrol",   "missile",  "torpedo",  "fast attack", "mine",
                                 "landing",  "amphibious", "auxiliary", "rescue",   "survey",
                                 "tender",   "replenishment", "training", "research", "boat"};
  for (const char* needle : kSmall) {
    if (type.find(needle) != std::string::npos) return NavalModelKind::patrol;
  }
  return NavalModelKind::other;
}

double navalUnitCost(const NavalVessel& vessel, bool sameNation) {
  const std::string type = lowerCopy(vessel.type);
  double cost = 2200.0;
  if (type.find("submarine") != std::string::npos) cost = 5200.0;
  else if (type.find("carrier") != std::string::npos) cost = 12000.0;
  else if (type.find("battleship") != std::string::npos) cost = 9000.0;
  else if (type.find("cruiser") != std::string::npos) cost = 6500.0;
  else if (type.find("destroyer") != std::string::npos) cost = 4800.0;
  else if (type.find("frigate") != std::string::npos) cost = 3800.0;
  else if (type.find("corvette") != std::string::npos) cost = 3000.0;
  else if (type.find("patrol") != std::string::npos) cost = 1400.0;
  else if (type.find("ammunition") != std::string::npos) cost = 2000.0;

  if (sameNation) cost *= 0.8;
  return std::round(cost);
}

const std::vector<PirateBand>& pirateBands() {
  // Fixed data: three bands, each holding a patch of water. A band's name is what its hulls
  // fly on the map, and each anchor is one sentry: where it sits, which way it faces and which
  // hull sprite it draws. Nothing else about a pirate is stored - it has no economy to run and
  // no harbour to sail from, so these stations are its whole story.
  static const std::vector<PirateBand> kBands = {
      {"Somali Pirate",
       {
           {"Sea Wolf", {12.09, 53.29}, 90.0, NavalModelKind::frigate},
           {"Coral Fang", {17.03, 50.23}, 75.0, NavalModelKind::other},
           {"Monsoon", {11.06, 55.29}, 200.0, NavalModelKind::cruiser},
           {"Salt Dhow", {18.08, 51.15}, 45.0, NavalModelKind::patrol},
           {"Pearl Ghost", {10.10, 52.30}, 45.0, NavalModelKind::patrol},
       }},
      {"Barbary corsairs",
       {
           {"Barbary Falcon", {37.82, 3.10}, 90.0, NavalModelKind::patrol},
           {"Sirocco", {39.15, 5.89}, 75.0, NavalModelKind::other},
           {"Scimitar", {38.14, 9.85}, 200.0, NavalModelKind::frigate},
           {"Sea Ghoul", {41.31, 7.65}, 45.0, NavalModelKind::frigate},
       }},
      {"Ching Shih",
       {
           {"Red Banner", {29.03, 128.60}, 90.0, NavalModelKind::cruiser},
           {"Dragon Tail", {27.08, 132.53}, 75.0, NavalModelKind::patrol},
           {"Typhoon", {26.30, 124.61}, 200.0, NavalModelKind::frigate},
           {"Junk Raider", {21.83, 125.26}, 45.0, NavalModelKind::patrol},
       }},
  };
  return kBands;
}

const std::vector<EnemyPower>& enemyPowers() {
  // The nations a game is played against, and the waters each is seeded in. A power is handed
  // the real harbour nearest its waters as its capital and lives off that port's economy from
  // then on: there is no anchor to respawn at and nothing on this list is placed on the map.
  static const std::vector<EnemyPower> kPowers = {
      {"England", {50.20, -3.80}},
      {"Spain", {36.40, -6.40}},
      {"Netherlands", {52.90, 4.10}},
  };
  return kPowers;
}

void GameState::attach(Catalog* catalog, SeaRouter* router) {
  catalog_ = catalog;
  router_ = router;
}

void GameState::newGame() {
  money_ = kBaseMoney;
  research_ = Research{};
  researchPoints_ = 0;
  claimedPorts_.clear();
  ownedVessels_.clear();
  portUpgrades_.clear();
  portDefence_.clear();
  portMounts_.clear();
  voyages_.clear();
  navalUnits_.clear();
  admirals_ = generateAllAdmirals();
  fleets_.clear();
  nextFleetId_ = 1;
  projectiles_.clear();
  combatEvents_.clear();
  nextNavalUnitId_ = 1;
  nextProjectileId_ = 1;
  delivered_.clear();
  messages_.clear();
  clockOriginMs_ = 0.0;
  lastGameTimeMs_ = 0.0;

  if (catalog_ != nullptr && catalog_->vesselById(kStarterVesselId) != nullptr) {
    ownedVessels_.insert(kStarterVesselId);
  }

  // The player is handed one harbour for free, so a new game starts with a berth to sail from
  // and a quay to lay hulls down at instead of an empty treasury and no way to earn. It is
  // Nantes for now; change kStarterPortId to hand out a different one.
  if (catalog_ != nullptr) {
    if (Port* starter = catalog_->portByIdM(kStarterPortId)) {
      claimedPorts_.insert(kStarterPortId);
      ensurePortArmament(*starter);
    }
  }


  // The two kinds of opponent, seeded into the same hull list. Any hulls from a previous game
  // are dropped first, so a restart cannot leave two hulls sharing an id.
  botBoats_.clear();
  nextBoatId_ = 1;
  // The pirate bands, one sentry per anchor: fixed obstacles on their own water that never
  // move and own nothing. They are not players and are not in the faction roster.
  seedPirates();
  // The enemy powers, one per nation: each is given the real harbour nearest its own waters, an
  // opening hull there and a treasury, and from then on it is an opponent with an economy like
  // the player's. It buys everything it has; nothing is placed on an anchor for it.
  seedFactions();
}

void GameState::seedPirates() {
  if (catalog_ == nullptr) return;

  // Placed hull by hull on the band's stations. Nothing here is bought: a pirate hull is preset
  // data armed from its kind, owned by the band, with no class and no yard, so it can never be
  // reinforced, replaced or rearmed by an economy - it holds its patch until it is sunk.
  for (const PirateBand& band : pirateBands()) {
    for (const Anchor& anchor : band.anchors) {
      BotBoat boat;
      boat.id = nextBoatId_++;
      boat.name = anchor.name;
      boat.owner = band.name;
      boat.pirate = true;
      boat.classId = 0;    // a kind's own preset, not a class it was laid down from
      boat.homePortId = 0; // no yard: a pirate band never rearms at a port
      boat.position = anchor.position;
      boat.station = anchor.position;
      boat.headingDeg = anchor.heading;
      boat.kind = anchor.kind;
      fitBotArmament(boat);
      botBoats_.push_back(std::move(boat));
    }
  }
}

std::vector<int> GameState::ownedPortIds() const {
  std::vector<int> ids(claimedPorts_.begin(), claimedPorts_.end());
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::vector<int> GameState::ownedVesselIds() const {
  std::vector<int> ids(ownedVessels_.begin(), ownedVessels_.end());
  std::sort(ids.begin(), ids.end());
  return ids;
}

double GameState::passiveIncomePerSecond() const {
  int income = 0;

  for (int portID: ownedPortIds()){
    const Port* port = catalog_->portById(portID);
    if (port == nullptr) continue;
    income += 25 * port->activeFisher;
  }

  for (const NavalUnit& navalVessel : navalUnits()) {
    if(navalVessel.type == "Unmanned vessel" ||
      navalVessel.type == "Patrol vessel"){
        // income -= 50 * (catalog_->navalVesselById(navalVessel.classId)->armament.size() * 1.5);
      }
      if(navalVessel.type == "Corvette"){
        // income -= 75 * (catalog_->navalVesselById(navalVessel.classId)->armament.size() * 1.5);
      }
      if(navalVessel.type == "Frigate"){
        // income -= 100 * (catalog_->navalVesselById(navalVessel.classId)->armament.size() * 1.5);
      }
      if(navalVessel.type == "Submarine"){
        // income += 150 * (catalog_->navalVesselById(navalVessel.classId)->armament.size() * 1.5);
      }
      if(navalVessel.type == "Destroyer"){
        // income -= 190 * (catalog_->navalVesselById(navalVessel.classId)->armament.size() * 1.5);
      }
      if(navalVessel.type == "Destroyer"){
        // income -= 250 * (catalog_->navalVesselById(navalVessel.classId)->armament.size() * 1.5);
      }
  }


  const double raw = kPassiveIncomePerPortPerSecond * static_cast<double>(ownedPortCount())
  + income;

  return std::round(raw * 100.0) / 100.0;
}

bool GameState::claimPort(int portId, std::string& error) {
  if (catalog_ == nullptr) {
    error = "No game data loaded.";
    return false;
  }

  const Port* port = catalog_->portById(portId);
  if (port == nullptr) {
    error = "Unknown port.";
    return false;
  }
  if (portOwned(portId)) {
    error = "You already own this port.";
    return false;
  }

  // A harbour is not bought off the map: its battery has to be broken first, by ships sent to
  // bombard it. Until then the port is still somebody else's and still shooting.
  if (!portDefeated(portId)) {
    error = port->name + " is still fighting: bring its defences down first (";
    error += std::to_string(static_cast<int>(std::lround(portDefence(portId)))) + " / " +
             std::to_string(static_cast<int>(std::lround(portDefenceMax(portId)))) + ").";
    return false;
  }

  const double cost = portCost(*port);
  if (money_ < cost) {
    error = "Not enough money!";
    return false;
  }

  money_ -= cost;
  claimedPorts_.insert(portId);
  // A power that held the harbour has lost it: its yard list drops the port, so its income and
  // its navy shrink the way the player's would if the same happened to them.
  for (AIPlayer& power : factions_) {
    power.portIds.erase(std::remove(power.portIds.begin(), power.portIds.end(), portId),
                        power.portIds.end());
  }
  // The harbour's own battery is gone with its defences, so the quay starts fresh: the port's
  // machine gun is mounted and the shoreline mounts the fight left behind are dropped.
  portMounts_.erase(portId);
  if (Port* claimed = catalog_->portByIdM(portId)) ensurePortArmament(*claimed);
  return true;
}

double GameState::portDefenceMax(int portId) const {
  if (catalog_ == nullptr) return 0.0;
  const Port* port = catalog_->portById(portId);
  if (port == nullptr) return 0.0;
  return std::max(1.0, static_cast<double>(port->health) * kPortDefenceScale);
}

double GameState::portDefence(int portId) const {
  const auto found = portDefence_.find(portId);
  if (found != portDefence_.end()) return static_cast<double>(found->second);
  return portDefenceMax(portId);
}

bool GameState::portDefeated(int portId) const {
  return !portOwned(portId) && portDefence(portId) <= 0.0;
}

std::vector<int> GameState::contestedPortIds() const {
  std::vector<int> ids;
  for (const auto& entry : portDefence_) {
    if (entry.second <= 0.0f || portOwned(entry.first)) continue;
    ids.push_back(entry.first);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

bool GameState::bombardPort(int portId, float damage) {
  if (catalog_ == nullptr) return false;
  const Port* port = catalog_->portById(portId);
  if (port == nullptr) return false;

  const double current = portDefence(portId);
  if (current <= 0.0) return true;

  // A harbour's plating soaks its share of the round exactly like a hull's does.
  const double landed = static_cast<double>(damageAfterDefense(damage, static_cast<float>(port->def)));
  const double remaining = std::max(0.0, current - landed);
  portDefence_[portId] = static_cast<float>(remaining);
  if (remaining > 0.0) return false;

  messages_.push_back(port->name + "'s defences are broken. The harbour can be bought now.");
  return true;
}

void GameState::breakPortDefences(int portId) {
  if (catalog_ == nullptr || catalog_->portById(portId) == nullptr) return;
  portDefence_[portId] = 0.0f;
}

bool GameState::stormPort(int portId, std::string& error) {
  breakPortDefences(portId);
  return claimPort(portId, error);
}

bool GameState::buildVessel(int vesselId, std::string& error) {
  if (catalog_ == nullptr) {
    error = "No game data loaded.";
    return false;
  }

  const Vessel* vessel = catalog_->vesselById(vesselId);
  if (vessel == nullptr) {
    error = "Unknown vessel.";
    return false;
  }
  if (vesselOwned(vesselId)) {
    error = vessel->companyName + " (" + vessel->shipName + ") is already in your fleet.";
    return false;
  }

  const double cost = hullPrice(vessel->gt);
  if (money_ < cost) {
    error = "Not enough money!";
    return false;
  }

  money_ -= cost;
  ownedVessels_.insert(vesselId);
  return true;
}

const NavalUnit* GameState::navalUnitById(int id) const {
  for (const NavalUnit& unit : navalUnits_) {
    if (unit.id == id) return &unit;
  }
  return nullptr;
}

const Admiral* GameState::admiralById(int id) const {
  for (const Admiral& admiral : admirals_) {
    if (admiral.id == id) return &admiral;
  }
  return nullptr;
}

const Fleet* GameState::fleetById(int id) const {
  for (const Fleet& fleet : fleets_) {
    if (fleet.id == id) return &fleet;
  }
  return nullptr;
}

const Fleet* GameState::fleetForUnit(int unitId) const {
  for (const Fleet& fleet : fleets_) {
    if (fleet.contains(unitId)) return &fleet;
  }
  return nullptr;
}

int GameState::hiredAdmiralCount() const {
  int count = 0;
  for (const Admiral& admiral : admirals_) {
    if (admiral.hired) ++count;
  }
  return count;
}

float GameState::fleetBuffMultiplier(int unitId, AdmiralBuff buff) const {
  const Fleet* fleet = fleetForUnit(unitId);
  if (fleet == nullptr || fleet->admiralId == 0) return 1.0f;
  const Admiral* admiral = admiralById(fleet->admiralId);
  if (admiral == nullptr || !admiral->hired || admiral->buffKind() != buff) return 1.0f;
  return 1.0f + admiral->bonus();
}

std::vector<FleetRecap> GameState::fleetRecaps() const {
  const FleetLimits limits = fleetLimits();
  std::vector<FleetRecap> recaps;
  recaps.reserve(fleets_.size());

  for (const Fleet& fleet : fleets_) {
    FleetRecap recap;
    recap.fleetId = fleet.id;
    recap.name = fleet.name;
    recap.admiralId = fleet.admiralId;
    recap.capacity = limits.maxShipsPerFleet;

    for (const int unitId : fleet.unitIds) {
      const NavalUnit* unit = navalUnitById(unitId);
      if (unit == nullptr) continue;
      ++recap.shipCount;
      recap.health += unit->health;
      recap.maxHealth += unit->maxHealth;
      if (catalog_ != nullptr) {
        const NavalVessel* klass = catalog_->navalVesselById(unit->classId);
        if (klass != nullptr) recap.power += klass->totalPower();
      }

      if (unit->sunk()) {
        ++recap.sunk;
        continue;
      }
      if (unit->targetBoatId != 0) ++recap.engaged;
      if (unit->moving()) {
        ++recap.atSea;
      } else if (unit->targetBoatId == 0 && catalog_ != nullptr) {
        const Port* home = catalog_->portById(unit->homePortId);
        if (home != nullptr && haversineKm(unit->position, home->position) <= kRearmRadiusKm) {
          ++recap.docked;
        }
      }
      if (unit->maxHealth > 0.0f && unit->health < unit->maxHealth * 0.99f) ++recap.damaged;
    }
    recaps.push_back(std::move(recap));
  }
  return recaps;
}

std::vector<LatLng> GameState::navalCourse(const LatLng& from, const LatLng& to) const {
  const std::vector<LatLng> straight{from, to};
  if (router_ == nullptr || !router_->ready()) return straight;

  // Sample the direct line: when every sample is open water the unit can steer straight for
  // the target, which is what makes short hops across a bay feel immediate instead of routing
  // them through the whole sea grid.
  const double totalKm = haversineKm(from, to);
  const int steps = std::max(2, static_cast<int>(totalKm / 60.0) + 1);
  for (int i = 1; i < steps; ++i) {
    const double t = static_cast<double>(i) / steps;
    const LatLng point{from.lat + (to.lat - from.lat) * t, from.lng + (to.lng - from.lng) * t};
    if (router_->isLandAt(point.lat, point.lng)) {
      // Land in the way: fall back to the sea route around it, which is what carries a unit
      // out of the Mediterranean and around Africa to reach another ocean.
      const RouteResult route = router_->route(from, to);
      if (route.ok && route.points.size() >= 2) return route.points;
      return straight;
    }
  }
  return straight;
}

bool GameState::buildNavalUnit(int classId, int portId, double cost, std::string& error) {
  if (catalog_ == nullptr) {
    error = "No game data loaded.";
    return false;
  }
  const Port* port = catalog_->portById(portId);
  if (port == nullptr) {
    error = "Unknown port.";
    return false;
  }
  if (!portOwned(portId)) {
    error = "You do not own " + port->name + ".";
    return false;
  }
  if (!portHasModule(portId, 2)) {
    error = port->name + " has no shipyard fitted.";
    return false;
  }

  const NavalVessel* vessel = nullptr;
  for (const NavalVessel& row : catalog_->navalVessels()) {
    if (row.id == classId) {
      vessel = &row;
      break;
    }
  }
  if (vessel == nullptr) {
    error = "Unknown naval class.";
    return false;
  }
  if (money_ < cost) {
    error = "Not enough money!";
    return false;
  }

  money_ -= cost;
  NavalUnit unit;
  unit.id = nextNavalUnitId_++;
  unit.classId = vessel->id;
  unit.className = vessel->shipName;
  unit.type = vessel->type;
  unit.kind = navalModelKindFor(*vessel);
  unit.homePortId = port->id;
  unit.position = port->position;

  // Berth the new hull a short way off the quay, fanning each new ship out by the golden
  // angle so a task force lays alongside instead of piling onto the port's one pixel. A berth
  // that would land on the coast falls back to the port itself.
  if (router_ != nullptr && router_->ready()) {
    const double angle = static_cast<double>(unit.id) * 2.39996323;
    const double radius = 0.22 + 0.05 * static_cast<double>(unit.id % 3);
    const LatLng berth{port->position.lat + std::cos(angle) * radius,
                       port->position.lng + std::sin(angle) * radius * 1.4};
    if (!router_->isLandAt(berth.lat, berth.lng)) unit.position = berth;
  }

  unit.maxHealth = std::max(60.0f, vessel->health);
  unit.health = unit.maxHealth;
  unit.defense = vessel->defense;
  unit.fireOrder = kFireAuto;
  fitMounts(unit, *vessel);

  navalUnits_.push_back(std::move(unit));
  return true;
}

void GameState::fitMounts(NavalUnit& unit, const NavalVessel& klass) const {
  unit.mounts.assign(klass.armament.size(), MountState{});
  for (size_t i = 0; i < klass.armament.size(); ++i) {
    // A mount with no catalog magazine would never fire, so give it one round rather than
    // letting a data omission silence the ship.
    unit.mounts[i].ammo = std::max(1.0f, klass.armament[i].ammo);
    unit.mounts[i].reload = 0.0f;
  }
}

bool GameState::orderNavalUnit(int unitId, const LatLng& target, std::string& error) {
  NavalUnit* unit = nullptr;
  for (NavalUnit& candidate : navalUnits_) {
    if (candidate.id == unitId) {
      unit = &candidate;
      break;
    }
  }
  if (unit == nullptr) {
    error = "Unknown naval unit.";
    return false;
  }
  if (router_ != nullptr && router_->ready() && router_->isLandAt(target.lat, target.lng)) {
    error = "That point is on land.";
    return false;
  }

  std::vector<LatLng> course = navalCourse(unit->position, target);
  if (course.size() < 2) {
    error = "No course to that point.";
    return false;
  }
  course.erase(course.begin());  // the current position is not a waypoint
  unit->path = std::move(course);
  // Steering somewhere new is also a retreat: the ship stops shooting at whatever it was
  // engaging, otherwise it would sail away while still trading broadsides with a distant hull.
  unit->targetBoatId = 0;
  unit->targetPortId = 0;
  unit->targetExplicit = false;
  return true;
}

int GameState::orderNavalUnitsAttackPort(const std::vector<int>& unitIds, int portId,
                                         std::string& error) {
  const Port* port = catalog_ != nullptr ? catalog_->portById(portId) : nullptr;
  if (port == nullptr || !port->hasPosition()) {
    error = "Unknown port.";
    return 0;
  }
  if (portOwned(portId)) {
    error = "You already own this port.";
    return 0;
  }
  if (portDefeated(portId)) {
    error = port->name + "'s defences are already broken: buy it from the port panel.";
    return 0;
  }

  // Attacking a harbour puts it on the board: from this moment its battery is manned and its
  // defence pool is drawn, so the siege is visible before the first round lands.
  portDefence_.emplace(portId, static_cast<float>(portDefenceMax(portId)));

  int ordered = 0;
  for (const int id : expandFleetSelection(unitIds)) {
    NavalUnit* unit = nullptr;
    for (NavalUnit& candidate : navalUnits_) {
      if (candidate.id == id) unit = &candidate;
    }
    if (unit == nullptr || unit->sunk()) continue;

    // The course runs to open water beside the harbour - a river port's own coordinate reads
    // as land - and advanceNavalUnits clears it once the ship is inside its firing range.
    std::string localError;
    if (!orderNavalUnitToPort(id, portId, localError)) {
      error = localError;
      continue;
    }
    unit->targetPortId = portId;
    unit->targetExplicit = true;
    ++ordered;
  }
  return ordered;
}

bool GameState::orderNavalUnitToPort(int unitId, int portId, std::string& error) {
  const Port* port = catalog_ != nullptr ? catalog_->portById(portId) : nullptr;
  if (port == nullptr || !port->hasPosition()) {
    error = "Unknown port.";
    return false;
  }

  LatLng berth = port->position;
  if (router_ != nullptr && router_->ready() && router_->isLandAt(berth.lat, berth.lng)) {
    bool found = false;
    for (double radius = 0.12; radius <= 2.4 && !found; radius += 0.12) {
      for (int step = 0; step < 16 && !found; ++step) {
        const double angle = static_cast<double>(step) * (2.0 * 3.14159265358979323846 / 16.0);
        const LatLng candidate{port->position.lat + std::sin(angle) * radius,
                               port->position.lng + std::cos(angle) * radius * 1.4};
        if (!router_->isLandAt(candidate.lat, candidate.lng)) {
          berth = candidate;
          found = true;
        }
      }
    }
    if (!found) {
      error = "No open water beside that port.";
      return false;
    }
  }
  return orderNavalUnit(unitId, berth, error);
}


int GameState::calculatePower(){
  if (catalog_ == nullptr) return 0;
  int power = 0;
  for (const NavalUnit& unit : navalUnits_) {
    if (unit.sunk()) continue;
    const NavalVessel* klass = catalog_->navalVesselById(unit.classId);
    if (klass == nullptr) continue;
    for (const Armament& armament : klass->armament) {
      power += static_cast<int>(armament.power);
    }
  }
  return power + static_cast<int>(admirals_.size()) * 15;
}

const Port* GameState::healPortFor(const NavalUnit& unit) const {
  if (catalog_ == nullptr) return nullptr;
  const Port* best = nullptr;
  double bestKm = kRearmRadiusKm;
  for (const int portId : ownedPortIds()) {
    const Port* port = catalog_->portById(portId);
    if (port == nullptr || !port->hasPosition()) continue;
    const double km = haversineKm(unit.position, port->position);
    if (km <= bestKm) {
      bestKm = km;
      best = port;
    }
  }
  return best;
}

bool GameState::canHealShip(int unitId) const {
  const NavalUnit* unit = navalUnitById(unitId);
  if (unit == nullptr || unit->sunk() || unit->maxHealth <= 0.0f) return false;
  if (unit->health >= unit->maxHealth) return false;
  return healPortFor(*unit) != nullptr;
}

bool GameState::HealShip(int unitId, std::string& error) {
  NavalUnit* unit = nullptr;
  for (NavalUnit& candidate : navalUnits_) {
    if (candidate.id == unitId) {
      unit = &candidate;
      break;
    }
  }
  if (unit == nullptr) {
    error = "Unknown naval unit.";
    return false;
  }
  if (unit->sunk()) {
    error = "That hull is already on the bottom.";
    return false;
  }
  if (unit->maxHealth > 0.0f && unit->health >= unit->maxHealth) {
    error = unit->className + " needs no repair.";
    return false;
  }

  const Port* port = healPortFor(*unit);
  if (port == nullptr) {
    error = "No owned port alongside to repair at.";
    return false;
  }

  // Re-issuing a repair that is already running keeps the crew working rather than restarting
  // the clock, so a stray click cannot slow the job down.
  unit->healPortId = port->id;
  if (unit->healTimer <= 0.0f) unit->healTimer = static_cast<float>(kHealIntervalSeconds);
  return true;
}


void GameState::advanceRepairs(double dtSeconds) {
  if (dtSeconds <= 0.0) return;

  for (NavalUnit& unit : navalUnits_) {
    if (!unit.healing()) continue;

    // Repair work only continues alongside the port that took the ship in: sailing off, losing
    // the port or finishing the job ends the order instead of healing at sea.
    const Port* port = catalog_ != nullptr ? catalog_->portById(unit.healPortId) : nullptr;
    if (port == nullptr || !port->hasPosition() || !portOwned(port->id) || unit.sunk() ||
        unit.health >= unit.maxHealth ||
        haversineKm(unit.position, port->position) > kRearmRadiusKm) {
      unit.healPortId = 0;
      unit.healTimer = 0.0f;
      continue;
    }

    unit.healTimer -= static_cast<float>(dtSeconds);
    if (unit.healTimer > 0.0f) continue;

    // One step per elapsed interval, and the clock is restarted rather than accumulated: a
    // long frame (a load, a debug jump) must not chain several steps together and heal a fleet
    // in one tick.
    unit.healTimer = static_cast<float>(kHealIntervalSeconds);
    const float step = std::max(1.0f, unit.maxHealth * kHealStepFraction);
    unit.health = std::min(unit.maxHealth, unit.health + step);
    if (unit.health >= unit.maxHealth) {
      unit.healPortId = 0;
      unit.healTimer = 0.0f;
    }
  }
}

std::vector<int> GameState::expandFleetSelection(const std::vector<int>& unitIds) const {
  std::vector<int> expanded;
  auto add = [&](int id) {
    if (std::find(expanded.begin(), expanded.end(), id) == expanded.end()) expanded.push_back(id);
  };

  for (const int id : unitIds) {
    add(id);
    const Fleet* fleet = fleetForUnit(id);
    if (fleet == nullptr) continue;
    for (const int mateId : fleet->unitIds) {
      const NavalUnit* mate = navalUnitById(mateId);
      if (mate != nullptr && !mate->sunk()) add(mateId);
    }
  }
  return expanded;
}

int GameState::orderNavalUnits(const std::vector<int>& unitIds, const LatLng& target) {
  const std::vector<int> ids = expandFleetSelection(unitIds);
  std::vector<NavalUnit*> units;
  units.reserve(ids.size());
  for (const int id : ids) {
    NavalUnit* unit = nullptr;
    for (NavalUnit& candidate : navalUnits_) {
      if (candidate.id == id) {
        unit = &candidate;
        break;
      }
    }
    if (unit != nullptr && !unit->sunk()) units.push_back(unit);
  }
  if (units.empty()) return 0;

  // Lay the fleet out as a small block facing the destination: up to three ships wide, then
  // as many ranks as the roster needs. The block is centred on the clicked point and turned
  // to face the approach, which is the readable formation language of classic RTS armies.
  LatLng centroid{0.0, 0.0};
  for (const NavalUnit* unit : units) {
    centroid.lat += unit->position.lat;
    centroid.lng += unit->position.lng;
  }
  centroid.lat /= static_cast<double>(units.size());
  centroid.lng /= static_cast<double>(units.size());

  double approach = 0.0;
  if (!mapHeadingDegrees(centroid, target, approach)) approach = 0.0;
  const int count = static_cast<int>(units.size());
  const int columns = std::min(3, count);
  const int rows = (count + columns - 1) / columns;

  int ordered = 0;
  std::string error;
  for (int index = 0; index < count; ++index) {
    const int column = index % columns;
    const int row = index / columns;
    const double side = (static_cast<double>(column) - (columns - 1) * 0.5) *
                        kFormationSpacingKm;
    const double back = (static_cast<double>(row) - (rows - 1) * 0.5) *
                        kFormationSpacingKm;
    LatLng slot = pointOnBearing(target, approach + 180.0, back);
    slot = pointOnBearing(slot, approach + 90.0, side);
    // A slot that falls on a coast is pulled back to the clicked water point rather than
    // silently dropping that ship out of the formation.
    if (router_ != nullptr && router_->ready() && router_->isLandAt(slot.lat, slot.lng)) {
      slot = target;
    }
    if (orderNavalUnit(units[static_cast<size_t>(index)]->id, slot, error)) ++ordered;
  }
  return ordered;
}

int GameState::orderNavalUnitsAttack(const std::vector<int>& unitIds, int boatId,
                                     std::string& error) {
  const BotBoat* boat = botBoatById(boatId);
  if (boat == nullptr || boat->sunk()) {
    error = "No such target.";
    return 0;
  }

  int ordered = 0;
  for (const int id : expandFleetSelection(unitIds)) {
    const NavalUnit* unit = navalUnitById(id);
    if (unit == nullptr || unit->sunk()) continue;
    std::string localError;
    if (orderNavalUnitAttack(id, boatId, localError)) {
      ++ordered;
    } else {
      error = localError;
    }
  }
  return ordered;
}

void GameState::stopNavalUnit(int unitId) {
  for (NavalUnit& unit : navalUnits_) {
    if (unit.id == unitId) {
      unit.path.clear();
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Fleets and admirals
// ---------------------------------------------------------------------------

bool GameState::createFleet(const std::string& name, std::string& error) {
  const FleetLimits limits = fleetLimits();
  if (static_cast<int>(fleets_.size()) >= limits.maxFleets) {
    error = "Fleet command is full (" + std::to_string(fleets_.size()) + "/" +
            std::to_string(limits.maxFleets) + "). Research Fleet Command to raise more.";
    return false;
  }

  Fleet fleet;
  fleet.id = nextFleetId_++;
  fleet.name = trimmed(name);
  if (fleet.name.empty()) fleet.name = ordinal(static_cast<int>(fleets_.size()) + 1) + " Fleet";
  fleets_.push_back(std::move(fleet));
  return true;
}

bool GameState::renameFleet(int fleetId, const std::string& name, std::string& error) {
  const std::string clean = trimmed(name);
  if (clean.empty()) {
    error = "A fleet needs a name.";
    return false;
  }
  for (Fleet& fleet : fleets_) {
    if (fleet.id != fleetId) continue;
    fleet.name = clean;
    return true;
  }
  error = "Unknown fleet.";
  return false;
}

bool GameState::disbandFleet(int fleetId, std::string& error) {
  for (auto it = fleets_.begin(); it != fleets_.end(); ++it) {
    if (it->id != fleetId) continue;
    if (it->admiralId != 0) {
      for (Admiral& admiral : admirals_) {
        if (admiral.id == it->admiralId) admiral.fleetId = 0;
      }
    }
    fleets_.erase(it);
    return true;
  }
  error = "Unknown fleet.";
  return false;
}

bool GameState::assignUnitToFleet(int unitId, int fleetId, std::string& error) {
  const NavalUnit* unit = navalUnitById(unitId);
  if (unit == nullptr) {
    error = "Unknown naval unit.";
    return false;
  }
  if (unit->sunk()) {
    error = "A sunk hull cannot be assigned to a fleet.";
    return false;
  }

  Fleet* target = nullptr;
  for (Fleet& fleet : fleets_) {
    if (fleet.id == fleetId) target = &fleet;
  }
  if (target == nullptr) {
    error = "Unknown fleet.";
    return false;
  }
  if (target->contains(unitId)) {
    error = "That ship is already in " + target->name + ".";
    return false;
  }

  const Fleet* current = fleetForUnit(unitId);
  if (current != nullptr) {
    error = "That ship already serves in " + current->name + ".";
    return false;
  }

  const FleetLimits limits = fleetLimits();
  if (static_cast<int>(target->unitIds.size()) >= limits.maxShipsPerFleet) {
    error = target->name + " is full (" + std::to_string(limits.maxShipsPerFleet) +
            " ships). Research Naval Organization to enlarge it.";
    return false;
  }

  target->unitIds.push_back(unitId);
  return true;
}

bool GameState::removeUnitFromFleet(int unitId, std::string& error) {
  for (Fleet& fleet : fleets_) {
    const auto found = std::find(fleet.unitIds.begin(), fleet.unitIds.end(), unitId);
    if (found == fleet.unitIds.end()) continue;
    fleet.unitIds.erase(found);
    return true;
  }
  error = "That ship is not in a fleet.";
  return false;
}

bool GameState::recruitAdmiralToFleet(int admiralId, int fleetId, std::string& error) {
  Admiral* admiral = nullptr;
  for (Admiral& candidate : admirals_) {
    if (candidate.id == admiralId) admiral = &candidate;
  }
  if (admiral == nullptr) {
    error = "Unknown admiral.";
    return false;
  }
  // The fleet is checked first, so a bad target never costs the player a signing fee.
  if (fleetById(fleetId) == nullptr) {
    error = "Raise a fleet before recruiting an admiral.";
    return false;
  }
  if (admiral->hired) {
    error = admiral->fullName() + " is already on the payroll.";
    return false;
  }

  const FleetLimits limits = fleetLimits();
  if (hiredAdmiralCount() >= limits.maxAdmirals) {
    error = "The admiralty is full (" + std::to_string(hiredAdmiralCount()) + "/" +
            std::to_string(limits.maxAdmirals) + "). Research Admiral Corps to engage more.";
    return false;
  }
  if (money_ < static_cast<double>(admiral->buyCost)) {
    error = "Not enough money to engage " + admiral->fullName() + ".";
    return false;
  }

  money_ -= static_cast<double>(admiral->buyCost);
  admiral->hired = true;
  return assignAdmiralToFleet(admiralId, fleetId, error);
}

bool GameState::assignAdmiralToFleet(int admiralId, int fleetId, std::string& error) {
  Admiral* admiral = nullptr;
  for (Admiral& candidate : admirals_) {
    if (candidate.id == admiralId) admiral = &candidate;
  }
  if (admiral == nullptr) {
    error = "Unknown admiral.";
    return false;
  }
  if (!admiral->hired) {
    error = admiral->fullName() + " has not been engaged yet.";
    return false;
  }

  Fleet* target = nullptr;
  for (Fleet& fleet : fleets_) {
    if (fleet.id == fleetId) target = &fleet;
  }
  if (target == nullptr) {
    error = "Unknown fleet.";
    return false;
  }

  // An admiral commands one fleet at a time: moving them off their old command and replacing
  // the fleet's current admiral keeps the two sides of the relationship in step.
  for (Fleet& fleet : fleets_) {
    if (fleet.admiralId == admiralId) fleet.admiralId = 0;
  }
  if (target->admiralId != 0) {
    for (Admiral& other : admirals_) {
      if (other.id == target->admiralId) other.fleetId = 0;
    }
  }
  target->admiralId = admiral->id;
  admiral->fleetId = target->id;
  return true;
}

bool GameState::dismissAdmiralFromFleet(int fleetId, std::string& error) {
  Fleet* target = nullptr;
  for (Fleet& fleet : fleets_) {
    if (fleet.id == fleetId) target = &fleet;
  }
  if (target == nullptr) {
    error = "Unknown fleet.";
    return false;
  }
  if (target->admiralId == 0) {
    error = "That fleet has no admiral.";
    return false;
  }
  for (Admiral& admiral : admirals_) {
    if (admiral.id == target->admiralId) admiral.fleetId = 0;
  }
  target->admiralId = 0;
  return true;
}

bool GameState::releaseAdmiral(int admiralId, std::string& error) {
  for (Admiral& admiral : admirals_) {
    if (admiral.id != admiralId) continue;
    if (!admiral.hired) {
      error = admiral.fullName() + " is not on the payroll.";
      return false;
    }
    if (admiral.fleetId != 0) {
      error = "Dismiss " + admiral.fullName() + " from their fleet first.";
      return false;
    }
    admiral.hired = false;
    return true;
  }
  error = "Unknown admiral.";
  return false;
}


void GameState::setFireOrder(const std::vector<int>& unitIds, unsigned order) {
  if (order == 0u) order = kFireAuto;
  for (NavalUnit& unit : navalUnits_) {
    if (std::find(unitIds.begin(), unitIds.end(), unit.id) != unitIds.end()) {
      unit.fireOrder = order;
    }
  }
}

void GameState::setPosture(const std::vector<int>& unitIds, int posture) {
  posture = std::max(static_cast<int>(kPostureOffensive),
                     std::min(static_cast<int>(kPostureAggressive), posture));
  for (NavalUnit& unit : navalUnits_) {
    if (std::find(unitIds.begin(), unitIds.end(), unit.id) == unitIds.end()) continue;
    unit.posture = posture;
    // A boat the posture itself picked up is released when the posture changes; a target the
    // player explicitly ordered is not, so an attack order outlives a stance change.
    if (posture != kPostureAggressive && !unit.targetExplicit && unit.targetBoatId != 0) {
      unit.targetBoatId = 0;
      unit.path.clear();
    }
  }
}

const BotBoat* GameState::botBoatById(int id) const {
  for (const BotBoat& boat : botBoats_) {
    if (boat.id == id) return &boat;
  }
  return nullptr;
}

int GameState::nearestEnemyBoat(const LatLng& from) const {
  int best = 0;
  double bestKm = 1.0e18;
  for (const BotBoat& boat : botBoats_) {
    if (boat.sunk()) continue;
    const double km = haversineKm(from, boat.position);
    if (km < bestKm) {
      bestKm = km;
      best = boat.id;
    }
  }
  return best;
}

bool GameState::orderNavalUnitAttack(int unitId, int boatId, std::string& error) {
  NavalUnit* unit = nullptr;
  for (NavalUnit& candidate : navalUnits_) {
    if (candidate.id == unitId) {
      unit = &candidate;
      break;
    }
  }
  if (unit == nullptr) {
    error = "Unknown naval unit.";
    return false;
  }

  const BotBoat* target = botBoatById(boatId);
  if (target == nullptr || target->sunk()) {
    error = "No such target.";
    return false;
  }

  unit->targetBoatId = boatId;
  unit->targetPortId = 0;  // a hull is the target now, not the harbour it was bombarding
  unit->targetExplicit = true;

  // Close on the target's position, but let the ship stop when it reaches firing range rather
  // than sailing onto the wreck: advanceNavalUnits clears the course once it is in reach.
  std::vector<LatLng> course = navalCourse(unit->position, target->position);
  if (course.size() >= 2) {
    course.erase(course.begin());
    unit->path = std::move(course);
    double heading = 0.0;
    if (mapHeadingDegrees(unit->position, target->position, heading)) unit->headingDeg = heading;
  }
  return true;
}

bool GameState::engageNearestEnemy(int unitId) {
  const NavalUnit* unit = navalUnitById(unitId);
  if (unit == nullptr) return false;
  const int boatId = nearestEnemyBoat(unit->position);
  if (boatId == 0) return false;
  std::string ignored;
  return orderNavalUnitsAttack({unitId}, boatId, ignored) > 0;
}

std::vector<CombatEvent> GameState::takeCombatEvents() {
  std::vector<CombatEvent> events = std::move(combatEvents_);
  combatEvents_.clear();
  return events;
}



void GameState::fitBotArmament(BotBoat& boat) const {
  // A hull laid down from a catalogue class carries that class's own guns and figures - the
  // same ones the player's shipyard would give the same hull. A kind's preset is the fallback
  // for a hull with no class left in the save (one written before hulls carried one).
  const NavalVessel* klass =
      catalog_ != nullptr && boat.classId != 0 ? catalog_->navalVesselById(boat.classId)
                                               : nullptr;
  if (klass != nullptr) {
    boat.maxHealth = std::max(60.0f, klass->health);
    boat.defense = klass->defense;
    boat.armament = klass->armament;
  } else {
    boat.armament.clear();

    auto addWeapon = [&](const char* name, const char* type, FireCategory category,
                         float damage, float power, float reload, float ammo, float radius) {
      const int id = static_cast<int>(boat.armament.size());
      boat.armament.push_back(
          makeWeapon(id, name, type, category, damage, power, reload, ammo, radius, 0));
    };

    switch (boat.kind) {
      case NavalModelKind::aircraft_carrier:
        boat.maxHealth = 8000.0f;
        boat.defense = 2000.0f;
        addWeapon("76 mm main gun", "gun", FireCategory::medium, 60.0f, 25.0f, 4.0f, 300.0f,
                  14.0f);
        addWeapon("76 mm main gun", "gun", FireCategory::medium, 60.0f, 25.0f, 4.0f, 300.0f,
                  14.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        break;
      case NavalModelKind::cruiser:
        boat.maxHealth = 5000.0f;
        boat.defense = 1500.0f;
        addWeapon("76 mm main gun", "gun", FireCategory::medium, 60.0f, 25.0f, 4.0f, 300.0f,
                  14.0f);
        addWeapon("76 mm main gun", "gun", FireCategory::medium, 60.0f, 25.0f, 4.0f, 300.0f,
                  14.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        break;
      case NavalModelKind::destroyer:
        boat.maxHealth = 3500.0f;
        boat.defense = 1200.0f;
        addWeapon("76 mm main gun", "gun", FireCategory::medium, 60.0f, 25.0f, 4.0f, 300.0f,
                  14.0f);
        addWeapon("76 mm main gun", "gun", FireCategory::medium, 60.0f, 25.0f, 4.0f, 300.0f,
                  14.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        break;
      case NavalModelKind::frigate:
        boat.maxHealth = 2800.0f;
        boat.defense = 900.0f;
        addWeapon("76 mm main gun", "gun", FireCategory::medium, 60.0f, 25.0f, 4.0f, 300.0f,
                  14.0f);
        addWeapon("76 mm main gun", "gun", FireCategory::medium, 60.0f, 25.0f, 4.0f, 300.0f,
                  14.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        break;
      case NavalModelKind::corvette:
        boat.maxHealth = 2200.0f;
        boat.defense = 700.0f;
        addWeapon("57 mm main gun", "gun", FireCategory::medium, 45.0f, 20.0f, 3.0f, 250.0f,
                  12.0f);
        addWeapon("57 mm main gun", "gun", FireCategory::medium, 45.0f, 20.0f, 3.0f, 250.0f,
                  12.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        addWeapon("30 mm CIWS", "gun", FireCategory::light, 18.0f, 8.0f, 0.6f, 800.0f, 5.0f);
        break;
      case NavalModelKind::patrol:
        boat.maxHealth = 1200.0f;
        boat.defense = 400.0f;
        addWeapon("30 mm gun", "gun", FireCategory::light, 18.0f, 8.0f, 0.8f, 500.0f, 6.0f);
        addWeapon("12.7 mm MG", "machine gun", FireCategory::light, 10.0f, 8.0f, 4.0f,
                  1000.0f, 3.0f);
        addWeapon("12.7 mm MG", "machine gun", FireCategory::light, 10.0f, 8.0f, 4.0f,
                  1000.0f, 3.0f);
        break;
      case NavalModelKind::submarine:
        boat.maxHealth = 1200.0f;
        boat.defense = 400.0f;
        addWeapon("533 mm torpedo", "torpedo", FireCategory::heavy, 240.0f, 60.0f, 14.0f,
                  12.0f, 30.0f);
        addWeapon("533 mm torpedo", "torpedo", FireCategory::heavy, 240.0f, 60.0f, 14.0f,
                  12.0f, 30.0f);
        break;
      case NavalModelKind::other:
      default:
        boat.maxHealth = 1000.0f;
        boat.defense = 300.0f;
        addWeapon("12.7 mm MG", "machine gun", FireCategory::light, 8.0f, 5.0f, 4.0f,
                  1000.0f, 3.0f);
        break;
    }
  }

  // A hull's pace depends on what it is: a patrol boat runs down shipping, a submarine does
  // not sprint at the surface, and an auxiliary lumbers behind both. The class does not say,
  // so it comes from the hull kind either way.
  switch (boat.kind) {
    case NavalModelKind::patrol: boat.speedKmPerSecond = 260.0; break;
    case NavalModelKind::frigate: boat.speedKmPerSecond = 190.0; break;
    case NavalModelKind::corvette: boat.speedKmPerSecond = 230.0; break;
    case NavalModelKind::destroyer: boat.speedKmPerSecond = 240.0; break;
    case NavalModelKind::aircraft_carrier: boat.speedKmPerSecond = 180.0; break;
    case NavalModelKind::cruiser: boat.speedKmPerSecond = 210.0; break;
    case NavalModelKind::submarine: boat.speedKmPerSecond = 170.0; break;
    default: boat.speedKmPerSecond = 160.0; break;
  }

  boat.mounts.assign(boat.armament.size(), MountState{});
  for (size_t i = 0; i < boat.armament.size(); ++i) {
    boat.mounts[i].ammo = std::max(1.0f, boat.armament[i].ammo);
  }
  boat.health = boat.maxHealth;
}

// ---------------------------------------------------------------------------
// Enemy powers
// ---------------------------------------------------------------------------

void GameState::rearmBotBoat(BotBoat& boat, double dtSeconds) {
  if (catalog_ == nullptr || boat.homePortId == 0) return;
  const Port* yard = catalog_->portById(boat.homePortId);
  if (yard == nullptr || !yard->hasPosition()) return;
  if (haversineKm(boat.position, yard->position) > kRearmRadiusKm) return;

  for (size_t i = 0; i < boat.mounts.size() && i < boat.armament.size(); ++i) {
    const float capacity = std::max(0.0f, boat.armament[i].ammo);
    boat.mounts[i].ammo =
        std::min(capacity, boat.mounts[i].ammo +
                               kRearmRoundsPerSecond * static_cast<float>(dtSeconds));
  }
}

int GameState::factionOwningPort(int portId) const {
  for (size_t i = 0; i < factions_.size(); ++i) {
    if (factions_[i].ownsPort(portId)) return static_cast<int>(i);
  }
  return -1;
}

std::string GameState::factionNameForPort(int portId) const {
  const int index = factionOwningPort(portId);
  return index < 0 ? std::string() : factions_[static_cast<size_t>(index)].name;
}

int GameState::factionHomePort(int factionIndex) const {
  if (factionIndex < 0 || factionIndex >= static_cast<int>(factions_.size())) return 0;
  const AIPlayer& power = factions_[static_cast<size_t>(factionIndex)];
  return power.portIds.empty() ? 0 : power.portIds.front();
}

int GameState::factionShipCount(int factionIndex) const {
  if (factionIndex < 0 || factionIndex >= static_cast<int>(factions_.size())) return 0;
  const std::string& owner = factions_[static_cast<size_t>(factionIndex)].name;
  int live = 0;
  for (const BotBoat& boat : botBoats_) {
    if (!boat.sunk() && boat.owner == owner) ++live;
  }
  return live;
}

void GameState::seedFactions() {
  factions_.clear();
  merchantRuns_.clear();
  nextMerchantRunId_ = 1;
  if (catalog_ == nullptr) return;

  // Every power is given the real harbour closest to its own waters, skipping the player's free
  // port and any harbour another power already holds. From there on it lives off what that port
  // and its trade earn: there is no anchor to respawn at and no hidden income.
  const std::vector<EnemyPower>& roster = enemyPowers();
  for (size_t index = 0; index < roster.size(); ++index) {
    const EnemyPower& entry = roster[index];

    AIPlayer power;
    power.name = entry.name;
    power.money = kBaseMoney;
    // Staggered clocks, so three rivals do not all sail, build and research on the same tick.
    power.tradeTimer = kFactionTradeIntervalSeconds * (1.0 + 0.35 * static_cast<double>(index));
    power.buildTimer = kFactionBuildIntervalSeconds * (1.0 + 0.5 * static_cast<double>(index));
    power.researchTimer =
        kFactionResearchIntervalSeconds * (1.0 + 0.25 * static_cast<double>(index));

    const LatLng waters = entry.waters;
    int bestPort = 0;
    double bestKm = 1.0e18;
    for (const Port& port : catalog_->ports()) {
      if (port.id == kStarterPortId || !port.hasPosition()) continue;
      if (factionOwningPort(port.id) >= 0) continue;
      const double km = haversineKm(waters, port.position);
      if (km < bestKm) {
        bestKm = km;
        bestPort = port.id;
      }
    }
    if (bestPort != 0) power.portIds.push_back(bestPort);

    factions_.push_back(std::move(power));
  }

  // An opening hull each, bought out of the same treasury the rest of its programme uses, so a
  // power starts with a yard, a treasury and something afloat to defend them with.
  for (size_t index = 0; index < factions_.size(); ++index) {
    buildFactionShip(static_cast<int>(index));
  }
}

bool GameState::buildFactionShip(int factionIndex) {
  if (catalog_ == nullptr || factionIndex < 0 ||
      factionIndex >= static_cast<int>(factions_.size())) {
    return false;
  }
  AIPlayer& power = factions_[static_cast<size_t>(factionIndex)];
  if (power.portIds.empty()) return false;

  const Port* yard = catalog_->portById(power.portIds.front());
  if (yard == nullptr || !yard->hasPosition()) return false;

  // A navy is capped by what its power holds: two hulls per port, plus a flagship. A power
  // that has just been driven out of its harbours therefore cannot keep a fleet at sea.
  const int cap = 2 * static_cast<int>(power.portIds.size()) + 1;
  if (factionShipCount(factionIndex) >= cap) return false;

  // It lays down the strongest hull it can pay for and still keep a reserve, which is the same
  // purchase the player makes at a shipyard, priced by the same function.
  const double budget = power.money - kFactionReserveMoney;
  const NavalVessel* chosen = nullptr;
  double chosenCost = 0.0;
  for (const NavalVessel& vessel : catalog_->navalVessels()) {
    if (vessel.armament.empty()) continue;
    const double cost = navalUnitCost(vessel, false);
    if (cost > budget) continue;
    if (chosen == nullptr || cost > chosenCost) {
      chosen = &vessel;
      chosenCost = cost;
    }
  }
  if (chosen == nullptr) return false;

  power.money -= chosenCost;

  BotBoat boat;
  boat.id = nextBoatId_++;
  boat.classId = chosen->id;
  boat.name = chosen->shipName;
  boat.owner = power.name;
  boat.homePortId = yard->id;
  boat.kind = navalModelKindFor(*chosen);
  boat.position = yard->position;
  boat.headingDeg = 0.0;

  // Berth the new hull a little way off the quay, fanned out by the golden angle so a growing
  // navy lays alongside instead of stacking on one pixel. A berth on the coast falls back to
  // the port itself.
  if (router_ != nullptr && router_->ready()) {
    const double angle = static_cast<double>(boat.id) * 2.39996323;
    const double radius = 0.25 + 0.05 * static_cast<double>(boat.id % 4);
    const LatLng berth{yard->position.lat + std::cos(angle) * radius,
                       yard->position.lng + std::sin(angle) * radius * 1.4};
    if (!router_->isLandAt(berth.lat, berth.lng)) boat.position = berth;
  }

  fitBotArmament(boat);
  botBoats_.push_back(std::move(boat));
  return true;
}

bool GameState::buyFactionTechnology(int factionIndex) {
  if (factionIndex < 0 || factionIndex >= static_cast<int>(factions_.size())) return false;
  AIPlayer& power = factions_[static_cast<size_t>(factionIndex)];

  // The cheapest thing its tree has unlocked and its points can pay for. A power therefore
  // climbs the same technologies the player does, from the same research points, and its
  // freight payouts rise with them.
  const std::string* best = nullptr;
  int bestCost = 0;
  for (const Technology& technology : technologies()) {
    if (!techUnlocked(power.research, technology.id)) continue;
    const int cost = nextLevelCost(power.research, technology.id);
    if (cost < 0 || cost > power.researchPoints) continue;
    if (best == nullptr || cost < bestCost) {
      best = &technology.id;
      bestCost = cost;
    }
  }
  if (best == nullptr) return false;

  power.researchPoints -= bestCost;
  setTechLevel(power.research, *best, techLevel(power.research, *best) + 1);
  return true;
}

bool GameState::dispatchMerchantRun(int factionIndex, double now) {
  if (catalog_ == nullptr || router_ == nullptr) return false;
  if (factionIndex < 0 || factionIndex >= static_cast<int>(factions_.size())) return false;
  AIPlayer& power = factions_[static_cast<size_t>(factionIndex)];
  if (power.portIds.empty()) return false;

  // A power keeps as many crossings at sea as it has ports, and no more: one hull per berth.
  int running = 0;
  for (const MerchantRun& run : merchantRuns_) {
    if (run.faction == factionIndex) ++running;
  }
  if (running >= static_cast<int>(power.portIds.size())) return false;

  const int fromPortId = power.portIds[static_cast<size_t>(running) % power.portIds.size()];
  const Port* from = catalog_->portById(fromPortId);
  if (from == nullptr || !from->hasPosition()) return false;

  const std::shared_ptr<const SeaField> field = router_->field(from->position);
  if (!field) return false;

  // Somewhere worth sailing to: a harbour the power does not own, far enough out to pay a
  // freight but inside the reach of its shipping.
  std::vector<const Port*> candidates;
  for (const Port& port : catalog_->ports()) {
    if (port.id == fromPortId || !port.hasPosition() || power.ownsPort(port.id)) continue;
    double km = 0.0;
    if (!field->distanceKmTo(port.position, km)) continue;
    if (km < kMerchantMinRunKm || km > kMerchantMaxRunKm) continue;
    candidates.push_back(&port);
  }
  if (candidates.empty()) return false;

  std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
  const Port& destination = *candidates[pick(rng_)];

  RouteResult route;
  if (!field->routeTo(destination.position, route) || route.points.size() < 2) return false;

  MerchantRun run;
  run.id = nextMerchantRunId_++;
  run.faction = factionIndex;
  run.fromPortId = fromPortId;
  run.toPortId = destination.id;
  run.distanceKm = route.distanceKm;
  run.route = route.points;

  // What it carries is what the departure country actually trades, so the same crossing is
  // priced the way the player's would be for the same pair of ports.
  const CountryTrade* trade = catalog_->tradeFor(from->country);
  if (trade != nullptr && !trade->exports.empty()) {
    const TradeGood& good =
        trade->exports[static_cast<size_t>(pick(rng_) % trade->exports.size())];
    run.cargoDescription = good.description;
    run.cargoValueK = good.valueK;
  } else {
    run.cargoDescription = "mixed cargo";
    run.cargoValueK = kMerchantFallbackCargoK;
  }

  const VoyageOutcome outcome =
      resolveVoyage(run.cargoValueK, run.distanceKm, power.research, 100.0, /*realTrade=*/false);
  run.durationMs = outcome.durationMs > 0.0 ? outcome.durationMs : 1.0;
  run.startedAtMs = now;
  run.progress = 0.0;

  merchantRuns_.push_back(std::move(run));
  return true;
}

void GameState::advanceMerchantRuns(double now) {
  for (MerchantRun& run : merchantRuns_) {
    if (run.progress >= 100.0) continue;
    run.progress = std::min(100.0, (now - run.startedAtMs) / run.durationMs * 100.0);
  }

  std::vector<MerchantRun> arrived;
  for (const MerchantRun& run : merchantRuns_) {
    if (run.progress >= 100.0) arrived.push_back(run);
  }
  if (arrived.empty()) return;

  // Settled with the same dice and the same economy the player's crossings use: a rival can
  // lose a cargo to the sea exactly as the player can.
  std::uniform_real_distribution<double> roll(0.0, 100.0);
  for (const MerchantRun& run : arrived) {
    if (run.faction < 0 || run.faction >= static_cast<int>(factions_.size())) continue;
    AIPlayer& power = factions_[static_cast<size_t>(run.faction)];
    const VoyageOutcome outcome =
        resolveVoyage(run.cargoValueK, run.distanceKm, power.research, roll(rng_), false);
    power.money += outcome.reward;
    power.researchPoints += outcome.points;
  }

  merchantRuns_.erase(
      std::remove_if(merchantRuns_.begin(), merchantRuns_.end(),
                     [](const MerchantRun& run) { return run.progress >= 100.0; }),
      merchantRuns_.end());
}

void GameState::updateFactions(double dtSeconds) {
  if (factions_.empty()) return;

  // A long frame must not let a power make a burst of decisions at once, so its clocks are
  // advanced by at most a couple of seconds a tick.
  const double tick = std::min(std::max(0.0, dtSeconds), 2.0);
  const double now = lastGameTimeMs_;

  for (size_t index = 0; index < factions_.size(); ++index) {
    AIPlayer& power = factions_[index];

    // Income: exactly the rate the player earns per port held, for every port held, so a power
    // that loses a harbour sees its economy shrink the way the player's would.
    power.money += kPassiveIncomePerPortPerSecond * static_cast<double>(power.portIds.size()) * tick;

    power.tradeTimer -= tick;
    if (power.tradeTimer <= 0.0) {
      power.tradeTimer = kFactionTradeIntervalSeconds;
      dispatchMerchantRun(static_cast<int>(index), now);
    }

    power.buildTimer -= tick;
    if (power.buildTimer <= 0.0) {
      power.buildTimer = kFactionBuildIntervalSeconds;
      buildFactionShip(static_cast<int>(index));
    }

    power.researchTimer -= tick;
    if (power.researchTimer <= 0.0) {
      power.researchTimer = kFactionResearchIntervalSeconds;
      buyFactionTechnology(static_cast<int>(index));
    }
  }

  advanceMerchantRuns(now);
}

void GameState::advanceNavalUnits(double dtSeconds) {
  if (dtSeconds <= 0.0) return;

  for (NavalUnit& unit : navalUnits_) {
    if (unit.sunk()) continue;

    if (unit.repathTimer > 0.0) unit.repathTimer -= dtSeconds;

    // A ship engaging a boat closes to its firing range and then holds there, rather than
    // sailing straight through its target without ever slowing down. If the quarry then opens
    // the range again - a bot backing off, a faster hull running - an ordered attack is
    // resumed instead of the ship sitting at a range it no longer has.
    if (unit.targetBoatId != 0) {
      const BotBoat* target = botBoatById(unit.targetBoatId);
      if (target == nullptr || target->sunk()) {
        unit.targetBoatId = 0;
        if (!unit.targetExplicit) unit.path.clear();
        unit.targetExplicit = false;
      } else if (catalog_ != nullptr) {
        const NavalVessel* klass = catalog_->navalVesselById(unit.classId);
        const float reach =
            klass != nullptr
                ? engagementRangeKm(klass->armament, unit.fireOrder) *
                      fleetBuffMultiplier(unit.id, AdmiralBuff::Range)
                : 0.0f;
        const double km = haversineKm(unit.position, target->position);
        const bool chases = unit.targetExplicit || unit.posture == kPostureAggressive;
        if (reach > 0.0f && km <= static_cast<double>(reach) * kEngagementStandoff) {
          unit.path.clear();
        } else if (chases && unit.path.empty() && unit.repathTimer <= 0.0) {
          std::vector<LatLng> course = navalCourse(unit.position, target->position);
          if (course.size() >= 2) {
            course.erase(course.begin());
            unit.path = std::move(course);
          }
          unit.repathTimer = kBotRepathSeconds;
        }
      }
    }
    // A ship bombarding a harbour holds the range the same way it holds it against a boat:
    // it closes until its own mounts bear, then stops there and works the defences down. The
    // siege lifts when the port is taken or its battery is broken, and the ship is released.
    if (unit.targetPortId != 0 && unit.targetBoatId == 0) {
      const Port* harbour =
          catalog_ != nullptr ? catalog_->portById(unit.targetPortId) : nullptr;
      if (harbour == nullptr || portOwned(unit.targetPortId) ||
          portDefence(unit.targetPortId) <= 0.0) {
        unit.targetPortId = 0;
        unit.targetExplicit = false;
        unit.path.clear();
      } else if (catalog_ != nullptr) {
        const NavalVessel* klass = catalog_->navalVesselById(unit.classId);
        const float reach = klass != nullptr
                                ? engagementRangeKm(klass->armament, unit.fireOrder) *
                                      fleetBuffMultiplier(unit.id, AdmiralBuff::Range)
                                : 0.0f;
        const double km = haversineKm(unit.position, harbour->position);
        if (reach > 0.0f && km <= static_cast<double>(reach) * kEngagementStandoff) {
          unit.path.clear();  // inside the firing range: hold station and bombard
        } else if (unit.path.empty() && unit.repathTimer <= 0.0) {
          // Still out of reach: steer for the open water beside the quay. A river port's own
          // coordinate is land, so the berth is what a ship can actually be sent to.
          const int portId = unit.targetPortId;
          std::string ignored;
          orderNavalUnitToPort(unit.id, portId, ignored);
          unit.targetPortId = portId;
          unit.targetExplicit = true;
          unit.repathTimer = kBotRepathSeconds;
        }
      }
    }

    double budget;
    if (unit.kind == NavalModelKind::frigate || unit.kind == NavalModelKind::destroyer || unit.kind == NavalModelKind::patrol){
      budget = unit.speedKmPerSecond *
                    static_cast<double>(fleetBuffMultiplier(unit.id, AdmiralBuff::Speed)) *
                    static_cast<double>(fleetBuffMultiplier(unit.id, AdmiralBuff::SpeedDestroyerFrigatePatrol)) *
                    dtSeconds;
    }else{
      budget = unit.speedKmPerSecond *
        static_cast<double>(fleetBuffMultiplier(unit.id, AdmiralBuff::Speed)) *
        dtSeconds;
    }
    while (budget > 0.0 && !unit.path.empty()) {
      const LatLng from = unit.position;
      const LatLng target = unit.path.front();
      const double legKm = haversineKm(from, target);

      if (legKm <= 1e-6) {
        unit.position = target;
        unit.path.erase(unit.path.begin());
        continue;
      }
      if (budget >= legKm) {
        budget -= legKm;
        unit.position = target;
        unit.path.erase(unit.path.begin());
      } else {
        const double t = budget / legKm;
        unit.position = {from.lat + (target.lat - from.lat) * t,
                         from.lng + (target.lng - from.lng) * t};
        budget = 0.0;
      }

      double heading = 0.0;
      if (mapHeadingDegrees(from, unit.position, heading)) unit.headingDeg = heading;
    }

    // A hull alongside its home port tops up a partly used magazine while it lies there: the
    // crew restocks the ready racks, so a ship that comes home between fights sails again with
    // full magazines instead of the rounds left over from the last one.
    if (unit.path.empty() && unit.targetBoatId == 0 && catalog_ != nullptr) {
      const Port* home = catalog_->portById(unit.homePortId);
      const NavalVessel* klass = catalog_->navalVesselById(unit.classId);
      if (home != nullptr && klass != nullptr &&
          haversineKm(unit.position, home->position) <= kRearmRadiusKm) {
        for (size_t i = 0; i < unit.mounts.size() && i < klass->armament.size(); ++i) {
          const float capacity = std::max(0.0f, klass->armament[i].ammo);
          unit.mounts[i].ammo = std::min(
              capacity, unit.mounts[i].ammo +
                            kRearmRoundsPerSecond *
                                fleetBuffMultiplier(unit.id, AdmiralBuff::Repair) *
                                static_cast<float>(dtSeconds));
        }
      }
    }
  }
}

void GameState::advanceBotBoats(double dtSeconds) {
  if (dtSeconds <= 0.0) return;

  for (BotBoat& boat : botBoats_) {
    if (boat.sunk()) {
      boat.path.clear();
      boat.targetUnitId = 0;
      boat.retaliating = false;
      boat.evading = false;
      continue;
    }

    // A pirate holds its station. It has no yard to return to and no quarrel to pursue, so the
    // hunt, the retreat and the rearm below - an enemy power's war effort - are not a pirate's:
    // it sits where it was placed and shoots what comes inside its own gun range (see fire).
    if (boat.pirate) {
      boat.path.clear();
      boat.targetUnitId = 0;
      boat.retaliating = false;
      boat.evading = false;
      continue;
    }

    const double reach = static_cast<double>(engagementRangeKm(boat.armament, kFireAuto));
    const double huntRadius = botHuntRadiusKm(reach);
    // A boat that was shot at holds its answer across a longer leash than one that merely
    // noticed traffic nearby: it knows who fired at it, so it will cross more water than its
    // lookouts could have seen.
    const double breakOff =
        boat.retaliating ? kBotRetaliationLeashKm : huntRadius * kBotBreakOffMultiple;

    // Keep the ship already being hunted while it is afloat and still inside the break-off
    // distance; otherwise pick the nearest of the player's ships inside the hunt radius.
    NavalUnit* target = nullptr;
    if (boat.targetUnitId != 0) {
      for (NavalUnit& candidate : navalUnits_) {
        if (candidate.id == boat.targetUnitId && !candidate.sunk()) target = &candidate;
      }
      if (target != nullptr && haversineKm(boat.position, target->position) > breakOff) {
        target = nullptr;
      }
    }
    if (target == nullptr) {
      boat.targetUnitId = 0;
      boat.retaliating = false;
      double bestKm = huntRadius;
      for (NavalUnit& candidate : navalUnits_) {
        if (candidate.sunk()) continue;
        const double km = haversineKm(boat.position, candidate.position);
        if (km < bestKm) {
          bestKm = km;
          target = &candidate;
        }
      }
      if (target != nullptr) boat.targetUnitId = target->id;
    }
    if (target == nullptr) {
      boat.path.clear();
      boat.evading = false;
      // Nothing to hunt: a hull back at its own yard takes on ammunition there, so a rival's
      // navy is not spent after a single fight.
      rearmBotBoat(boat, dtSeconds);
      continue;
    }

    // Fight in a band around the outermost mount's reach: close while the target is outside
    // it, hold once the guns bear, and back off if the enemy gets inside it. The fight happens
    // at range rather than ending in a collision, and a bot never lets itself be run down.
    const double distance = haversineKm(boat.position, target->position);
    const double hold = reach * kEngagementStandoff;

    if (boat.repathTimer > 0.0) boat.repathTimer -= dtSeconds;
    if (reach > 0.0 && distance < reach * kBotEvadeFraction) boat.evading = true;
    else if (distance >= hold) boat.evading = false;

    if (boat.evading) {
      if (boat.path.empty() && boat.repathTimer <= 0.0) {
        // Retire directly away from the quarry, to the far edge of the band.
        double away = 0.0;
        if (!mapHeadingDegrees(target->position, boat.position, away)) away = 0.0;
        const LatLng retreat = pointOnBearing(target->position, away, hold);
        std::vector<LatLng> course = navalCourse(boat.position, retreat);
        if (course.size() >= 2) {
          course.erase(course.begin());
          boat.path = std::move(course);
        }
        boat.repathTimer = kBotRepathSeconds;
      }
    } else if (distance <= hold) {
      boat.path.clear();
    } else if (boat.path.empty() && boat.repathTimer <= 0.0) {
      std::vector<LatLng> course = navalCourse(boat.position, target->position);
      if (course.size() >= 2) {
        course.erase(course.begin());
        boat.path = std::move(course);
      }
      boat.repathTimer = kBotRepathSeconds;
    }

    advanceAlongPath(boat.position, boat.path, boat.speedKmPerSecond * dtSeconds,
                     boat.headingDeg);
  }
}

int GameState::fireSalvo(const std::vector<Armament>& armament, std::vector<MountState>& mounts,
                         const std::vector<size_t>& plan, const FireContext& context) {
  // Every mount reloads on its own clock, so the salvo walks the plan once: a mount that is
  // reloading, dry, out of reach or unaffordable is simply stepped over, and the rest of the
  // battery still fires. The plan's order is the gunnery officer's priority - Auto puts the
  // fastest killer first, a named order keeps the class's own mount order.
  std::uniform_real_distribution<double> jitter(0.9, 1.1);

  int fired = 0;
  for (const size_t index : plan) {
    if (index >= mounts.size() || index >= armament.size()) continue;
    const Armament& weapon = armament[index];
    MountState& mount = mounts[index];
    if (mount.reload > 0.0f || mount.ammo <= 0.0f) continue;
    if (static_cast<double>(weaponRangeKm(weapon)) * context.rangeMultiplier <
        context.distanceKm) {
      continue;
    }
    if (context.paysForRounds && !roundsAffordable(weapon, money_)) continue;

    mount.ammo -= 1.0f;
    // `ammo` is one magazine, not the ship's whole supply: the mount fires at its own cadence
    // until the magazine is dry, then spends `reload_time` changing it. The refill itself is
    // done on the combat tick (see updateCombat), so the reload timer is visible to the HUD for
    // exactly as long as the crew is working.
    if (mount.ammo <= 0.0f) {
        mount.reload = std::max(mountReloadSeconds(weapon), weapon.reload_time)  ;
    } else {
      mount.reload = mountReloadSeconds(weapon) * static_cast<float>(jitter(rng_));
    }
    if (context.paysForRounds && weapon.cost > 0) money_ -= static_cast<double>(weapon.cost);

    Projectile shot;
    shot.id = nextProjectileId_++;
    shot.category = weapon.category;
    shot.delivery = deliveryFor(weapon);
    shot.from = context.origin;
    shot.position = context.origin;
    shot.target = context.targetPosition;
    shot.sourceUnitId = context.sourceUnitId;
    shot.sourceBoatId = context.sourceBoatId;
    shot.sourcePortId = context.sourcePortId;
    shot.targetUnitId = context.targetUnitId;
    shot.targetBoatId = context.targetBoatId;
    shot.targetPortId = context.targetPortId;
    shot.damage = weapon.damage * context.damageMultiplier;
    shot.speedKmPerSecond =
        flightSpeedKmPerSecond(weapon.category, shot.delivery, context.distanceKm);
    projectiles_.push_back(shot);

    CombatEvent event;
    event.kind = CombatEvent::Kind::Muzzle;
    event.at = context.origin;
    event.category = weapon.category;
    event.delivery = shot.delivery;
    event.mountIndex = static_cast<int>(index);
    event.entityId = context.sourceBoatId != 0 ? context.sourceBoatId : context.sourceUnitId;
    event.entityIsBoat = context.sourceBoatId != 0;
    event.headingDeg = context.bearingDeg;
    combatEvents_.push_back(event);

    ++fired;
  }
  return fired;
}

void GameState::updateCombat(double dtSeconds) {
  if (dtSeconds <= 0.0) return;

  // A long frame - a load, a debug step - must not let a round teleport past its target, so
  // the fight is stepped in slices no longer than a quarter second.
  const double step = std::min(dtSeconds, 0.25);

  for (NavalUnit& unit : navalUnits_) {
    const NavalVessel* klass =
        catalog_ != nullptr ? catalog_->navalVesselById(unit.classId) : nullptr;
    for (size_t i = 0; i < unit.mounts.size(); ++i) {
      MountState& mount = unit.mounts[i];
      if ((klass->armament[i].name.find("torpedo") != std::string::npos || klass->armament[i].name.find("Torpedo") != std::string::npos) &&
          fleetBuffMultiplier(unit.id, AdmiralBuff::TorpedoSpeed)){

            mount.reload = std::max(0.0f, mount.reload - static_cast<float>(step)) * (1 - fleetBuffMultiplier(unit.id, AdmiralBuff::TorpedoSpeed));
      }else{
        mount.reload = std::max(0.0f, mount.reload - static_cast<float>(step));
      }
      // A dry mount that has finished its reload comes back with a full magazine. Each mount
      // cycles on its own clock, so a broadside does not all fall silent at once.
      if (mount.reload <= 0.0f && mount.ammo <= 0.0f && klass != nullptr &&
          i < klass->armament.size()) {
            if (fleetBuffMultiplier(unit.id, AdmiralBuff::Magazine)){
              mount.ammo = std::max(1.0f, klass->armament[i].ammo) * fleetBuffMultiplier(unit.id, AdmiralBuff::Magazine);
            }else{
              mount.ammo = std::max(1.0f, klass->armament[i].ammo);
            }
      }
    }
    // A defensive ship only remembers an attacker for a few seconds: long enough to shoot
    // back, not so long that it stays in a fight the enemy has already broken off.
    if (unit.underFireTimer > 0.0f) {
      unit.underFireTimer = std::max(0.0f, unit.underFireTimer - static_cast<float>(step));
      if (unit.underFireTimer == 0.0f) unit.lastAttackerBoatId = 0;
    }
  }
  for (BotBoat& boat : botBoats_) {
    for (size_t i = 0; i < boat.mounts.size(); ++i) {
      MountState& mount = boat.mounts[i];
      mount.reload = std::max(0.0f, mount.reload - static_cast<float>(step));
      if (mount.reload <= 0.0f && mount.ammo <= 0.0f && i < boat.armament.size()) {
        mount.ammo = std::max(1.0f, boat.armament[i].ammo);
      }
    }
  }

  // A fleet fights as one formation: the first live target any member holds becomes the
  // squadron's target, so the whole line concentrates fire instead of each hull picking a
  // different enemy. An explicitly ordered attack is never overwritten.
  for (const Fleet& fleet : fleets_) {
    int fleetTarget = 0;
    for (const int unitId : fleet.unitIds) {
      const NavalUnit* unit = navalUnitById(unitId);
      if (unit == nullptr || unit->sunk() || unit->targetBoatId == 0) continue;
      const BotBoat* boat = botBoatById(unit->targetBoatId);
      if (boat != nullptr && !boat->sunk()) {
        fleetTarget = boat->id;
        break;
      }
    }
    if (fleetTarget == 0) continue;
    for (NavalUnit& unit : navalUnits_) {
      if (!fleet.contains(unit.id) || unit.sunk() || unit.targetExplicit) continue;
      unit.targetBoatId = fleetTarget;
    }
  }

  // The player's ships fire at an ordered target, or at whatever their posture picks up.
  if (catalog_ != nullptr) {
    for (NavalUnit& unit : navalUnits_) {
      if (unit.sunk()) continue;

      BotBoat* target = nullptr;
      if (unit.targetBoatId != 0) {
        for (BotBoat& boat : botBoats_) {
          if (boat.id == unit.targetBoatId) target = &boat;
        }
        if (target == nullptr || target->sunk()) {
          if (!unit.targetExplicit) unit.path.clear();
          unit.targetBoatId = 0;
          unit.targetExplicit = false;
          target = nullptr;
        }
      }

      const NavalVessel* klass = catalog_->navalVesselById(unit.classId);
      if (klass == nullptr) continue;

      const double reach = static_cast<double>(engagementRangeKm(klass->armament, unit.fireOrder)) *
                           fleetBuffMultiplier(unit.id, AdmiralBuff::Range);

      // No target at all: the posture decides whether the ship acquires one on its own. A hull
      // under an explicit bombard order does not take a boat on the way - the harbour is its
      // business until the order is done - but it still answers whatever shoots at it.
      if (target == nullptr) {
        if (unit.targetPortId == 0 && unit.posture == kPostureAggressive) {
          // Aggressive ships engage the nearest boat inside the patrol radius, and their
          // acquired course means advanceNavalUnits closes the range for them.
          const int boatId = nearestEnemyBoat(unit.position);
          for (BotBoat& boat : botBoats_) {
            if (boat.id != boatId) continue;
            if (haversineKm(unit.position, boat.position) <= kAggroRadiusKm) {
              target = &boat;
              unit.targetBoatId = boat.id;
              unit.targetExplicit = false;
              std::vector<LatLng> course = navalCourse(unit.position, boat.position);
              if (course.size() >= 2) {
                course.erase(course.begin());
                unit.path = std::move(course);
              }
            }
            break;
          }
        } else if (unit.targetPortId == 0 && unit.posture == kPostureOffensive) {
          // Offensive ships fire on anything already inside their reach but keep their course.
          double bestKm = reach + 1.0;
          for (BotBoat& boat : botBoats_) {
            if (boat.sunk()) continue;
            const double km = haversineKm(unit.position, boat.position);
            if (km <= reach && km < bestKm) {
              bestKm = km;
              target = &boat;
            }
          }
        } else if (unit.underFireTimer > 0.0f && unit.lastAttackerBoatId != 0) {
          // Defensive ships answer the boat that is actually shooting at them, and only while
          // it is still inside reach.
          for (BotBoat& boat : botBoats_) {
            if (boat.id != unit.lastAttackerBoatId) continue;
            if (!boat.sunk() && haversineKm(unit.position, boat.position) <= reach) {
              target = &boat;
            }
            break;
          }
        }
      }

      // A harbour under siege is the one target a ship can have without a boat in its sights.
      // The battery is worked against the port's defence pool - there is no hull to sink - and
      // the harbour's own guns answer through updatePortDefenses. The siege is this ship's
      // whole business this tick, so the boat path below is skipped.
      if (target == nullptr && unit.targetPortId != 0) {
        Port* harbour = catalog_->portByIdM(unit.targetPortId);
        if (harbour == nullptr || portOwned(harbour->id) || portDefence(harbour->id) <= 0.0) {
          unit.targetPortId = 0;
          unit.targetExplicit = false;
        } else {
          const double portKm = haversineKm(unit.position, harbour->position);
          const double portReach =
              static_cast<double>(engagementRangeKm(klass->armament, unit.fireOrder)) *
              fleetBuffMultiplier(unit.id, AdmiralBuff::Range);
          if (portKm <= portReach) {
            // A stationary hull trains its bow on the harbour the way it does on a boat.
            if (!unit.moving()) {
              double bow = 0.0;
              if (mapHeadingDegrees(unit.position, harbour->position, bow)) {
                const double delta = headingDelta(unit.headingDeg, bow);
                const double maxTurn = kTurnRateDegPerSecond * step;
                unit.headingDeg = std::fmod(
                    unit.headingDeg + std::max(-maxTurn, std::min(maxTurn, delta)) + 360.0, 360.0);
              }
            }

            const std::vector<size_t> portPlan =
                firingPlanIndices(klass->armament, unit.fireOrder,
                                  static_cast<float>(portDefence(harbour->id)),
                                  /*enemyIsSubmarine=*/false, static_cast<float>(harbour->def));
            double bearing = 0.0;
            if (!mapHeadingDegrees(unit.position, harbour->position, bearing)) {
              bearing = unit.headingDeg;
            }

            FireContext portSalvo;
            portSalvo.origin = unit.position;
            portSalvo.targetPosition = harbour->position;
            portSalvo.sourceUnitId = unit.id;
            portSalvo.targetPortId = harbour->id;
            portSalvo.distanceKm = portKm;
            portSalvo.bearingDeg = bearing;
            // The bombardment is the player's own order, so the player pays for it.
            portSalvo.paysForRounds = true;
            portSalvo.damageMultiplier = fleetBuffMultiplier(unit.id, AdmiralBuff::Damage);
            portSalvo.rangeMultiplier = fleetBuffMultiplier(unit.id, AdmiralBuff::Range);
            fireSalvo(klass->armament, unit.mounts, portPlan, portSalvo);
          }
        }
        continue;
      }

      if (target == nullptr) continue;

      const double distance = haversineKm(unit.position, target->position);

      // A stationary hull trains its bow on the target. The turn is presentation, not a firing
      // arc restriction: the mounts still bear, but the hull visibly commits to the fight.
      if (!unit.moving()) {
        double bearing = 0.0;
        if (mapHeadingDegrees(unit.position, target->position, bearing)) {
          const double delta = headingDelta(unit.headingDeg, bearing);
          const double maxTurn = kTurnRateDegPerSecond * step;
          unit.headingDeg = std::fmod(
              unit.headingDeg + std::max(-maxTurn, std::min(maxTurn, delta)) + 360.0, 360.0);
        }
      }

      const bool enemyIsSubmarine = target->kind == NavalModelKind::submarine;
      const float targetDefense = target->defense;
      const std::vector<size_t> plan = firingPlanIndices(klass->armament, unit.fireOrder,
                                                         target->health, enemyIsSubmarine,
                                                         targetDefense);

      double bearing = 0.0;
      if (!mapHeadingDegrees(unit.position, target->position, bearing)) bearing = unit.headingDeg;

      FireContext salvo;
      salvo.origin = unit.position;
      salvo.targetPosition = target->position;
      salvo.sourceUnitId = unit.id;
      salvo.targetBoatId = target->id;
      salvo.distanceKm = distance;
      salvo.bearingDeg = bearing;
      // Rounds cost money: a mount the treasury cannot pay for holds its fire, which is the
      // running cost of keeping a fleet at sea. The salvo skips such a mount and tries the
      // next in the plan, so a cheap gun still speaks while the missile waits for funds.
      salvo.paysForRounds = true;
      salvo.damageMultiplier = fleetBuffMultiplier(unit.id, AdmiralBuff::Damage);
      salvo.rangeMultiplier = fleetBuffMultiplier(unit.id, AdmiralBuff::Range);
      fireSalvo(klass->armament, unit.mounts, plan, salvo);
    }
  }

  // Every bot hull shoots at whatever it can reach, pirates included: a pirate never moves, but
  // it is a sentry, not a pacifist, so a ship inside its gun range is fired on. Their gunners
  // prefer the ship the hull is hunting, so an enemy keeps its fire on one quarry instead of
  // flickering between two hulls, and they open up as soon as a mount bears - not when the
  // enemy is alongside. They do not spend the player's money: their magazines are the bot's own.
  for (BotBoat& boat : botBoats_) {
    if (boat.sunk()) continue;

    const double reach = static_cast<double>(engagementRangeKm(boat.armament, kFireAuto));
    NavalUnit* target = nullptr;
    double bestScore = 1.0e18;
    for (NavalUnit& candidate : navalUnits_) {
      if (candidate.sunk()) continue;
      const double km = haversineKm(boat.position, candidate.position);
      if (km > reach) continue;
      const double score = candidate.id == boat.targetUnitId ? km * 0.5 : km;
      if (score < bestScore) {
        bestScore = score;
        target = &candidate;
      }
    }
    if (target == nullptr) continue;

    const double bestKm = haversineKm(boat.position, target->position);

    // A hull holding station comes about to face the ship it is engaging. One that is still
    // closing keeps its course, otherwise it would sail crabwise toward its target.
    double bearing = 0.0;
    if (mapHeadingDegrees(boat.position, target->position, bearing)) {
      if (!boat.moving()) {
        const double delta = headingDelta(boat.headingDeg, bearing);
        const double maxTurn = kTurnRateDegPerSecond * step;
        boat.headingDeg = std::fmod(
            boat.headingDeg + std::max(-maxTurn, std::min(maxTurn, delta)) + 360.0, 360.0);
      }
    }

    const bool enemyIsSubmarine = target->kind == NavalModelKind::submarine;
    const float targetDefense = target->defense;
    const std::vector<size_t> plan = firingPlanIndices(boat.armament, kFireAuto, target->health,
                                                       enemyIsSubmarine, targetDefense);

    FireContext salvo;
    salvo.origin = boat.position;
    salvo.targetPosition = target->position;
    salvo.sourceBoatId = boat.id;
    salvo.targetUnitId = target->id;
    salvo.distanceKm = bestKm;
    salvo.bearingDeg = bearing;
    // Bot magazines are the bot's own, so its rounds are not drawn from the player's treasury.
    salvo.paysForRounds = false;
    fireSalvo(boat.armament, boat.mounts, plan, salvo);
  }

  // The player's ports fight too: a coastal battery answers whatever raider comes inside its
  // reach, so a corsair cannot sail past an owned harbour with impunity.
  updatePortDefenses(step);

  // Fly the rounds. Each one homes on its target's live position, so a ship under way can
  // still be hit; a round whose target has already gone down carries on to the last position
  // it knew and lands in the water.
  for (Projectile& shot : projectiles_) {
    if (shot.damage < 0.0f) continue;  // already resolved, waiting to be reaped

    if (shot.targetBoatId != 0) {
      for (BotBoat& boat : botBoats_) {
        if (boat.id == shot.targetBoatId && !boat.sunk()) shot.target = boat.position;
      }
    } else if (shot.targetUnitId != 0) {
      for (NavalUnit& unit : navalUnits_) {
        if (unit.id == shot.targetUnitId && !unit.sunk()) shot.target = unit.position;
      }
    } else if (shot.targetPortId != 0) {
      const Port* harbour = catalog_ != nullptr ? catalog_->portById(shot.targetPortId) : nullptr;
      if (harbour != nullptr) shot.target = harbour->position;
    }

    const double remaining = haversineKm(shot.position, shot.target);
    const double travel = shot.speedKmPerSecond * step;
    if (travel >= remaining || remaining <= 1e-6) {
      shot.position = shot.target;
      resolveImpact(shot);
      shot.damage = -1.0f;  // reaped below
    } else {
      const double t = travel / remaining;
      shot.position = {shot.position.lat + (shot.target.lat - shot.position.lat) * t,
                       shot.position.lng + (shot.target.lng - shot.position.lng) * t};
    }
  }

  projectiles_.erase(
      std::remove_if(projectiles_.begin(), projectiles_.end(),
                     [](const Projectile& shot) { return shot.damage < 0.0f; }),
      projectiles_.end());
}

void GameState::ensurePortArmament(Port& port) {
  if (!port.equippedArmement.empty()) return;

  const Armament gun = basicPortArmament();
  port.equippedArmement.push_back(gun);
  port.attack += static_cast<int>(gun.power + 0.5f);
}

void GameState::updatePortDefenses(double dtSeconds) {
  if (catalog_ == nullptr) return;

  // A harbour the player has attacked mans its own battery and answers the ships that came to
  // take it, with the same rules the coastal batteries of an owned port use. Only contested
  // ports fight: an untouched neutral coast is not a minefield, but taking one is a battle.
  for (const int portId : contestedPortIds()) {
    if (portDefence(portId) <= 0.0) continue;
    Port* port = catalog_->portByIdM(portId);
    if (port == nullptr || !port->hasPosition()) continue;

    const std::vector<Armament> battery = harbourBattery(port->attack);
    std::vector<MountState>& mounts = portMounts_[portId];
    if (mounts.size() < battery.size()) {
      const size_t before = mounts.size();
      mounts.resize(battery.size());
      for (size_t i = before; i < mounts.size(); ++i) mounts[i].ammo = std::max(1.0f, battery[i].ammo);
    }
    for (size_t i = 0; i < mounts.size(); ++i) {
      mounts[i].reload = std::max(0.0f, mounts[i].reload - static_cast<float>(dtSeconds));
      if (mounts[i].reload <= 0.0f && mounts[i].ammo <= 0.0f && i < battery.size()) {
        mounts[i].ammo = std::max(1.0f, battery[i].ammo);
      }
    }

    // The battery engages the closest of the player's ships inside its reach. It never chases:
    // the guns cover the approaches to the harbour and nothing further.
    const double reach = static_cast<double>(engagementRangeKm(battery, kFireAuto));
    if (reach <= 0.0) continue;

    NavalUnit* target = nullptr;
    double bestKm = reach + 1.0;
    for (NavalUnit& unit : navalUnits_) {
      if (unit.sunk()) continue;
      const double km = haversineKm(port->position, unit.position);
      if (km <= reach && km < bestKm) {
        bestKm = km;
        target = &unit;
      }
    }
    if (target == nullptr) continue;

    const std::vector<size_t> plan =
        firingPlanIndices(battery, kFireAuto, target->health, /*enemyIsSubmarine=*/false,
                          target->defense);

    double bearing = 0.0;
    if (!mapHeadingDegrees(port->position, target->position, bearing)) bearing = 0.0;

    FireContext salvo;
    salvo.origin = port->position;
    salvo.targetPosition = target->position;
    salvo.sourcePortId = port->id;
    salvo.targetUnitId = target->id;
    salvo.distanceKm = bestKm;
    salvo.bearingDeg = bearing;
    // A harbour fights out of its own magazines: the player never gave the order, so the
    // battery must not spend the treasury down behind their back.
    salvo.paysForRounds = false;
    fireSalvo(battery, mounts, plan, salvo);
  }

  for (const int portId : ownedPortIds()) {
    Port* port = catalog_->portByIdM(portId);
    if (port == nullptr || !port->hasPosition()) continue;

    // A harbour the player never armed still answers with the machine gun it was founded
    // with, which also restores the gun in a game loaded from a save written before ports
    // carried one.
    ensurePortArmament(*port);

    std::vector<MountState>& mounts = portMounts_[portId];
    if (mounts.size() < port->equippedArmement.size()) {
      const size_t before = mounts.size();
      mounts.resize(port->equippedArmement.size());
      // A mount fitted since the last tick comes loaded, like a ship's.
      for (size_t i = before; i < mounts.size(); ++i) {
        mounts[i].ammo = std::max(1.0f, port->equippedArmement[i].ammo);
      }
    }

    for (size_t i = 0; i < mounts.size(); ++i) {
      MountState& mount = mounts[i];
      mount.reload = std::max(0.0f, mount.reload - static_cast<float>(dtSeconds));
      // A dry mount comes back full once the crew has changed the magazine.
      if (mount.reload <= 0.0f && mount.ammo <= 0.0f && i < port->equippedArmement.size()) {
        mount.ammo = std::max(1.0f, port->equippedArmement[i].ammo);
      }
    }

    // The battery engages the closest raider inside the reach of any fitted mount. It never
    // chases: the guns cover the approaches to the harbour and nothing further.
    const double reach =
        static_cast<double>(engagementRangeKm(port->equippedArmement, kFireAuto));
    if (reach <= 0.0) continue;

    BotBoat* target = nullptr;
    double bestKm = reach + 1.0;
    for (BotBoat& boat : botBoats_) {
      if (boat.sunk()) continue;
      const double km = haversineKm(port->position, boat.position);
      if (km <= reach && km < bestKm) {
        bestKm = km;
        target = &boat;
      }
    }
    if (target == nullptr) continue;

    const bool enemyIsSubmarine = target->kind == NavalModelKind::submarine;
    const std::vector<size_t> plan =
        firingPlanIndices(port->equippedArmement, kFireAuto, target->health, enemyIsSubmarine,
                          target->defense);

    double bearing = 0.0;
    if (!mapHeadingDegrees(port->position, target->position, bearing)) bearing = 0.0;

    FireContext salvo;
    salvo.origin = port->position;
    salvo.targetPosition = target->position;
    salvo.sourcePortId = port->id;
    salvo.targetBoatId = target->id;
    salvo.distanceKm = bestKm;
    salvo.bearingDeg = bearing;
    // The port's rounds come out of its own magazine rather than the treasury: the player never
    // gave the order, so a harbour must not spend the bank down behind their back.
    salvo.paysForRounds = false;
    fireSalvo(port->equippedArmement, mounts, plan, salvo);
  }
}

void GameState::resolveImpact(const Projectile& shot) {
  CombatEvent event;
  event.at = shot.position;
  event.category = shot.category;
  event.delivery = shot.delivery;

  // A round fired at a harbour lands on the harbour: there is no hull to sink, only a defence
  // pool to work down. A port already taken or already broken soaks the round in the water.
  if (shot.targetPortId != 0) {
    const Port* harbour = catalog_ != nullptr ? catalog_->portById(shot.targetPortId) : nullptr;
    event.entityId = shot.targetPortId;
    if (harbour == nullptr || portOwned(harbour->id) || portDefence(harbour->id) <= 0.0) {
      event.kind = CombatEvent::Kind::Impact;
      event.onWater = true;
      combatEvents_.push_back(event);
      return;
    }

    bombardPort(harbour->id, shot.damage * kDamageScale);
    event.kind = CombatEvent::Kind::Impact;
    combatEvents_.push_back(event);
    return;
  }

  if (shot.targetBoatId != 0) {
    BotBoat* target = nullptr;
    for (BotBoat& boat : botBoats_) {
      if (boat.id == shot.targetBoatId) target = &boat;
    }
    if (target == nullptr || target->sunk()) {
      event.kind = CombatEvent::Kind::Impact;
      event.onWater = true;
      event.entityId = shot.targetBoatId;
      event.entityIsBoat = true;
      event.delivery = shot.delivery;
      combatEvents_.push_back(event);
      return;
    }

    target->health = std::max(
        0.0f, target->health - damageAfterDefense(shot.damage * kDamageScale, target->defense));
    // Being hit is its own alarm: a boat that is shot from beyond its lookouts' reach still
    // turns on the shooter and closes to answer, rather than sitting there and taking it.
    if (shot.sourceUnitId != 0) {
      target->targetUnitId = shot.sourceUnitId;
      target->retaliating = true;
    }
    event.kind = CombatEvent::Kind::Impact;
    event.entityId = target->id;
    event.entityIsBoat = true;
    event.delivery = shot.delivery;
    combatEvents_.push_back(event);

    if (target->sunk()) {
      CombatEvent kill;
      kill.kind = CombatEvent::Kind::Kill;
      kill.at = target->position;
      kill.category = shot.category;
      kill.entityId = target->id;
      kill.entityIsBoat = true;
      kill.delivery = shot.delivery;
      kill.hullKind = target->kind;
      kill.headingDeg = target->headingDeg;
      // The kill feed needs both names and both sides: the victim is a bot boat (an enemy),
      // and the shooter is either one of the player's hulls or another bot.
      kill.victimName = target->name;
      kill.victimIsAlly = false;
      if (shot.sourceUnitId != 0) {
        const NavalUnit* attacker = navalUnitById(shot.sourceUnitId);
        kill.attackerName = attacker != nullptr ? attacker->className : "Unknown";
        kill.attackerIsAlly = true;
      } else if (shot.sourceBoatId != 0) {
        const BotBoat* attacker = botBoatById(shot.sourceBoatId);
        kill.attackerName = attacker != nullptr ? attacker->name : "Unknown";
        kill.attackerIsAlly = false;
      } else if (shot.sourcePortId != 0) {
        const Port* attacker = catalog_ != nullptr ? catalog_->portById(shot.sourcePortId) : nullptr;
        kill.attackerName = attacker != nullptr ? attacker->name : "Coastal battery";
        kill.attackerIsAlly = true;
      }
      combatEvents_.push_back(kill);
    }
    return;
  }

  if (shot.targetUnitId != 0) {
    NavalUnit* target = nullptr;
    for (NavalUnit& unit : navalUnits_) {
      if (unit.id == shot.targetUnitId) target = &unit;
    }
    if (target == nullptr || target->sunk()) {
      event.kind = CombatEvent::Kind::Impact;
      event.onWater = true;
      event.entityId = shot.targetUnitId;
      event.delivery = shot.delivery;
      combatEvents_.push_back(event);
      return;
    }

    const float defense =
        target->defense * fleetBuffMultiplier(target->id, AdmiralBuff::Defense);
    target->health = std::max(
        0.0f, target->health - damageAfterDefense(shot.damage * kDamageScale, defense));
    // Remember who is shooting: a defensive ship returns fire at this boat for a few seconds.
    if (shot.sourceBoatId != 0) {
      target->lastAttackerBoatId = shot.sourceBoatId;
      target->underFireTimer = 6.0f;
    }
    event.kind = CombatEvent::Kind::Impact;
    event.entityId = target->id;
    event.delivery = shot.delivery;
    combatEvents_.push_back(event);

    if (target->sunk()) {
      CombatEvent kill;
      kill.kind = CombatEvent::Kind::Kill;
      kill.at = target->position;
      kill.category = shot.category;
      kill.delivery = shot.delivery;
      kill.entityId = target->id;
      kill.hullKind = target->kind;
      kill.headingDeg = target->headingDeg;
      // The victim is one of the player's hulls (an ally); the shooter is normally a bot
      // boat. Friendly fire is possible in the data, so name a player's hull when it did it.
      kill.victimName = target->className;
      kill.victimIsAlly = true;
      if (shot.sourceBoatId != 0) {
        const BotBoat* attacker = botBoatById(shot.sourceBoatId);
        kill.attackerName = attacker != nullptr ? attacker->name : "Unknown";
        kill.attackerIsAlly = false;
      } else if (shot.sourceUnitId != 0) {
        const NavalUnit* attacker = navalUnitById(shot.sourceUnitId);
        kill.attackerName = attacker != nullptr ? attacker->className : "Unknown";
        kill.attackerIsAlly = true;
      }
      combatEvents_.push_back(kill);
      // messages_.push_back("Lost " + target->className + " to enemy fire.");
    }
  }
}

bool GameState::buyTechnology(const std::string& technologyId, std::string& error) {
  const int cost = nextLevelCost(research_, technologyId);
  if (cost < 0) {
    error = "That technology is already fully researched.";
    return false;
  }
  if (cost > researchPoints_) {
    error = "Not enough research points: " + std::to_string(cost) + " needed, " +
            std::to_string(researchPoints_) + " available.";
    return false;
  }

  // A child technology only opens once every parent has been researched at least once.
  const std::vector<std::string> missing = missingParents(research_, technologyId);
  if (!missing.empty()) {
    std::string names;
    for (const std::string& parent : missing) {
      if (!names.empty()) names += ", ";
      names += parent;
    }
    error = "Requires: " + names + ".";
    return false;
  }

  researchPoints_ -= cost;
  setTechLevel(research_, technologyId, techLevel(research_, technologyId) + 1);
  return true;
}

// ---------------------------------------------------------------------------
// Port upgrades and modules
// ---------------------------------------------------------------------------

int GameState::portLevel(int portId) const {
  const auto found = portUpgrades_.find(portId);
  if (found == portUpgrades_.end()) return 1;
  return std::max(1, std::min(found->second.level, MAX_PORT_LEVEL));
}

int GameState::portSlotCount(int portId) const { return portSlotCountForLevel(portLevel(portId)); }

const std::vector<int>& GameState::portModules(int portId) const {
  static const std::vector<int> empty;
  const auto found = portUpgrades_.find(portId);
  return found == portUpgrades_.end() ? empty : found->second.moduleIds;
}

bool GameState::portHasModule(int portId, int moduleId) const {
  const std::vector<int>& modules = portModules(portId);
  return std::find(modules.begin(), modules.end(), moduleId) != modules.end();
}


double GameState::portUpgradeCost(int portId) const {
  if (catalog_ == nullptr) return -1.0;
  const Port* port = catalog_->portById(portId);
  if (port == nullptr) return -1.0;

  const int level = portLevel(portId);
  if (level >= MAX_PORT_LEVEL) return -1.0;
  // Each upgrade costs twice the one before it, starting at twice the claim price.
  return portCost(*port) * static_cast<double>(1 << level);
}

bool GameState::upgradePort(int portId, std::string& error) {
  if (!portOwned(portId)) {
    error = "Claim the port before upgrading it.";
    return false;
  }

  const double cost = portUpgradeCost(portId);
  if (cost < 0.0) {
    error = "That port is already fully upgraded.";
    return false;
  }
  if (money_ < cost) {
    error = "Not enough money!";
    return false;
  }

  const Port* port = catalog_->portById(portId);

  money_ -= cost;
  PortUpgrade& upgrade = portUpgrades_[portId];
  upgrade.level = std::min(portLevel(portId) + 1, MAX_PORT_LEVEL);
  return true;
}

bool GameState::installPortModule(int portId, int moduleId, double cost, std::string& error) {
  if (!portOwned(portId)) {
    error = "Claim the port before fitting modules.";
    return false;
  }

  PortUpgrade& upgrade = portUpgrades_[portId];
  if (portHasModule(portId, moduleId)) {
    error = "That module is already fitted here.";
    return false;
  }
  if (static_cast<int>(upgrade.moduleIds.size()) >= portSlotCount(portId)) {
    error = "No free module slot - upgrade the port first.";
    return false;
  }
  if (money_ < cost) {
    error = "Not enough money!";
    return false;
  }

  money_ -= cost;
  upgrade.moduleIds.push_back(moduleId);
  return true;
}

std::vector<const Vessel*> GameState::readyVessels() const {
  std::vector<const Vessel*> ready;
  if (catalog_ == nullptr) return ready;

  for (int id : ownedVesselIds()) {
    const bool busy = std::any_of(voyages_.begin(), voyages_.end(),
                                  [&](const ActiveVoyage& v) { return v.vesselId == id; });
    if (busy) continue;
    if (const Vessel* vessel = catalog_->vesselById(id)) ready.push_back(vessel);
  }
  return ready;
}

bool GameState::startVoyage(int fromPortId, int toPortId, int vesselId, const CargoOption& cargo,
                            double nowMs, std::string& error) {
  if (catalog_ == nullptr || router_ == nullptr) {
    error = "No game data loaded.";
    return false;
  }

  const Port* fromPort = catalog_->portById(fromPortId);
  const Port* toPort = catalog_->portById(toPortId);
  const Vessel* vessel = catalog_->vesselById(vesselId);

  if (fromPort == nullptr || toPort == nullptr || vessel == nullptr) {
    error = "Unknown port or vessel.";
    return false;
  }
  if (!portOwned(fromPortId)) {
    error = "You must own the departure port to sail from it.";
    return false;
  }
  if (std::any_of(voyages_.begin(), voyages_.end(),
                  [&](const ActiveVoyage& v) { return v.vesselId == vesselId; })) {
    error = vessel->companyName + " is already at sea. Give the route to a vessel that is in port.";
    return false;
  }

  const bool owned = vesselOwned(vesselId);
  const double cost = owned ? 0.0 : hullPrice(vessel->gt);
  if (cost > money_) {
    error = "Not enough money for this hull: " + moneyText(cost) + " needed.";
    return false;
  }

  const RouteResult route = router_->route(fromPort->position, toPort->position);
  const VoyageOutcome preview = resolveVoyage(cargo.valueK, route.distanceKm, research_, 100.0,
                                              cargo.realTrade);

  if (cost > 0.0) {
    money_ -= cost;
    ownedVessels_.insert(vesselId);
  }

  ActiveVoyage voyage;
  voyage.id = std::to_string(vesselId) + "-" + std::to_string(static_cast<long long>(nowMs));
  voyage.fromPortId = fromPortId;
  voyage.toPortId = toPortId;
  voyage.vesselId = vesselId;
  voyage.cargo = cargo;
  voyage.distanceKm = route.distanceKm;
  voyage.durationMs = preview.durationMs > 0 ? preview.durationMs : 1.0;
  voyage.realTrade = cargo.realTrade;
  voyage.route = route.points;
  voyage.startedAtMs = gameTime(nowMs);
  voyage.progress = 0.0;

  voyages_.push_back(std::move(voyage));
  return true;
}

void GameState::cancelVoyage(const std::string& id) {
  voyages_.erase(std::remove_if(voyages_.begin(), voyages_.end(),
                                [&](const ActiveVoyage& v) { return v.id == id; }),
                 voyages_.end());
}

void GameState::update(double nowMs) {
  const double now = gameTime(nowMs);
  const double dtSeconds = (now - lastGameTimeMs_) / 1000.0;
  lastGameTimeMs_ = now;
  // The rival powers make their decisions before anything moves, so a hull laid down or a
  // crossing dispatched this tick takes part in it.
  updateFactions(dtSeconds);
  advanceNavalUnits(dtSeconds);
  advanceBotBoats(dtSeconds);
  updateCombat(dtSeconds);
  advanceRepairs(dtSeconds);

  for (ActiveVoyage& voyage : voyages_) {
    if (voyage.progress >= 100.0) continue;
    voyage.progress = std::min(100.0, (now - voyage.startedAtMs) / voyage.durationMs * 100.0);
  }

  std::vector<ActiveVoyage> arrived;
  for (const ActiveVoyage& voyage : voyages_) {
    if (voyage.progress >= 100.0 && delivered_.count(voyage.id) == 0) arrived.push_back(voyage);
  }
  if (arrived.empty()) return;

  std::uniform_real_distribution<double> roll(0.0, 100.0);
  double totalReward = 0.0;
  int totalPoints = 0;
  std::ostringstream report;

  for (const ActiveVoyage& voyage : arrived) {
    delivered_.insert(voyage.id);

    const VoyageOutcome outcome = resolveVoyage(voyage.cargo.valueK, voyage.distanceKm, research_,
                                                roll(rng_), voyage.realTrade);
    totalReward += outcome.reward;
    totalPoints += outcome.points;

    const Port* toPort = catalog_ != nullptr ? catalog_->portById(voyage.toPortId) : nullptr;
    const std::string toName = toPort != nullptr ? toPort->name : "port";

    report << voyage.cargo.description << " delivered to " << toName << ".\n";
    if (outcome.realTrade) {
      report << "Real trade bonus " << static_cast<int>(kRealTradeBonusShare * 100)
             << "%: +" << moneyText(outcome.bonus) << ".\n";
    }
    if (outcome.lost) {
      report << "Cargo was hit in transit: -" << moneyText(outcome.lostAmount) << ".\n";
    } else {
      report << "The cargo arrived intact.\n";
    }
    report << "Freight earned: " << moneyText(outcome.reward) << ".\n";
    report << "Research: +" << outcome.points << " points.\n\n";
  }

  money_ += totalReward;
  researchPoints_ += totalPoints;

  voyages_.erase(std::remove_if(voyages_.begin(), voyages_.end(),
                                [&](const ActiveVoyage& v) { return delivered_.count(v.id) > 0; }),
                 voyages_.end());

  messages_.push_back(report.str());
}

std::vector<FleetEntry> GameState::fleet() const {
  std::vector<FleetEntry> entries;
  if (catalog_ == nullptr) return entries;

  for (int id : ownedVesselIds()) {
    FleetEntry entry;
    entry.vessel = catalog_->vesselById(id);

    const ActiveVoyage* voyage = nullptr;
    for (const ActiveVoyage& candidate : voyages_) {
      if (candidate.vesselId == id) {
        voyage = &candidate;
        break;
      }
    }

    if (voyage != nullptr) {
      entry.voyage = voyage;
      entry.busy = true;
      entry.toPort = catalog_->portById(voyage->toPortId);
      entry.etaMs = std::max(0.0, voyage->durationMs * (1.0 - voyage->progress / 100.0));
    }

    entries.push_back(entry);
  }

  return entries;
}

std::vector<std::string> GameState::takeMessages() {
  std::vector<std::string> drained;
  drained.swap(messages_);
  return drained;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

std::string GameState::saveToJson() const {
  nlohmann::json root;
  root["version"] = 1;
  root["money"] = money_;
  root["researchPoints"] = researchPoints_;
  nlohmann::json research = nlohmann::json::object();
  for (const Technology& technology : technologies()) {
    research[technology.id] = techLevel(research_, technology.id);
  }
  root["research"] = research;

  root["claimedPorts"] = ownedPortIds();
  root["ownedVessels"] = ownedVesselIds();

  // A siege is part of the state: which harbours have been attacked and what is left of
  // their defences has to survive a save, or a reload would hand the player a fresh port.
  nlohmann::json defences = nlohmann::json::array();
  for (const auto& entry : portDefence_) {
    nlohmann::json row;
    row["port"] = entry.first;
    row["health"] = entry.second;
    defences.push_back(std::move(row));
  }
  root["portDefence"] = std::move(defences);

  nlohmann::json upgrades = nlohmann::json::array();
  for (const int portId : ownedPortIds()) {
    if (portUpgrades_.find(portId) == portUpgrades_.end()) continue;
    const PortUpgrade& upgrade = portUpgrades_.at(portId);
    nlohmann::json entry;
    entry["port"] = portId;
    entry["level"] = upgrade.level;
    entry["modules"] = upgrade.moduleIds;
    upgrades.push_back(std::move(entry));
  }
  root["portUpgrades"] = std::move(upgrades);

  nlohmann::json voyages = nlohmann::json::array();
  for (const ActiveVoyage& voyage : voyages_) {
    nlohmann::json entry;
    entry["id"] = voyage.id;
    entry["fromPort"] = voyage.fromPortId;
    entry["toPort"] = voyage.toPortId;
    entry["vessel"] = voyage.vesselId;
    entry["cargoCode"] = voyage.cargo.productCode;
    entry["cargoDescription"] = voyage.cargo.description;
    entry["cargoValueK"] = voyage.cargo.valueK;
    entry["distanceKm"] = voyage.distanceKm;
    entry["durationMs"] = voyage.durationMs;
    entry["realTrade"] = voyage.realTrade;
    entry["startedAtMs"] = voyage.startedAtMs;
    entry["progress"] = voyage.progress;

    nlohmann::json route = nlohmann::json::array();
    for (const LatLng& point : voyage.route) route.push_back({point.lat, point.lng});
    entry["route"] = std::move(route);

    voyages.push_back(std::move(entry));
  }
  root["voyages"] = std::move(voyages);

  nlohmann::json naval = nlohmann::json::array();
  for (const NavalUnit& unit : navalUnits_) {
    nlohmann::json entry;
    entry["id"] = unit.id;
    entry["classId"] = unit.classId;
    entry["className"] = unit.className;
    entry["type"] = unit.type;
    entry["kind"] = static_cast<int>(unit.kind);
    entry["homePort"] = unit.homePortId;
    entry["lat"] = unit.position.lat;
    entry["lng"] = unit.position.lng;
    entry["heading"] = unit.headingDeg;
    entry["speed"] = unit.speedKmPerSecond;
    entry["health"] = unit.health;
    entry["fireOrder"] = unit.fireOrder;
    entry["posture"] = unit.posture;
    entry["targetBoat"] = unit.targetBoatId;
    entry["targetPort"] = unit.targetPortId;

    nlohmann::json mounts = nlohmann::json::array();
    for (const MountState& mount : unit.mounts) mounts.push_back(mount.ammo);
    entry["mounts"] = std::move(mounts);

    nlohmann::json path = nlohmann::json::array();
    for (const LatLng& point : unit.path) path.push_back({point.lat, point.lng});
    entry["path"] = std::move(path);
    naval.push_back(std::move(entry));
  }
  root["navalUnits"] = std::move(naval);

  // Fleets and the admiralty: only the mutable parts of the roster are written, because the
  // cards themselves come from generateAllAdmirals() at load time.
  nlohmann::json admirals = nlohmann::json::array();
  for (const Admiral& admiral : admirals_) {
    nlohmann::json entry;
    entry["id"] = admiral.id;
    entry["hired"] = admiral.hired;
    entry["level"] = admiral.level;
    entry["experience"] = admiral.experience;
    entry["fleet"] = admiral.fleetId;
    admirals.push_back(std::move(entry));
  }
  root["admirals"] = std::move(admirals);

  nlohmann::json fleets = nlohmann::json::array();
  for (const Fleet& fleet : fleets_) {
    nlohmann::json entry;
    entry["id"] = fleet.id;
    entry["name"] = fleet.name;
    entry["admiral"] = fleet.admiralId;
    entry["units"] = fleet.unitIds;
    fleets.push_back(std::move(entry));
  }
  root["fleets"] = std::move(fleets);

  // The rival powers: their economy and their research are state, not a script, so a save
  // carries their harbours, their purses and their trees.
  nlohmann::json powers = nlohmann::json::array();
  for (const AIPlayer& power : factions_) {
    nlohmann::json entry;
    entry["name"] = power.name;
    entry["money"] = power.money;
    entry["ports"] = power.portIds;
    entry["researchPoints"] = power.researchPoints;
    entry["tradeTimer"] = power.tradeTimer;
    entry["buildTimer"] = power.buildTimer;
    entry["researchTimer"] = power.researchTimer;
    nlohmann::json levels = nlohmann::json::object();
    for (const Technology& technology : technologies()) {
      levels[technology.id] = techLevel(power.research, technology.id);
    }
    entry["research"] = std::move(levels);
    powers.push_back(std::move(entry));
  }
  root["factions"] = std::move(powers);

  nlohmann::json runs = nlohmann::json::array();
  for (const MerchantRun& run : merchantRuns_) {
    nlohmann::json entry;
    entry["id"] = run.id;
    entry["faction"] = run.faction;
    entry["from"] = run.fromPortId;
    entry["to"] = run.toPortId;
    entry["cargo"] = run.cargoDescription;
    entry["cargoValueK"] = run.cargoValueK;
    entry["distanceKm"] = run.distanceKm;
    entry["durationMs"] = run.durationMs;
    entry["progress"] = run.progress;
    nlohmann::json route = nlohmann::json::array();
    for (const LatLng& point : run.route) route.push_back({point.lat, point.lng});
    entry["route"] = std::move(route);
    runs.push_back(std::move(entry));
  }
  root["merchantRuns"] = std::move(runs);

  nlohmann::json boats = nlohmann::json::array();
  for (const BotBoat& boat : botBoats_) {
    nlohmann::json entry;
    entry["id"] = boat.id;
    entry["name"] = boat.name;
    entry["owner"] = boat.owner;
    entry["pirate"] = boat.pirate;
    entry["classId"] = boat.classId;
    entry["homePort"] = boat.homePortId;
    entry["kind"] = static_cast<int>(boat.kind);
    entry["maxHealth"] = boat.maxHealth;
    entry["defense"] = boat.defense;
    entry["health"] = boat.health;
    // Bot hulls steam after the player's ships, so where they are is part of the saved state: an
    // enemy that was hunting must not snap back to its berth on load, and a pirate must reload
    // on the station it holds rather than wherever its band was seeded from.
    entry["lat"] = boat.position.lat;
    entry["lng"] = boat.position.lng;
    entry["stationLat"] = boat.station.lat;
    entry["stationLng"] = boat.station.lng;
    entry["heading"] = boat.headingDeg;
    entry["targetUnit"] = boat.targetUnitId;
    entry["retaliating"] = boat.retaliating;
    nlohmann::json mounts = nlohmann::json::array();
    for (const MountState& mount : boat.mounts) mounts.push_back(mount.ammo);
    entry["mounts"] = std::move(mounts);
    boats.push_back(std::move(entry));
  }
  root["botBoats"] = std::move(boats);

  return root.dump(2);
}

bool GameState::loadFromJson(const std::string& text, std::string& error, double nowMs) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
  } catch (const std::exception& exception) {
    error = std::string("Save file is not valid JSON: ") + exception.what();
    return false;
  }

  newGame();

  money_ = root.value("money", kBaseMoney);
  researchPoints_ = root.value("researchPoints", 0);

  if (root.contains("research")) {
    const auto& levels = root["research"];
    for (auto it = levels.begin(); it != levels.end(); ++it) {
      setTechLevel(research_, it.key(), it.value().get<int>());
    }
  }

  if (root.contains("claimedPorts")) {
    for (const auto& id : root["claimedPorts"]) claimedPorts_.insert(id.get<int>());
  }
  if (root.contains("ownedVessels")) {
    for (const auto& id : root["ownedVessels"]) ownedVessels_.insert(id.get<int>());
  }

  portDefence_.clear();
  if (root.contains("portDefence")) {
    for (const auto& entry : root["portDefence"]) {
      portDefence_[entry.value("port", 0)] = entry.value("health", 0.0f);
    }
  }

  portUpgrades_.clear();
  if (root.contains("portUpgrades")) {
    for (const auto& entry : root["portUpgrades"]) {
      PortUpgrade upgrade;
      upgrade.level = std::max(1, std::min(entry.value("level", 1), MAX_PORT_LEVEL));
      if (entry.contains("modules")) {
        for (const auto& moduleId : entry["modules"]) {
          upgrade.moduleIds.push_back(moduleId.get<int>());
        }
      }
      portUpgrades_[entry.value("port", 0)] = std::move(upgrade);
    }
  }

  voyages_.clear();
  if (root.contains("voyages")) {
    for (const auto& entry : root["voyages"]) {
      ActiveVoyage voyage;
      voyage.id = entry.value("id", std::string());
      voyage.fromPortId = entry.value("fromPort", 0);
      voyage.toPortId = entry.value("toPort", 0);
      voyage.vesselId = entry.value("vessel", 0);
      voyage.cargo.productCode = entry.value("cargoCode", std::string());
      voyage.cargo.description = entry.value("cargoDescription", std::string());
      voyage.cargo.valueK = entry.value("cargoValueK", 0.0);
      voyage.distanceKm = entry.value("distanceKm", 0.0);
      voyage.durationMs = entry.value("durationMs", 1.0);
      voyage.realTrade = entry.value("realTrade", false);
      voyage.progress = entry.value("progress", 0.0);

      if (entry.contains("route")) {
        for (const auto& point : entry["route"]) {
          voyage.route.push_back({point[0].get<double>(), point[1].get<double>()});
        }
      }

      // Re-anchor the crossing to the current clock so a save/load does not fast-forward
      // or rewind it.
      voyage.startedAtMs = -voyage.progress / 100.0 * voyage.durationMs;

      if (voyage.route.size() < 2 && catalog_ != nullptr && router_ != nullptr) {
        const Port* fromPort = catalog_->portById(voyage.fromPortId);
        const Port* toPort = catalog_->portById(voyage.toPortId);
        if (fromPort != nullptr && toPort != nullptr) {
          voyage.route = router_->route(fromPort->position, toPort->position).points;
        }
      }

      voyages_.push_back(std::move(voyage));
    }
  }

  navalUnits_.clear();
  nextNavalUnitId_ = 1;
  if (root.contains("navalUnits")) {
    for (const auto& entry : root["navalUnits"]) {
      NavalUnit unit;
      unit.id = entry.value("id", 0);
      unit.classId = entry.value("classId", 0);
      unit.className = entry.value("className", std::string());
      unit.type = entry.value("type", std::string());
      unit.kind = static_cast<NavalModelKind>(entry.value("kind", 0));
      unit.homePortId = entry.value("homePort", 0);
      unit.position = {entry.value("lat", 0.0), entry.value("lng", 0.0)};
      unit.headingDeg = entry.value("heading", 0.0);
      unit.speedKmPerSecond = entry.value("speed", 220.0);

      // A hull from a save that predates the combat layer has no health of its own: it takes
      // its class's full figure, and its mounts are fitted fresh.
      const NavalVessel* klass =
          catalog_ != nullptr ? catalog_->navalVesselById(unit.classId) : nullptr;
      unit.maxHealth = klass != nullptr ? std::max(60.0f, klass->health) : 100.0f;
      unit.health = entry.value("health", unit.maxHealth);
      unit.defense = klass != nullptr ? klass->defense : 0.0f;
      unit.fireOrder = entry.value("fireOrder", static_cast<unsigned>(kFireAuto));
      unit.posture = std::max(static_cast<int>(kPostureOffensive),
                              std::min(static_cast<int>(kPostureAggressive),
                                       entry.value("posture", static_cast<int>(kPostureOffensive))));
      unit.targetBoatId = entry.value("targetBoat", 0);
      unit.targetPortId = entry.value("targetPort", 0);
      unit.targetExplicit = unit.targetPortId != 0;
      if (klass != nullptr) fitMounts(unit, *klass);
      if (entry.contains("mounts")) {
        for (size_t i = 0; i < unit.mounts.size() && i < entry["mounts"].size(); ++i) {
          unit.mounts[i].ammo = entry["mounts"][i].get<float>();
        }
      }

      if (entry.contains("path")) {
        for (const auto& point : entry["path"]) {
          unit.path.push_back({point[0].get<double>(), point[1].get<double>()});
        }
      }
      if (unit.id >= nextNavalUnitId_) nextNavalUnitId_ = unit.id + 1;
      navalUnits_.push_back(std::move(unit));
    }
  }

  // The admiral roster itself is fixed data; the save only carries what the player changed.
  if (root.contains("admirals")) {
    for (const auto& entry : root["admirals"]) {
      const int id = entry.value("id", 0);
      for (Admiral& admiral : admirals_) {
        if (admiral.id != id) continue;
        admiral.hired = entry.value("hired", false);
        admiral.level = std::max(1, entry.value("level", 1));
        admiral.experience = std::max(0, entry.value("experience", 0));
        admiral.fleetId = entry.value("fleet", 0);
        break;
      }
    }
  }

  fleets_.clear();
  nextFleetId_ = 1;
  if (root.contains("fleets")) {
    for (const auto& entry : root["fleets"]) {
      Fleet fleet;
      fleet.id = entry.value("id", 0);
      fleet.name = entry.value("name", std::string());
      fleet.admiralId = entry.value("admiral", 0);
      if (fleet.name.empty()) fleet.name = ordinal(static_cast<int>(fleets_.size()) + 1) + " Fleet";
      if (entry.contains("units")) {
        for (const auto& unitId : entry["units"]) {
          const int id = unitId.get<int>();
          if (navalUnitById(id) != nullptr) fleet.unitIds.push_back(id);
        }
      }
      if (fleet.id >= nextFleetId_) nextFleetId_ = fleet.id + 1;
      fleets_.push_back(std::move(fleet));
    }
  }

  // Normalise both directions of the admiral/fleet link, and drop an assignment that points
  // at a fleet or an unhired officer the save no longer has.
  for (Admiral& admiral : admirals_) admiral.fleetId = 0;
  for (Fleet& fleet : fleets_) {
    bool valid = false;
    for (const Admiral& admiral : admirals_) {
      if (admiral.id == fleet.admiralId && admiral.hired) valid = true;
    }
    if (!valid) fleet.admiralId = 0;
    if (fleet.admiralId == 0) continue;
    for (Admiral& admiral : admirals_) {
      if (admiral.id == fleet.admiralId) admiral.fleetId = fleet.id;
    }
  }

  // The rival powers, rebuilt from the save. They are seeded from the roster at the start of a
  // game, so a save decides which harbours they now hold and what is left in their purses.
  factions_.clear();
  merchantRuns_.clear();
  nextMerchantRunId_ = 1;
  if (root.contains("factions")) {
    for (const auto& entry : root["factions"]) {
      AIPlayer power;
      power.name = entry.value("name", std::string());
      power.money = entry.value("money", kBaseMoney);
      if (entry.contains("ports")) {
        for (const auto& id : entry["ports"]) {
          const int portId = id.get<int>();
          if (power.ownsPort(portId)) continue;
          power.portIds.push_back(portId);
        }
      }
      power.researchPoints = entry.value("researchPoints", 0);
      power.tradeTimer = entry.value("tradeTimer", kFactionTradeIntervalSeconds);
      power.buildTimer = entry.value("buildTimer", kFactionBuildIntervalSeconds);
      power.researchTimer = entry.value("researchTimer", kFactionResearchIntervalSeconds);
      if (entry.contains("research")) {
        for (auto it = entry["research"].begin(); it != entry["research"].end(); ++it) {
          setTechLevel(power.research, it.key(), it.value().get<int>());
        }
      }
      factions_.push_back(std::move(power));
    }
  }

  if (root.contains("merchantRuns")) {
    for (const auto& entry : root["merchantRuns"]) {
      MerchantRun run;
      run.id = entry.value("id", 0);
      run.faction = entry.value("faction", 0);
      run.fromPortId = entry.value("from", 0);
      run.toPortId = entry.value("to", 0);
      run.cargoDescription = entry.value("cargo", std::string());
      run.cargoValueK = entry.value("cargoValueK", 0.0);
      run.distanceKm = entry.value("distanceKm", 0.0);
      run.durationMs = entry.value("durationMs", 1.0);
      run.progress = entry.value("progress", 0.0);
      if (entry.contains("route")) {
        for (const auto& point : entry["route"]) {
          run.route.push_back({point[0].get<double>(), point[1].get<double>()});
        }
      }
      // Re-anchor the crossing to the current clock, so a save/load neither fast-forwards nor
      // rewinds a rival's convoy.
      run.startedAtMs = -run.progress / 100.0 * run.durationMs;
      if (run.id >= nextMerchantRunId_) nextMerchantRunId_ = run.id + 1;
      merchantRuns_.push_back(std::move(run));
    }
  }

  // Rival hulls are rebuilt from the save rather than matched against a spawn table: they are
  // laid down during play, so the save is the only record of which power owns which hull, what
  // class it was built from and how much of its hull is left.
  botBoats_.clear();
  nextBoatId_ = 1;
  if (root.contains("botBoats")) {
    for (const auto& entry : root["botBoats"]) {
      BotBoat boat;
      boat.id = entry.value("id", 0);
      boat.name = entry.value("name", std::string());
      boat.owner = entry.value("owner", std::string());
      boat.pirate = entry.value("pirate", false);
      boat.classId = entry.value("classId", 0);
      boat.homePortId = entry.value("homePort", 0);
      boat.kind = static_cast<NavalModelKind>(entry.value("kind", 0));
      boat.position = {entry.value("lat", 0.0), entry.value("lng", 0.0)};
      // A save written before stations existed has none, so a hull with no station recorded
      // keeps the post it was reloaded on rather than snapping to the origin.
      boat.station = {entry.value("stationLat", boat.position.lat),
                      entry.value("stationLng", boat.position.lng)};
      boat.headingDeg = entry.value("heading", 0.0);
      boat.targetUnitId = entry.value("targetUnit", 0);
      boat.retaliating = entry.value("retaliating", false);
      fitBotArmament(boat);
      boat.health = std::min(entry.value("health", boat.maxHealth), boat.maxHealth);
      if (entry.contains("mounts")) {
        for (size_t i = 0; i < boat.mounts.size() && i < entry["mounts"].size(); ++i) {
          boat.mounts[i].ammo = entry["mounts"][i].get<float>();
        }
      }
      if (boat.id >= nextBoatId_) nextBoatId_ = boat.id + 1;
      botBoats_.push_back(std::move(boat));
    }
  }

  clockOriginMs_ = nowMs;
  lastGameTimeMs_ = 0.0;
  return true;
}

}  // namespace sea

#include "research.h"

#include <algorithm>
#include <cmath>

namespace sea {
namespace {

const char* kCapacityClasses[] = {"V", "S", "M", "L"};
constexpr int kCapacityClassCount = 4;

bool isCapacityClass(const std::string& value) {
  for (const char* candidate : kCapacityClasses) {
    if (value == candidate) return true;
  }
  return false;
}

int capacityRank(const std::string& value) {
  for (int index = 0; index < kCapacityClassCount; ++index) {
    if (value == kCapacityClasses[index]) return index;
  }
  return -1;
}

/// What a berth can take: the port's own max-vessel size when it is recorded, otherwise
/// its harbor-size band.
std::string portCapacityClass(const Port& port) {
  if (isCapacityClass(port.maxVessel)) return port.maxVessel;
  if (isCapacityClass(port.harborSize)) return port.harborSize;
  return "";
}

}  // namespace

const std::vector<Technology>& technologies() {
  static const std::vector<Technology> table = {
      {"cranes", "Automated Cranes",
       "Robotic loading crews turn vessels around faster, shortening every voyage.",
       {60, 140, 280}},
              {"boat_armoring1", "Boat Armoring I",
       "Give better defense and resistance to critikal attack for boat",
       {80, 180, 360}, {"steel"}},
      {"warehouses", "Warehouse Expansion",
       "Bigger storage lets every port take fuller consignments and charge for them.",
       {50, 110, 220}},
      {"security", "Security Systems",
       "Seals, guards and trackers keep cargo from disappearing in transit.",
       {40, 90, 180}},
      {"dredging", "Deepwater Dredging",
       "Deeper channels let every port you own take hulls a class larger.",
       {80, 180, 360}},

       //fleet organisation
       {"fleet_command", "Fleet Command",
       "A permanent staff lets the navy keep one more fleet at sea per level.",
       {80, 180, 360}},

       {"fleet_organization", "Naval Organization",
       "Streamlined squadron rosters add two ships to every fleet per level.",
       {80, 180, 360}, {"fleet_command"}},

       {"admiral_corps", "Admiral Corps",
       "A larger flag-officer corps permits one more admiral on the payroll per level.",
       {80, 180, 360}, {"fleet_command"}},

       //defenses
       {"steel", "Steel manufacturing",
       "Starts developping steel materials wich can be useful for later",
       {80, 180, 360}},

        {"microship", "Microship",
       "Microship developpement will be useful for better missiles, radar and more",
       {80, 180, 360}},


      {"ballistic", "Ballistic",
       "Starts developping ballistic materials",
       {80, 180, 360}, {"steel"}},

      {"machine_guns", "Machine Guns",
       "Machine guns are doing consistently fast but low damage to structures",
       {80, 180, 360}, {"ballistic"}},


      {"operated_autocannon", "Remotely operated autocannon",
       "Autocannon are self managed and will be doing constant average damage to an ennemy",
       {80, 180, 360}, {"microship", "rocket"}},


      {"advanced_torpedos", "Mod 2 torpedo ",
       "Mod 2 torpedos are lightweight but powerful torpedos used in submarines",
       {80, 180, 360}, {"microship", "rocket"}},

       // type of ships
       // here is the order
       // Patrol vessel (owned at beginning)
       // Corvette -> Frigate  ->  Destroyer -> Cruiser
       //check later for aicraft carrier and Amphibious vessel might carries plane and helicopter
      // The rest will be able to check later

       {"cruiser", "Cruiser",
       "The largest and most heavily armed dedicated surface combatant.",
       {80, 180, 360}, {"destroyer"}},

       {"destroyer", "Destroyer",
       "A fast and powerful war ship with a formidable firepower",
       {80, 180, 360}, {"frigate"}},


        {"frigate", "Frigate",
       "A balanced escort hull and the first step on the surface combatant line.",
       {80, 180, 360}, {"microship", "corvette", "ballistic"}},

      {"corvette", "Corvette",
       "Corvette are medium and polyvalent war vessel",
       {80, 180, 360}, {"steel"}},

        //nuclear
      {"nuclear", "Nuclear Stability",
       "Nuclear stability offers you great possibility in terms of speed and weapons power",
       {80, 180, 360}, { "microship", "frigate", "ballistic", "boat_armoring3", "submarines_armoring3"}},

       {"nuclear_submarines", "Nuclear-powered submarines (SSBN)",
       "Nuclear powered submarines offers more power of movement and permits the player to shoot nuclear armed missiles",
       {80, 180, 360}, { "nuclear"}},


      {"nuclear_ships", "Nuclear-powered ships",
       "Nuclear powered ships offers more power of movement and permits the player",
       {80, 180, 360}, { "nuclear"}},

      {"nuclear_armed_missiles", "Nuclear armed missiles",
       "Nuclear armed missiles are the most powerful missile you can shoot it will cause mass destruction",
       {80, 180, 360}, { "nuclear_submarines"}},





        // rockets
       {"anti_ship_missile", "Anti-ship cruise missiles",
       "Can be fire from any structures permit the player to deal great damage to ships",
       {80, 180, 360}, {"steel", "microship", "frigate", "ballistic", "submarines_armoring1"}},

      {"torpedo_tube", "Surface torpedo Tubes",
       "Can be fire from surfaces structures used to deal great damage to submarines",
       {80, 180, 360}, {"ballistic", "microship", "corvette"}},

      {"sadral_launchers", "Sadral launchers",
       "Sadral launchers will fastly fire 6 rockets then needs time to reload",
       {80, 180, 360}, { "microship", "rocket"}},

      {"rocket", "Rocket",
       "Rockets are heavier to load but are doing much more damage to an ennemy",
       {80, 180, 360}, { "microship", "ballistic"}},
       //ships


       {"boat_armoring2", "Boat Armoring II",
       "Give better defense and resistance to critikal attack for boat",
       {80, 180, 360}, { "boat_armoring1"}},

       {"boat_armoring3", "Boat Armoring III",
       "Give better defense and resistance to critikal attack for boat",
       {80, 180, 360}, { "boat_armoring2"}},

       //submarines
      {"submarine", "Submarines ",
       "Submarine is an independant and powerful watercraft capable of operating on his own and operating under the surface of water",
       {80, 180, 360}, {"steel"}},


        {"submarines_armoring1", "Submarines Armoring I",
       "Give better defense and resistance to critikal attack for submarines",
       {80, 180, 360}, {"submarine"}},

       {"submarines_armoring2", "Submarines Armoring II",
       "Give better defense and resistance to critikal attack for submarines",
       {80, 180, 360}, { "submarines_armoring1"}},

       {"submarines_armoring3", "Submarines Armoring III",
       "Give better defense and resistance to critikal attack for submarines",
       {80, 180, 360}, { "submarines_armoring2"}},

       //Radar ...
       {"electromagnetic_wave", "Electromagnetic Wave",
       "Unlock electromagnetic wave as a technology",
       {80, 180, 360}, {"microship"}},

       {"jammer", "Stream Jammer",
       "Unlock stream jammer as a port module",
       {80, 180, 360}, {"electromagnetic_wave", "microship"}},


       {"radar", "Radar",
       "Unlock Radar",
       {80, 180, 360}, {"electromagnetic_wave", "microship"}},

       {"port_radar", "Port Radar",
       "Port will detect any for further away",
       {80, 180, 360}, {"radar"}},

       {"submarine_radar", "Submarine radar",
       "Submarine will detect any for further away",
       {80, 180, 360}, {"radar"}},

       {"ship_radar", "Ship Radar",
       "Ships will detect any for further away",
       {80, 180, 360}, {"radar"}},

       {"control_tower", "Control Tower",
       "Control tower detect ennemy ships from far away",
       {80, 180, 360}, {"radar", "port_radar"}},


      {"airport", "Airport",
       "Unlock airport as port module.",
       {80, 180, 360}, {"steel", "control_tower"}},

      {"aircraft", "Aircraft",
       "Unlock aircraft carrier, Aircraft carrier mostly defend themselves with aircraft.",
       {80, 180, 360}, { "airport"}},
  };
  return table;
}

int techLevel(const Research& research, const std::string& id) {
  int raw = 0;
  if (id == "cranes") raw = research.cranes;
  else if (id == "dredging") raw = research.dredging;
  else if (id == "security") raw = research.security;
  else if (id == "warehouses") raw = research.warehouses;
  else {
    const auto found = research.levels.find(id);
    raw = found == research.levels.end() ? 0 : found->second;
  }

  return std::min(kMaxTechLevel, std::max(0, raw));
}

void setTechLevel(Research& research, const std::string& id, int level) {
  const int clamped = std::min(kMaxTechLevel, std::max(0, level));
  if (id == "cranes") research.cranes = clamped;
  else if (id == "dredging") research.dredging = clamped;
  else if (id == "security") research.security = clamped;
  else if (id == "warehouses") research.warehouses = clamped;
  else research.levels[id] = clamped;
}

bool techAcquired(const Research& research, const std::string& id) {
  return techLevel(research, id) > 0;
}

bool techUnlocked(const Research& research, const std::string& id) {
  const auto found = std::find_if(technologies().begin(), technologies().end(),
                                  [&](const Technology& t) { return t.id == id; });
  if (found == technologies().end()) return false;
  for (const std::string& parent : found->parentTechRequired) {
    if (!techAcquired(research, parent)) return false;
  }
  return true;
}

std::vector<std::string> missingParents(const Research& research, const std::string& id) {
  std::vector<std::string> missing;
  const auto found = std::find_if(technologies().begin(), technologies().end(),
                                  [&](const Technology& t) { return t.id == id; });
  if (found == technologies().end()) return missing;
  for (const std::string& parent : found->parentTechRequired) {
    if (!techAcquired(research, parent)) missing.push_back(parent);
  }
  return missing;
}

int nextLevelCost(const Research& research, const std::string& id) {
  const auto found = std::find_if(technologies().begin(), technologies().end(),
                                  [&](const Technology& t) { return t.id == id; });
  if (found == technologies().end()) return -1;

  const int level = techLevel(research, id);
  return level < kMaxTechLevel ? found->costs[static_cast<size_t>(level)] : -1;
}

int investedPoints(const Research& research) {
  int total = 0;
  for (const Technology& technology : technologies()) {
    const int level = techLevel(research, technology.id);
    for (int i = 0; i < level; ++i) total += technology.costs[static_cast<size_t>(i)];
  }
  return total;
}

std::string techEffectText(const std::string& id, int level) {
  if (id == "cranes") {
    return level == 0 ? "Not installed - every voyage runs at full length."
                      : "Voyages complete " + std::to_string(10 * level) + "% faster.";
  }
  if (id == "warehouses") {
    return level == 0 ? "Not installed - freight pays base rates."
                      : "Freight pays " + std::to_string(12 * level) + "% more on every delivery.";
  }
  if (id == "security") {
    if (level == 0) return "No protection - 15% of runs lose a quarter of the cargo.";
    const double risk = std::max(0.0, 15.0 - 5.0 * level);
    if (risk == 0.0) return "Cargo losses are impossible.";
    return "Loss risk down to " + std::to_string(static_cast<int>(risk)) + "%.";
  }
  if (id == "dredging") {
    if (level == 0) return "Not dredged - berths handle their rated hull class only.";
    return "Every berth handles hulls " + std::to_string(level) + " class" +
           (level > 1 ? "es" : "") + " larger.";
  }
  if (id == "fleet_command") {
    return level == 0 ? "No permanent staff - the navy may keep 2 fleets."
                      : "The navy may keep " + std::to_string(2 + level) + " fleets.";
  }
  if (id == "fleet_organization") {
    return level == 0 ? "Squadrons carry their base 4 ships."
                      : "Every fleet may carry " + std::to_string(4 + 2 * level) + " ships.";
  }
  if (id == "admiral_corps") {
    return level == 0 ? "2 admirals may be kept on the payroll."
                      : std::to_string(2 + level) + " admirals may be kept on the payroll.";
  }
  return "";
}

double voyageDurationMultiplier(const Research& research) {
  return 1.0 - 0.1 * techLevel(research, "cranes");
}

double rewardMultiplier(const Research& research) {
  return 1.0 + 0.12 * techLevel(research, "warehouses");
}

double lossRiskPercent(const Research& research) {
  return std::max(0.0, 15.0 - 5.0 * techLevel(research, "security"));
}

int researchPointsForVoyage(double distanceKm) {
  const int km = std::isfinite(distanceKm) && distanceKm > 0
                     ? static_cast<int>(std::floor(distanceKm))
                     : 0;
  if (km <= 0) return 0;

  const int bySquareRoot = static_cast<int>(std::floor(std::sqrt(km / 25.0)));
  const int byDistance = static_cast<int>(std::llround(km / 250.0));

  return std::min(30, std::max(bySquareRoot, byDistance));
}

char vesselClass(double length) {
  if (!std::isfinite(length) || length <= 0) return '\0';
  if (length < 91) return 'S';
  if (length < 152) return 'M';
  return 'L';
}

char berthClass(const Port& port, const Research& research) {
  const std::string base = portCapacityClass(port);
  if (base.empty()) return '\0';

  const int rank = std::min(kCapacityClassCount, capacityRank(base) + 1 + techLevel(research, "dredging"));
  return kCapacityClasses[static_cast<size_t>(rank - 1)][0];
}

bool canBerth(const Port& port, double length, const Research& research) {
  const char hull = vesselClass(length);
  if (hull == '\0') return true;

  const char berth = berthClass(port, research);
  if (berth == '\0') return false;

  const int berthRank = capacityRank(std::string(1, berth));
  const int hullRank = capacityRank(std::string(1, hull));

  // A hull may use a berth one class smaller than itself.
  return berthRank >= hullRank - 1;
}

std::string harborSizeLabel(const std::string& band) {
  if (band == "L") return "Large";
  if (band == "M") return "Medium";
  if (band == "S") return "Small";
  if (band == "V") return "Very Small";
  return "Unknown";
}

}  // namespace sea

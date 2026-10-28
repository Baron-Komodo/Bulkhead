// Headless tests for the ported core logic. Mirrors the assertions the web game's Jest
// suite makes about the same maths.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cargo.h"
#include "data.h"
#include "game.h"
#include "json.hpp"
#include "geo.h"
#include "research.h"
#include "sea_route.h"
#include "voyage.h"

using namespace sea;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::cout << "  FAIL: " << label << "\n";
  }
}

void checkNear(double actual, double expected, double tolerance, const std::string& label) {
  check(std::fabs(actual - expected) <= tolerance,
        label + " (got " + std::to_string(actual) + ", want " + std::to_string(expected) + ")");
}

const Port* findPort(const Catalog& catalog, const std::string& name) {
  for (const Port& port : catalog.ports()) {
    if (port.name == name && port.hasPosition()) return &port;
  }
  return nullptr;
}

/// Walks every leg of a route and counts the sample points that fall on land, ignoring the
/// first and last stretch (the hop between a port and the nearest navigable cell, which may
/// cross a coast by design). Ported from the web game's seaRoute.test.js.
int landCrossings(const SeaRouter& router, const std::vector<LatLng>& points,
                  double toleranceDegrees = 2.25) {
  if (points.size() < 2) return 0;

  const LatLng start = points.front();
  const LatLng end = points.back();
  int offenders = 0;

  for (size_t index = 1; index < points.size(); ++index) {
    const LatLng from = points[index - 1];
    const LatLng to = points[index];
    double dLng = to.lng - from.lng;
    if (dLng > 180.0) dLng -= 360.0;
    if (dLng < -180.0) dLng += 360.0;

    const double distance = haversineKm(from.lat, from.lng, to.lat, from.lng + dLng);
    const int steps = std::max(1, static_cast<int>(std::lround(distance / 15.0)));

    for (int step = 0; step <= steps; ++step) {
      const double ratio = static_cast<double>(step) / steps;
      const double latitude = from.lat + (to.lat - from.lat) * ratio;
      const double longitude = from.lng + dLng * ratio;

      const bool nearStart = index == 1 && std::hypot(latitude - start.lat, longitude - start.lng) <=
                                             toleranceDegrees;
      const bool nearEnd = index == points.size() - 1 &&
                           std::hypot(latitude - end.lat, longitude - end.lng) <= toleranceDegrees;
      if (nearStart || nearEnd) continue;

      if (router.isLandAt(latitude, longitude)) ++offenders;
    }
  }

  return offenders;
}

}  // namespace

int main() {
  // -----------------------------------------------------------------------
  std::cout << "geo\n";
  checkNear(parseCoordinate("43,866667"), 43.866667, 1e-6, "European decimal comma");
  checkNear(parseCoordinate("146.833333"), 146.833333, 1e-6, "Anglo decimal point");
  check(std::isnan(parseCoordinate("")), "empty coordinate is NaN");
  check(std::isnan(parseCoordinate("north")), "garbage coordinate is NaN");

  // London to Paris is about 344 km.
  checkNear(haversineKm(51.5074, -0.1278, 48.8566, 2.3522), 343.5, 3.0, "haversine London-Paris");

  checkNear(voyageDurationMs(0), 30000, 1, "duration floor");
  checkNear(voyageDurationMs(12000), 75000, 1, "duration at full distance");
  checkNear(voyageDurationMs(100000), 75000, 1, "duration ceiling");
  checkNear(voyageDurationMs(12000, 0.5), 37500, 1, "cranes halve the curve");
  check(formatDuration(75000) == "1:15", "format 75s");
  check(formatDuration(5000) == "0:05", "format 5s");

  // -----------------------------------------------------------------------
  std::cout << "voyage economy\n";
  checkNear(hullPrice(0), 250, 1e-9, "hull price floor");
  checkNear(hullPrice(499167), 4992, 1e-9, "PRELUDE hull price");
  checkNear(cargoBonus(10000), 100, 1e-9, "cargo bonus sqrt");
  checkNear(cargoBonus(1000000000), 400, 1e-9, "cargo bonus cap");
  checkNear(longHaulMultiplier(5000), 1.0, 1e-9, "no premium below full distance");
  checkNear(longHaulMultiplier(32000), 1.25, 1e-9, "premium saturates at 25%");

  Research none;
  checkNear(lossRiskPercent(none), 15.0, 1e-9, "base loss risk");
  Research guarded;
  guarded.security = 3;
  checkNear(lossRiskPercent(guarded), 0.0, 1e-9, "max security removes risk");
  checkNear(rewardMultiplier(none), 1.0, 1e-9, "base reward multiplier");
  Research warehouses;
  warehouses.warehouses = 3;
  checkNear(rewardMultiplier(warehouses), 1.36, 1e-9, "warehouse reward multiplier");
  check(researchPointsForVoyage(0) == 0, "no points for no sailing");
  check(researchPointsForVoyage(12000) == 30, "points cap");

  const std::string rtId = "cranes";
  check(nextLevelCost(none, rtId) == 60, "first crane level costs 60");
  check(nextLevelCost(none, "nope") == -1, "unknown technology");
  Research maxed;
  maxed.cranes = 3;
  check(nextLevelCost(maxed, rtId) == -1, "fully researched has no next cost");

  // A voyage settles: freight from distance, bonus for real trade.
  const VoyageOutcome outcome = resolveVoyage(10000, 1000, none, 100.0, true);
  checkNear(outcome.freight, 700, 1e-9, "freight 1000km + sqrt(10000)");
  checkNear(outcome.bonus, 35, 1e-9, "5% reality bonus");
  checkNear(outcome.reward, 735, 1e-9, "reward after bonus");
  check(!outcome.lost, "no loss on a roll of 100");

  // -----------------------------------------------------------------------
  std::cout << "hull classes and berths\n";
  check(vesselClass(50) == 'S', "small hull");
  check(vesselClass(120) == 'M', "medium hull");
  check(vesselClass(400) == 'L', "large hull");
  Port port;
  port.harborSize = "M";
  check(berthClass(port, none) == 'M', "berth band");
  check(canBerth(port, 120, none), "medium hull fits medium berth");
  // A hull may use a berth one class smaller than itself, so a large hull still fits M.
  check(canBerth(port, 400, none), "large hull may use a medium berth");
  Port tiny;
  tiny.harborSize = "V";
  check(!canBerth(tiny, 400, none), "large hull does not fit a very small berth");
  Research dredged;
  dredged.dredging = 3;
  check(berthClass(port, dredged) == 'L', "dredging deepens a berth");
  check(canBerth(port, 400, dredged), "dredged berth takes a large hull");

  // -----------------------------------------------------------------------
  std::cout << "catalog\n";
  Catalog catalog;
  check(catalog.loadPorts(resolveDataFile("ports.json")), "ports load");
  check(catalog.loadVessels(resolveDataFile("vessels.json")), "vessels load");
  check(catalog.loadTrade(resolveDataFile("trade_summary.json")), "trade loads");
  check(catalog.ports().size() == 3630, "3630 ports");
  check(catalog.vessels().size() == 201, "201 vessels");
  check(catalog.vesselById(kStarterVesselId) != nullptr, "starter vessel exists");
  check(catalog.loadNavalShips(resolveDataFile("military_vessels.json")), "naval ships load");
  check(catalog.navalVessels().size() == 605, "605 naval classes");
  bool navalHasNames = true;
  for (const NavalVessel& naval : catalog.navalVessels()) {
    if (naval.shipName.empty()) navalHasNames = false;
  }
  check(navalHasNames, "every naval class has a name");

  check(catalog.loadCountryCodes(resolveDataFile("country_codes_V202601.json")),
        "country codes load");
  check(catalog.countryNameFor("nl") == "Netherlands", "iso2 resolves to a country name");
  check(catalog.countryNameFor("ZZ").empty(), "unknown iso2 resolves to nothing");

  const std::vector<CargoOption> goods = cargoOptions(catalog, "NL", "JP");
  check(!goods.empty(), "Netherlands exports something");
  bool anyReal = realTradeCount(goods) >= 0;
  check(anyReal, "real trade count is computable");
  check(std::is_sorted(goods.begin(), goods.end(), [](const CargoOption& a, const CargoOption& b) {
          if (a.realTrade != b.realTrade) return a.realTrade;
          return a.valueK > b.valueK;
        }),
        "cargo sorted real-trade first");

  // -----------------------------------------------------------------------
  std::cout << "sea routing\n";
  SeaRouter router;
  check(router.loadGrid(resolveDataFile("sea-grid.json")), "sea grid loads");

  // Golden values produced by running the web game's own seaRoute.js over the same grid
  // and port data. The C++ search must reproduce them: this is the fidelity check that
  // matters for a port.
  struct GoldenCase {
    int fromId;
    int toId;
    double distanceKm;
  };
  const GoldenCase golden[] = {
      {882, 3285, 14403.7},   // Rotterdam - Tokyo, via the Northwest Passage
      {1043, 290, 107.0},     // Balboa - Cristobal
      {2939, 23, 1417.4},     // Port Said - Jiddah
      {2294, 1558, 3172.5},   // Genova - Odesa
      {2026, 1564, 3162.1},   // Basrah - Mumbai
      {3482, 3068, 243.4},    // Seattle - Vancouver
      {51, 1080, 1061.4},     // Chicago - Detroit
      {882, 1070, 10657.1},   // Rotterdam - Santos
      {882, 1564, 12415.6},   // Rotterdam - Mumbai
      {1558, 882, 6752.5},    // Odesa - Rotterdam
      {1543, 1532, 7554.6},   // Auckland - Honolulu, across the antimeridian
      {1504, 1047, 1552.9},   // Toronto - Duluth, the Great Lakes
      {2214, 882, 14590.0},   // Ushuaia - Rotterdam
      {882, 3324, 12336.2},   // Rotterdam - Buenos Aires
  };

  for (const GoldenCase& testCase : golden) {
    const Port* from = catalog.portById(testCase.fromId);
    const Port* to = catalog.portById(testCase.toId);
    if (from == nullptr || to == nullptr) {
      check(false, "golden ports " + std::to_string(testCase.fromId) + "/" +
                       std::to_string(testCase.toId) + " present");
      continue;
    }

    const RouteResult route = router.route(from->position, to->position);
    const std::string label = from->name + " -> " + to->name;
    check(route.ok, label + " is a sea route");
    checkNear(route.distanceKm, testCase.distanceKm, 1.0, label + " distance matches the JS reference");
    check(route.points.size() >= 2, label + " has points");
    checkNear(route.points.front().lat, from->position.lat, 1e-6, label + " starts at the port");
    checkNear(route.points.back().lng, to->position.lng, 1e-6, label + " ends at the port");
  }

  // The course must be water the whole way, not just between the ports. This is the same
  // check the web game's own test suite makes.
  const int waterPairs[][2] = {{882, 3285}, {882, 1070}, {2294, 1558}, {2026, 1564},
                               {3482, 3068}, {51, 1080}, {2214, 882}, {1504, 1047}};
  for (const auto& pair : waterPairs) {
    // A trimmed port extract may no longer carry one of these berths; that is a data failure,
    // not a reason to dereference a null port and take the whole suite down with it.
    const Port* from = catalog.portById(pair[0]);
    const Port* to = catalog.portById(pair[1]);
    if (from == nullptr || to == nullptr) {
      check(false, std::string("water route ports ") + std::to_string(pair[0]) + "/" +
                       std::to_string(pair[1]) + " present");
      continue;
    }
    const RouteResult route = router.route(from->position, to->position);
    const int crossings = landCrossings(router, route.points);
    check(crossings == 0, std::string("route ") + std::to_string(pair[0]) + "-" +
                              std::to_string(pair[1]) + " stays on water (" +
                              std::to_string(crossings) + " land points)");
  }

  // Rotterdam to Tokyo runs the Canadian Arctic, never the frozen central basin.
  {
    const RouteResult route =
        router.route(catalog.portById(882)->position, catalog.portById(3285)->position);
    double maxLat = -90.0;
    for (const LatLng& point : route.points) maxLat = std::max(maxLat, point.lat);
    check(maxLat > 70.0 && maxLat <= 75.0, "Rotterdam-Tokyo runs the Northwest Passage");
  }

  // A river port with no water within the snap radius has no sea route, and the field
  // says so rather than guessing.
  {
    const std::shared_ptr<const SeaField> field =
        router.field(catalog.portById(882)->position); // Rotterdam
    double distance = 0.0;
    check(field != nullptr, "field for Rotterdam");
    check(!field->distanceKmTo(catalog.portById(1057)->position, distance),
          "Manaus has no sea route on this grid");
    check(!field->distanceKmTo({15.0, 20.0}, distance), "the Sahara has no sea route");
    check(field->distanceKmTo(catalog.portById(3285)->position, distance),
          "the field prices Tokyo");
    checkNear(distance, 14403.7, 1.0, "field distance agrees with the route");
  }

  // -----------------------------------------------------------------------
  std::cout << "fire control\n";
  {
    // A battery built by hand so the rules are exercised without a catalog row: a fast light
    // gun, a slow heavy missile, and a torpedo that is the only mount able to hurt a
    // submarine.
    auto mount = [](const char* type, FireCategory category, float damage, float reload,
                    float ammo, float radius, int cost, bool vsShips, bool vsSub) {
      Armament weapon;
      weapon.name = type;
      weapon.type = type;
      weapon.category = category;
      weapon.damage = damage;
      weapon.reload_time = reload;
      weapon.ammo = ammo;
      weapon.radius = radius;
      weapon.cost = cost;
      weapon.is_efficient_against_ships = vsShips;
      weapon.is_efficient_against_submarine = vsSub;
      return weapon;
    };

    const Armament machineGun =
        mount("machine gun", FireCategory::light, 10.0f, 0.2f, 1000.0f, 3.0f, 5, true, false);
    const Armament missile =
        mount("missile", FireCategory::heavy, 500.0f, 20.0f, 8.0f, 90.0f, 900, true, false);
    const Armament torpedo =
        mount("torpedo", FireCategory::heavy, 300.0f, 14.0f, 12.0f, 30.0f, 400, false, true);
    const std::vector<Armament> battery = {machineGun, missile, torpedo};

    // A round has to be paid for before it is fired, and a free mount always can be.
    Armament free = machineGun;
    free.cost = 0;
    check(roundsAffordable(free, -10.0), "a free mount fires with an empty treasury");
    check(!roundsAffordable(missile, 899.0), "a mount the treasury cannot pay for is refused");
    check(roundsAffordable(missile, 900.0), "a mount the treasury can pay for is allowed");

    // Auto works the whole battery against a surface hull, fastest killer first. Every mount
    // stays in the plan, so one on reload never silences the rest.
    const std::vector<size_t> surface =
        firingPlanIndices(battery, kFireAuto, 4000.0f, false, 800.0f);
    check(surface.size() == battery.size(), "Auto keeps every mount against a surface hull");
    check(!surface.empty() && surface.front() == 0, "Auto leads with the fastest killer");
    check(std::find(surface.begin(), surface.end(), 1) != surface.end(),
          "Auto keeps the missile in the plan behind the gun");

    // Against a submarine the torpedo leads, but the guns stay in the plan as a last resort
    // instead of the hull falling silent the moment the tubes are reloading.
    const std::vector<size_t> submerged =
        firingPlanIndices(battery, kFireAuto, 4000.0f, true, 800.0f);
    check(!submerged.empty() && submerged.front() == 2, "Auto leads with the ASW mount");
    check(submerged.size() == battery.size(), "Auto keeps the guns for a submarine target");

    // A named order is narrower: only lighter mounts, and only those that can hurt the target.
    const std::vector<size_t> light =
        firingPlanIndices(battery, kFireLight, 4000.0f, false, 800.0f);
    check(light.size() == 1 && light.front() == 0, "a Light order keeps only the light gun");
    const std::vector<size_t> lightSub =
        firingPlanIndices(battery, kFireLight, 4000.0f, true, 800.0f);
    check(lightSub.empty(), "a Light order does not fire a gun at a submarine");

    // Reach is read from the mounts the order allows, so an Auto hull engages at the range of
    // its longest weapon.
    checkNear(engagementRangeKm(battery, kFireAuto), weaponRangeKm(missile), 1e-3,
              "Auto reaches as far as the longest mount");
    checkNear(engagementRangeKm(battery, kFireLight), weaponRangeKm(machineGun), 1e-3,
              "a Light order reaches only as far as the light mounts");
  }

  // -----------------------------------------------------------------------
  std::cout << "fire rate\n";
  {
    // The catalog gives every gun the same load time, so a mount is told apart by its own text:
    // the bore written in the name or description, and the kind of mount it is.
    auto piece = [](const char* name, const char* type, float reload, float damage,
                    float ammo) {
      Armament weapon;
      weapon.name = name;
      weapon.description = name;
      weapon.type = type;
      weapon.category = FireCategory::medium;
      weapon.damage = damage;
      weapon.reload_time = reload;
      weapon.ammo = ammo;
      weapon.radius = 12.0f;
      weapon.is_efficient_against_ships = true;
      return weapon;
    };

    const Armament heavyMg = piece("cal/ 12.7mm MG", "machine gun", 0.2f, 10.0f, 1000.0f);
    const Armament thirty = piece("MSI 30mm DS30M", "gun", 5.0f, 60.0f, 400.0f);
    const Armament seventySix = piece("Otobreda 76 mm", "gun", 5.0f, 60.0f, 400.0f);
    const Armament fiveInch = piece("5\"/54 Mk 45", "gun", 5.0f, 60.0f, 6400.0f);
    const Armament calibreLost = piece("mm AK-176 gun", "gun", 5.0f, 60.0f, 400.0f);
    const Armament tube = piece("533 mm torpedo", "torpedo", 30.0f, 150.0f, 16.0f);

    checkNear(weaponCalibreMm(heavyMg), 12.7, 1e-3, "a 12.7 mm mount reads its own bore");
    checkNear(weaponCalibreMm(thirty), 30.0, 1e-3, "a 30mm mount reads its own bore");
    checkNear(weaponCalibreMm(fiveInch), 127.0, 1e-3, "a 5-inch mount reads as 127 mm");
    checkNear(weaponCalibreMm(tube), 533.0, 1e-3, "a torpedo tube reads its own bore");
    // A model number is not a calibre: "AK-176" is a 76 mm gun whose name lost the bore, and
    // guessing from "176" would make it a destroyer's main battery.
    checkNear(weaponCalibreMm(calibreLost), 0.0, 1e-9,
              "a name with the calibre lost reads as no bore");

    // Bore for bore, the small mount puts out more rounds than the big one, and each mount
    // keeps its own cadence instead of sharing the type's single load time.
    const double mgRate = fireRateRoundsPerMinute(heavyMg);
    const double thirtyRate = fireRateRoundsPerMinute(thirty);
    const double seventySixRate = fireRateRoundsPerMinute(seventySix);
    const double fiveInchRate = fireRateRoundsPerMinute(fiveInch);
    check(thirtyRate > seventySixRate && seventySixRate > fiveInchRate,
          "a smaller bore fires faster than a bigger one");
    check(mgRate > thirtyRate, "a machine-gun mount outpaces the cannon beside it");
    check(fiveInchRate < 12.0 * 1.5,
          "a 5-inch turret does not inherit the catalog's load time for every gun");
    checkNear(mountReloadSeconds(thirty) * thirtyRate, 60.0, 1e-3,
              "the reload is the reciprocal of the rate");
    check(mountReloadSeconds(tube) > mountReloadSeconds(thirty),
          "a torpedo tube cycles slower than a gun mount");

    // A mount the rule cannot place keeps the tempo the data gave it, rather than going silent
    // or turning into a machine gun.
    checkNear(fireRateRoundsPerMinute(calibreLost), 60.0 / 5.0 * kFireRateScale, 1e-3,
              "a calibre-less gun keeps the catalog tempo");
    // A launcher is rated by its hoist, not by a bore, so the catalog's own figure stands.
    checkNear(fireRateRoundsPerMinute(piece("Harpoon missiles", "missile", 60.0f, 250.0f, 8.0f)),
              1.0, 1e-6, "a missile launcher keeps its one round a minute");
    checkNear(fireRateRoundsPerMinute(tube), 2.0, 1e-6,
              "a torpedo tube keeps the catalog's reload");

    // `reload_time` is the time to change a magazine, not the time between two rounds, so it
    // must not move the mount's cadence. The magazine change only shows up once the magazine
    // is dry, which is what timeToKill prices in for a target that needs several magazines.
    Armament slowMagazine = thirty;
    slowMagazine.reload_time = 30.0f;
    checkNear(fireRateRoundsPerMinute(slowMagazine), thirtyRate, 1e-6,
              "a longer magazine change does not slow the gun");
    Armament smallMagazine = thirty;
    smallMagazine.ammo = 10.0f;
    smallMagazine.reload_time = 5.0f;
    const float roundsToKill = shotsToKill(smallMagazine, 4000.0f, 800.0f);
    check(roundsToKill > smallMagazine.ammo, "the target needs more than one magazine");
    checkNear(timeToKill(smallMagazine, 4000.0f, 800.0f),
              roundsToKill * mountReloadSeconds(smallMagazine) + smallMagazine.reload_time,
              1e-3, "a dry magazine adds one magazine change to the kill time");

    // Auto works the plan by time to kill, and that now follows the mount's own cadence: the
    // fast light mount still speaks before the slow heavy one on the same hull.
    const std::vector<Armament> sameBore = {fiveInch, thirty};
    const std::vector<size_t> plan =
        firingPlanIndices(sameBore, kFireAuto, 4000.0f, false, 800.0f);
    check(!plan.empty() && plan.front() == 1, "Auto leads with the faster mount");

    // Every mount the game can actually fit gets a workable rate, and the catalog's thousands
    // of mounts do not all come out of the rule with the same one.
    std::vector<int> rates;
    bool everyMountRated = true;
    for (const NavalVessel& vessel : catalog.navalVessels()) {
      for (const Armament& weapon : vessel.armament) {
        const float rpm = fireRateRoundsPerMinute(weapon);
        const float reload = mountReloadSeconds(weapon);
        if (!(rpm > 0.0f) || !(reload > 0.0f) || reload > 120.0f) everyMountRated = false;
        const int rounded = static_cast<int>(std::lround(rpm));
        if (std::find(rates.begin(), rates.end(), rounded) == rates.end()) rates.push_back(rounded);
      }
    }
    check(everyMountRated, "every mount in the catalog has a usable rate");
    check(rates.size() >= 8, "the catalog's mounts do not all share one rate");
  }

  // -----------------------------------------------------------------------
  std::cout << "game state\n";
  GameState game;
  game.attach(&catalog, &router);
  game.newGame();
  checkNear(game.money(), 5000, 1e-9, "base money");
  check(game.vesselOwned(kStarterVesselId), "starter boat granted");
  // A new game hands the player one harbour, so the shipyard, the navigation panel and the
  // treasury have somewhere to work from before anything is taken by force.
  check(game.portOwned(kStarterPortId), "starter port granted");
  check(game.portDefeated(kStarterPortId) == false, "the granted port is not read as conquered");
  checkNear(game.passiveIncomePerSecond(), kPassiveIncomePerPortPerSecond, 1e-9,
            "the granted port pays the per-port rate");

  std::string error;
  int smallPort = 0;
  for (const Port& port : catalog.ports()) {
    if (port.harborSize == "V" && port.hasPosition()) {
      smallPort = port.id;
      break;
    }
  }
  check(smallPort != 0, "found a small port");

  // A neutral harbour is not bought off the map: the money buys it, but only once its battery
  // has been broken. stormPort is the scripted shortcut - break the defences, then pay - which
  // is what the captures and these checks use; a player bombards it with a squadron instead.
  const double before = game.money();
  check(!game.claimPort(smallPort, error), "a harbour that is still fighting cannot be bought");
  check(game.money() == before, "a refused purchase costs nothing");
  check(game.stormPort(smallPort, error), "take a small port by storm");
  checkNear(game.money(), before - portCost(*catalog.portById(smallPort)), 1e-9, "port cost deducted");
  check(game.portOwned(smallPort), "port now owned");
  checkNear(game.passiveIncomePerSecond(), 2 * kPassiveIncomePerPortPerSecond, 1e-9,
            "two ports pay twice the per-port rate");
  // The rate follows the port count alone: a deep treasury must not change it.
  game.changeMoney(1000000.0);
  checkNear(game.passiveIncomePerSecond(), 2 * kPassiveIncomePerPortPerSecond, 1e-9,
            "passive income ignores the treasury");
  game.changeMoney(-1000000.0);

  // The biggest hull in the catalog (PRELUDE, 499,167 GT) costs 4,992, which is more than
  // the bank left after claiming a port.
  check(!game.buildVessel(1, error), "cannot afford the biggest hull");
  check(game.vesselOwned(1) == false, "failed purchase adds nothing");

  // -----------------------------------------------------------------------
  std::cout << "port upgrades\n";
  const double baseCost = portCost(*catalog.portById(smallPort));
  check(game.portLevel(smallPort) == 1, "a fresh port is level 1");
  check(game.portSlotCount(smallPort) == 2, "level 1 grants two module slots");
  check(game.portModules(smallPort).empty(), "a fresh port has no modules fitted");
  check(portSlotCountForLevel(MAX_PORT_LEVEL) == 5, "a fully upgraded port holds five modules");
  checkNear(game.portUpgradeCost(smallPort), 2 * baseCost, 1e-9,
            "the first upgrade costs twice the claim price");

  const double ownedMoney = game.money();
  check(game.upgradePort(smallPort, error), "upgrade the port to level 2");
  checkNear(game.money(), ownedMoney - 2 * baseCost, 1e-9, "the upgrade price is deducted");
  check(game.portLevel(smallPort) == 2, "the upgrade raises the level");
  check(game.portSlotCount(smallPort) == 3, "the upgrade adds a module slot");
  checkNear(game.portUpgradeCost(smallPort), 4 * baseCost, 1e-9, "each upgrade doubles the price");

  check(game.installPortModule(smallPort, 1, 600, error), "fit a module in an owned port");
  check(game.portHasModule(smallPort, 1), "the module reads back as fitted");
  check(!game.installPortModule(smallPort, 1, 600, error), "the same module cannot be fitted twice");

  check(game.upgradePort(smallPort, error), "upgrade the port to level 3");
  check(game.portLevel(smallPort) == 3, "level 3 reached");
  check(game.portUpgradeCost(smallPort) > game.money(), "the last upgrade is out of reach");
  check(!game.upgradePort(smallPort, error), "an unaffordable upgrade is refused");
  check(game.portLevel(smallPort) == 3, "a refused upgrade changes nothing");

  GameState fresh;
  fresh.attach(&catalog, &router);
  fresh.newGame();
  check(!fresh.upgradePort(smallPort, error), "an unowned port cannot be upgraded");
  check(!fresh.installPortModule(smallPort, 1, 600, error),
        "a module cannot be fitted in an unowned port");

  // The enemy powers: a new game gives each of them a real harbour in its own waters, a
  // treasury and an opening hull laid down from the catalogue. Nothing about a power comes
  // from a spawn table - what it has on the water is what it could pay for.
  check(fresh.factions().size() >= 2, "a new game seeds the enemy powers");
  // Both kinds of hostile share the hull list: the powers' warships and the pirate sentries.
  // A pirate is a preset hull holding a station - no class, no yard - so only an enemy power's
  // hull has to name a catalogue class.
  bool everyPowerHasAHome = true;
  bool everyHullIsOwned = true;
  bool everyHullIsClassed = true;
  bool everyPirateIsStationed = true;
  size_t pirateHulls = 0;
  // One slot per NavalModelKind, so indexing this array by a hull kind never runs off its end.
  int kindCounts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  for (const BotBoat& boat : fresh.botBoats()) {
    if (boat.owner.empty() || boat.name.empty()) everyHullIsOwned = false;
    kindCounts[static_cast<int>(boat.kind)] += 1;
    if (boat.pirate) {
      ++pirateHulls;
      if (haversineKm(boat.position, boat.station) > 1.0 || boat.classId != 0 ||
          boat.homePortId != 0) {
        everyPirateIsStationed = false;
      }
      continue;
    }
    if (boat.classId == 0 || catalog.navalVesselById(boat.classId) == nullptr) {
      everyHullIsClassed = false;
    }
  }
  for (size_t index = 0; index < fresh.factions().size(); ++index) {
    const int home = fresh.factionHomePort(static_cast<int>(index));
    if (home == 0 || home == kStarterPortId) everyPowerHasAHome = false;
    if (fresh.factionNameForPort(home) != fresh.factions()[index].name) {
      everyPowerHasAHome = false;
    }
  }
  check(everyPowerHasAHome, "every power holds the harbour nearest its own waters");
  check(everyHullIsOwned, "every hostile hull names its owner and its own name");
  check(everyHullIsClassed, "every enemy hull was laid down from a catalogue class");
  check(pirateHulls > 0 && everyPirateIsStationed,
        "a pirate hull holds its station with no class and no yard");
  check(fresh.botBoats().size() >= fresh.factions().size(),
        "every power starts with something afloat");
  check(fresh.factionOwningPort(kStarterPortId) < 0, "the player's free port is not a power's");

  // -----------------------------------------------------------------------
  std::cout << "\nnaval units\n";

  // A shipyard fitting and a hull are both purchases, so give the treasury some room. The
  // checks above only looked at the money before this point.
  game.changeMoney(100000.0);

  // A hull can only be laid down at a port you own that has a shipyard fitted.
  check(!game.buildNavalUnit(1, smallPort, 1000.0, error),
        "a port without a shipyard lays down nothing");
  check(game.installPortModule(smallPort, 2, 200, error), "fit the shipyard module");
  check(!game.buildNavalUnit(999999, smallPort, 1000.0, error), "an unknown class is refused");
  check(game.buildNavalUnit(1, smallPort, 1000.0, error), "lay down a class at the yard");
  check(game.navalUnitCount() == 1, "the new hull joins the naval force");

  // Posture is a standing order: a fresh hull is offensive, and the setter applies the value
  // to every listed unit.
  check(game.navalUnits().front().posture == kPostureOffensive, "a fresh hull is offensive");
  game.setPosture({game.navalUnits().front().id}, kPostureAggressive);
  check(game.navalUnits().front().posture == kPostureAggressive, "posture is set on the hull");
  game.setPosture({game.navalUnits().front().id}, kPostureDefensive);
  check(game.navalUnits().front().posture == kPostureDefensive, "posture can be changed");

  const Port* yard = catalog.portById(smallPort);
  const NavalUnit* unit = game.navalUnitById(game.navalUnits().front().id);
  check(unit != nullptr, "the built hull reads back by id");
  if (unit != nullptr && yard != nullptr) {
    check(haversineKm(unit->position, yard->position) < 60.0, "the hull is berthed at the yard");
    check(!unit->moving(), "a fresh hull is not under way");
  }

  // The sprite kind and the price are derived from the class's type.
  const NavalVessel* subClass = nullptr;
  const NavalVessel* patrolClass = nullptr;
  for (const NavalVessel& vessel : catalog.navalVessels()) {
    const std::string type = [&] {
      std::string lowered = vessel.type;
      std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return lowered;
    }();
    if (subClass == nullptr && type.find("submarine") != std::string::npos) subClass = &vessel;
    if (patrolClass == nullptr && type.find("patrol") != std::string::npos) patrolClass = &vessel;
  }
  check(subClass != nullptr && navalModelKindFor(*subClass) == NavalModelKind::submarine,
        "a submarine class uses the submarine hull model");
  check(patrolClass != nullptr && navalModelKindFor(*patrolClass) == NavalModelKind::patrol,
        "a patrol craft uses the small-hull model");
  check(subClass != nullptr && patrolClass != nullptr &&
            navalUnitCost(*subClass, false) > navalUnitCost(*patrolClass, false),
        "a submarine costs more than a patrol craft");
  check(subClass != nullptr && navalUnitCost(*subClass, true) < navalUnitCost(*subClass, false),
        "the home yard discounts the hull");

  // Ordering a hull to open water gives it a course; a point on land is refused.
  LatLng openWater = yard != nullptr ? yard->position : LatLng{};
  bool foundWater = false;
  for (double span = 0.5; span <= 8.0 && !foundWater; span += 0.5) {
    const double offsets[][2] = {{span, span},  {span, -span}, {-span, span},  {-span, -span},
                                 {span, 0.0},   {0.0, span},  {-span, 0.0},  {0.0, -span}};
    for (const auto& offset : offsets) {
      const LatLng candidate{openWater.lat + offset[0], openWater.lng + offset[1]};
      if (!router.isLandAt(candidate.lat, candidate.lng)) {
        openWater = candidate;
        foundWater = true;
        break;
      }
    }
  }
  check(foundWater, "found open water near the yard");
  if (unit != nullptr && yard != nullptr && foundWater) {
    check(game.orderNavalUnit(unit->id, openWater, error), "a hull accepts an order to open water");
    unit = game.navalUnitById(game.navalUnits().front().id);
    check(unit != nullptr && unit->moving(), "the ordered hull is under way");

    const double beforeKm = haversineKm(unit->position, openWater);
    game.update(1000.0);  // one second of game time
    unit = game.navalUnitById(game.navalUnits().front().id);
    check(unit != nullptr && haversineKm(unit->position, openWater) < beforeKm,
          "the hull closes on its destination as the clock runs");

    game.stopNavalUnit(unit->id);
    check(!game.navalUnitById(unit->id)->moving(), "hold position clears the course");
  }

  // A crossing that would run over land falls back to the sea route: a Mediterranean yard to
  // a Pacific berth is far longer than the straight line and comes back as a long polyline.
  const Port* medPort = nullptr;
  const Port* pacificPort = nullptr;
  for (const Port& port : catalog.ports()) {
    if (!port.hasPosition()) continue;
    if (medPort == nullptr && port.position.lng > -6.0 && port.position.lng < 33.0 &&
        port.position.lat > 33.0 && port.position.lat < 45.0) {
      medPort = &port;
    }
    if (pacificPort == nullptr && port.position.lng > 105.0 && port.position.lng < 140.0 &&
        port.position.lat > 5.0 && port.position.lat < 40.0) {
      pacificPort = &port;
    }
  }
  check(medPort != nullptr && pacificPort != nullptr, "found a Mediterranean and a Pacific port");
  if (medPort != nullptr && pacificPort != nullptr) {
    const std::vector<LatLng> course = game.navalCourse(medPort->position, pacificPort->position);
    check(course.size() > 10, "a cross-basin course follows the sea route, not a straight line");

    // A short hop over open water stays a straight two-point line.
    const std::vector<LatLng> hop = game.navalCourse({0.0, -30.0}, {2.0, -28.0});
    check(hop.size() == 2, "an open-water hop is a straight line");
  }

  // -----------------------------------------------------------------------
  std::cout << "fleets and admirals\n";

  // Base caps come from the unmodified research tree: two fleets, four ships each and two
  // admirals. The three new technologies raise one of those caps per level.
  Research untrained;
  check(fleetLimits(untrained).maxFleets == kBaseFleetLimit, "base fleet cap");
  check(fleetLimits(untrained).maxShipsPerFleet == kBaseShipsPerFleet, "base ships per fleet");
  check(fleetLimits(untrained).maxAdmirals == kBaseAdmiralLimit, "base admiral cap");
  Research expanded;
  setTechLevel(expanded, kFleetCommandTech, 1);
  setTechLevel(expanded, kFleetOrganizationTech, 1);
  setTechLevel(expanded, kAdmiralCorpsTech, 1);
  check(fleetLimits(expanded).maxFleets == kBaseFleetLimit + 1, "Fleet Command adds a fleet");
  check(fleetLimits(expanded).maxShipsPerFleet == kBaseShipsPerFleet + 2,
        "Naval Organization adds two ships");
  check(fleetLimits(expanded).maxAdmirals == kBaseAdmiralLimit + 1, "Admiral Corps adds an admiral");

  check(game.fleets().empty(), "a new game starts with no fleets");
  check(game.createFleet("Home Fleet", error), "raise a named fleet");
  const int homeFleetId = game.fleets().back().id;
  check(game.fleets().back().name == "Home Fleet", "the fleet keeps its name");

  const int fleetUnitId = game.navalUnits().front().id;
  check(game.assignUnitToFleet(fleetUnitId, homeFleetId, error), "assign a ship to a fleet");
  check(game.fleetForUnit(fleetUnitId) != nullptr, "the ship reads back in a fleet");
  check(!game.assignUnitToFleet(fleetUnitId, homeFleetId, error), "a ship cannot join twice");
  check(game.fleetRecaps().front().shipCount == 1, "the recap counts the assigned ship");

  // Fleet orders are fleet-wide: one member accepting a course or a target drags the rest of
  // the squadron along, and movement uses a formation rather than one shared destination.
  check(game.buildNavalUnit(1, smallPort, 1000.0, error), "build a second hull for the fleet");
  const int secondUnitId = game.navalUnits().back().id;
  check(game.assignUnitToFleet(secondUnitId, homeFleetId, error), "the second hull joins the fleet");
  const LatLng formationTarget{0.0, -30.0};
  check(game.orderNavalUnits({fleetUnitId}, formationTarget) == 2,
        "ordering one fleet ship moves the whole fleet");
  const NavalUnit* formationA = game.navalUnitById(fleetUnitId);
  const NavalUnit* formationB = game.navalUnitById(secondUnitId);
  check(formationA != nullptr && formationA->moving() && formationB != nullptr &&
            formationB->moving(),
        "both fleet ships are under way");
  check(formationA != nullptr && formationB != nullptr &&
            haversineKm(formationA->path.back(), formationB->path.back()) > 50.0,
        "formation slots keep the ships from stacking");

  const int sharedBoatId = game.botBoats().empty() ? 0 : game.botBoats().front().id;
  if (sharedBoatId != 0) {
    check(game.orderNavalUnitsAttack({fleetUnitId}, sharedBoatId, error) == 2,
          "one attack order is given to the whole fleet");
    check(game.navalUnitById(fleetUnitId)->targetBoatId == sharedBoatId &&
              game.navalUnitById(secondUnitId)->targetBoatId == sharedBoatId,
          "the fleet shares one target");
  }
  game.orderNavalUnits({fleetUnitId}, formationTarget);  // clears the targets for later checks

  check(game.createFleet("", error), "an unnamed fleet is given an ordinal");
  check(game.fleets().back().name == "2nd Fleet", "the ordinal name is readable");
  const int secondFleetId = game.fleets().back().id;
  check(!game.createFleet("Third Fleet", error), "the base fleet cap is enforced");
  check(game.fleets().size() == 2, "a refused fleet raises nothing");

  const double beforeRecruit = game.money();
  check(!game.recruitAdmiralToFleet(4, 999999, error), "an admiral cannot be recruited without a fleet");
  checkNear(game.money(), beforeRecruit, 1e-9, "a refused recruitment charges nothing");
  check(game.recruitAdmiralToFleet(4, homeFleetId, error), "recruit an admiral to a fleet");
  const Admiral* speedAdmiral = game.admiralById(4);
  check(speedAdmiral != nullptr && speedAdmiral->hired, "the admiral joins the payroll");
  check(game.fleetById(homeFleetId)->admiralId == 4, "recruitment posts the admiral to the fleet");
  checkNear(game.fleetBuffMultiplier(fleetUnitId, AdmiralBuff::Speed), 1.10, 1e-5,
            "the speed admiral boosts fleet speed");
  checkNear(game.fleetBuffMultiplier(fleetUnitId, AdmiralBuff::Damage), 1.0, 1e-5,
            "an unrelated stat is not boosted");
  checkNear(game.fleetBuffMultiplier(999999, AdmiralBuff::Speed), 1.0, 1e-5,
            "an independent ship gets no bonus");
  check(game.fleetRecaps().front().admiralId == 4, "the recap names the fleet's admiral");

  check(game.assignAdmiralToFleet(4, secondFleetId, error), "an admiral can be posted elsewhere");
  check(game.fleetById(homeFleetId)->admiralId == 0, "moving an admiral frees the old fleet");
  check(game.fleetById(secondFleetId)->admiralId == 4, "the new fleet holds the admiral");
  check(game.assignAdmiralToFleet(4, homeFleetId, error), "post the admiral back home");

  check(game.removeUnitFromFleet(fleetUnitId, error), "detach a ship from its fleet");
  check(!game.fleetById(homeFleetId)->contains(fleetUnitId),
        "the detached ship leaves the roster");
  check(!game.removeUnitFromFleet(fleetUnitId, error), "a detached ship cannot be detached twice");
  check(game.assignUnitToFleet(fleetUnitId, homeFleetId, error), "re-attach the ship");

  check(game.dismissAdmiralFromFleet(homeFleetId, error), "dismiss an admiral from command");
  check(game.fleetById(homeFleetId)->admiralId == 0, "the fleet has no admiral after dismissal");
  checkNear(game.fleetBuffMultiplier(fleetUnitId, AdmiralBuff::Speed), 1.0, 1e-5,
            "dismissal ends the bonus");
  check(game.assignAdmiralToFleet(4, homeFleetId, error), "the admiral can be re-posted");
  check(!game.releaseAdmiral(4, error), "a commanding admiral cannot be released from the payroll");
  check(game.recruitAdmiralToFleet(5, secondFleetId, error), "recruit a second admiral to its fleet");
  check(game.dismissAdmiralFromFleet(secondFleetId, error), "dismiss the second admiral first");
  check(game.releaseAdmiral(5, error), "release an unassigned admiral from the payroll");
  check(game.admiralById(5) != nullptr && !game.admiralById(5)->hired,
        "the released admiral frees their slot");

  // Save round-trip.
  const std::string saved = game.saveToJson();
  GameState restored;
  restored.attach(&catalog, &router);
  std::string loadError;
  check(restored.loadFromJson(saved, loadError, 0.0), "save round-trips");
  checkNear(restored.money(), game.money(), 1e-6, "money survives save");
  check(restored.portOwned(smallPort), "claimed port survives save");
  check(restored.portLevel(smallPort) == 3, "the port level survives save");
  check(restored.portHasModule(smallPort, 1), "a fitted module survives save");
  check(restored.navalUnitCount() == game.navalUnitCount(), "the naval force survives save");
  check(restored.fleets().size() == game.fleets().size(), "the fleets survive save");
  check(restored.fleetById(homeFleetId) != nullptr &&
            restored.fleetById(homeFleetId)->name == "Home Fleet",
        "a saved fleet keeps its name");
  check(restored.fleetById(homeFleetId) != nullptr &&
            restored.fleetById(homeFleetId)->contains(fleetUnitId),
        "a saved fleet keeps its ships");
  check(restored.admiralById(4) != nullptr && restored.admiralById(4)->hired,
        "a hired admiral survives save");
  check(restored.fleetById(homeFleetId) != nullptr &&
            restored.fleetById(homeFleetId)->admiralId == 4,
        "a saved command survives save");
  if (!restored.navalUnits().empty()) {
    const NavalUnit& saved0 = restored.navalUnits().front();
    const NavalUnit& live0 = game.navalUnits().front();
    check(saved0.classId == live0.classId && saved0.kind == live0.kind,
          "a saved hull keeps its class and hull model");
    checkNear(saved0.position.lat, live0.position.lat, 1e-9, "a saved hull keeps its latitude");
    checkNear(saved0.position.lng, live0.position.lng, 1e-9, "a saved hull keeps its longitude");
  }

  // -----------------------------------------------------------------------
  std::cout << "\nbot aggression\n";

  // An enemy hull reacts to traffic in its own stretch of water, or to a hull that has shot at
  // it. It must not steam across the sea after a ship that merely exists somewhere: the flat
  // 1200 km hunt radius the bots used to have had a boat off Algiers chasing anything between
  // Gibraltar and Cyprus. A hull is put on a chosen station by saving, editing its position
  // and reloading, which is the only way to place a ship on an arbitrary patch of water. A
  // pirate band is the other kind of hostile and never hunts at all, so the probe is anchored
  // to a power's warship rather than to a sentry that holds station by design.
  {
    int corsairId = 0;
    LatLng corsairStation{0.0, 0.0};
    {
      GameState pick;
      pick.attach(&catalog, &router);
      pick.newGame();
      for (const BotBoat& boat : pick.botBoats()) {
        if (boat.pirate) continue;  // a sentry holds station; only an enemy hull hunts
        corsairId = boat.id;
        corsairStation = boat.position;
        break;
      }
    }
    check(corsairId != 0, "an enemy power's hull is at sea");

    // Every boat carries an id of its own. The factions are spawned one after another, and a
    // repeated id makes every id-keyed lookup - botBoatById, the hunt, the kill feed, the
    // saved station of each hull - resolve to the wrong faction's boat.
    {
      GameState ids;
      ids.attach(&catalog, &router);
      ids.newGame();
      std::vector<int> seen;
      bool uniqueIds = true;
      for (const BotBoat& boat : ids.botBoats()) {
        if (std::find(seen.begin(), seen.end(), boat.id) != seen.end()) uniqueIds = false;
        seen.push_back(boat.id);
      }
      check(uniqueIds, "every bot boat has its own id");

      std::string loadError;
      GameState reloaded;
      reloaded.attach(&catalog, &router);
      const bool loaded = reloaded.loadFromJson(ids.saveToJson(), loadError, 0.0);
      check(loaded && reloaded.botBoats().size() == ids.botBoats().size(),
            "the bot fleet survives a save");
      bool stationsKept = loaded;
      for (size_t i = 0; i < ids.botBoats().size() && i < reloaded.botBoats().size(); ++i) {
        if (haversineKm(ids.botBoats()[i].position, reloaded.botBoats()[i].position) > 1.0) {
          stationsKept = false;
        }
      }
      check(stationsKept, "every bot hull reloads at its own station");

      // The sweep a hull keeps is local whatever it carries: the 1200 km flat radius a boat used
      // to have covered the whole Mediterranean from a single anchor, so every mounted hull
      // watches a patch of its own size, inside the 400 km cap.
      bool everySweepLocal = true;
      for (const BotBoat& boat : ids.botBoats()) {
        const double reach = engagementRangeKm(boat.armament, kFireAuto);
        const double sweep = botHuntRadiusKm(reach);
        if (sweep > 400.0 || sweep < 180.0) everySweepLocal = false;
      }
      check(everySweepLocal, "no rival hull watches more than 400 km of water");
      // And the sweep follows the guns rather than the flat radius: a machine-gun hull watches
      // less water than one carrying a 76 mm turret.
      check(botHuntRadiusKm(150.0) < botHuntRadiusKm(390.0),
            "a short-gunned hull watches a smaller patch than a long-gunned one");
      checkNear(botHuntRadiusKm(0.0), 180.0, 1e-9, "a boat with no usable mount still keeps watch");
    }

    // A hull that can hit from beyond every bot mount (the catalog's longest-legged class
    // reaches 750 km; a corsair's biggest gun 389), so a corsair can be shot from open water
    // with nothing of the player's anywhere near it.
    int sniperClassId = 0;
    for (const NavalVessel& vessel : catalog.navalVessels()) {
      if (engagementRangeKm(vessel.armament, kFireAuto) < 600.0f) continue;
      sniperClassId = vessel.id;
      break;
    }
    check(sniperClassId != 0, "a class outranges the corsairs");
    game.changeMoney(200000.0);
    std::string buildError;
    check(game.buildNavalUnit(sniperClassId, smallPort, 9000.0, buildError),
          "lay down a long-range hull");

    const nlohmann::json fleetSave = nlohmann::json::parse(game.saveToJson());
    check(fleetSave.contains("navalUnits") && !fleetSave["navalUnits"].empty(),
          "a save carries the hulls");

    // Where that hull actually is in this game, rather than where it started: an enemy that
    // has been hunting the player's ships is not sitting on its home berth any more, and a
    // probe staged on the old station would be staged on empty water.
    for (const auto& boat : fleetSave["botBoats"]) {
      if (boat.value("id", 0) != corsairId) continue;
      if (boat.value("health", 0.0f) <= 0.0f) continue;
      corsairStation = {boat.value("lat", corsairStation.lat),
                        boat.value("lng", corsairStation.lng)};
      break;
    }

    // Drops the last hull in `save` on `station` and lets one second of the game run. Returns
    // its id, or 0. The posture defaults to defensive, so what a corsair does in those probes
    // is its own doing rather than an answer to a shot.
    auto stage = [&](const nlohmann::json& save, const LatLng& station, GameState& probe,
                     int posture = kPostureDefensive) -> int {
      nlohmann::json root = save;
      if (!root.contains("navalUnits") || root["navalUnits"].empty()) return 0;
      nlohmann::json hull = root["navalUnits"].back();
      hull["lat"] = station.lat;
      hull["lng"] = station.lng;
      hull["path"] = nlohmann::json::array();
      hull["targetBoat"] = 0;
      root["navalUnits"] = nlohmann::json::array({hull});
      root["fleets"] = nlohmann::json::array();

      probe.attach(&catalog, &router);
      std::string loadError;
      check(probe.loadFromJson(root.dump(), loadError, 0.0), "the hull reloads at its station");
      if (probe.navalUnits().empty()) return 0;
      const int unitId = probe.navalUnits().front().id;
      probe.setPosture({unitId}, posture);
      probe.update(1000.0);  // one second of game time
      return unitId;
    };

    // Ammo is one magazine, not the ship's whole supply. A mount loaded with a single round
    // fires it, drops to zero and spends its reload_time changing the magazine; when that
    // reload ends the magazine comes back full without a return to port.
    if (!game.botBoats().empty()) {
      nlohmann::json magazineRoot = fleetSave;
      nlohmann::json hull = magazineRoot["navalUnits"].back();
      const LatLng station = game.botBoats().front().position;
      hull["lat"] = station.lat;
      hull["lng"] = station.lng;
      hull["path"] = nlohmann::json::array();
      hull["targetBoat"] = 0;
      for (auto& ammo : hull["mounts"]) ammo = 1.0;
      magazineRoot["navalUnits"] = nlohmann::json::array({hull});
      magazineRoot["fleets"] = nlohmann::json::array();

      GameState magazineSea;
      magazineSea.attach(&catalog, &router);
      std::string magazineError;
      check(magazineSea.loadFromJson(magazineRoot.dump(), magazineError, 0.0),
            "the magazine probe loads");
      if (!magazineSea.navalUnits().empty()) {
        const int probeId = magazineSea.navalUnits().front().id;
        magazineSea.setPosture({probeId}, kPostureOffensive);
        magazineSea.update(1000.0);
        const NavalUnit* probe = magazineSea.navalUnitById(probeId);
        check(probe != nullptr && !probe->mounts.empty(), "the magazine probe carries a mount");
        if (probe != nullptr && !probe->mounts.empty()) {
          const float reloadWanted = probe->mounts.front().reload;
          checkNear(probe->mounts.front().ammo, 0.0, 1e-6,
                    "a magazine fires down to zero");
          check(reloadWanted > 0.0f, "an empty magazine starts its reload");

          // Run frames past the reload. The tick refills the magazine; with the target still in
          // reach the mount may spend one round of the fresh magazine in the same tick.
          double clock = 1000.0;
          for (int tick = 0; tick < 400 && probe != nullptr && !probe->mounts.empty() &&
                              probe->mounts.front().ammo <= 0.0f;
               ++tick) {
            clock += 250.0;
            magazineSea.update(clock);
            probe = magazineSea.navalUnitById(probeId);
          }
          check(probe != nullptr && !probe->mounts.empty() && probe->mounts.front().ammo > 0.0f,
                "a finished reload brings the magazine back");
        }
      }
    }

    // A station far out in open ocean, more than 800 km from every hull on the map bar none:
    // past the longest sweep any hull watches (a lookout is capped at 400 km), so nothing
    // should pick the ship up.
    const LatLng farStation{43.0, -18.0};
    GameState farSea;
    const int farUnitId = stage(fleetSave, farStation, farSea);
    bool farHunted = false;
    for (const BotBoat& boat : farSea.botBoats()) {
      if (boat.targetUnitId == farUnitId) farHunted = true;
    }
    check(farUnitId != 0 && !farHunted, "no bot hull hunts a ship 800 km away");

    // The same ship 120 km off the corsair's own water is picked up on the first tick.
    LatLng nearStation = corsairStation;
    nearStation.lat += 120.0 / 111.32;
    if (router.isLandAt(nearStation.lat, nearStation.lng)) nearStation = corsairStation;
    GameState nearSea;
    const int nearUnitId = stage(fleetSave, nearStation, nearSea);
    const BotBoat* nearCorsair = nearSea.botBoatById(corsairId);
    check(nearUnitId != 0 && nearCorsair != nullptr &&
              nearCorsair->targetUnitId == nearUnitId,
          "an enemy hull hunts traffic in its own waters");

    // And a hull that opens fire from beyond that water is answered anyway: being attacked is
    // its own trigger, and the answer is allowed to run further than the lookouts.
    GameState shotSea;
    const int shotUnitId = stage(fleetSave, farStation, shotSea);
    std::string attackError;
    check(shotUnitId != 0 && shotSea.orderNavalUnitAttack(shotUnitId, corsairId, attackError),
          "a hull is sent at an enemy warship from far off");
    bool answered = false;
    for (int tick = 0; tick < 80 && !answered; ++tick) {
      shotSea.update(1000.0 + 250.0 * (tick + 1));
      const BotBoat* boat = shotSea.botBoatById(corsairId);
      answered = boat != nullptr && boat->retaliating && boat->targetUnitId == shotUnitId;
    }
    check(answered, "an enemy hull turns on the hull that shoots it, however far off");

    // A ship shoots at the cadence its mounts are rated at, not at the catalog's single load
    // time for the whole weapon type. A hull is dropped on the corsair's water, the corsair
    // opens fire, and the hull answers with its own battery on the following tick.
    int gunboatClassId = 0;
    size_t gunMount = 0;
    for (const NavalVessel& vessel : catalog.navalVessels()) {
      if (vessel.health < 3000.0f) continue;
      for (size_t i = 0; i < vessel.armament.size(); ++i) {
        const Armament& weapon = vessel.armament[i];
        if (deliveryFor(weapon) != Delivery::Gun) continue;
        if (weaponCalibreMm(weapon) < 20.0f) continue;
        // A mount the rule re-rates sharply, so the difference between the top of the catalog
        // and the derived cadence is unmistakable in the observed reload.
        if (mountReloadSeconds(weapon) > weapon.reload_time * 0.5f) continue;
        gunboatClassId = vessel.id;
        gunMount = i;
        break;
      }
      if (gunboatClassId != 0) break;
    }
    check(gunboatClassId != 0, "a hull in the catalog carries a gun");

    std::string gunboatError;
    if (gunboatClassId != 0) {
      check(game.buildNavalUnit(gunboatClassId, smallPort, 9000.0, gunboatError),
            "lay down the gunned hull");
    }
    nlohmann::json gunboatRoot = nlohmann::json::parse(game.saveToJson());

    // A defensive hull answers whoever is shooting at it, so the probe has to be staged in a
    // fight it can reach back into. A rival that carries long-range missiles holds station
    // beyond a gun's own reach, so the probe is given the rival whose battery is shortest and
    // no others: a second hull with longer legs would take the fight away from the gun.
    int answerId = 0;
    LatLng answerStation = corsairStation;
    {
      GameState scout;
      scout.attach(&catalog, &router);
      std::string scoutError;
      if (scout.loadFromJson(gunboatRoot.dump(), scoutError, 0.0)) {
        double shortest = 1.0e18;
        for (const BotBoat& boat : scout.botBoats()) {
          if (boat.sunk() || boat.armament.empty()) continue;
          const double reach = engagementRangeKm(boat.armament, kFireAuto);
          if (reach <= 0.0 || reach >= shortest) continue;
          shortest = reach;
          answerId = boat.id;
          answerStation = boat.position;
        }
      }
    }
    check(answerId != 0, "a rival hull is close enough for the gun to answer");

    nlohmann::json keptBoats = nlohmann::json::array();
    for (const auto& boat : gunboatRoot["botBoats"]) {
      if (boat.value("id", 0) == answerId) keptBoats.push_back(boat);
    }
    gunboatRoot["botBoats"] = std::move(keptBoats);

    // The hull is staged in an offensive posture on top of the rival's own berth, so its battery
    // speaks without waiting to be shot at, and the fight is stepped a quarter second at a time
    // until the gun has fired. A mount that never fires at all fails the check below rather than
    // passing it on a technicality.
    GameState gunboatSea;
    const int gunboatId = gunboatClassId != 0
                              ? stage(gunboatRoot, answerStation, gunboatSea, kPostureOffensive)
                              : 0;

    float gunReload = 0.0f;
    double gunClock = 1000.0;
    for (int tick = 0; tick < 40 && gunReload <= 0.0f; ++tick) {
      gunClock += 250.0;
      gunboatSea.update(gunClock);
      const NavalUnit* fired = gunboatSea.navalUnitById(gunboatId);
      if (fired != nullptr && gunMount < fired->mounts.size()) {
        gunReload = fired->mounts[gunMount].reload;
      }
    }

    const NavalUnit* gunboat = gunboatSea.navalUnitById(gunboatId);
    const NavalVessel* gunboatClass =
        gunboat != nullptr ? catalog.navalVesselById(gunboat->classId) : nullptr;
    check(gunboat != nullptr && gunboatClass != nullptr &&
              gunMount < gunboat->mounts.size(),
          "the gunned hull is in the fight");

    // The gun came away from its first salvo on its own reload: a second or two, not the flat
    // five seconds every gun in the catalog is loaded with. The mount also had to be able to
    // fire at all at this range, so a silent battery would fail this rather than pass it.
    float gunReloadWanted = 0.0f;
    float gunCatalogReload = 0.0f;
    if (gunboat != nullptr && gunboatClass != nullptr && gunMount < gunboat->mounts.size()) {
      const Armament& gun = gunboatClass->armament[gunMount];
      gunReload = gunboat->mounts[gunMount].reload;
      gunReloadWanted = mountReloadSeconds(gun);
      gunCatalogReload = gun.reload_time;
    }
    check(gunReload > 0.0f && gunReload <= gunReloadWanted * 1.11f,
          "the gun reloads on its own cadence");
    check(gunReload > 0.0f && gunReload < gunCatalogReload * 0.5f,
          "the catalog's flat load time no longer drives the mount");
  }

  // -----------------------------------------------------------------------
  std::cout << "\nrival powers\n";
  {
    // An opponent is a player: a treasury fed by its ports, merchant crossings that pay
    // freight, hulls laid down at its own yard and a research programme. There is no spawn
    // table behind it any more, so what a power has on the water is what it could pay for.
    GameState world;
    world.attach(&catalog, &router);
    world.newGame();

    check(world.factions().size() >= 2, "a new game seeds the enemy powers");

    std::vector<int> homes;
    bool everyPowerHasAHome = true;
    bool homesAreDistinct = true;
    for (size_t index = 0; index < world.factions().size(); ++index) {
      const AIPlayer& power = world.factions()[index];
      const int home = world.factionHomePort(static_cast<int>(index));
      if (home == 0 || home == kStarterPortId) everyPowerHasAHome = false;
      if (world.factionNameForPort(home) != power.name) everyPowerHasAHome = false;
      if (std::find(homes.begin(), homes.end(), home) != homes.end()) homesAreDistinct = false;
      homes.push_back(home);
    }
    check(everyPowerHasAHome, "every power is given a harbour in its own waters");
    check(homesAreDistinct, "the powers do not share a harbour");

    // The opening navy: one hull each, laid down from the catalogue and owned by its power. A
    // pirate sentry is not part of it - it is a preset hull on a station, with no class and no
    // yard - so only the powers' hulls are checked here.
    bool everyHullIsClassed = true;
    bool everyHullIsOwned = true;
    for (const BotBoat& boat : world.botBoats()) {
      if (boat.pirate) continue;
      if (boat.classId == 0 || catalog.navalVesselById(boat.classId) == nullptr) {
        everyHullIsClassed = false;
      }
      if (boat.owner.empty() || boat.name.empty() || boat.homePortId == 0) {
        everyHullIsOwned = false;
      }
    }
    check(!world.botBoats().empty(), "the powers put a hull in the water at the start");
    check(everyHullIsClassed, "an enemy hull comes from a catalogue class");
    check(everyHullIsOwned, "an enemy hull names its power and its yard");

    // Income. Nothing can be bought in the first twenty seconds - the build clocks have not
    // come round and no power can afford a hull yet - so the treasury can only move one way.
    std::vector<double> purses;
    for (const AIPlayer& power : world.factions()) purses.push_back(power.money);
    double clock = 0.0;
    for (int tick = 0; tick < 80; ++tick) {  // 20 s at a quarter second a tick
      clock += 250.0;
      world.update(clock);
    }
    bool everyPowerEarned = true;
    for (size_t index = 0; index < world.factions().size(); ++index) {
      const AIPlayer& power = world.factions()[index];
      const double floorIncome = kPassiveIncomePerPortPerSecond *
                                 static_cast<double>(power.portIds.size()) * 18.0;
      if (power.money < purses[index] + floorIncome) everyPowerEarned = false;
    }
    check(everyPowerEarned, "a power's ports pay it what the player's pay the player");

    // Six minutes of the economy running: crossings go out and settle, hulls are laid down,
    // and the points those crossings earn are filed as research. Six minutes because the
    // cheapest technology costs forty points and a crossing pays a few, so it takes a power
    // two or three runs before it can buy its first one.
    bool everTraded = false;
    bool everBuilt = false;
    const size_t startingHulls = world.botBoats().size();
    for (int tick = 0; tick < 1440; ++tick) {
      clock += 250.0;
      world.update(clock);
      if (!world.merchantRuns().empty()) everTraded = true;
      if (world.botBoats().size() > startingHulls) everBuilt = true;
    }
    check(everTraded, "the powers run merchant crossings");
    check(everBuilt, "the powers lay new hulls down");

    bool researchedSomething = false;
    bool naviesWithinCap = true;
    for (size_t index = 0; index < world.factions().size(); ++index) {
      const AIPlayer& power = world.factions()[index];
      for (const Technology& technology : technologies()) {
        if (techLevel(power.research, technology.id) > 0) researchedSomething = true;
      }
      const int cap = 2 * static_cast<int>(power.portIds.size()) + 1;
      if (world.factionShipCount(static_cast<int>(index)) > cap) naviesWithinCap = false;
    }
    check(researchedSomething, "the powers file research from their crossings");
    check(naviesWithinCap, "a power never keeps more hulls than its ports support");

    // An enemy navy is its own money: sinking a hull takes it out of that power's order of
    // battle until it pays for another one. The loss is written into a save and reloaded, which
    // is how a hull is removed without staging a battle. A pirate has no order of battle, so
    // the doomed hull is drawn from a power's navy.
    const BotBoat* doomedBoat = nullptr;
    for (const BotBoat& boat : world.botBoats()) {
      if (boat.pirate) continue;
      doomedBoat = &boat;
      break;
    }
    if (doomedBoat != nullptr) {
      int ownerIndex = -1;
      for (size_t index = 0; index < world.factions().size(); ++index) {
        if (world.factions()[index].name == doomedBoat->owner) {
          ownerIndex = static_cast<int>(index);
        }
      }
      check(ownerIndex >= 0, "every enemy hull belongs to a seeded power");
      if (ownerIndex >= 0) {
        const int lostId = doomedBoat->id;
        const int homePortId = world.factionHomePort(ownerIndex);
        const int navyBefore = world.factionShipCount(ownerIndex);

        nlohmann::json root = nlohmann::json::parse(world.saveToJson());
        bool wrote = false;
        for (auto& boat : root["botBoats"]) {
          if (boat.value("id", 0) == lostId) {
            boat["health"] = 0.0;
            wrote = true;
          }
        }
        check(wrote, "the hull to be lost is in the save");

        GameState afterKill;
        afterKill.attach(&catalog, &router);
        std::string killError;
        check(afterKill.loadFromJson(root.dump(), killError, 0.0), "the world reloads");
        check(afterKill.factionShipCount(ownerIndex) == navyBefore - 1,
              "a sunk rival hull leaves its power's navy");
        check(afterKill.factionOwningPort(homePortId) == ownerIndex,
              "a power keeps its harbour through a save");
        check(afterKill.factions().size() == world.factions().size(), "the powers survive a save");
        check(afterKill.merchantRuns().size() == world.merchantRuns().size(),
              "the merchant crossings survive a save");
      }
    }
  }

  // -----------------------------------------------------------------------
  std::cout << "\npirates and enemies\n";
  {
    // The two kinds of hostile are not the same thing. A pirate band is a fixed group of hulls
    // placed on anchor stations: it owns nothing, buys nothing, takes nothing and never leaves
    // its patch. An enemy power is a player - it holds ports, trades, researches and builds -
    // and it is not a pirate band.
    GameState sea;
    sea.attach(&catalog, &router);
    sea.newGame();

    check(!pirateBands().empty(), "the fixed pirate roster is not empty");
    size_t expectedPirates = 0;
    for (const PirateBand& band : pirateBands()) expectedPirates += band.anchors.size();

    size_t pirateHulls = 0;
    bool everyPirateOnStation = true;
    bool everyPirateFliesItsBand = true;
    bool piratesOwnNothing = true;
    bool piratesAreArmed = true;
    for (const BotBoat& boat : sea.botBoats()) {
      if (!boat.pirate) continue;
      ++pirateHulls;
      if (haversineKm(boat.position, boat.station) > 1.0) everyPirateOnStation = false;
      if (boat.homePortId != 0 || boat.classId != 0) piratesOwnNothing = false;
      if (boat.armament.empty()) piratesAreArmed = false;
      bool knownBand = false;
      for (const PirateBand& band : pirateBands()) {
        if (band.name == boat.owner) knownBand = true;
      }
      for (const AIPlayer& power : sea.factions()) {
        if (power.name == boat.owner) knownBand = false;  // a band is not a power
      }
      if (!knownBand) everyPirateFliesItsBand = false;
    }
    check(pirateHulls == expectedPirates, "one hull is placed on every pirate anchor");
    check(everyPirateOnStation, "a pirate hull starts on its own station");
    check(everyPirateFliesItsBand,
          "a pirate hull flies its band's name and is no enemy power's");
    check(piratesOwnNothing, "a pirate hull has no class and no yard");
    check(piratesAreArmed, "a pirate sentry is armed, so it can fire from its station");

    // A band is static. A minute of the game running - while the powers trade, build and
    // research around it - must not move a single pirate off the station it was placed on, and
    // must not make it hunt: it is a fixed obstacle, not a hunter.
    std::vector<LatLng> stations;
    for (const BotBoat& boat : sea.botBoats()) {
      if (boat.pirate) stations.push_back(boat.station);
    }
    double clock = 0.0;
    for (int tick = 0; tick < 240; ++tick) {  // 60 s at a quarter second a tick
      clock += 250.0;
      sea.update(clock);
    }
    bool stayedPut = true;
    bool neverHunted = true;
    size_t seen = 0;
    for (const BotBoat& boat : sea.botBoats()) {
      if (!boat.pirate) continue;
      if (seen >= stations.size() || haversineKm(boat.position, stations[seen]) > 1.0) {
        stayedPut = false;
      }
      if (boat.targetUnitId != 0) neverHunted = false;
      ++seen;
    }
    check(stayedPut && seen == expectedPirates, "a pirate band holds its station over time");
    check(neverHunted, "a pirate never picks a ship to hunt");

    // A power, by contrast, is dynamic: it earns from its ports and lays hulls down while the
    // bands sit still. That contrast is the whole point of the split.
    bool powersBuilt = false;
    const size_t powerHullsAtStart = sea.botBoats().size() - expectedPirates;
    for (int tick = 0; tick < 1440 && !powersBuilt; ++tick) {  // six more minutes
      clock += 250.0;
      sea.update(clock);
      size_t powerHulls = 0;
      for (const BotBoat& boat : sea.botBoats()) {
        if (!boat.pirate) ++powerHulls;
      }
      if (powerHulls > powerHullsAtStart) powersBuilt = true;
    }
    check(powersBuilt, "the enemy powers keep building while the pirates sit still");

    // A pirate is a sentry, not a pacifist. It never moves, but a ship laid inside its gun
    // range is fired on. The hull is put on the pirate's own station through a save, the only
    // way to place a ship on an arbitrary patch of water, and left in a defensive posture so
    // anything it takes is the sentry's doing rather than a return shot of its own.
    LatLng watchStation{0.0, 0.0};
    int watchPirateId = 0;
    for (const BotBoat& boat : sea.botBoats()) {
      if (!boat.pirate) continue;
      watchPirateId = boat.id;
      watchStation = boat.station;
      break;
    }
    check(watchPirateId != 0, "a pirate sentry is on the map");

    sea.changeMoney(200000.0);
    std::string yardError;
    check(sea.installPortModule(kStarterPortId, 2, 0, yardError),
          "fit a shipyard for the sentry probe");
    bool laidDown = false;
    for (const NavalVessel& vessel : catalog.navalVessels()) {
      if (vessel.armament.empty()) continue;
      if (sea.buildNavalUnit(vessel.id, kStarterPortId, 0.0, yardError)) {
        laidDown = true;
        break;
      }
    }
    check(laidDown, "a hull is available for the sentry probe");

    if (laidDown && watchPirateId != 0 && !sea.navalUnits().empty()) {
      nlohmann::json root = nlohmann::json::parse(sea.saveToJson());
      nlohmann::json hull = root["navalUnits"].back();
      hull["lat"] = watchStation.lat;
      hull["lng"] = watchStation.lng;
      hull["path"] = nlohmann::json::array();
      hull["targetBoat"] = 0;
      root["navalUnits"] = nlohmann::json::array({hull});
      root["fleets"] = nlohmann::json::array();

      GameState sentry;
      sentry.attach(&catalog, &router);
      std::string sentryError;
      check(sentry.loadFromJson(root.dump(), sentryError, 0.0), "the sentry probe loads");

      const BotBoat* watch = sentry.botBoatById(watchPirateId);
      check(watch != nullptr && watch->pirate, "the sentry survives the save as a pirate");
      if (watch != nullptr && !sentry.navalUnits().empty()) {
        const LatLng heldBefore = watch->position;
        const float hullBefore = sentry.navalUnits().front().health;
        const int probeId = sentry.navalUnits().front().id;
        sentry.setPosture({probeId}, kPostureDefensive);
        for (int tick = 0; tick < 12; ++tick) sentry.update(1000.0 * (tick + 1));

        const BotBoat* fired = sentry.botBoatById(watchPirateId);
        const NavalUnit* probe = sentry.navalUnitById(probeId);
        check(fired != nullptr && haversineKm(fired->position, heldBefore) < 1.0,
              "a pirate sentry holds its station under fire");
        check(probe != nullptr && probe->health < hullBefore,
              "a pirate sentry fires at a ship inside its gun range");
        check(fired != nullptr && fired->targetUnitId == 0,
              "a pirate sentry never hunts the ship it shoots at");
      }
    }
  }

  // -----------------------------------------------------------------------
  std::cout << "port defences\n";
  {
    GameState harbour;
    harbour.attach(&catalog, &router);
    harbour.newGame();
    harbour.changeMoney(1.0e7);

    // The nearest unarmed port to a raider: a coastal battery only defends the water its own
    // mounts can shoot across, so the pair has to be inside the machine gun's reach.
    const Port* watchPort = nullptr;
    int raidBoatId = 0;
    double bestKm = 2000.0;
    for (const Port& port : catalog.ports()) {
      if (!port.hasPosition() || !port.equippedArmement.empty()) continue;
      for (const BotBoat& boat : harbour.botBoats()) {
        const double km = haversineKm(port.position, boat.position);
        if (km < bestKm) {
          bestKm = km;
          watchPort = &port;
          raidBoatId = boat.id;
        }
      }
    }
    check(watchPort != nullptr && raidBoatId != 0, "a raider sits inside a port's waters");

    if (watchPort != nullptr && raidBoatId != 0) {
      const int portId = watchPort->id;
      const int attackBefore = watchPort->attack;
      std::string harbourError;
      check(harbour.stormPort(portId, harbourError), "claim the threatened harbour");

      const Port* armed = catalog.portById(portId);
      check(armed != nullptr && armed->equippedArmement.size() == 1,
            "a claimed port mounts one weapon by default");
      if (armed != nullptr && !armed->equippedArmement.empty()) {
        check(deliveryFor(armed->equippedArmement.front()) == Delivery::MachineGun,
              "the default weapon is a machine gun");
        check(armed->equippedArmement.front().ammo > 1.0f,
              "the default machine gun carries a belt, not one round");
        checkNear(armed->attack,
                  attackBefore +
                      static_cast<int>(armed->equippedArmement.front().power + 0.5f),
                  1e-9, "the default gun adds its power to the port's attack");
      }

      const BotBoat* raid = harbour.botBoatById(raidBoatId);
      const float healthBefore = raid != nullptr ? raid->health : 0.0f;

      // Two seconds of the fight: the harbour opens fire without an order and its rounds land.
      for (int tick = 0; tick < 8; ++tick) harbour.update(250.0 * (tick + 1));

      const BotBoat* hit = harbour.botBoatById(raidBoatId);
      check(raid != nullptr && hit != nullptr && hit->health < healthBefore,
            "a port fires its battery at a raider in range");

      const Port* defended = catalog.portById(portId);
      check(defended != nullptr && defended->equippedArmement.size() == 1,
            "an armed port is not fitted with a second default gun");
    }
  }

  // -----------------------------------------------------------------------
  std::cout << "harbour siege\n";
  {
    // Taking a harbour is a battle, not a purchase: the ships are sent to bombard it, its own
    // battery answers, and only a port whose defences are broken can be bought.
    GameState siege;
    siege.attach(&catalog, &router);
    siege.newGame();
    siege.changeMoney(1.0e7);

    const Port* home = catalog.portById(kStarterPortId);
    check(home != nullptr && home->name == "NANTES", "the granted harbour is Nantes");
    check(home != nullptr && home->country == "FR", "the granted harbour is in France");

    // The nearest small harbour by sea from the home yard, so the crossing is short and one
    // hull can see the siege through.
    const std::shared_ptr<const SeaField> field =
        home != nullptr ? router.field(home->position) : nullptr;
    const Port* target = nullptr;
    double bestKm = 1.0e18;
    for (const Port& port : catalog.ports()) {
      if (port.id == kStarterPortId || !port.hasPosition() || port.harborSize != "V") continue;
      double km = 0.0;
      if (!field || !field->distanceKmTo(port.position, km)) continue;
      if (km < bestKm) {
        bestKm = km;
        target = &port;
      }
    }
    check(target != nullptr, "a small neutral harbour is reachable from the yard");

    if (target != nullptr && home != nullptr) {
      const int targetId = target->id;
      std::string siegeError;
      check(!siege.portDefeated(targetId), "an untouched harbour still fights");
      check(!siege.portContested(targetId), "an untouched harbour is nobody's battlefield");
      checkNear(siege.portDefence(targetId), siege.portDefenceMax(targetId), 1e-9,
                "an untouched harbour stands at full defence");
      check(siege.portDefenceMax(targetId) > static_cast<double>(target->health),
            "a harbour's defence pool is a fortification, not a hull's hit points");

      // Fit the yard and lay down the hardest-hitting surface class the catalogue has.
      check(siege.installPortModule(kStarterPortId, 2, 0, siegeError),
            "fit a shipyard at the home yard");
      const NavalVessel* hammer = nullptr;
      for (const NavalVessel& vessel : catalog.navalVessels()) {
        if (vessel.armament.empty()) continue;
        if (vessel.type.find("Submarine") != std::string::npos) continue;
        if (hammer == nullptr || vessel.totalDamage() > hammer->totalDamage()) hammer = &vessel;
      }
      check(hammer != nullptr, "a surface class can be laid down");

      int unitId = 0;
      if (hammer != nullptr) {
        check(siege.buildNavalUnit(hammer->id, kStarterPortId, navalUnitCost(*hammer, false),
                                   siegeError),
              "lay down the hull that will take the harbour");
        if (!siege.navalUnits().empty()) unitId = siege.navalUnits().back().id;
      }

      check(unitId != 0, "the bombarding hull is in the game");
      if (unitId != 0) {
        check(siege.orderNavalUnitsAttackPort({unitId}, targetId, siegeError) == 1,
              "order the hull to bombard the harbour");
        check(siege.portContested(targetId), "the attacked harbour mans its battery");
        check(!siege.claimPort(targetId, siegeError),
              "a harbour under siege cannot be bought yet");

        // The fight itself: the hull steams over, holds its range and works the defences down
        // while the harbour's battery answers.
        double clock = 0.0;
        for (int tick = 0; tick < 2400 && !siege.portDefeated(targetId); ++tick) {
          clock += 250.0;
          siege.update(clock);
        }

        check(siege.portDefence(targetId) < siege.portDefenceMax(targetId),
              "the bombardment wears the harbour's defences down");
        check(siege.portDefeated(targetId), "enough bombardment breaks the harbour");
        check(siege.contestedPortIds().empty(), "a broken harbour stops fighting");
        check(siege.portDefence(targetId) <= 0.0, "a broken harbour has nothing left in the pool");
        check(siege.claimPort(targetId, siegeError), "a broken harbour can be bought");
        check(siege.portOwned(targetId), "the broken harbour is now the player's");
        check(siege.orderNavalUnitsAttackPort({unitId}, targetId, siegeError) == 0,
              "an owned harbour is not a target");
        check(!siegeError.empty(), "a refused siege order says why");
      }
    }

    // The harbour's own battery, checked without relying on the reach a class stands off at: a
    // hull is put on the port's water in a defensive posture, so anything that hits it is the
    // harbour's doing.
    GameState batteryGame;
    batteryGame.attach(&catalog, &router);
    batteryGame.newGame();
    batteryGame.changeMoney(1.0e7);
    if (target != nullptr) {
      batteryGame.installPortModule(kStarterPortId, 2, 0, error);
      if (!catalog.navalVessels().empty()) {
        const NavalVessel& hullClass = catalog.navalVessels().front();
        batteryGame.buildNavalUnit(hullClass.id, kStarterPortId,
                                   navalUnitCost(hullClass, false), error);
      }
      check(!batteryGame.navalUnits().empty(), "a hull is available for the battery probe");

      // Contest the harbour by ordering the hull at it, then move the hull onto the port's
      // own water in the save, so the battery has a target inside its reach on the first tick.
      if (!batteryGame.navalUnits().empty()) {
        batteryGame.orderNavalUnitsAttackPort({batteryGame.navalUnits().front().id}, target->id,
                                              error);

        LatLng station = target->position;
        if (router.isLandAt(station.lat, station.lng)) {
          bool found = false;
          for (double radius = 0.12; radius <= 1.0 && !found; radius += 0.12) {
            for (int step = 0; step < 16 && !found; ++step) {
              const double angle = static_cast<double>(step) * (2.0 * 3.14159265358979323846 / 16.0);
              const LatLng candidate{target->position.lat + std::sin(angle) * radius,
                                     target->position.lng + std::cos(angle) * radius * 1.4};
              if (!router.isLandAt(candidate.lat, candidate.lng)) {
                station = candidate;
                found = true;
              }
            }
          }
        }

        nlohmann::json root = nlohmann::json::parse(batteryGame.saveToJson());
        nlohmann::json hull = root["navalUnits"].front();
        hull["lat"] = station.lat;
        hull["lng"] = station.lng;
        hull["path"] = nlohmann::json::array();
        hull["targetBoat"] = 0;
        hull["targetPort"] = 0;
        root["navalUnits"] = nlohmann::json::array({hull});
        root["fleets"] = nlohmann::json::array();

        GameState battery;
        battery.attach(&catalog, &router);
        std::string batteryError;
        check(battery.loadFromJson(root.dump(), batteryError, 0.0),
              "the harbour-battery probe loads");
        check(battery.portContested(target->id),
              "a contested harbour survives the save as contested");
        if (!battery.navalUnits().empty()) {
          const int probeId = battery.navalUnits().front().id;
          const float healthBefore = battery.navalUnits().front().health;
          battery.setPosture({probeId}, kPostureDefensive);
          for (int tick = 0; tick < 12; ++tick) battery.update(1000.0 * (tick + 1));
          const NavalUnit* probe = battery.navalUnitById(probeId);
          check(probe != nullptr && probe->health < healthBefore,
                "a contested harbour answers the ships that came for it");
        }
      }
    }
  }

  // -----------------------------------------------------------------------
  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
  return g_failures == 0 ? 0 : 1;
}

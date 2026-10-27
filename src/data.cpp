#include "data.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "json.hpp"

namespace sea {
namespace {

bool fileExists(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return static_cast<bool>(file);
}

std::string executableDir() {
  // raylib is not available in the logic library, so use the platform's own call.
#if defined(_WIN32)
  char buffer[4096];
  const unsigned long length = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
  if (length == 0) return "";
  std::string path(buffer, length);
  const size_t slash = path.find_last_of("\\/");
  return slash == std::string::npos ? "" : path.substr(0, slash);
#else
  char buffer[4096];
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) return "";
  std::string path(buffer, static_cast<size_t>(length));
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? "" : path.substr(0, slash);
#endif
}

std::vector<TradeGood> readGoods(const nlohmann::json& list) {
  std::vector<TradeGood> goods;
  goods.reserve(list.size());

  for (const auto& entry : list) {
    TradeGood good;
    good.productCode = entry.value("productCode", std::string());
    good.description = entry.value("description", std::string());
    good.valueK = entry.value("value", 0.0);
    goods.push_back(std::move(good));
  }

  return goods;
}

void sortByValue(std::vector<TradeGood>& goods) {
  std::stable_sort(goods.begin(), goods.end(), [](const TradeGood& a, const TradeGood& b) {
    if (a.valueK != b.valueK) return a.valueK > b.valueK;
    return a.description < b.description;
  });
}


std::string readString(const nlohmann::json& object, const char* key) {
  if (!object.contains(key)) return "";
  const auto& value = object[key];
  if (value.is_string()) return value.get<std::string>();
  if (value.is_number_integer()) return std::to_string(value.get<long long>());
  if (value.is_number()) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value.get<double>());
    return std::string(buffer);
  }
  return "";
}

}  // namespace

std::string resolveDataFile(const std::string& name) {
  std::vector<std::string> candidates;

  if (const char* env = std::getenv("SEA_BUILDER_DATA")) {
    candidates.emplace_back(std::string(env) + "/" + name);
  }

  candidates.emplace_back("data/" + name);
  candidates.emplace_back("../data/" + name);

  const std::string exeDir = executableDir();
  if (!exeDir.empty()) {
    candidates.push_back(exeDir + "/data/" + name);
    candidates.push_back(exeDir + "/../data/" + name);
    candidates.push_back(exeDir + "/../../data/" + name);
  }

  for (const std::string& candidate : candidates) {
    if (fileExists(candidate)) return candidate;
  }

  // Fall back to the first candidate so the error message names something sensible.
  return candidates.empty() ? name : candidates.front();
}

bool Catalog::loadPorts(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  nlohmann::json rows;
  try {
    file >> rows;
  } catch (const std::exception&) {
    return false;
  }

  ports_.clear();
  ports_.reserve(rows.size());

  for (const auto& row : rows) {
    Port port;
    port.id = row.value("id_port", 0);
    port.name = row.value("PORT_NAME", std::string());
    port.country = row.value("COUNTRY", std::string());
    port.position.lat = parseCoordinate(readString(row, "LATITUDE"));
    port.position.lng = parseCoordinate(readString(row, "LONGITUDE"));
    port.maxVessel = row.value("MAX_VESSEL", std::string());
    port.harborSize = row.value("HARBORSIZE", std::string());
    port.repairCode = row.value("REPAIRCODE", std::string());
    port.drydock = row.value("DRYDOCK", std::string());
    port.craneFixed = row.value("CRANEFIXED", std::string());
    port.fuelOil = row.value("FUEL_OIL", std::string());
    port.diesel = row.value("DIESEL", std::string());
    port.commAir = row.value("COMM_AIR", std::string());
    port.commRail = row.value("COMM_RAIL", std::string());
    port.buyPrice = row.value("BUY_PRICE", 0.0);
    port.level = row.value("LEVEL", 1);
    port.health = row.value("health", 0);
    port.def = row.value("def", 0);
    port.attack = row.value("attack", 0);
    ports_.push_back(std::move(port));
  }

  portIndex_.clear();
  for (size_t index = 0; index < ports_.size(); ++index) {
    portIndex_[ports_[index].id] = index;
  }

  return !ports_.empty();
}

bool Catalog::loadVessels(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  nlohmann::json rows;
  try {
    file >> rows;
  } catch (const std::exception&) {
    return false;
  }

  vessels_.clear();
  vessels_.reserve(rows.size());

  for (const auto& row : rows) {
    Vessel vessel;
    vessel.id = row.value("id_vessel", 0);
    vessel.companyName = row.value("Company_Name", std::string());
    vessel.shipName = row.value("ship_name", std::string());
    vessel.builtYear = row.value("built_year", 0);
    vessel.gt = row.value("gt", 0.0);
    vessel.dwt = row.value("dwt", 0.0);
    vessel.length = row.value("length", 0.0);
    vessel.width = row.value("width", 0.0);
    vessel.fleetNation = row.value("fleet_nation", std::string());
    vessels_.push_back(std::move(vessel));
  }

  vesselIndex_.clear();
  for (size_t index = 0; index < vessels_.size(); ++index) {
    vesselIndex_[vessels_[index].id] = index;
  }

  return !vessels_.empty();
}

bool Catalog::loadNavalShips(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  nlohmann::json data;
  try {
    file >> data;
  } catch (const std::exception&) {
    return false;
  }

  // The scraped file is an object with a "vessels" array; a bare array is accepted too so
  // the loader keeps working if the scraper is ever pointed at a trimmed extract.
  const nlohmann::json* rows = &data;
  if (data.is_object()) {
    const auto found = data.find("vessels");
    if (found == data.end()) return false;
    rows = &(*found);
  }
  if (!rows->is_array()) return false;

  navalVessels_.clear();
  navalVessels_.reserve(rows->size());

  int nextId = 1;
  for (const auto& row : *rows) {
    NavalVessel vessel;
    vessel.id = nextId++;
    vessel.shipName = row.value("ship_name", std::string());
    vessel.type = row.value("type", std::string());
    vessel.manufacturer = row.value("manufacturer", std::string());
    vessel.origin = row.value("origin", std::string());
    vessel.commissioned = readString(row, "commissioned");
    vessel.displacement = row.value("displacement", std::string());
    vessel.maxSpeed = row.value("max_speed", std::string());
    vessel.summary = row.value("summary", std::string());

    vessel.power = row.value("power", 0.0f);
    vessel.health = row.value("health", 0.0f);
    vessel.defense = row.value("defense", 0.0f);

    if (row.contains("armament") && row["armament"].is_array()){
      for(const auto& armamentRow: row["armament"]){
        // Older scrapes stored each weapon as a plain string; skip those so the loader
        // still succeeds when it is pointed at an unprocessed extract.
        if (!armamentRow.is_object()) continue;
        Armament weapon;
        weapon.id =  armamentRow.value("id", 0);
        weapon.name =  armamentRow.value("name", std::string());
        weapon.type =  armamentRow.value("type", std::string());
        weapon.category =  static_cast<FireCategory>(armamentRow.value("category", 0));
        weapon.description =  armamentRow.value("description", std::string());
        weapon.damage =  armamentRow.value("damage", 0.0f);
        weapon.power =  armamentRow.value("power", 0.0f);
        weapon.reload_time =  armamentRow.value("reload_time", 0.0f);
        weapon.ammo =  armamentRow.value("ammo", 0.0f);
        weapon.is_efficient_against_ships =  armamentRow.value("is_efficient_against_ships", false);
        weapon.is_efficient_against_submarine =  armamentRow.value("is_efficient_against_submarine", false);
        weapon.radius =  armamentRow.value("radius", 0.0f);
        weapon.cost =  armamentRow.value("cost", 0);
        vessel.armament.push_back(weapon);
      }
    }

    if (row.contains("operators") && row["operators"].is_array()) {
      for (const auto& operatorRow : row["operators"]) {
        const std::string name = operatorRow.value("name", std::string());
        if (name.empty()) continue;
        if (!vessel.navies.empty()) vessel.navies += ", ";
        vessel.navies += name;
      }
    }

    navalVessels_.push_back(std::move(vessel));
  }

  navalIndex_.clear();
  for (size_t index = 0; index < navalVessels_.size(); ++index) {
    navalIndex_[navalVessels_[index].id] = index;
  }

  return !navalVessels_.empty();
}

bool Catalog::loadCountryCodes(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  nlohmann::json rows;
  try {
    file >> rows;
  } catch (const std::exception&) {
    return false;
  }
  if (!rows.is_array()) return false;

  countryNames_.clear();
  for (const auto& row : rows) {
    std::string iso2 = row.value("country_iso2", std::string());
    const std::string name = row.value("country_name", std::string());
    if (iso2.empty() || name.empty()) continue;
    std::transform(iso2.begin(), iso2.end(), iso2.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    // A code can repeat (e.g. an old customs union); the first name wins so the plain
    // country reads before a historical variant.
    countryNames_.emplace(std::move(iso2), name);
  }

  return !countryNames_.empty();
}

bool Catalog::loadTrade(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  nlohmann::json data;
  try {
    file >> data;
  } catch (const std::exception&) {
    return false;
  }

  trade_.clear();
  std::unordered_map<std::string, TradeGood> best;  // world's biggest by product code

  for (auto it = data.begin(); it != data.end(); ++it) {
    CountryTrade country;
    if (it->contains("exports")) country.exports = readGoods((*it)["exports"]);
    if (it->contains("imports")) country.imports = readGoods((*it)["imports"]);
    sortByValue(country.exports);
    sortByValue(country.imports);

    for (const TradeGood& good : country.exports) {
      const auto found = best.find(good.productCode);
      if (found == best.end() || good.valueK > found->second.valueK) {
        best[good.productCode] = good;
      }
    }

    trade_[it.key()] = std::move(country);
  }

  worldExports_.clear();
  worldExports_.reserve(best.size());
  for (auto& entry : best) worldExports_.push_back(entry.second);
  sortByValue(worldExports_);

  return !trade_.empty();
}

const Port* Catalog::portById(int id) const {
  const auto found = portIndex_.find(id);
  return found == portIndex_.end() ? nullptr : &ports_[found->second];
}

Port* Catalog::portByIdM(int id) {
  const auto found = portIndex_.find(id);
  return found == portIndex_.end() ? nullptr : &ports_[found->second];
}

const Vessel* Catalog::vesselById(int id) const {
  const auto found = vesselIndex_.find(id);
  return found == vesselIndex_.end() ? nullptr : &vessels_[found->second];
}

const NavalVessel* Catalog::navalVesselById(int id) const {
  const auto found = navalIndex_.find(id);
  return found == navalIndex_.end() ? nullptr : &navalVessels_[found->second];
}

std::string Catalog::countryNameFor(const std::string& iso2) const {
  std::string key = iso2;
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  const auto found = countryNames_.find(key);
  return found == countryNames_.end() ? std::string() : found->second;
}

const CountryTrade* Catalog::tradeFor(const std::string& countryCode) const {
  const auto found = trade_.find(countryCode);
  return found == trade_.end() ? nullptr : &found->second;
}

std::vector<TradeGood> Catalog::exportedGoods(const std::string& countryCode) const {
  const CountryTrade* country = tradeFor(countryCode);
  if (country == nullptr) return {};
  std::vector<TradeGood> goods = country->exports;
  sortByValue(goods);
  return goods;
}

std::vector<TradeGood> Catalog::importedGoods(const std::string& countryCode) const {
  const CountryTrade* country = tradeFor(countryCode);
  if (country == nullptr) return {};
  std::vector<TradeGood> goods = country->imports;
  sortByValue(goods);
  return goods;
}

}  // namespace sea

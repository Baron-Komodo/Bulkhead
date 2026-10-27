#pragma once

// What a route can carry, ported from sea-builder/src/cargo.js.
//
// Cargo is the departure country's whole export list, sorted so the goods the arrival port
// actually imports come first — those are the ones that pay the reality bonus.

#include <string>
#include <vector>

#include "data.h"

namespace sea {

struct CargoOption {
  std::string productCode;
  std::string description;
  double valueK = 0.0;
  bool realTrade = false;
};

/// Whether a good is a real flow from one country to the other: the departure country
/// exports it and the arrival country imports it.
bool isRealTrade(const Catalog& catalog, const std::string& productCode,
                 const std::string& fromCountry, const std::string& toCountry);

/// Cargo for a route, most valuable load first, real flows lifted to the top.
std::vector<CargoOption> cargoOptions(const Catalog& catalog, const std::string& fromCountry,
                                      const std::string& toCountry);

/// How many of a route's goods are real trade flows.
int realTradeCount(const std::vector<CargoOption>& options);

}  // namespace sea

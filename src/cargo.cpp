#include "cargo.h"

#include <algorithm>

namespace sea {

bool isRealTrade(const Catalog& catalog, const std::string& productCode,
                 const std::string& fromCountry, const std::string& toCountry) {
  if (productCode.empty() || fromCountry.empty() || toCountry.empty()) return false;

  const std::vector<TradeGood> exports = catalog.exportedGoods(fromCountry);
  const std::vector<TradeGood> imports = catalog.importedGoods(toCountry);

  const bool exported = std::any_of(exports.begin(), exports.end(),
                                    [&](const TradeGood& good) { return good.productCode == productCode; });
  const bool imported = std::any_of(imports.begin(), imports.end(),
                                    [&](const TradeGood& good) { return good.productCode == productCode; });

  return exported && imported;
}

std::vector<CargoOption> cargoOptions(const Catalog& catalog, const std::string& fromCountry,
                                      const std::string& toCountry) {
  std::vector<TradeGood> goods = catalog.exportedGoods(fromCountry);
  if (goods.empty()) goods = catalog.worldExports();

  std::vector<CargoOption> options;
  options.reserve(goods.size());

  for (const TradeGood& good : goods) {
    CargoOption option;
    option.productCode = good.productCode;
    option.description = good.description;
    option.valueK = good.valueK;
    option.realTrade = isRealTrade(catalog, good.productCode, fromCountry, toCountry);
    options.push_back(std::move(option));
  }

  std::stable_sort(options.begin(), options.end(), [](const CargoOption& a, const CargoOption& b) {
    if (a.realTrade != b.realTrade) return a.realTrade;
    if (a.valueK != b.valueK) return a.valueK > b.valueK;
    return a.description < b.description;
  });

  return options;
}

int realTradeCount(const std::vector<CargoOption>& options) {
  return static_cast<int>(
      std::count_if(options.begin(), options.end(), [](const CargoOption& o) { return o.realTrade; }));
}

}  // namespace sea

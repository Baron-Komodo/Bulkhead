#include "voyage.h"

#include <algorithm>
#include <cmath>

#include "geo.h"

namespace sea {

double longHaulMultiplier(double distanceKm) {
  const double distance = std::isfinite(distanceKm) && distanceKm > 0 ? distanceKm : 0.0;
  if (distance <= kVoyageFullDistanceKm) return 1.0;

  const double span =
      (distance - kVoyageFullDistanceKm) / (kLongHaulFullDistanceKm - kVoyageFullDistanceKm);
  return 1.0 + kLongHaulBonusShare * std::min(1.0, span);
}

double hullPrice(double gt) {
  if (!std::isfinite(gt) || gt <= 0) return kMinHullPrice;
  return std::max(kMinHullPrice, std::round(gt / 100.0));
}

double portCost(const Port& port) {
  if (port.harborSize == "L") return 5000.0;
  if (port.harborSize == "M") return 3000.0;
  if (port.harborSize == "S") return 1000.0;
  if (port.harborSize == "V") return 500.0;
  return 1000.0;
}

double cargoBonus(double cargoValueK) {
  if (!std::isfinite(cargoValueK) || cargoValueK <= 0) return 0.0;
  return std::min(kCargoBonusCap, std::round(std::sqrt(cargoValueK)));
}

VoyageOutcome resolveVoyage(double cargoValueK, double distanceKm, const Research& research,
                            double lossRoll, bool realTrade) {
  VoyageOutcome outcome;

  const double distance = std::isfinite(distanceKm) ? std::max(0.0, distanceKm) : 0.0;
  const double longHaul = longHaulMultiplier(distance);

  const double freight = std::round(distance * kFreightPerKm * longHaul) +
                         std::round(cargoBonus(cargoValueK));
  const double bonus = realTrade ? std::round(freight * kRealTradeBonusShare) : 0.0;
  const double gross = std::round((freight + bonus) * rewardMultiplier(research));
  const bool lost = std::isfinite(lossRoll) && lossRoll < lossRiskPercent(research);
  const double lostAmount = lost ? std::round(gross * kVoyageLossShare) : 0.0;

  outcome.distanceKm = distance;
  outcome.durationMs = voyageDurationMs(distance, voyageDurationMultiplier(research));
  outcome.realTrade = realTrade;
  outcome.longHaul = longHaul;
  outcome.freight = freight;
  outcome.bonus = bonus;
  outcome.gross = gross;
  outcome.lost = lost;
  outcome.lostAmount = lostAmount;
  outcome.points = researchPointsForVoyage(distance);
  outcome.reward = gross - lostAmount;

  return outcome;
}

}  // namespace sea

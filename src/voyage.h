#pragma once

// The voyage economy, ported from sea-builder/src/voyage.js. Pure: the caller supplies
// the dice.

#include "data.h"
#include "research.h"

namespace sea {

constexpr double kFreightPerKm = 0.6;
constexpr double kCargoBonusCap = 400.0;
constexpr double kRealTradeBonusShare = 0.05;
constexpr double kLongHaulBonusShare = 0.25;
constexpr double kLongHaulFullDistanceKm = 20000.0;
constexpr double kVoyageLossShare = 0.25;
constexpr double kMinHullPrice = 250.0;

/// The multiplier on a route's freight by length.
double longHaulMultiplier(double distanceKm);

/// Price of a hull, from its gross tonnage.
double hullPrice(double gt);

/// What it costs to claim a port.
double portCost(const Port& port);

/// The cargo's own contribution: the square root of the country's trade in that good.
double cargoBonus(double cargoValueK);

/// Settles a completed voyage.
struct VoyageOutcome {
  double distanceKm = 0.0;
  double durationMs = 0.0;
  bool realTrade = false;
  double longHaul = 1.0;
  double freight = 0.0;     // freight plus cargo bonus, before bonuses and research
  double bonus = 0.0;       // reality bonus
  double gross = 0.0;
  bool lost = false;
  double lostAmount = 0.0;
  int points = 0;
  double reward = 0.0;
};

/// `lossRoll` is a value in 0..100; cargo is hit when it comes in under the loss risk.
VoyageOutcome resolveVoyage(double cargoValueK, double distanceKm, const Research& research,
                            double lossRoll, bool realTrade);

}  // namespace sea

#include "combat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace sea {
namespace {

std::string lowerType(const std::string& value) {
  std::string lowered = value;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowered;
}

/// Whether `word` starts at `at` in already-lowered text.
bool startsWithToken(const std::string& text, size_t at, const char* word) {
  const size_t length = std::strlen(word);
  return at + length <= text.size() && text.compare(at, length, word) == 0;
}

/// The first shell size written in a piece of catalog text. A number only counts when a calibre
/// unit follows it - "mm", "in", a quote for inches, or "cal" - so the model number of a mount
/// ("Mk.137", "AK-176") is not mistaken for its bore. A size outside 1..600 mm is a year, a
/// range or a crew figure, not a gun.
float calibreInText(const std::string& raw) {
  const std::string text = lowerType(raw);
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    // ".50 cal" starts with the dot; every other size starts with a digit.
    const bool leadingDot = c == '.' && i + 1 < text.size() &&
                            std::isdigit(static_cast<unsigned char>(text[i + 1]));
    if (!std::isdigit(c) && !leadingDot) continue;

    const char* begin = text.c_str() + i;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin || value <= 0.0) continue;

    size_t cursor = static_cast<size_t>(end - text.c_str());
    while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '-')) ++cursor;

    double millimetres = 0.0;
    if (startsWithToken(text, cursor, "mm")) {
      millimetres = value;
    } else if (startsWithToken(text, cursor, "in") || startsWithToken(text, cursor, "cal") ||
               (cursor < text.size() && text[cursor] == '"')) {
      millimetres = value * 25.4;  // inches and hundredths of an inch
    }
    if (millimetres >= 1.0 && millimetres <= 600.0) return static_cast<float>(millimetres);

    // No unit after this number: step past it and keep looking, so "Mk.137 SRBOC / 12.7 mm"
    // still finds the calibre written later in the line.
    i = cursor;
  }
  return 0.0f;
}

/// Real-world cadence of a projectile mount by bore, before the game's compression. The curve is
/// the classic trade: a small cannon chatters, a heavy turret thumps. A 30 mm round every two
/// seconds - what the scraped flat "5 s" gave every gun - is what this puts right.
float gunRateForCalibre(float millimetres) {
  static const struct {
    float upTo;
    float rpm;
  } table[] = {
      {10.0f, 900.0f},  {15.0f, 700.0f},  {20.0f, 550.0f}, {25.0f, 420.0f},
      {30.0f, 320.0f},  {40.0f, 200.0f},  {57.0f, 130.0f}, {76.0f, 90.0f},
      {100.0f, 60.0f},  {130.0f, 35.0f},  {160.0f, 22.0f}, {203.0f, 12.0f},
      {460.0f, 8.0f},
  };
  for (const auto& row : table) {
    if (millimetres <= row.upTo) return row.rpm;
  }
  return 8.0f;
}

/// The tempo a mount of this kind is rated at when its own text cannot place it. The figures are
/// the real-world pacing each family was tuned around, so a calibre-less gun or a launcher still
/// fires at the right cadence. `reload_time` is deliberately not used here: it is the time the
/// crew needs to change a magazine, not the time between two rounds.
float fallbackRateFor(Delivery delivery) {
  switch (delivery) {
    case Delivery::MachineGun: return 300.0f;
    case Delivery::Gun: return 12.0f;
    case Delivery::Rocket: return 6.0f;
    case Delivery::Torpedo: return 2.0f;
    case Delivery::Missile: return 1.0f;
  }
  return 1.0f;
}

}  // namespace

Delivery deliveryFor(const Armament& weapon) {
  const std::string type = lowerType(weapon.type);
  if (type.find("torpedo") != std::string::npos) return Delivery::Torpedo;
  if (type.find("missile") != std::string::npos) return Delivery::Missile;
  if (type.find("rocket") != std::string::npos) return Delivery::Rocket;
  if (type.find("machine") != std::string::npos || type.find("mg") != std::string::npos ||
      type.find("ciws") != std::string::npos) {
    return Delivery::MachineGun;
  }
  return Delivery::Gun;
}

const char* deliveryName(Delivery delivery) {
  switch (delivery) {
    case Delivery::Gun: return "Gun";
    case Delivery::MachineGun: return "Machine gun";
    case Delivery::Rocket: return "Rocket";
    case Delivery::Missile: return "Missile";
    case Delivery::Torpedo: return "Torpedo";
  }
  return "Gun";
}

bool isGuided(Delivery delivery) {
  return delivery == Delivery::Rocket || delivery == Delivery::Missile ||
         delivery == Delivery::Torpedo;
}

unsigned fireBit(FireCategory category) {
  switch (category) {
    case FireCategory::light:
      return kFireLight;
    case FireCategory::medium:
      return kFireMedium;
    case FireCategory::heavy:
      return kFireHeavy;
  }
  return kFireNone;
}

bool allowsCategory(unsigned order, FireCategory category) {
  if (order & kFireAuto) return true;
  return (order & fireBit(category)) != 0u;
}

std::string fireOrderName(unsigned order) {
  if (order == 0u) return "None";
  if (order & kFireAuto) return "Auto";

  std::string name;
  auto add = [&](const char* word) {
    if (!name.empty()) name += " + ";
    name += word;
  };
  if (order & kFireLight) add("Light");
  if (order & kFireMedium) add("Medium");
  if (order & kFireHeavy) add("Heavy");
  return name.empty() ? "None" : name;
}

float damageAfterDefense(float damage, float defense) {
  if (damage <= 0.0f) return 0.0f;
  const float mitigation =
      std::min(kMaxMitigation, std::max(0.0f, defense) / (std::max(0.0f, defense) + kDefenseSoak));
  return damage * (1.0f - mitigation);
}

float weaponRangeKm(const Armament& weapon) {
  // The scraped radius is nautical miles; one of those is 1.852 km.
  const float km = std::max(0.0f, weapon.radius) * 1.852f * kRangeScale;
  // A mount with no recorded radius still has to be able to shoot: give it a short reach
  // rather than leaving it as a decoration on the deck.
  return std::min(kMaxEngagementKm, km > 1.0f ? km : 40.0f);
}

float weaponCalibreMm(const Armament& weapon) {
  if (const float fromName = calibreInText(weapon.name)) return fromName;
  return calibreInText(weapon.description);
}

float fireRateRoundsPerMinute(const Armament& weapon) {
  const Delivery delivery = deliveryFor(weapon);
  const float calibre = weaponCalibreMm(weapon);

  // A launcher is not a gun: its cadence is how fast a hoist or a tube crew can cycle, not how
  // long a magazine change takes, so it keeps its family's own tempo. Only the guns, where one
  // flat load time once stood in for every bore from 12.7 mm to 16-inch, are re-rated by bore.
  if (delivery != Delivery::Gun && delivery != Delivery::MachineGun) {
    return std::max(0.5f, fallbackRateFor(delivery));
  }

  float rpm = calibre > 0.0f ? gunRateForCalibre(calibre) : fallbackRateFor(delivery);
  // A machine-gun mount is the lightest thing the rule can place: a pintle-sized weapon throws
  // its rounds faster than a cannon of the same bore.
  if (delivery == Delivery::MachineGun) rpm *= 1.5f;
  rpm *= kFireRateScale;

  return std::max(0.5f, std::min(3600.0f, rpm));
}

float mountReloadSeconds(const Armament& weapon) {
  return std::max(0.05f, 60.0f / fireRateRoundsPerMinute(weapon));
}

float engagementRangeKm(const std::vector<Armament>& mounts, unsigned order) {
  float best = 0.0f;
  for (size_t i = 0; i < mounts.size(); ++i) {
    const Armament& weapon = mounts[i];
    if (!allowsCategory(order, weapon.category)) continue;
    best = std::max(best, weaponRangeKm(weapon));
  }
  return best;
}

float shotsToKill(const Armament& weapon, float health, float defense) {
  if (weapon.damage <= 0.0f) return std::numeric_limits<float>::infinity();
  // The catalog's damage numbers are comparison figures, so they go through the same scale
  // the impact resolver uses; otherwise a mount is ranked as if it were eight times weaker
  // than it really is.
  const float perShot = weapon.damage * kDamageScale;
  const float actualDamage = damageAfterDefense(perShot, defense);
  if (actualDamage <= 0.0f) return std::numeric_limits<float>::infinity();
  const float shots = std::ceil(std::max(0.0f, health) / actualDamage);
  return std::max(1.0f, shots);
}

float timeToKill(const Armament& weapon, float enemyHealth, float defense) {
  if (weapon.damage <= 0.0f) return std::numeric_limits<float>::max();

  const float shots = shotsToKill(weapon, enemyHealth, defense);
  const float reload = mountReloadSeconds(weapon);

  // The mount fires its magazine dry at the mount's own cadence, changes it, and carries on, so
  // rank it by the full job: every round, plus one magazine change for each magazine it empties
  // before the shot that finishes the target.
  const float spent = shots * reload;
  if (weapon.ammo <= 0.0f) return spent;
  const float magazines = std::ceil(shots / weapon.ammo);
  return spent + (magazines - 1.0f) * std::max(0.0f, weapon.reload_time);
}

bool effectiveAgainst(const Armament& weapon, bool enemyIsSubmarine) {
  return enemyIsSubmarine ? weapon.is_efficient_against_submarine
                          : weapon.is_efficient_against_ships;
}

bool roundsAffordable(const Armament& weapon, double money) {
  return weapon.cost <= 0 || money >= static_cast<double>(weapon.cost);
}

/// How far a port's battery reaches, in the scraped nautical-mile scale. A harbour's guns cover
/// the approaches to the harbour, not the whole basin: at kRangeScale this is about 220 km, so a
/// raider has to close in before the battery opens up. The old 100 nm clamped to
/// kMaxEngagementKm and let a single claimed port dominate an entire sea.
constexpr float kPortBatteryRangeNm = 8.0f;

std::vector<Armament> portArmamentCatalog() {
  return {
      Armament{1, "advanced_torpedos", "torpedos", medium,
               "Mod 2 torpedos are lightweight but powerful torpedos used in submarines",
               25.0f, 12.0f, 15.0f, 1, false, true, kPortBatteryRangeNm, 100},
      // A machine gun's magazine is its belt, not a single round: with the catalog's one-shot
      // magazine the mount would spend fifteen seconds reloading after every burst, which is
      // no way for a harbour to defend itself.
      Armament{2, "machine_guns", "machine gun", light,
               "Machine guns are doing consistently fast but low damage to structures",
               25.0f, 12.0f, 15.0f, 200, false, true, kPortBatteryRangeNm, 100},
  };
}

Armament basicPortArmament() {
  for (const Armament& weapon : portArmamentCatalog()) {
    if (deliveryFor(weapon) == Delivery::MachineGun) return weapon;
  }
  return portArmamentCatalog().front();
}

// A neutral harbour's own guns. The scraped `attack` rating (10 to about 200) is the only
// figure the data gives a port to shoot with, so it is turned into two numbers: how far the
// battery reaches and how hard one round of it lands. A major base therefore covers more water
// and bites harder than a fishing quay, and the fight reads differently from port to port.

/// Reach the battery starts from, in nautical miles, before the port's rating stretches it.
/// The catalogue's radius is nautical miles, so this is about 220 km once weaponRangeKm is
/// done with it - the same waters a claimed port's machine gun already covers.
constexpr float kHarbourBaseReachNm = 8.0f;
/// Reach one point of the port's `attack` rating adds, in nautical miles.
constexpr float kHarbourReachPerAttackNm = 0.05f;
/// The longest reach any port's battery is granted, so a data outlier cannot cover an ocean.
constexpr float kHarbourMaxReachNm = 40.0f;
/// Share of the port's `attack` rating one round of the battery carries. Taken whole the
/// rating would out-shoot every hull afloat several times over; this leaves a harbour able to
/// hurt a squadron without drowning it.
constexpr float kHarbourRoundDamageShare = 0.08f;
/// Rounds in one shore mount's ready rack before the crew has to restock it.
constexpr float kHarbourMagazine = 400.0f;

std::vector<Armament> harbourBattery(int attack) {
  const float rating = static_cast<float>(std::max(1, std::min(attack, 400)));

  Armament gun = basicPortArmament();
  gun.name = "harbour battery";
  gun.damage = std::max(2.0f, rating * kHarbourRoundDamageShare);
  gun.power = rating;
  gun.ammo = kHarbourMagazine;
  gun.radius = std::min(kHarbourMaxReachNm, kHarbourBaseReachNm + rating * kHarbourReachPerAttackNm);
  // A shore battery shoots at hulls; it carries no torpedo tubes and no depth charges.
  gun.is_efficient_against_ships = true;
  gun.is_efficient_against_submarine = false;

  return {gun, gun};
}

std::vector<size_t> firingPlanIndices(const std::vector<Armament>& mounts, unsigned order,
                                      float enemyHealth, bool enemyIsSubmarine, float defense) {
  std::vector<size_t> plan;
  for (size_t i = 0; i < mounts.size(); ++i) {
    if (allowsCategory(order, mounts[i].category)) plan.push_back(i);
  }

  if (order & kFireAuto) {
    // Auto works the whole battery. The mount that finishes this target soonest speaks first;
    // a mount that cannot hurt this kind of hull is kept at the back of the queue instead of
    // being dropped, so a submarine still gets shot at by the guns once the torpedoes are
    // empty rather than the hull falling silent.
    std::stable_sort(plan.begin(), plan.end(), [&](size_t a, size_t b) {
      const bool effectiveA = effectiveAgainst(mounts[a], enemyIsSubmarine);
      const bool effectiveB = effectiveAgainst(mounts[b], enemyIsSubmarine);
      if (effectiveA != effectiveB) return effectiveA;
      return timeToKill(mounts[a], enemyHealth, defense) <
             timeToKill(mounts[b], enemyHealth, defense);
    });
    return plan;
  }

  // A named order is a narrower brief: only the mounts that can actually hurt this kind of
  // hull are kept, in the class's own mount order.
  plan.erase(std::remove_if(plan.begin(), plan.end(),
                            [&](size_t index) {
                              return !effectiveAgainst(mounts[index], enemyIsSubmarine);
                            }),
             plan.end());
  return plan;
}

std::vector<size_t> firingPlanIndices(const NavalVessel& vessel, unsigned order,
                                      float enemyHealth, bool enemyIsSubmarine, float defense) {
  return firingPlanIndices(vessel.armament, order, enemyHealth, enemyIsSubmarine, defense);
}

}  // namespace sea

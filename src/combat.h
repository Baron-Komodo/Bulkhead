#pragma once

// The combat rules: which of a class's mounts a firing order may use, how far they reach and
// how long they need to put a target down. Pure data - no game state and no rendering - so
// the rules can be exercised headless and the view never has to know them.

#include <string>
#include <vector>

#include "data.h"

namespace sea {

/// A firing order: any combination of the three weapon categories, or Auto, which lets the
/// gunnery officer pick the mounts that will finish the target fastest. Held as a bit set so
/// "light + heavy" is a single value.
enum FireOrder : unsigned {
  kFireNone = 0u,
  kFireLight = 1u << 0,
  kFireMedium = 1u << 1,
  kFireHeavy = 1u << 2,
  kFireAuto = 1u << 3,
  kFireAll = kFireLight | kFireMedium | kFireHeavy,
};

/// How a mount delivers its damage. The catalog spells this out in words ("gun", "machine
/// gun", "rocket", "missile", "torpedo"); the game and the view need it as a value so a
/// torpedo can run under the surface while a rocket leaves a smoke trail.
enum class Delivery { Gun, MachineGun, Rocket, Missile, Torpedo };

/// The delivery a mount uses, read from its catalog `type`. Unknown text falls back to Gun;
/// a torpedo or a rocket is never silently drawn as a cannon shell.
Delivery deliveryFor(const Armament& weapon);

/// The delivery as it reads in the interface ("Torpedo", "Rocket", ...).
const char* deliveryName(Delivery delivery);

/// Whether the delivery is a guided weapon that leaves the tube and runs to the target, as
/// opposed to a gun whose round is already at full speed when it leaves the muzzle.
bool isGuided(Delivery delivery);

/// The order bit for one weapon category.
unsigned fireBit(FireCategory category);

/// Whether a firing order allows a category: Auto allows all three, a named order only what
/// its bits set.
bool allowsCategory(unsigned order, FireCategory category);

/// The order as it reads on a button ("Light", "Heavy + Light", "Auto", "None").
std::string fireOrderName(unsigned order);

// --- Tuning -----------------------------------------------------------------
// The scraped stats were authored as comparison figures, not as a damage model: a 3000 t
// corvette carries health 6000 and defense 1500 while a 30 mm round does 60. Taken literally,
// every hull would be impregnable and every duel would run for minutes. These constants are
// the only shaping the numbers need to behave like an RTS fight: a patrol boat pops in a
// couple of seconds, a frigate trades blows for half a minute.

/// Multiplier applied to a weapon's damage before the target's defense soaks its share.
constexpr float kDamageScale = 6.0f;
/// The defense value at which incoming damage is halved.
constexpr float kDefenseSoak = 3000.0f;
/// The most of a hit any defense can cancel, so a strike always bites.
constexpr float kMaxMitigation = 0.70f;

/// The damage that survives the target's defense.
float damageAfterDefense(float damage, float defense);

/// The range, in game kilometres, at which a mount opens fire. The scraped `radius` is in
/// nautical miles; it is stretched so the categories separate on the map - a machine gun
/// fights at boarding range, a missile halfway across the basin.
constexpr float kRangeScale = 15.0f;
/// Nothing is engaged beyond this, however long a mount's nominal range is.
constexpr float kMaxEngagementKm = 2500.0f;

/// A mount's reach, in game kilometres.
float weaponRangeKm(const Armament& weapon);

// --- Rate of fire -----------------------------------------------------------
// The catalog gives every mount of a kind the same `reload_time` - every gun is 5 s, whether it
// is a 30 mm cannon or a 5-inch turret. That figure is the time to change a magazine, not the
// time between two rounds, so the rate is derived from the mount itself: its calibre first,
// then the kind of mount it is. The result is real-world cadence, compressed into game time by
// kFireRateScale the way kRangeScale stretches real ranges.

/// Gun fire rates are real-world figures scaled like the ranges: a 76 mm turret really cycles
/// about 90 rounds a minute, which at this map's pace would empty its magazine and end a duel in
/// seconds. The factor keeps every mount's cadence relative to the others - a 30 mm cannon still
/// fires about six times as fast as a 5-inch - while the fight lasts long enough to steer.
constexpr float kFireRateScale = 0.35f;

/// The shell size a mount's own text gives, in millimetres: "30mm", "12.7 mm", "5 in",
/// "5\"/54 Mk 45", ".50 cal". Returns 0 when the text names no calibre - plenty of scraped
/// names lost the number ("mm AK-176 gun") and have to fall back on the type alone.
float weaponCalibreMm(const Armament& weapon);

/// Rounds per minute the mount puts out. A gun is rated from its bore and put through
/// kFireRateScale; a calibre-less gun falls back on its kind's own tempo, so a weapon the rule
/// cannot place still fires at a sensible rate. Rockets, torpedoes and missiles keep their
/// launcher tempo unchanged: their cadence is the hoist, not the magazine change.
float fireRateRoundsPerMinute(const Armament& weapon);

/// The seconds between two rounds from this mount: 60 / fireRateRoundsPerMinute, floored so a
/// mount is never an infinite stream.
float mountReloadSeconds(const Armament& weapon);

/// The furthest reach of any mount a firing order may use, or 0 when the class has none.
float engagementRangeKm(const std::vector<Armament>& mounts, unsigned order);

/// Shots a mount needs to empty `health` away, honouring the magazine. Less than 1 for a
/// single-shot killer.
float shotsToKill(const Armament& weapon, float health, float defense);

/// Seconds `weapon` needs to remove `enemyHealth`: shots x the mount's cadence, plus a magazine
/// change for every magazine fired dry before the killing shot. Slow-firing hard hitters come
/// out low, which is what Auto sorts on. The cadence is the mount's own derived one; the
/// magazine change is the catalog's `reload_time`.
float timeToKill(const Armament& weapon, float enemyHealth, float defense);

/// Whether a mount means anything against a target: the scraped `is_efficient_against_*`
/// flags decide. A weapon flagged only against submarines is wasted on a surface hull, and
/// the other way round.
bool effectiveAgainst(const Armament& weapon, bool enemyIsSubmarine);

/// Whether the treasury can pay for one round of the mount. A mount with no cost per round
/// always can, so a fleet with an empty exchequer still fires what it has for free.
bool roundsAffordable(const Armament& weapon, double money);

// --- Port batteries ----------------------------------------------------------
// A port is a fixed installation rather than a hull, but it shoots with the same rules: its
// fitted mounts answer raiders that come inside their reach. These helpers live here so the
// armament screen and the game agree on what a port can mount.

/// The weapons a port can mount: the coastal battery the armament screen offers. Each one is
/// gated behind the research technology of the same id, so a weapon appears armed in the list
/// only once its research is acquired.
std::vector<Armament> portArmamentCatalog();

/// The mount a port carries before the player arms it: one basic machine gun, so a claimed
/// harbour can answer a raider instead of standing bare.
Armament basicPortArmament();

/// The battery a harbour mans for its own defence before it belongs to anybody: its scraped
/// `attack` rating turned into gun mounts, so a neutral port fights for itself when a squadron
/// comes to take it. Two mounts are returned - one either side of the mole - so the battery
/// keeps talking while one of them changes its magazine.
std::vector<Armament> harbourBattery(int attack);

/// The ordered indices of the mounts a firing order may use against a target.
///
/// A named order keeps the class's own mount order and drops a category it does not allow, and
/// drops mounts that cannot hurt this kind of hull. An `order` of Auto keeps the whole battery:
/// the mount that finishes the job soonest speaks first, mounts that cannot hurt this kind of
/// hull go last rather than being dropped. That way a torpedo tube on reload never silences
/// the gun beside it - the gunnery officer works down the plan until a mount is ready.
std::vector<size_t> firingPlanIndices(const std::vector<Armament>& mounts, unsigned order,
                                      float enemyHealth, bool enemyIsSubmarine, float defense);

/// firingPlanIndices against a class's own armament.
std::vector<size_t> firingPlanIndices(const NavalVessel& vessel, unsigned order,
                                      float enemyHealth, bool enemyIsSubmarine, float defense);

}  // namespace sea

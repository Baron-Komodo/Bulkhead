#pragma once

// The single-player game state, ported from the parts of UI.js that used to live in React
// state and the backend: money, claimed ports, the fleet, voyages under way, and research.
// Persistence is a local JSON save file instead of the HTTP API.

#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cargo.h"
#include "combat.h"
#include "data.h"
#include "geo.h"
#include "management.h"
#include "research.h"
#include "sea_route.h"
#include "voyage.h"

namespace sea {

constexpr double kBaseMoney = 5000.0;
constexpr int kStarterVesselId = 201;
/// The one harbour the player is handed at the start of a game, so the shipyard, the navigation
/// panel and the treasury have somewhere to work from before anything is taken by force.
/// Fixed for now: Nantes, France (World Port Index row 853).
constexpr int kStarterPortId = 853;
/// A harbour's defence pool, in hit points, as a multiple of its scraped `health` rating. Those
/// scraped figures are comparison values (100 to 990), far below a warship's hull points, so
/// the pool is scaled up: a port is a fortification a squadron is sent to break, not a target
/// one lucky salvo finishes.
constexpr double kPortDefenceScale = 20.0;

// The rival powers' clocks and purse. They earn the player's own per-port income and run the
// player's own voyage economy, so there is no hidden handicap in either direction: a power
// that holds more ports simply earns more, and one that loses its harbours is in trouble.
constexpr double kFactionTradeIntervalSeconds = 25.0;      // between merchant dispatches
constexpr double kFactionBuildIntervalSeconds = 30.0;      // between attempts at a new hull
constexpr double kFactionResearchIntervalSeconds = 30.0;   // between research purchases
/// Money a power keeps back before it lays a hull down.
constexpr double kFactionReserveMoney = 1500.0;
/// How far a rival's merchant crossings run: short hops pay too little to be worth a berth,
/// and a power does not sail the other side of the world while it is still small.
constexpr double kMerchantMinRunKm = 700.0;
constexpr double kMerchantMaxRunKm = 9000.0;
/// The cargo a crossing carries when its country's trade list has nothing for it.
constexpr double kMerchantFallbackCargoK = 8000.0;
// Passive income, in money per second, granted for each port the player owns. It scales only
// with the number of claimed ports and never with the current treasury.
constexpr double kPassiveIncomePerPortPerSecond = 25.0;

/// One vessel under way: the course it is sailing and how far along it is.
struct ActiveVoyage {
  std::string id;
  int fromPortId = 0;
  int toPortId = 0;
  int vesselId = 0;
  CargoOption cargo;
  double distanceKm = 0.0;
  double durationMs = 0.0;
  bool realTrade = false;
  std::vector<LatLng> route;  // port to port, as drawn
  double startedAtMs = 0.0;   // game time the crossing began
  double progress = 0.0;      // 0..100
};

/// A one-glance summary of one player fleet, for the Fleet Command screen. The counts are
/// live: a hull added to the fleet, sunk in action or sent to sea is reflected immediately.
struct FleetRecap {
  int fleetId = 0;
  std::string name;
  int admiralId = 0;
  int shipCount = 0;
  int atSea = 0;
  int engaged = 0;
  int docked = 0;
  int damaged = 0;
  int sunk = 0;
  int capacity = 0;
  float health = 0.0f;
  float maxHealth = 0.0f;
  float power = 0.0f;
};

/// Live status of one owned vessel, for the fleet screens.
struct FleetEntry {
  const Vessel* vessel = nullptr;
  const ActiveVoyage* voyage = nullptr;
  bool busy = false;
  const Port* toPort = nullptr;
  double etaMs = 0.0;
};

/// What the player has bought on one owned port: its level (which sets how many module slots
/// it has) and the module ids fitted into those slots.
struct PortUpgrade {
  int level = 1;
  std::vector<int> moduleIds;
};

/// Which pre-rendered hull sprite a naval class draws with. The map has one 3D-derived sprite
/// sheet per kind, so a class only needs to name its kind.
enum class NavalModelKind {  patrol, frigate, corvette, destroyer, aircraft_carrier, cruiser, submarine, other };

/// The sprite kind a scraped naval class uses, decided from its type string ("Submarine",
/// "Destroyer", "Patrol vessel", ...).
NavalModelKind navalModelKindFor(const NavalVessel& vessel);

/// The build cost of a naval class, in money. A class laid down in its own nation's yard is
/// cheaper, mirroring the shipyard's NATION BONUS row.
double navalUnitCost(const NavalVessel& vessel, bool sameNation);

/// The live state of one weapon mount on a hull: rounds left in its current magazine and the
/// seconds still to run on its reload. `ammo` starts at the catalog's magazine size, falls by
/// one per shot, and returns to full after a magazine change. The catalog's Armament is the
/// mount's specification; this is what a particular ship has left of it.
struct MountState {
  float ammo = 0.0f;
  float reload = 0.0f;
};

/// A ship's standing posture. Offensive ships fire on anything that comes inside their own
/// weapon range without being told to; defensive ships hold fire until they are hit and then
/// return fire only at their attacker; aggressive ships chase and engage the nearest enemy
/// boat as soon as it enters their patrol radius.
enum Posture : int {
  kPostureOffensive = 0,
  kPostureDefensive = 1,
  kPostureAggressive = 2,
};

/// One player-owned warship or submarine on the world map: built at a port's shipyard and
/// then steered around the map like an RTS unit. The game state owns where it is, where it is
/// going and what it is shooting at; the UI owns which of them are selected.
struct NavalUnit {
  int id = 0;
  int classId = 0;      // NavalVessel::id this unit was laid down from
  std::string className;
  std::string type;
  NavalModelKind kind = NavalModelKind::other;
  int homePortId = 0;
  LatLng position;
  double headingDeg = 0.0;          // facing, degrees clockwise from north
  double speedKmPerSecond = 220.0;  // RTS pace across the globe
  std::vector<LatLng> path;         // remaining waypoints, empty when stopped

  // Combat. Health starts at the class's own figure and only falls; the mounts array runs
  // parallel to the class's armament. fireOrder is the standing order the panel's row sets.
  float maxHealth = 0.0f;
  float health = 0.0f;
  float defense = 0.0f;
  unsigned fireOrder = kFireAuto;
  int targetBoatId = 0;      // boat being engaged, 0 for none
  int targetPortId = 0;      // harbour being besieged, 0 for none
  bool targetExplicit = false;  // true when the player ordered the attack, not the posture
  double repathTimer = 0.0;  // seconds until a fresh course may be plotted at a moving target
  int posture = kPostureOffensive;
  int lastAttackerBoatId = 0;   // the boat that last hit us, for defensive return fire
  float underFireTimer = 0.0f;  // seconds left in which a defensive ship still returns fire
  std::vector<MountState> mounts;

  // Repair order. A damaged hull alongside one of the player's ports is patched in steps: the
  // timer runs down and every step the crew welds a slice of hit points back, so a repair takes
  // time instead of landing in the frame the button is pressed. healPortId is the quay doing
  // the work, 0 when no repair is under way.
  int healPortId = 0;
  float healTimer = 0.0f;  // seconds until the next step of the repair

  bool moving() const { return !path.empty(); }
  bool sunk() const { return health <= 0.0f; }
  bool healing() const { return healPortId != 0; }
};

/// How far a bot hull notices one of the player's ships, from its own longest reach. A corsair
/// reacts to the traffic in its own stretch of water, not to a hull on the far side of the sea;
/// getting shot at is the other trigger, and that one ignores this radius.
double botHuntRadiusKm(double reachKm);

/// A warship belonging to a rival power: the same kind of hull the player commands, but owned
/// by another faction rather than the player. It is not part of the player's fleet, so it
/// cannot be selected or ordered. It does fight on its own terms: when one of the player's
/// ships comes into its own waters, or shoots at it, it hunts that ship, steams to the edge of
/// its own weapon range and shoots from there, so it is never a target that only shoots once
/// the player has run it down. A rival's hull is paid for out of that power's treasury and
/// laid down at its own yard, so sinking one costs it money and a berth, not a respawn.
struct BotBoat {
  int id = 0;
  std::string name;   // the hull's name
  std::string owner;  // the rival power that owns it, shown on the map
  int classId = 0;    // NavalVessel the hull was laid down from, 0 for a kind's own preset
  int homePortId = 0; // the yard that built it and where it tops its magazines up
  LatLng position;
  double headingDeg = 0.0;                          // facing, degrees clockwise from north
  NavalModelKind kind = NavalModelKind::other;  // which baked hull sprite to draw

  // Steering. A bot closes on the ship it is hunting until it is inside its own reach and
  // then holds station there, which is what keeps the fight at arm's length.
  double speedKmPerSecond = 220.0;  // the same RTS pace the player's hulls use
  std::vector<LatLng> path;         // remaining waypoints, empty when holding station
  int targetUnitId = 0;             // the player's ship this hull is hunting, 0 for none
  bool retaliating = false;         // hunting because it was shot at, not because it was close
  double repathTimer = 0.0;         // seconds until a fresh course may be plotted
  bool evading = false;             // backing off to reopen the range, not closing

  float maxHealth = 0.0f;
  float health = 0.0f;
  float defense = 0.0f;
  std::vector<Armament> armament;
  std::vector<MountState> mounts;

  bool moving() const { return !path.empty(); }
  bool sunk() const { return health <= 0.0f; }
};

/// A shell, rocket or torpedo in flight. The projectile is simulated by the game rather than
/// faked by the view, so damage lands where and when the round does, not the instant the
/// order is given. It homes on its target's live position, which is what lets a ship that is
/// under way be hit and what makes a shell visibly chase a turning hull.
struct Projectile {
  int id = 0;  // view-only identity, so a trail can be paced per round
  FireCategory category = FireCategory::light;
  Delivery delivery = Delivery::Gun;  // shell, machine-gun tracer, rocket, missile or torpedo
  LatLng from;
  LatLng position;
  LatLng target;         // where it is heading; refreshed from the target each step
  int sourceUnitId = 0;  // the player's ship that fired, 0 for a bot or a port
  int sourceBoatId = 0;  // the bot that fired, 0 for a player's ship or a port
  int sourcePortId = 0;  // the player's port battery that fired, 0 otherwise
  int targetUnitId = 0;  // the player's ship being hit, 0 for a boat
  int targetBoatId = 0;  // the boat being hit, 0 for a player's ship
  int targetPortId = 0;  // the harbour being bombarded, 0 unless a port is the target
  float damage = 0.0f;
  double speedKmPerSecond = 0.0;
};

/// A moment of a fight handed to the view to play back: the report of a gun, a hit, or a hull
/// going down. The game decides what happened; the view decides what it looks like.
struct CombatEvent {
  enum class Kind { Muzzle, Impact, Kill };
  Kind kind = Kind::Muzzle;
  LatLng at;
  FireCategory category = FireCategory::light;
  Delivery delivery = Delivery::Gun;
  int mountIndex = 0;  // which mount fired, so a broadside fans out instead of stacking
  int entityId = 0;    // the unit or boat the event is about
  bool entityIsBoat = false;
  NavalModelKind hullKind = NavalModelKind::other;  // hull sprite, so a wreck can be drawn
  double headingDeg = 0.0;
  bool onWater = false;  // the round fell short / hit the sea rather than a deck

  // Kill events only: who sank whom, so the HUD can print a kill feed without having to
  // search the fleet after the fact. A player's hull is an ally; a bot boat is an enemy.
  std::string attackerName;
  std::string victimName;
  bool attackerIsAlly = false;
  bool victimIsAlly = false;
};

  struct Anchor {
    const char* name;
    const char* owner;
    LatLng position;
    double heading;
    NavalModelKind kind;
  };

/// One pirate faction: the name its hulls fly and the anchor points they are placed at when a
/// game starts. The list is fixed data, but the commander reads it too, so a faction that has
/// been wiped out can be rebuilt from the same anchors it began at.
struct BotFaction {
  std::string name;
  std::vector<Anchor> anchors;
};

const std::vector<BotFaction>& botFactions();

// --- Rival powers ------------------------------------------------------------
// An opponent is a player, not a swarm: it owns ports, earns money from them, runs merchant
// crossings that pay freight and research points, lays hulls down at its own yard and spends
// those points on a research programme. It is seeded from the faction roster above - the same
// three powers, each with waters of its own - but from then on it lives off its economy, so
// sinking its ships and taking its harbours hurts it the way it would hurt the player.

/// One AI opponent's economy and war effort.
struct AIPlayer {
  std::string name;          // also the owner string its hulls carry on the map
  double money = 0.0;
  std::vector<int> portIds;  // ports it owns; the first is its home yard
  Research research;
  int researchPoints = 0;

  // Game seconds until the next decision of each kind, so a power acts on its own clock
  // rather than every frame. They are staggered at the start, so three rivals do not all move
  // on the same tick.
  double tradeTimer = 0.0;
  double buildTimer = 0.0;
  double researchTimer = 0.0;

  bool ownsPort(int portId) const {
    return std::find(portIds.begin(), portIds.end(), portId) != portIds.end();
  }
};

/// One merchant crossing a rival power is running. It is the same trade the player does - a
/// sea route priced by the same economy - drawn on the map so an opponent's shipping reads as
/// traffic rather than as a number ticking up somewhere off screen.
struct MerchantRun {
  int id = 0;
  int faction = 0;  // index into GameState::factions()
  int fromPortId = 0;
  int toPortId = 0;
  std::string cargoDescription;
  double cargoValueK = 0.0;
  double distanceKm = 0.0;
  double durationMs = 0.0;
  double startedAtMs = 0.0;  // game time the crossing began
  double progress = 0.0;     // 0..100
  std::vector<LatLng> route;
};

class GameState {
 public:

  void attach(Catalog* catalog, SeaRouter* router);

  /// Resets to a fresh game: base money and the starter boat.
  void newGame();

  // Queries -----------------------------------------------------------------
  double money() const { return money_; }
  void changeMoney(double val) { if (money_ + val <= 0){ money_ = 0;}else{ money_ += val;}}
  const Research& researchLevels() const { return research_; }
  int researchPoints() const { return researchPoints_; }
  bool portOwned(int portId) const { return claimedPorts_.count(portId) > 0; }
  bool vesselOwned(int vesselId) const { return ownedVessels_.count(vesselId) > 0; }
  size_t ownedPortCount() const { return claimedPorts_.size(); }
  size_t ownedVesselCount() const { return ownedVessels_.size(); }

  // Harbour defences (the siege) ---------------------------------------------
  /// The hit points left in a port's harbour defences, and the pool they started from. A
  /// harbour nobody has attacked stands at full strength; an owned port keeps its pool but no
  /// longer fights for it.
  double portDefence(int portId) const;
  double portDefenceMax(int portId) const;
  /// Whether a neutral port's battery has been broken, which is what makes it buyable.
  bool portDefeated(int portId) const;
  /// Whether a port has been attacked and is still fighting back. Cheap enough for the map to
  /// ask once per marker: a single lookup, unlike the full contested list.
  bool portContested(int portId) const {
    const auto found = portDefence_.find(portId);
    return found != portDefence_.end() && found->second > 0.0f && !portOwned(portId);
  }
  /// The ports the player has attacked and not yet taken, in id order: their batteries are
  /// live and their defence pools are what the map draws.
  std::vector<int> contestedPortIds() const;



  /// Whether the hull is damaged and lying alongside one of the player's ports, so a repair
  /// order would be accepted right now.
  bool canHealShip(int unitId) const;

  /// Starts repairing a damaged hull from the nearest owned port it is alongside. The hit
  /// points come back in steps of kHealIntervalSeconds handled by update(), not all at once,
  /// so the ship sits in dock for a moment while the map shows the crew at work.
  bool HealShip(int unitId, std::string& error);

  /// Money earned every second from the owned ports: a fixed rate per port, so it rises with
  /// the port count and does not depend on the treasury. Rounded to two decimals.
  double passiveIncomePerSecond() const;

  std::vector<int> ownedPortIds() const;
  std::vector<int> ownedVesselIds() const;
  std::vector<FleetEntry> fleet() const;
  const std::vector<ActiveVoyage>& voyages() const { return voyages_; }

  // Fleets and admirals -----------------------------------------------------
  const std::vector<Admiral>& admirals() const { return admirals_; }
  const Admiral* admiralById(int id) const;
  const std::vector<Fleet>& fleets() const { return fleets_; }
  const Fleet* fleetById(int id) const;
  /// The fleet a naval unit belongs to, or nullptr when it is independent.
  const Fleet* fleetForUnit(int unitId) const;
  /// The caps the current research allows.
  FleetLimits fleetLimits() const { return sea::fleetLimits(research_); }
  int hiredAdmiralCount() const;
  /// A live recap for every fleet, in the order the fleets were raised.
  std::vector<FleetRecap> fleetRecaps() const;

  /// The admiral's bonus multiplier applied to one unit, for a specific stat. Independent
  /// ships, fleets without an admiral and admirals boosting another stat return 1.0.
  float fleetBuffMultiplier(int unitId, AdmiralBuff buff) const;

  // Naval units (RTS) -------------------------------------------------------
  const std::vector<NavalUnit>& navalUnits() const { return navalUnits_; }
  const NavalUnit* navalUnitById(int id) const;
  size_t navalUnitCount() const { return navalUnits_.size(); }

  /// The boats the bot player owns, drawn on the map with their owner's name. They cannot be
  /// selected or ordered, but they hunt the player's ships and fight them at range.
  const std::vector<BotBoat>& botBoats() const { return botBoats_; }
  const BotBoat* botBoatById(int id) const;

  /// The rival powers, in the order they were seeded.
  const std::vector<AIPlayer>& factions() const { return factions_; }

  /// The power that owns a port, or -1 for a port that is neutral or the player's own.
  int factionOwningPort(int portId) const;

  /// The name of the power that owns a port, empty for a neutral port or one of the player's.
  std::string factionNameForPort(int portId) const;

  /// A power's home yard: where its merchant runs sail from and its hulls are laid down. 0
  /// when the power has no port left.
  int factionHomePort(int factionIndex) const;

  /// How many hulls a power has afloat, sunk ones excluded.
  int factionShipCount(int factionIndex) const;

  /// A power's merchant crossings under way, drawn by the map.
  const std::vector<MerchantRun>& merchantRuns() const { return merchantRuns_; }

  /// The rounds still in the air, drawn by the view as shells, rockets and torpedoes.
  const std::vector<Projectile>& projectiles() const { return projectiles_; }

  /// Drains the combat events raised since the last call, the way messages are drained.
  std::vector<CombatEvent> takeCombatEvents();

  /// The nearest living enemy boat to a unit, or 0 when there is none.
  int nearestEnemyBoat(const LatLng& from) const;

  /// The course a unit would take from `from` to `to`: a straight rhumb line when the whole
  /// crossing is open water, otherwise the sea route around the land between them. The
  /// returned polyline starts at `from` and ends at `to`.
  std::vector<LatLng> navalCourse(const LatLng& from, const LatLng& to) const;

  /// The vessels of the fleet that are not under way.
  std::vector<const Vessel*> readyVessels() const;
  std::vector<const NavalVessel*> readyNavalVessel() const;

  // Port upgrades ------------------------------------------------------------
  /// The port's level, 1 for a port that was never upgraded. Unknown ports read as 1.
  int portLevel(int portId) const;
  /// How many module slots the port's level grants.
  int portSlotCount(int portId) const;
  /// The module ids fitted at the port, in the order they were installed.
  const std::vector<int>& portModules(int portId) const;
  /// Whether the module is already fitted at the port.
  bool portHasModule(int portId, int moduleId) const;
  /// The price of the port's next upgrade, or -1 when it is fully upgraded or unknown.
  double portUpgradeCost(int portId) const;

  // Actions -----------------------------------------------------------------
  /// Buys a port the player has already beaten. A harbour that is still fighting cannot be
  /// bought: its battery has to be broken first (see orderNavalUnitsAttackPort).
  bool claimPort(int portId, std::string& error);

  /// Orders every listed unit, and every fleet mate, to bombard a neutral port: they close on
  /// the harbour, hold station at their own firing range and work its defences down while its
  /// battery answers. Returns how many units accepted the order.
  int orderNavalUnitsAttackPort(const std::vector<int>& unitIds, int portId, std::string& error);

  /// Breaks a port's defences outright and then buys it, in one step. The scripted captures
  /// and the tests have no squadron to send, so they take a harbour this way; a player goes
  /// through orderNavalUnitsAttackPort and claimPort.
  bool stormPort(int portId, std::string& error);

  bool buildVessel(int vesselId, std::string& error);
  bool buyTechnology(const std::string& technologyId, std::string& error);

  /// Lays down a naval class at an owned port that has a shipyard fitted, charging `cost`.
  /// The new unit appears in the water beside the port, ready to be ordered around.
  bool buildNavalUnit(int classId, int portId, double cost, std::string& error);

  /// Sends one unit toward a point, picking a straight line or a sea route for it. Fails for
  /// an unknown unit or a point on land.
  bool orderNavalUnit(int unitId, const LatLng& target, std::string& error);

  /// Sends a unit to the water beside a port. A port's own coordinate often sits on the coast
  /// (and reads as land in the sea grid), so this finds an open berth just offshore first.
  bool orderNavalUnitToPort(int unitId, int portId, std::string& error);

  /// Sends every listed unit toward the same point, expanding the selection to whole fleets
  /// and laying the ships out in a block formation so they do not pile onto one another.
  /// Returns how many units accepted a course.
  int orderNavalUnits(const std::vector<int>& unitIds, const LatLng& target);

  /// Orders every listed unit, and every fleet mate, to attack one boat. Fleet members share
  /// the target so a command is a fleet action rather than a solo charge.
  int orderNavalUnitsAttack(const std::vector<int>& unitIds, int boatId, std::string& error);

  /// Clears a unit's course, leaving it station-keeping where it is.
  void stopNavalUnit(int unitId);

  // Fleet and admiral actions ----------------------------------------------
  /// Raises a named fleet. An empty name is given the next ordinal ("1st Fleet"); the
  /// research-derived fleet cap is enforced. The new fleet becomes the last in the list.
  bool createFleet(const std::string& name, std::string& error);
  bool renameFleet(int fleetId, const std::string& name, std::string& error);
  /// Disbands a fleet and releases its admiral. Its ships become independent again.
  bool disbandFleet(int fleetId, std::string& error);
  /// Adds a ship to a fleet. A ship can only serve in one fleet and the fleet's researched
  /// capacity is enforced.
  bool assignUnitToFleet(int unitId, int fleetId, std::string& error);
  /// Takes a ship out of whatever fleet it is in. Fails when it is already independent.
  bool removeUnitFromFleet(int unitId, std::string& error);
  /// Recruits an admiral directly to a fleet: charges the engagement cost, respects the
  /// admiral cap and posts them in command in one action. An admiral is never hired into the
  /// void; every engagement belongs to a fleet.
  bool recruitAdmiralToFleet(int admiralId, int fleetId, std::string& error);
  /// Puts a hired admiral in command of a fleet, moving them off any previous fleet and
  /// replacing the fleet's current admiral.
  bool assignAdmiralToFleet(int admiralId, int fleetId, std::string& error);
  /// Releases the fleet's admiral back ashore.
  bool dismissAdmiralFromFleet(int fleetId, std::string& error);
  /// Removes an unassigned admiral from the payroll, freeing an admiral slot. A commanding
  /// admiral must be dismissed from their fleet first.
  bool releaseAdmiral(int admiralId, std::string& error);

  /// Orders one unit to engage a boat: it closes to firing range and stays there until the
  /// boat sinks or the player gives it another order. Fails for an unknown unit or boat.
  bool orderNavalUnitAttack(int unitId, int boatId, std::string& error);

  /// Orders a unit onto the nearest enemy boat at all. False when the sea is empty.
  bool engageNearestEnemy(int unitId);

  /// Sets the firing order (kFireLight / kFireMedium / kFireHeavy / kFireAuto, OR-ed) on
  /// every listed unit.
  void setFireOrder(const std::vector<int>& unitIds, unsigned order);

  /// Sets the standing posture (kPostureOffensive / kPostureDefensive / kPostureAggressive)
  /// on every listed unit. Switching away from aggressive releases a boat the posture was
  /// chasing; an explicitly ordered attack is left alone.
  void setPosture(const std::vector<int>& unitIds, int posture);

  /// Spends the next upgrade price and raises the port's level by one, which adds a module
  /// slot. Fails when the port is unowned, fully upgraded or unaffordable.
  bool upgradePort(int portId, std::string& error);

  /// Fits a module in a free slot of an owned port, charging `cost`. Fails when the port is
  /// unowned, out of slots, already has that module or the money is short.
  bool installPortModule(int portId, int moduleId, double cost, std::string& error);

  /// Dispatches a voyage. Buys the hull first if it is not already owned, matching the
  /// navigation panel's "buy hull & sail" button.
  bool startVoyage(int fromPortId, int toPortId, int vesselId, const CargoOption& cargo,
                   double nowMs, std::string& error);

  void cancelVoyage(const std::string& id);

  // Time and persistence ----------------------------------------------------
  /// Advances every occupied vessel from wall-clock-ish game time, delivering arrivals.
  void update(double nowMs);

  /// Sets the clock without advancing (used when loading a save).
  void resetClock(double nowMs) { clockOriginMs_ = nowMs; }

  std::string saveToJson() const;
  bool loadFromJson(const std::string& text, std::string& error, double nowMs);

  /// The most recent game time seen by update(), for callers that need to anchor a
  /// voyage without threading the clock through every call.
  double lastGameTimeMs() const { return lastGameTimeMs_; }

  // Messages the UI should surface (deliveries, losses), drained as they are shown.
  std::vector<std::string> takeMessages();

  std::vector<Technology> getTechnologies() const{return technologies_; }

  /// Whether the technology has been researched at least once.
  bool isTechAcquired(const std::string& id) const { return techAcquired(research_, id); }

  /// Whether every parent of the technology has been acquired, so it can be researched next.
  bool isTechUnlocked(const std::string& id) const { return techUnlocked(research_, id); }


  int calculatePower();

 private:
  double gameTime(double nowMs) const { return nowMs - clockOriginMs_; }

  /// Sails every moving unit along its course for `dtSeconds` of game time.
  void advanceNavalUnits(double dtSeconds);

  /// Lets each bot hull pick a ship to hunt and steam to its own firing range. Bots close the
  /// distance themselves instead of waiting to be run down, so an engagement opens at range.
  void advanceBotBoats(double dtSeconds);

  /// Runs one step of the fight: reloads every mount, lets both sides fire at what they can
  /// reach, flies the rounds and lands them on whatever they hit.
  void updateCombat(double dtSeconds);

  /// Works every repair order: a hull being patched alongside a port gains hit points one
  /// step at a time, and the order is dropped when it is full, sunk, or has left the quay.
  void advanceRepairs(double dtSeconds);

  /// Mounts the basic machine gun on a port that has no armament of its own, so a claimed
  /// harbour is never left bare. Does nothing to a port the player has armed.
  void ensurePortArmament(Port& port);

  /// Runs every rival power's economy and war effort for one tick: income from its ports,
  /// merchant crossings dispatched and settled, hulls laid down, technologies filed.
  void updateFactions(double dtSeconds);

  /// Advances every rival merchant crossing and pays the freighter home when it arrives.
  void advanceMerchantRuns(double now);

  /// Sends a merchant crossing from one of the power's ports. Returns false when it has no
  /// port to sail from, no destination worth the run, or no free berth for another run.
  bool dispatchMerchantRun(int factionIndex, double now);

  /// Lays a warship down at a power's home yard, if it can afford one and still has room in
  /// its navy. Returns false when it cannot.
  bool buildFactionShip(int factionIndex);

  /// Spends a power's research points on the cheapest technology its tree has unlocked and it
  /// can afford. Returns false when there is nothing to buy.
  bool buyFactionTechnology(int factionIndex);

  /// Reloads every owned port's battery and fires it at the nearest enemy boat inside the
  /// reach of a fitted mount, and lets every contested harbour's own battery answer the
  /// player's ships in the same way. A port is a fixed installation: it covers its own
  /// approaches and never chases.
  void updatePortDefenses(double dtSeconds);

  /// Works one salvo's worth of damage into a port's defence pool, after the harbour's own
  /// `def` has soaked its share. Returns true when that was the round that broke them.
  bool bombardPort(int portId, float damage);

  /// Drops a port's defence pool to nothing without a fight, for the scripted paths.
  void breakPortDefences(int portId);

  /// The owned port the hull is currently alongside, or nullptr when it is not at one.
  const Port* healPortFor(const NavalUnit& unit) const;

  /// Fills a hull's mounts array from its class's armament.
  void fitMounts(NavalUnit& unit, const NavalVessel& klass) const;

  /// The listed ids plus every living ship in the same fleet, de-duplicated.
  std::vector<int> expandFleetSelection(const std::vector<int>& unitIds) const;

  /// What a salvo needs to know about the shooter and its target. Bots and the player's ships
  /// fight with the same rules; only who owns the round and who pays for it differ.
  struct FireContext {
    LatLng origin;
    LatLng targetPosition;
    int sourceUnitId = 0;   // the player's ship firing, 0 for a bot or a port
    int sourceBoatId = 0;   // the bot firing, 0 for a player's ship or a port
    int sourcePortId = 0;   // the player's port battery firing, 0 otherwise
    int targetUnitId = 0;   // the player's ship being hit, 0 for a boat
    int targetBoatId = 0;   // the boat being hit, 0 for a player's ship
    int targetPortId = 0;   // the harbour being bombarded, 0 unless a port is the target
    double distanceKm = 0.0;
    double bearingDeg = 0.0;
    bool paysForRounds = false;  // player hulls buy their rounds; bot magazines are their own
    float damageMultiplier = 1.0f;  // an admiral's fleet-wide damage bonus
    float rangeMultiplier = 1.0f;   // an admiral's fleet-wide reach bonus
  };

  /// Works down a firing plan and fires every mount that can shoot right now, in the plan's
  /// order. A mount that is reloading, dry, out of reach, or that the treasury cannot pay for
  /// is skipped and the next one is tried, so a heavy mount on reload never silences the guns
  /// beside it. Returns the number of mounts that fired.
  int fireSalvo(const std::vector<Armament>& armament, std::vector<MountState>& mounts,
                const std::vector<size_t>& plan, const FireContext& context);

  /// Lands one round: soaks the target's defense off the damage, applies what is left and
  /// raises the impact - and, when that was the last of its hit points, the kill.
  void resolveImpact(const Projectile& shot);

  /// Seeds the rival powers at the start of a game: each is given the real harbour nearest
  /// its own waters, an opening hull there and a treasury.
  void seedFactions();

  /// Fills a rival hull's armament, health and defense from the class it was laid down from,
  /// or from its kind's own preset when it has no class (a save from before hulls had one).
  void fitBotArmament(BotBoat& boat) const;

  /// Tops a rival hull's magazines up while it lies at the yard that built it, the way the
  /// player's ships rearm at their home port: a rival's navy must not run dry for good after
  /// one fight.
  void rearmBotBoat(BotBoat& boat, double dtSeconds);


  Catalog* catalog_ = nullptr;
  SeaRouter* router_ = nullptr;

  double money_ = kBaseMoney;
  Research research_;
  std::vector<Technology> technologies_;

  int researchPoints_ = 0;
  std::unordered_set<int> claimedPorts_;
  std::unordered_set<int> ownedVessels_;
  std::unordered_set<int> ownedNavalVessels_;
  std::unordered_map<int, PortUpgrade> portUpgrades_;
  // Harbour defences: the hit points left in a port's battery, for the ports that have been
  // attacked. A port with no entry here has never been shot at and stands at full strength.
  std::unordered_map<int, float> portDefence_;
  // Live ammunition and reload state of each owned port's battery, parallel to the port's
  // fitted mounts. The catalog says which weapons a port carries; this says what is left in
  // their magazines.
  std::unordered_map<int, std::vector<MountState>> portMounts_;
  std::vector<ActiveVoyage> voyages_;
  std::vector<NavalUnit> navalUnits_;
  std::vector<Admiral> admirals_;
  std::vector<Fleet> fleets_;
  int nextFleetId_ = 1;
  std::vector<BotBoat> botBoats_;
  std::vector<AIPlayer> factions_;
  std::vector<MerchantRun> merchantRuns_;
  int nextMerchantRunId_ = 1;
  std::vector<Projectile> projectiles_;
  std::vector<CombatEvent> combatEvents_;
  int nextNavalUnitId_ = 1;
  int nextBoatId_ = 1;
  int nextProjectileId_ = 1;
  std::unordered_set<std::string> delivered_;
  std::vector<std::string> messages_;

  double clockOriginMs_ = 0.0;
  double lastGameTimeMs_ = 0.0;
  mutable std::mt19937 rng_{std::random_device{}()};
};

}  // namespace sea

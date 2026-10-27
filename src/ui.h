#pragma once

// The game's screens and input, all immediate-mode: the top bar, the map, the port panel,
// the navigation planner, the fleet and the research tree.

#include <memory>
#include <string>
#include <vector>

#include "combat_view.h"
#include "fonts.h"
#include "game.h"
#include "map_view.h"
#include "naval_view.h"
#include "raylib.h"




namespace sea {

enum class Screen { Map, Fleet, Research, FleetCommand };

// The number of weapon kinds an armament chip can represent, for the firing-flash clocks.
constexpr int kDeliveryKinds = static_cast<int>(Delivery::Torpedo) + 1;

class App {
 public:
  bool init();
  void update(float deltaSeconds, double nowMs);
  void draw();
  bool shouldClose() const;
  /// False while the start menu is up, so the shell can keep the game clock stopped.
  bool isPlaying() const { return !startMenu_; }

  static std::string cursor;
  // Scripted hooks used by the screenshot smoke test (SEA_BUILDER_DEMO etc).
  void debugDemo();
  void debugStartVoyage();
  void debugShowScreen(const std::string& name);
  std::string debugStatus() const;
  GameState getGameState()const {return game_;}

  GameState game_;


 private:
  void handleGlobalKeys();
  void drawStartMenu();
  void drawTopBar();
  void drawBottomBar();
  void drawMapScreen();
  void drawPortPanel();
  void drawModulePopup();
  /// The armament screen of an owned port: a modal list of the weapons this port can mount.
  void drawArmementPopup();
  void drawShipyardPanel();
  void drawVoyagePanel();
  void drawNavalPanel();
  /// The map's Stellaris-style status overlays: a health bar and power for a lone hull, or a
  /// single pooled banner when ships are close enough to read as one fleet.
  void drawNavalOverlays(const Rectangle& viewport);
  void drawNavalDebugScreen();

  /// Turns the kill events of this frame into kill-feed lines, and draws the lines that have
  /// not expired. The feed is a HUD layer: attacker and victim take their side's colour.
  void recordKillFeed(const std::vector<CombatEvent>& events);
  void drawKillFeed();

  /// The bounds of the selected-units action panel, shared by the drawing and the map's input
  /// guard so a click on the panel never also lands on the map underneath it.
  Rectangle navalPanelBounds() const;

  // Naval selection helpers, used by the map screen.
  int navalUnitAtScreen(Vector2 screen, float radius) const;
  /// The bot boat under the cursor, or 0. Used to turn a right click on an enemy hull into an
  /// attack order instead of a move order.
  int botBoatAtScreen(Vector2 screen, float radius) const;
  bool navalSelected(int unitId) const;
  void selectNavalUnit(int unitId);
  void toggleNavalSelection(int unitId);
  void selectNavalInRect(Vector2 a, Vector2 b);
  void drawFleetScreen();
  void drawFleetCommandScreen();
  void drawResearchScreen();
  void drawNavigationPanel();
  void drawMessages();

  void openNavigation();
  void dispatchVoyage();
  void showToast(const std::string& text);
  void drainMessages();


  // Small immediate-mode widgets.
  bool button(Rectangle bounds, const std::string& label, bool enabled, bool primary = false);
  bool textField(int id, Rectangle bounds, std::string& text, const std::string& placeholder);
  void label(const std::string& text, float x, float y, float size, Color color,
             FontWeight weight = FontWeight::Regular);
  void labelCentered(const std::string& text, Rectangle bounds, float size, Color color,
                     FontWeight weight = FontWeight::Regular);
  Vector2 measureText(const std::string& text, float size,
                      FontWeight weight = FontWeight::Regular);
  // Greedy word wrap into lines no wider than maxWidth, for tooltips and popups.
  std::vector<std::string> wrapText(const std::string& text, float size, float maxWidth);
    const Vessel* infoVessel_ = nullptr;
void drawVesselInfoModal();

  Catalog catalog_;
  SeaRouter router_;
  MapView map_;
  FontBook fonts_;
  NavalView naval_;
  CombatView combat_;

  // The start menu is the first thing drawn: the game screen and its input stay idle until
  // the player chooses Play.
  bool startMenu_ = true;
  bool quitRequested_ = false;

  Screen screen_ = Screen::Map;
  const Port* selectedPort_ = nullptr;
  int modulePopupPortId_ = 0;  // port whose "fit a module" list is open, 0 when closed
  bool modulePopupJustOpened_ = false;  // ignore the click that opened the list this frame

  int armamentPortId_ = 0;  // owned port whose armament screen is open, 0 when closed
  bool armamentJustOpened_ = false;  // ignore the click that opened the screen this frame

  int shipyardPortId_ = 0;  // owned port whose shipyard screen is open, 0 when closed
  bool shipyardJustOpened_ = false;  // ignore the click that opened the shipyard this frame
  std::string shipyardQuery_;
  float shipyardScroll_ = 0.0f;

  bool navigationOpen_ = false;
  bool claimListOpen_ = false;

  // Naval selection and the map's move orders. Selection lives on the app rather than in the
  // game state: it is a view concern and is not saved.
  std::vector<int> selectedNaval_;

  // One flash clock per weapon kind, indexed by Delivery. A muzzle event lights the armament
  // chip of the hull that fired, so the panel shows which battery is actually in the fight.
  struct ArmamentFlash {
    int unitId = 0;
    double untilMs = 0.0;
  };
  ArmamentFlash armamentFlash_[kDeliveryKinds];
  bool boxSelecting_ = false;
  Vector2 boxStart_{0.0f, 0.0f};
  Vector2 boxCurrent_{0.0f, 0.0f};
  Vector2 rightPressPos_{0.0f, 0.0f};
  bool rightDragged_ = false;
  LatLng moveMarker_{};
  double moveMarkerUntilMs_ = 0.0;
  int attackMarkerBoatId_ = 0;
  double attackMarkerUntilMs_ = 0.0;
  bool debugSprites_ = false;

  int navDeparture_ = 0;
  int navArrival_ = 0;
  int navVessel_ = 0;
  std::string navCargoCode_;
  std::string queryDeparture_;
  std::string queryArrival_;
  std::string queryVessel_;
  std::string queryCargo_;
  float scrollDeparture_ = 0.0f;
  float scrollArrival_ = 0.0f;
  float scrollVessel_ = 0.0f;
  float scrollCargo_ = 0.0f;
  float fleetScroll_ = 0.0f;
  std::string fleetQuery_;

  // Fleet Command / Admiralty screen. The tab is 0 for fleets and 1 for the admiral roster;
  // the selected fleet and the admiral assignment target survive between frames.
  int fleetCommandTab_ = 0;
  int fleetCommandSelected_ = 0;
  int fleetCommandRenameFor_ = 0;
  int admiralAssignTarget_ = 0;
  std::string fleetCommandNewName_;
  std::string fleetCommandRename_;
  float fleetCommandListScroll_ = 0.0f;
  float fleetCommandMemberScroll_ = 0.0f;
  float fleetCommandAvailableScroll_ = 0.0f;
  float admiraltyScroll_ = 0.0f;

  // Research tree view: pan/zoom of the dependency graph and the selected node.
  float researchZoom_ = 1.0f;
  Vector2 researchPan_{0.0f, 0.0f};
  bool researchPanning_ = false;
  bool researchDragged_ = false;
  Vector2 researchDragStart_{0.0f, 0.0f};
  Vector2 researchPanOrigin_{0.0f, 0.0f};
  std::string researchSelectedId_ = "cranes";
  bool researchNeedsCenter_ = true;

  int navDepartureCachedField_ = 0;
  std::shared_ptr<const SeaField> navField_;

  int focusField_ = -1;
  bool clickConsumed_ = false;

  std::vector<std::string> inbox_;
  std::string toast_;

  /// The CS-style kill feed: newest line at the top, each one fading out after a few seconds.
  struct KillFeedEntry {
    std::string attacker;
    std::string victim;
    bool attackerIsAlly = false;
    bool victimIsAlly = false;
    double expiresAtMs = 0.0;
  };
  std::vector<KillFeedEntry> killFeed_;
  double toastUntilMs_ = 0.0;
  double nowMs_ = 0.0;
  std::string savePath_;
  bool dataLoaded_ = false;
};

}  // namespace sea

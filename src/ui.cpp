#include "ui.h"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "cargo.h"
#include "research.h"
#include "rlgl.h"
#include "save.h"
#include "voyage.h"


namespace sea {

// Definition for the static member declared in App: the shell's pointer artwork path.
std::string App::cursor;

  namespace {


// The interface is laid out in "design" pixels and rendered a little larger, so the same
// labels and icons read on a high-resolution display without rewriting every position. Text
// and artwork share the same bump; the bars grow with the text so nothing is clipped.
constexpr float kUiTextScale = 1.20f;
constexpr float kUiIconScale = 1.20f;
constexpr float kTopBar = 64.0f;
constexpr float kBottomBar = 56.0f;
// How long an armament chip stays lit after one of its mounts fires.
constexpr double kArmamentFlashMs = 220.0;

// Image and flag helpers, defined below the shipyard/modal code so every screen can use them.
const Texture2D* cachedPhoto(const std::string& path);
const Texture2D* flagForOrigin(const std::string& origin);
void drawFlag(const Texture2D& flag, Rectangle box);
/// Draws an admiral's portrait, cropped to fill `box` without stretching it. Missing artwork
/// falls back to the officer's initials on a dark plate.
void drawAdmiralPortrait(const Admiral& admiral, Rectangle box);

// The shell palette: brushed gunmetal with hard 1px bevels, an amber highlight and a cold
// steel-blue for counters. It is deliberately flat and utilitarian rather than glowy - the
// dialogs of StarCraft, the menus of Unreal and the VGUI skin in Counter-Strike all read as
// machined metal, not neon glass.
constexpr Color kSteelTop = {64, 70, 76, 255};
constexpr Color kSteelBottom = {34, 38, 42, 255};
constexpr Color kSteelFlat = {46, 51, 56, 255};
constexpr Color kSteelSunk = {26, 30, 34, 255};
constexpr Color kBevelHi = {128, 138, 146, 255};
constexpr Color kBevelLo = {10, 12, 14, 255};

constexpr Color kPanelLight = {58, 64, 70, 255};
constexpr Color kAccent = {232, 163, 61, 255};
constexpr Color kSteel = {104, 172, 194, 255};
constexpr Color kText = {216, 221, 224, 255};
constexpr Color kMuted = {146, 154, 160, 255};
constexpr Color kGreen = {92, 176, 92, 255};
constexpr Color kBlue = {94, 152, 196, 255};
constexpr Color kRed = {204, 76, 68, 255};
constexpr Color kGold = {232, 163, 61, 255};
// The bot player's colour: distinct from the player's blue ports and white fleet, but not the
// alarm red reserved for selection, so another player's boat reads as a rival, not a warning.
constexpr Color kBot = {206, 164, 236, 255};
constexpr Color kShadow = {0, 0, 0, 120};

// --- Chrome primitives ------------------------------------------------------ //

/// A vertical two-stop gradient filling `r` exactly.
void gradientV(Rectangle r, Color top, Color bottom) {
  DrawRectangleGradientV(static_cast<int>(r.x), static_cast<int>(r.y),
                         static_cast<int>(r.width), static_cast<int>(r.height), top, bottom);
}

/// Hard 1px bevel: light on the top and left for a raised surface, dark on the bottom and
/// right. Pass raised=false to invert it, which is how a recessed well is drawn.
void bevelEdges(Rectangle r, bool raised) {
  const Color topLeft = raised ? kBevelHi : kBevelLo;
  const Color bottomRight = raised ? kBevelLo : kBevelHi;
  const float x0 = r.x;
  const float y0 = r.y;
  const float x1 = r.x + r.width - 1.0f;
  const float y1 = r.y + r.height - 1.0f;
  DrawLineEx({x0, y0}, {x1, y0}, 1.0f, topLeft);
  DrawLineEx({x0, y0}, {x0, y1}, 1.0f, topLeft);
  DrawLineEx({x0, y1}, {x1, y1}, 1.0f, bottomRight);
  DrawLineEx({x1, y0}, {x1, y1}, 1.0f, bottomRight);
}

/// A raised dialog plate: drop shadow, brushed gradient, dark rim and a light bevel.
void raisedPanel(Rectangle r) {
  DrawRectangle(static_cast<int>(r.x + 2.0f), static_cast<int>(r.y + 3.0f),
                static_cast<int>(r.width), static_cast<int>(r.height), kShadow);
  gradientV(r, kSteelTop, kSteelBottom);
  DrawRectangleLinesEx(r, 1.0f, kBevelLo);
  bevelEdges({r.x + 1.0f, r.y + 1.0f, r.width - 2.0f, r.height - 2.0f}, true);
}

/// A recessed well for list frames, readouts and text fields.
void sunkenPanel(Rectangle r) {
  gradientV(r, kSteelSunk, {36, 40, 45, 255});
  DrawRectangleLinesEx(r, 1.0f, kBevelLo);
  bevelEdges({r.x + 1.0f, r.y + 1.0f, r.width - 2.0f, r.height - 2.0f}, false);
}

/// A header strip for the top of a plate: bright plate, amber rule underneath, and a small
/// amber block marker on the left. Titles are drawn over it by the caller.
void headerStrip(Rectangle r) {
  gradientV(r, {74, 80, 86, 255}, {42, 46, 51, 255});
  DrawLineEx({r.x, r.y}, {r.x + r.width - 1.0f, r.y}, 1.0f, kBevelHi);
  DrawLineEx({r.x, r.y + r.height - 2.0f}, {r.x + r.width - 1.0f, r.y + r.height - 2.0f}, 1.0f,
             kAccent);
  // DrawRectangle(static_cast<int>(r.x + 6.0f), static_cast<int>(r.y + r.height * 0.5f - 4.0f), 8, 8,
  //               kAccent);
}

std::string number(double value) {
  const long long rounded = static_cast<long long>(std::llround(value));
  std::string digits = std::to_string(rounded < 0 ? -rounded : rounded);
  std::string grouped;
  int count = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (count == 3) {
      grouped.push_back(',');
      count = 0;
    }
    grouped.push_back(*it);
    ++count;
  }
  std::reverse(grouped.begin(), grouped.end());
  return (rounded < 0 ? "-" : "") + grouped;
}

std::string moneyText(double value) { return "$" + number(value); }

// Fixed two-decimal rendering, used for the passive income readout so it does not print the
// raw double's trailing digits.
std::string twoDecimals(double value) {
  std::string sign ;
  if (value > 0.0f){
    sign = "+";
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  return sign + std::string(buffer);
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool match(const std::string& query, const std::vector<std::string>& values) {
  const std::string needle = lower(query);
  if (needle.empty()) return true;
  for (const std::string& value : values) {
    if (lower(value).find(needle) != std::string::npos) return true;
  }
  return false;
}

/// Whether a naval class belongs to the port's nation: its origin country, or one of its
/// recorded operators, names that country. Same-nation classes are the ones the shipyard
/// builds without an import surcharge.
bool navalFromNation(const NavalVessel& vessel, const std::string& iso2,
                     const std::string& nationName) {
  const std::string navies = lower(vessel.navies);
  auto names = [&](const std::string& name) {
    const std::string needle = lower(name);
    return !needle.empty() &&
           (lower(vessel.origin) == needle || navies.find(needle) != std::string::npos);
  };
  if (names(nationName)) return true;

  // A handful of nations are spelled differently in the scraped naval data, so their ISO2
  // code accepts the naval spelling as well.
  static const std::pair<const char*, const char*> kAliases[] = {
      {"US", "United States"}, {"RU", "Russia"}, {"KR", "South Korea"},
      {"KP", "North Korea"},   {"TR", "Turkey"},
  };
  for (const auto& alias : kAliases) {
    if (iso2 == alias.first && names(alias.second)) return true;
  }
  return false;
}

struct Row {
  int id = -1;
  std::string title;
  std::string meta;
  std::string warning;
  bool disabled = false;
};

std::string trimTo(const std::string& text, size_t maxChars) {
  if (text.size() <= maxChars) return text;
  return text.substr(0, maxChars - 1) + "...";
}

/// A short badge for a module box: the first letters of its words ("Commercial Port" ->
/// "CP"), so a 56px box can name the module without the hover tooltip.
std::string moduleInitials(const std::string& name) {
  std::string initials;
  bool startOfWord = true;
  for (const char c : name) {
    if (std::isalpha(static_cast<unsigned char>(c))) {
      if (startOfWord) {
        initials.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
      }
      startOfWord = false;
    } else {
      startOfWord = true;
    }
  }
  if (initials.size() > 3) initials.resize(3);
  return initials.empty() ? "?" : initials;
}

/// The green -> amber -> red ramp a health readout fills with as it drops. A full bar reads
/// healthy, a half bar is a warning and the last third is critical, the same language the RTS
/// shells use for a unit's hit points.
Color healthRamp(float fraction) {
  if (fraction > 0.66f) return kGreen;
  if (fraction > 0.33f) return kGold;
  return kRed;
}

/// A thin recessed bar with a coloured fill, the building block of every health readout on the
/// map.
void healthBar(Rectangle bar, float fraction) {
  const float clamped = std::max(0.0f, std::min(1.0f, fraction));
  DrawRectangle(static_cast<int>(bar.x) - 1, static_cast<int>(bar.y) - 1,
                static_cast<int>(bar.width) + 2, static_cast<int>(bar.height) + 2, kBevelLo);
  DrawRectangle(static_cast<int>(bar.x), static_cast<int>(bar.y), static_cast<int>(bar.width),
                static_cast<int>(bar.height), Color{16, 20, 24, 235});
  DrawRectangle(static_cast<int>(bar.x), static_cast<int>(bar.y),
                static_cast<int>(bar.width * clamped), static_cast<int>(bar.height),
                healthRamp(clamped));
}

}  // namespace

bool App::init() {
  fonts_.load();
  catalog_.loadPorts(resolveDataFile("ports.json"));
  catalog_.loadVessels(resolveDataFile("vessels.json"));
  catalog_.loadNavalShips(resolveDataFile("military_vessels.json"));
  catalog_.loadCountryCodes(resolveDataFile("country_codes_V202601.json"));
  catalog_.loadTrade(resolveDataFile("trade_summary.json"));
  router_.loadGrid(resolveDataFile("sea-grid.json"));
  map_.load(resolveDataFile("countries-low.geojson"), resolveDataFile("lakes.geojson"));
  // Bakes the naval hull sprites; needs the window's GL context, so it runs after init.
  naval_.load();

  game_.attach(&catalog_, &router_);
  game_.newGame();
  combat_.reset();

  savePath_ = defaultSavePath();

  App::cursor = "data/cursor/basic.png";

  dataLoaded_ = catalog_.ports().empty() == false && router_.ready();
  if (!dataLoaded_) {
    showToast("Game data could not be loaded. Check the data/ directory.");
  } else {
    showToast("Welcome. Click a port to claim it, then open Navigation.");
  }
  return true;
}

bool App::shouldClose() const { return quitRequested_ || WindowShouldClose(); }

// ---------------------------------------------------------------------------
// Start menu
// ---------------------------------------------------------------------------

void App::drawStartMenu() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());

  // Open sea at dusk: a cold gradient with a faint horizon, so the steel plate and its amber
  // primary button sit on a surface rather than a flat fill.
  gradientV({0.0f, 0.0f, width, height}, Color{26, 36, 43, 255}, Color{8, 11, 14, 255});
  const float horizon = height * 0.62f;
  DrawLineEx({0.0f, horizon}, {width, horizon}, 1.0f, Color{52, 66, 74, 255});
  for (int wave = 1; wave <= 4; ++wave) {
    const float y = horizon + static_cast<float>(wave) * 18.0f;
    DrawLineEx({width * 0.5f - 200.0f - wave * 40.0f, y},
               {width * 0.5f + 200.0f + wave * 40.0f, y}, 1.0f, Color{30, 40, 46, 255});
  }

  const float plateW = std::min(520.0f, width - 60.0f);
  const float plateH = 286.0f;
  const Rectangle plate{(width - plateW) * 0.5f, (height - plateH) * 0.5f, plateW, plateH};
  raisedPanel(plate);

  const Rectangle titleStrip{plate.x + 4.0f, plate.y + 4.0f, plate.width - 8.0f, 38.0f};
  headerStrip(titleStrip);
  labelCentered("OCEAN BUILDER", titleStrip, 22.0f, kAccent, FontWeight::Bold);
  labelCentered("MARITIME TRADING", {plate.x, plate.y + 48.0f, plate.width, 20.0f}, 12.0f,
                kMuted, FontWeight::SemiBold);
  DrawLineEx({plate.x + 24.0f, plate.y + 76.0f},
             {plate.x + plate.width - 24.0f, plate.y + 76.0f}, 1.0f, kBevelLo);

  const float buttonW = 300.0f;
  const float buttonX = plate.x + (plate.width - buttonW) * 0.5f;
  if (button({buttonX, plate.y + 96.0f, buttonW, 48.0f}, "PLAY", dataLoaded_, true)) {
    startMenu_ = false;
    showToast("Welcome. Click a port to claim it, then open Navigation.");
  }
  if (button({buttonX, plate.y + 154.0f, buttonW, 44.0f}, "QUIT", true)) quitRequested_ = true;

  if (!dataLoaded_) {
    labelCentered("Game data could not be loaded. Check the data/ directory.",
                  {plate.x + 8.0f, plate.y + 208.0f, plate.width - 16.0f, 20.0f}, 12.0f, kRed,
                  FontWeight::SemiBold);
  } else {
    labelCentered("Left-click a command to begin.",
                  {plate.x + 8.0f, plate.y + 208.0f, plate.width - 16.0f, 20.0f}, 12.0f, kMuted,
                  FontWeight::SemiBold);
  }

  label("Ocean Builder - desktop edition", 12.0f, height - 22.0f, 11.0f, kMuted);
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

void App::label(const std::string& text, float x, float y, float size, Color color,
                FontWeight weight) {
  fonts_.draw(text, x, y, size * kUiTextScale, color, weight);
}

void App::labelCentered(const std::string& text, Rectangle bounds, float size, Color color,
                        FontWeight weight) {
  const Vector2 measured = measureText(text, size, weight);
  label(text, bounds.x + (bounds.width - measured.x) * 0.5f,
        bounds.y + (bounds.height - measured.y) * 0.5f, size, color, weight);
}

Vector2 App::measureText(const std::string& text, float size, FontWeight weight) {
  return fonts_.measure(text, size * kUiTextScale, weight);
}

std::vector<std::string> App::wrapText(const std::string& text, float size, float maxWidth) {
  std::istringstream words(text);
  std::vector<std::string> lines;
  std::string word;
  std::string line;
  while (words >> word) {
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && measureText(candidate, size).x > maxWidth) {
      lines.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

bool App::button(Rectangle bounds, const std::string& text, bool enabled, bool primary) {
  const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
  const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

  Color top = primary ? Color{122, 92, 42, 255} : kPanelLight;
  Color bottom = primary ? Color{72, 54, 24, 255} : kSteelBottom;
  Color edge = primary ? kAccent : kBevelHi;
  if (!enabled) {
    top = Color{50, 54, 58, 255};
    bottom = Color{34, 37, 40, 255};
    edge = kBevelLo;
  } else if (hovered) {
    top = primary ? Color{154, 116, 52, 255} : Color{84, 92, 100, 255};
    bottom = primary ? Color{100, 74, 32, 255} : Color{50, 56, 62, 255};
  }

  // A pressed button drops into the surface: the bevel inverts and the face shifts down a
  // pixel, the stock 2000s click response.
  const Rectangle face = pressed
                             ? Rectangle{bounds.x + 1.0f, bounds.y + 1.0f, bounds.width - 2.0f,
                                         bounds.height - 2.0f}
                             : bounds;
  gradientV(face, pressed ? bottom : top, pressed ? top : bottom);
  DrawRectangleLinesEx(face, 1.0f, kBevelLo);
  bevelEdges({face.x + 1.0f, face.y + 1.0f, face.width - 2.0f, face.height - 2.0f}, !pressed);
  DrawLineEx({face.x + 1.0f, face.y + (pressed ? 1.0f : 0.0f)},
             {face.x + face.width - 2.0f, face.y + (pressed ? 1.0f : 0.0f)}, 1.0f, edge);

  const Color textColor = !enabled ? kMuted : (primary ? Color{255, 228, 176, 255} : kText);
  labelCentered(text, face, primary ? 16.0f : 15.0f, textColor, FontWeight::SemiBold);

  if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    clickConsumed_ = true;
    return true;
  }
  return false;
}

bool App::textField(int id, Rectangle bounds, std::string& text, const std::string& placeholder) {
  const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
  const bool focused = focusField_ == id;

  sunkenPanel(bounds);
  if (focused) {
    DrawRectangleLinesEx({bounds.x + 2.0f, bounds.y + 2.0f, bounds.width - 4.0f, bounds.height - 4.0f},
                         1.0f, kAccent);
  }

  if (focused) {
    int character = 0;
    while ((character = GetCharPressed()) > 0) {
      if (character >= 32 && character < 127) text.push_back(static_cast<char>(character));
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !text.empty()) text.pop_back();
  }

  const std::string shown = text.empty() && !focused ? placeholder : text;
  label(trimTo(shown, 48), bounds.x + 9.0f, bounds.y + (bounds.height - 18.0f) * 0.5f, 16.0f,
        text.empty() && !focused ? kMuted : kText);

  if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    focusField_ = id;
    clickConsumed_ = true;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void App::update(float deltaSeconds, double nowMs) {
  nowMs_ = nowMs;
  // Nothing advances behind the start menu: the game clock and every input handler wait for
  // the player to choose Play.
  if (startMenu_) return;

  game_.update(nowMs);
  // The game resolves the fight; the view turns this frame's muzzle flashes, hits and kills
  // into particles and wrecks.
  const std::vector<CombatEvent> events = game_.takeCombatEvents();
  // Light the firing hull's armament chip. Muzzle events carry the mount's delivery and the
  // ship that fired, so the lead ship's panel can highlight exactly the battery in action.
  // Only the lead hull's events are kept: its chips are the ones on screen, and a later
  // salvo from another selected ship must not overwrite its flash.
  const int leadHullId = selectedNaval_.empty() ? 0 : selectedNaval_.front();
  for (const CombatEvent& event : events) {
    if (leadHullId == 0 || event.entityId != leadHullId ||
        event.kind != CombatEvent::Kind::Muzzle || event.entityIsBoat) {
      continue;
    }
    const int kind = static_cast<int>(event.delivery);
    if (kind < 0 || kind >= kDeliveryKinds) continue;
    armamentFlash_[kind].unitId = event.entityId;
    armamentFlash_[kind].untilMs = nowMs + kArmamentFlashMs;
  }
  recordKillFeed(events);
  combat_.update(deltaSeconds, events, game_);
  // A sunk hull cannot be ordered any more, so drop it from the selection rather than leaving
  // a dead ship in the action panel.
  selectedNaval_.erase(
      std::remove_if(selectedNaval_.begin(), selectedNaval_.end(),
                     [&](int id) {
                       const NavalUnit* unit = game_.navalUnitById(id);
                       return unit == nullptr || unit->sunk();
                     }),
      selectedNaval_.end());
  drainMessages();



  if (!toast_.empty() && nowMs > toastUntilMs_) toast_ = "";

  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  map_.setViewport({0.0f, kTopBar, width, height - kTopBar - kBottomBar});

  clickConsumed_ = false;
  modulePopupJustOpened_ = false;
  shipyardJustOpened_ = false;

  // The right button both pans the map and issues a move order. Which one it was is decided on
  // release: a press that never travelled more than a few pixels is a click, not a drag.
  if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
    rightPressPos_ = GetMousePosition();
    rightDragged_ = false;
  }
  if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
    const Vector2 mouse = GetMousePosition();
    if (std::hypot(mouse.x - rightPressPos_.x, mouse.y - rightPressPos_.y) > 6.0f) {
      rightDragged_ = true;
    }
  }

  handleGlobalKeys();
}

void App::handleGlobalKeys() {
  if (IsKeyPressed(KEY_ESCAPE)) {
    if (infoVessel_ != nullptr) infoVessel_ = nullptr;
    else if (shipyardPortId_ != 0) shipyardPortId_ = 0;
    else if (modulePopupPortId_ != 0) modulePopupPortId_ = 0;
    else if (!selectedNaval_.empty()) selectedNaval_.clear();
    else if (navigationOpen_) navigationOpen_ = false;
    else if (selectedPort_ != nullptr) selectedPort_ = nullptr;
    else if (screen_ != Screen::Map) screen_ = Screen::Map;
  }

  if (IsKeyPressed(KEY_F5) || (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S))) {
    showToast(saveGame(game_, savePath_) ? "Game saved to " + savePath_ : "Could not save.");
  }
  if (IsKeyPressed(KEY_F9)) {
    std::string error;
    if (loadGame(game_, savePath_, error, nowMs_)) {
      combat_.reset();
      showToast("Game loaded from " + savePath_);
    } else {
      showToast(error);
    }
  }

  // Map panning and zooming, only while nothing is covering the map.
  if (screen_ != Screen::Map || navigationOpen_ || infoVessel_ != nullptr ||
      shipyardPortId_ != 0) {
    return;
  }

  const Vector2 mouse = GetMousePosition();
  const bool onMap = CheckCollisionPointRec(mouse, map_.viewport());

  if (onMap && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
    const Vector2 delta = GetMouseDelta();
    map_.pan(delta);
  }
  if (onMap && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
    map_.pan(GetMouseDelta());
  }
  if (onMap) {
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) map_.zoomAt(mouse, wheel);
  }

  // Arrow keys / WASD pan too.
  const float step = 400.0f * GetFrameTime();
  Vector2 keys{0.0f, 0.0f};
  if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) keys.x += step;
  if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) keys.x -= step;
  if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) keys.y += step;
  if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) keys.y -= step;
  if (keys.x != 0.0f || keys.y != 0.0f) map_.pan(keys);
}

void App::draw() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());

  ClearBackground(Color{18, 20, 22, 255});

  if (startMenu_) {
    drawStartMenu();
    return;
  }

  if (screen_ == Screen::Map) drawMapScreen();
  else if (screen_ == Screen::Fleet) drawFleetScreen();
  else if (screen_ == Screen::FleetCommand) drawFleetCommandScreen();
  else drawResearchScreen();

  drawTopBar();
  drawBottomBar();
  // The bottom bar's Main Menu button switches back to the menu mid-frame; stop here so no
  // game overlay is drawn on top of the map for that one frame.
  if (startMenu_) return;

  // Frame rate, on a small plate in the map's top-left corner where no panel or marker lives.
  // Drawn last so it stays readable over the chart, and only on the map screen so it cannot
  // sit on top of the fleet or research layouts.
  if (screen_ == Screen::Map) {
    DrawRectangle(10, static_cast<int>(kTopBar) + 8, 78, 24, Color{10, 14, 18, 190});
    label(std::to_string(GetFPS()) + " FPS", 14.0f, kTopBar + 11.0f, 10.0f, kText,
          FontWeight::SemiBold);
  }

  if (screen_ == Screen::Map) drawNavalPanel();
  if (navigationOpen_) drawNavigationPanel();
  drawKillFeed();
  drawMessages();
  drawVesselInfoModal();
  drawShipyardPanel();
  if (debugSprites_) drawNavalDebugScreen();

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !clickConsumed_) focusField_ = -1;
  (void)width;
  (void)height;
}

// ---------------------------------------------------------------------------
// Top and bottom bars
// ---------------------------------------------------------------------------

void App::drawTopBar() {
  const float width = static_cast<float>(GetScreenWidth());
  const Rectangle bar{0.0f, 0.0f, width, kTopBar};
  gradientV(bar, kSteelTop, kSteelBottom);
  DrawLineEx({0.0f, 0.0f}, {width, 0.0f}, 1.0f, kBevelHi);
  DrawLineEx({0.0f, kTopBar - 2.0f}, {width, kTopBar - 2.0f}, 1.0f, kBevelLo);
  DrawLineEx({0.0f, kTopBar - 1.0f}, {width, kTopBar - 1.0f}, 1.0f, kAccent);

  const std::vector<FleetEntry> fleet = game_.fleet();
  int underWay = 0;
  for (const FleetEntry& entry : fleet) {
    if (entry.busy) ++underWay;
  }

  // A sunken title plate, the stamp every 2000s shell puts in its corner.
  const Rectangle plate{10.0f, 9.0f, 224.0f, kTopBar - 19.0f};
  sunkenPanel(plate);
  label("OCEAN", plate.x + 12.0f, plate.y + 4.0f, 20.0f, kAccent, FontWeight::Bold);
  label("BUILDER", plate.x + 94.0f, plate.y + 4.0f, 20.0f, kText, FontWeight::Bold);

  auto readout = [&](float x, float w, const std::string& caption, const std::string& value,
                     Color color) {
    const Rectangle box{x, 9.0f, w, kTopBar - 19.0f};
    sunkenPanel(box);
    label(caption, box.x + 10.0f, box.y + 3.0f, 11.0f, kMuted, FontWeight::SemiBold);
    label(trimTo(value, 24), box.x + 10.0f, box.y + 15.0f, 18.0f, color, FontWeight::Bold);
  };


  //need to modify later to make calculus about how much consume
  // money_at_x = len(ports) * 1.05 - money_cost(ships)

  readout(246.0f, 150.0f, "TREASURY", moneyText(game_.money()) + twoDecimals(game_.passiveIncomePerSecond()), kGold);
  readout(404.0f, 212.0f, "FLEET  -  " + std::to_string(underWay) + " AT SEA",
          std::to_string(fleet.size()) + " hulls  -  " + std::to_string(fleet.size() - underWay) +
              " ready",
          kText);
  readout(624.0f, 108.0f, "PORTS", std::to_string(game_.ownedPortCount()), kSteel);
  readout(740.0f, 172.0f, "RESEARCH", std::to_string(game_.researchPoints()) + " pts", kGreen);

  float x = width - 520.0f;
  if (button({x, 11.0f, 118.0f, kTopBar - 22.0f}, "Navigation", dataLoaded_, true)) openNavigation();
  x += 126.0f;
  if (button({x, 11.0f, 100.0f, kTopBar - 22.0f}, "Fleet", true)) screen_ = Screen::Fleet;
  x += 108.0f;
  if (button({x, 11.0f, 110.0f, kTopBar - 22.0f}, "Research", true)) {
    screen_ = Screen::Research;
    researchNeedsCenter_ = true;
  }
  x += 118.0f;
  if (button({x, 11.0f, 70.0f, kTopBar - 22.0f}, "Save", true)) {
    showToast(saveGame(game_, savePath_) ? "Saved." : "Could not save.");
  }
  x += 78.0f;
  if (button({x, 11.0f, 70.0f, kTopBar - 22.0f}, "Load", true)) {
    std::string error;
    if (loadGame(game_, savePath_, error, nowMs_)) {
      combat_.reset();
      showToast("Loaded.");
    } else {
      showToast(error);
    }
  }
}

void App::drawBottomBar() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  const float y = height - kBottomBar;
  const Rectangle bar{0.0f, y, width, kBottomBar};
  gradientV(bar, kSteelBottom, kSteelTop);
  DrawLineEx({0.0f, y}, {width, y}, 1.0f, kBevelLo);
  DrawLineEx({0.0f, y + 1.0f}, {width, y + 1.0f}, 1.0f, kAccent);

  struct Tab {
    const char* label;
    Screen screen;
  };
  const Tab tabs[] = {{"MAP", Screen::Map},
                      {"FLEET MANAGER", Screen::Fleet},
                      {"RESEARCH", Screen::Research},
                      {"ADMIRALTY", Screen::FleetCommand},
                      };

  // File-folder tabs, bottom-aligned and body-connected: the active one sits proud of the bar
  // with an amber cap, the two idle ones are recessed a few pixels. That is the VGUI tab row
  // from Counter-Strike's options dialogs.
  float x = 16.0f;
  for (const Tab& tab : tabs) {
    const bool active = screen_ == tab.screen;
    const Rectangle bounds{x, y + (active ? 5.0f : 9.0f), 156.0f,
                           kBottomBar - (active ? 5.0f : 9.0f) - 5.0f};
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    gradientV(bounds, active ? kSteelTop : Color{56, 62, 68, 255},
              active ? kSteelFlat : kSteelBottom);
    DrawRectangleLinesEx(bounds, 1.0f, kBevelLo);
    bevelEdges({bounds.x + 1.0f, bounds.y + 1.0f, bounds.width - 2.0f, bounds.height - 2.0f}, !active);
    DrawLineEx({bounds.x, bounds.y}, {bounds.x + bounds.width, bounds.y}, 2.0f,
               active ? kAccent : kBevelHi);
    labelCentered(tab.label, bounds, 15.0f, active ? kText : kMuted,
                  active ? FontWeight::Bold : FontWeight::SemiBold);
    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      screen_ = tab.screen;
      if (tab.screen == Screen::Research) researchNeedsCenter_ = true;
      clickConsumed_ = true;
    }
    x += 162.0f;
  }

  label("Right-drag pan  |  Wheel zoom  |  F5 save  F9 load", x + 12.0f, y + 17.0f, 14.0f,
        kMuted);

  // A way back out of the game: the menu pauses the clock, and Play resumes the same game
  // rather than starting over. Everything transient is closed so the menu is not sitting on
  // top of a half-open panel when it is dismissed again.
  if (button({width - 134.0f, y + 9.0f, 118.0f, kBottomBar - 14.0f}, "Main Menu", true)) {
    startMenu_ = true;
    navigationOpen_ = false;
    claimListOpen_ = false;
    modulePopupPortId_ = 0;
    armamentPortId_ = 0;
    shipyardPortId_ = 0;
    infoVessel_ = nullptr;
    selectedPort_ = nullptr;
    selectedNaval_.clear();
    boxSelecting_ = false;
    focusField_ = -1;
    toast_ = "";
    screen_ = Screen::Map;
  }
}

// ---------------------------------------------------------------------------
// Map screen
// ---------------------------------------------------------------------------

void App::drawMapScreen() {
  map_.drawWorld();

  const Vector2 mouse = GetMousePosition();
  const Rectangle viewport = map_.viewport();
  const bool mapHovered = CheckCollisionPointRec(mouse, viewport);

  const Port* hoveredPort = nullptr;
  if (mapHovered && selectedPort_ == nullptr && !navigationOpen_) {
    hoveredPort = map_.portAtScreen(catalog_.ports(), mouse, 14.0f);
  }

  // Batched port markers: a small square per port, one draw call.
  //
  // Two details matter here, both of them silent when wrong. The shape texture is bound
  // explicitly, because raw rlgl vertices otherwise inherit whatever texture the batch was
  // left on (the font atlas, since text is drawn before the map). And the triangles face the
  // camera: counter-clockwise in OpenGL's y-up window space, which with raylib's top-left
  // origin is the winding that reads as clockwise on screen. The other winding is a back face
  // and is culled, and then the markers draw nothing at all.
  const float half = (map_.scale() > 8.0f ? 3.5f : 2.5f) * kUiIconScale;
  const Texture2D shapes = GetShapesTexture();
  const Rectangle shapeRect = GetShapesTextureRectangle();

  rlBegin(RL_TRIANGLES);
  rlSetTexture(shapes.id);
  rlTexCoord2f((shapeRect.x + shapeRect.width * 0.5f) / shapes.width,
               (shapeRect.y + shapeRect.height * 0.5f) / shapes.height);
  for (const Port& port : catalog_.ports()) {
    if (!port.hasPosition()) continue;
    const Vector2 position = map_.project(port.position);
    if (position.x < viewport.x - half || position.x > viewport.x + viewport.width + half ||
        position.y < viewport.y - half || position.y > viewport.y + viewport.height + half) {
      continue;
    }

    Color color = kBlue;
    if (game_.portOwned(port.id)) color = kGreen;
    // A rival's harbour, and any harbour the player has already put under siege, is marked as
    // hostile: red is another power's, or one that is already fighting them.
    else if (game_.factionOwningPort(port.id) >= 0 || game_.portContested(port.id)) color = kRed;
    if (selectedPort_ != nullptr && selectedPort_->id == port.id) color = kRed;
    else if (hoveredPort != nullptr && hoveredPort->id == port.id) color = kGold;

    rlColor4ub(color.r, color.g, color.b, color.a);
    const float x0 = position.x - half;
    const float x1 = position.x + half;
    const float y0 = position.y - half;
    const float y1 = position.y + half;
    rlVertex2f(x0, y0); rlVertex2f(x1, y1); rlVertex2f(x1, y0);
    rlVertex2f(x0, y0); rlVertex2f(x0, y1); rlVertex2f(x1, y1);
  }
  rlEnd();
  rlSetTexture(0);

  // Harbour defences: a small bar over every port the player has attacked and not yet taken,
  // so the progress of a siege can be read without opening the port panel. Untouched ports
  // carry no bar at all - that is what marks a harbour as still somebody else's.
  for (const int portId : game_.contestedPortIds()) {
    const Port* port = catalog_.portById(portId);
    if (port == nullptr || !port->hasPosition()) continue;
    const Vector2 screen = map_.project(port->position);
    if (screen.x < viewport.x - 24.0f || screen.x > viewport.x + viewport.width + 24.0f ||
        screen.y < viewport.y - 24.0f || screen.y > viewport.y + viewport.height + 24.0f) {
      continue;
    }
    const double maxDefence = std::max(1.0, game_.portDefenceMax(portId));
    const float fraction = static_cast<float>(game_.portDefence(portId) / maxDefence);
    const Rectangle bar{screen.x - 11.0f, screen.y - half - 8.0f, 22.0f, 3.0f};
    DrawRectangle(static_cast<int>(bar.x) - 1, static_cast<int>(bar.y) - 1,
                  static_cast<int>(bar.width) + 2, static_cast<int>(bar.height) + 2,
                  Color{18, 20, 23, 220});
    DrawRectangle(static_cast<int>(bar.x), static_cast<int>(bar.y),
                  static_cast<int>(bar.width * std::max(0.0f, std::min(1.0f, fraction))),
                  static_cast<int>(bar.height), kRed);
  }

  // Trade routes. A dark underlay keeps the amber dashes readable over both the ocean
  // and the pale land, and the antimeridian split makes ocean crossings draw end to end.
  for (const ActiveVoyage& voyage : game_.voyages()) {
    map_.drawRoute(voyage.route, Color{0, 0, 0, 150}, 6.0f, true);
    map_.drawRoute(voyage.route, kAccent, 3.0f, true);
  }

  // The rival powers' merchant crossings. Their shipping is drawn as traffic of its own - a
  // darker line and a small hostile-red hull on it - so an opponent's economy can be watched
  // the way the player's own convoys are, and a raid on it means something.
  for (const MerchantRun& run : game_.merchantRuns()) {
    if (run.route.size() < 2) continue;
    map_.drawRoute(run.route, Color{122, 58, 56, 210}, 2.0f, true);

    LatLng where;
    if (!interpolateRoutePosition(run.route, run.progress, where)) continue;
    const Vector2 screen = map_.project(where);
    if (screen.x < viewport.x - 20.0f || screen.x > viewport.x + viewport.width + 20.0f ||
        screen.y < viewport.y - 20.0f || screen.y > viewport.y + viewport.height + 20.0f) {
      continue;
    }
    DrawCircleV(screen, 3.0f, kRed);
    DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y), 6.0f, kRed);
  }

  // --- Bot boats ------------------------------------------------------------
  // Boats owned by the bot player. The hull itself is an ordinary boat: the same baked sprite
  // sheet and the same untinted draw the player's own ships use, so it does not read as
  // terrain or as a special unit. What marks it as another player's is the owner's ring and
  // the caption naming the owner. A hull under the cursor is outlined so the player can see
  // that a right click will be an attack order rather than a move.
  const int hoveredBoatId = botBoatAtScreen(mouse, 26.0f);
  for (const BotBoat& boat : game_.botBoats()) {
    if (boat.sunk()) continue;
    const Vector2 screen = map_.project(boat.position);
    if (screen.x < viewport.x - 48.0f || screen.x > viewport.x + viewport.width + 48.0f ||
        screen.y < viewport.y - 48.0f || screen.y > viewport.y + viewport.height + 48.0f) {
      continue;
    }

    const float length = (map_.scale() > 8.0f ? 30.0f : 22.0f) * kUiIconScale;
    if (naval_.ready(boat.kind)) {
      naval_.drawHull(boat.kind, boat.headingDeg, screen, length, WHITE);
    } else {
      // Fallback when the hull models are missing: a wedge pointing along the heading.
      const float angle = static_cast<float>((boat.headingDeg - 90.0) * PI / 180.0);
      const float size = length * 0.42f;
      const Vector2 nose{screen.x + std::cos(angle) * size, screen.y + std::sin(angle) * size};
      const Vector2 left{screen.x + std::cos(angle + 2.4f) * size,
                         screen.y + std::sin(angle + 2.4f) * size};
      const Vector2 right{screen.x + std::cos(angle - 2.4f) * size,
                          screen.y + std::sin(angle - 2.4f) * size};
      DrawTriangle(nose, right, left, kBot);
    }

    // The owner's ring: the bot's colour, distinct from the green ring the player's own
    // selection uses, so a bot hull is never mistaken for a selected one.
    const int ring = static_cast<int>(length * 0.5f) + 2;
    // DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y), ring, kBot);

    // Target marker: a selected ship is engaging this hull. The ring pulses for a moment
    // after the order is given, then settles into a steady red bracket.
    bool targeted = false;
    for (const int id : selectedNaval_) {
      const NavalUnit* unit = game_.navalUnitById(id);
      if (unit != nullptr && unit->targetBoatId == boat.id) {
        targeted = true;
        break;
      }
    }
    if (targeted) {

      const bool fresh = attackMarkerBoatId_ == boat.id && nowMs_ < attackMarkerUntilMs_;
      const float pulse = fresh ? 3.0f * std::sin(static_cast<float>(nowMs_) * 0.012f) : 0.0f;
      DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y),
                      static_cast<float>(ring) + 6.0f + pulse, kRed);
      DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y),
                      static_cast<float>(ring) + 7.0f + pulse, Color{120, 26, 26, 200});
      for (const int id : selectedNaval_) {
        const NavalUnit* unit = game_.navalUnitById(id);
        if (unit == nullptr || unit->targetBoatId != boat.id) continue;
        DrawLineEx(map_.project(unit->position), screen, 1.4f, Color{220, 70, 70, 120});
      }
      // std::cout << "targetting\n"
      App::cursor = "data/basic.png";
    } else if (hoveredBoatId == boat.id && !selectedNaval_.empty()) {
      DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y),
                      static_cast<float>(ring) + 5.0f, Color{240, 120, 90, 220});
      // std::cout << "hovering boat\n";
      App::cursor = "data/attack.png";
    }

    // At world zoom every rival's nameplate overlaps every other one, turning whole
    // continents into a wall of purple text. Keep the hulls visible, but only spell out a
    // bot boat when the map is close enough to read it or the player is interacting with it.
    const bool showBotCaption = map_.scale() >= 6.0f || hoveredBoatId == boat.id || targeted;
    if (showBotCaption) {
      const std::string caption = boat.owner + " - " + boat.name;
      const Vector2 text = measureText(caption, 15.0f, FontWeight::SemiBold);
      label(caption, screen.x - text.x * 0.5f, screen.y - length * 0.75f - 16.0f, 15.0f, kBot,
            FontWeight::SemiBold);
    }

    if (boat.sunk()) continue;
    // A small red bar shows how much of the target is left, so the player can see an attack
    // working without opening a panel.
    if (targeted) {
      const float fraction = boat.maxHealth > 0.0f ? boat.health / boat.maxHealth : 0.0f;
      const Rectangle bar{screen.x - length * 0.6f, screen.y + length * 0.62f, length * 1.2f,
                          3.0f};
      DrawRectangleRec(bar, Color{30, 12, 12, 220});
      DrawRectangleRec({bar.x, bar.y, bar.width * std::clamp(fraction, 0.0f, 1.0f), bar.height},
                       Color{210, 64, 54, 255});
    }
  }

  // --- Naval units ----------------------------------------------------------
  // Courses first, so a hull always rides on top of the line it is sailing along.
  for (const NavalUnit& unit : game_.navalUnits()) {
    if (unit.sunk() || unit.path.size() < 1) continue;
    std::vector<LatLng> course{unit.position};
    course.insert(course.end(), unit.path.begin(), unit.path.end());
    map_.drawRoute(course, navalSelected(unit.id) ? kGreen : kSteel, 2.0f, true);
  }

  for (const NavalUnit& unit : game_.navalUnits()) {
    if (unit.sunk()) continue;
    const Vector2 screen = map_.project(unit.position);
    if (screen.x < viewport.x - 48.0f || screen.x > viewport.x + viewport.width + 48.0f ||
        screen.y < viewport.y - 48.0f || screen.y > viewport.y + viewport.height + 48.0f) {
      continue;
    }

    const bool selected = navalSelected(unit.id);
    const float length = (map_.scale() > 8.0f ? 34.0f : 24.0f) * kUiIconScale;

    if (selected) {
      const int radius = static_cast<int>(length * 0.5f);
      DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y), radius + 2, kGreen);
      DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y), radius + 3,
                      Color{30, 90, 50, 200});
    }

    if (naval_.ready(unit.kind)) {
      const Color tint = unit.kind == NavalModelKind::submarine ? Color{186, 206, 222, 255}
                                                                : Color{255, 255, 255, 255};
      naval_.drawHull(unit.kind, unit.headingDeg, screen, length, tint);
    } else {
      // Fallback when the hull models are missing: a small wedge pointing along the course.
      const float angle = static_cast<float>((unit.headingDeg - 90.0) * PI / 180.0);
      const float size = length * 0.4f;
      const Vector2 nose{screen.x + std::cos(angle) * size,
                         screen.y + std::sin(angle) * size};
      const Vector2 left{screen.x + std::cos(angle + 2.4f) * size,
                         screen.y + std::sin(angle + 2.4f) * size};
      const Vector2 right{screen.x + std::cos(angle - 2.4f) * size,
                          screen.y + std::sin(angle - 2.4f) * size};
      DrawTriangle(nose, right, left,
                   unit.kind == NavalModelKind::submarine ? kSteel : kBlue);
    }
  }

  // --- Trade vessels --------------------------------------------------------
  // Cargo hulls are deliberately drawn after the navy and the bot boats: a lone merchantman
  // is easy to lose among the white warships and the rival hulls, so it gets a high-contrast
  // silhouette, a pulsing ring and a nameplate that sits above the map.
  for (const ActiveVoyage& voyage : game_.voyages()) {
    LatLng position;
    if (!interpolateRoutePosition(voyage.route, voyage.progress, position)) continue;

    LatLng ahead;
    const double lookahead = std::min(voyage.progress, 100.0 - 0.6);
    interpolateRoutePosition(voyage.route, lookahead + 0.6, ahead);

    double heading = 0.0;
    if (!mapHeadingDegrees(position, ahead, heading)) heading = 0.0;

    const Vector2 screen = map_.project(position);
    if (screen.x < viewport.x - 90.0f || screen.x > viewport.x + viewport.width + 90.0f ||
        screen.y < viewport.y - 90.0f || screen.y > viewport.y + viewport.height + 90.0f) {
      continue;
    }

    const float angle = static_cast<float>((heading - 90.0) * PI / 180.0);
    const Vector2 forward{std::cos(angle), std::sin(angle)};
    const Vector2 right{-forward.y, forward.x};
    auto offset = [&](float along, float across) {
      return Vector2{screen.x + forward.x * along + right.x * across,
                     screen.y + forward.y * along + right.y * across};
    };

    // Wake: a few translucent strokes trailing astern.
    for (int wake = 0; wake < 3; ++wake) {
      const float across = (static_cast<float>(wake) - 1.0f) * 5.0f;
      const float stern = 22.0f + static_cast<float>(wake) * 3.0f;
      DrawLineEx(offset(-stern, across), offset(-stern - 16.0f - wake * 5.0f, across), 2.0f,
                 Color{160, 210, 230, 110});
    }

    // Pulsing ring, so the eye finds the vessel even at world zoom.
    const float pulse = 2.0f * std::sin(static_cast<float>(nowMs_) * 0.006f);
    DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y),
                    25.0f + pulse, Color{232, 163, 61, 190});
    DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y),
                    27.0f + pulse, Color{40, 28, 10, 180});

    // Hull: a small cargo-ship silhouette, always the same screen size.
    constexpr float kHullLength = 38.0f;
    constexpr float kHullBeam = 14.0f;
    const Vector2 hull[5] = {
        offset(-kHullLength * 0.45f, -kHullBeam * 0.5f),
        offset(-kHullLength * 0.45f, kHullBeam * 0.5f),
        offset(kHullLength * 0.18f, kHullBeam * 0.5f),
        offset(kHullLength * 0.58f, 0.0f),
        offset(kHullLength * 0.18f, -kHullBeam * 0.5f)};
    DrawTriangleFan(hull, 5, Color{232, 163, 61, 255});
    for (int edge = 0; edge < 5; ++edge) {
      DrawLineEx(hull[edge], hull[(edge + 1) % 5], 2.0f, Color{24, 20, 12, 255});
    }

    // Deck cargo and the bridge, so it reads as a merchantman rather than a warship.
    for (int box = -1; box <= 1; ++box) {
      const Vector2 at = offset(static_cast<float>(box) * 7.0f - 3.0f, 0.0f);
      DrawRectanglePro({at.x, at.y, 7.0f, 9.0f}, {3.5f, 4.5f}, heading,
                       box == 0 ? Color{244, 246, 248, 255} : Color{110, 156, 176, 255});
    }
    const Vector2 bridge = offset(-13.0f, 0.0f);
    DrawRectanglePro({bridge.x, bridge.y, 9.0f, 11.0f}, {4.5f, 5.5f}, heading,
                     Color{230, 236, 240, 255});

    // Nameplate: vessel, destination and progress, on a dark plate that stays legible
    // over land, ocean and bot-boat captions.
    const Vessel* vessel = catalog_.vesselById(voyage.vesselId);
    const Port* destination = catalog_.portById(voyage.toPortId);
    const std::string owner = vessel != nullptr && !vessel->companyName.empty()
                                  ? vessel->companyName
                                  : "TRADE VESSEL";
    const std::string caption = trimTo(owner, 22) + "  >  " +
                                trimTo(destination != nullptr ? destination->name : "UNKNOWN", 20);
    const double remainingKm =
        std::max(0.0, voyage.distanceKm * (1.0 - voyage.progress / 100.0));
    const std::string progress = std::to_string(static_cast<int>(std::round(voyage.progress))) +
                                 "%  |  " +
                                 std::to_string(static_cast<long long>(std::round(remainingKm))) +
                                 " km to run";
    const Vector2 captionSize = measureText(caption, 14.0f, FontWeight::SemiBold);
    const Vector2 progressSize = measureText(progress, 12.0f, FontWeight::Regular);
    const float plateW = std::max(captionSize.x, progressSize.x) + 16.0f;
    const float plateH = captionSize.y + progressSize.y + 10.0f;
    float plateX = screen.x - plateW * 0.5f;
    plateX = std::clamp(plateX, viewport.x + 6.0f, viewport.x + viewport.width - plateW - 6.0f);
    float plateY = screen.y - 58.0f - plateH;
    if (plateY < viewport.y + 6.0f) plateY = screen.y + 34.0f;
    DrawRectangleRounded({plateX, plateY, plateW, plateH}, 0.25f, 4, Color{8, 12, 16, 225});
    DrawRectangleLinesEx({plateX, plateY, plateW, plateH}, 1.0f, kAccent);
    label(caption, plateX + 8.0f, plateY + 4.0f, 14.0f, kAccent, FontWeight::SemiBold);
    label(progress, plateX + 8.0f, plateY + 5.0f + captionSize.y, 12.0f, kText,
          FontWeight::Regular);
  }

  // The combat layer rides on top of the hulls: wrecks, rounds in flight and the particles
  // they throw. Status bars are drawn after it so a smoke cloud never hides a health readout.
  combat_.draw(map_, naval_, viewport, game_);

  // Status overlays ride on top of the hulls: a small bar and power for a lone ship, and one
  // pooled banner per group of ships.
  drawNavalOverlays(viewport);

  // --- Hovered bot boat readout --------------------------------------------
  // The same intelligence the player has on their own hulls, for an enemy under the cursor:
  // hull integrity, defense and what is left in each magazine. It is read from the live mounts,
  // so a reload in progress is visible rather than a stale catalogue figure.
  if (hoveredBoatId != 0 && mapHovered && !navigationOpen_ && shipyardPortId_ == 0) {
    const BotBoat* boat = game_.botBoatById(hoveredBoatId);
    if (boat != nullptr && !boat->sunk()) {
      const int listed = static_cast<int>(std::min<size_t>(boat->armament.size(), 6));
      const int rows = std::max(1, listed);
      const bool more = boat->armament.size() > static_cast<size_t>(listed);
      const float panelWidth = 292.0f;
      const float panelHeight =
          126.0f + 18.0f * static_cast<float>(rows) + (more ? 14.0f : 0.0f);

      // Open beside the hull, then flip to its other side when the window edge is close, so
      // the panel never runs off the map.
      const float screenWidth = static_cast<float>(GetScreenWidth());
      const float screenHeight = static_cast<float>(GetScreenHeight());
      const Vector2 anchor = map_.project(boat->position);
      float x = anchor.x + 34.0f;
      if (x + panelWidth > screenWidth - 8.0f) x = anchor.x - panelWidth - 34.0f;
      x = std::clamp(x, 8.0f, std::max(8.0f, screenWidth - panelWidth - 8.0f));
      const float minY = kTopBar + 8.0f;
      const float maxY =
          std::max(minY, screenHeight - kBottomBar - panelHeight - 8.0f);
      const float y = std::clamp(anchor.y + 26.0f, minY, maxY);

      const Rectangle panel{x, y, panelWidth, panelHeight};
      raisedPanel(panel);
      headerStrip({panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 24.0f});
      label("HOSTILE CONTACT", panel.x + 12.0f, panel.y + 8.0f, 13.0f, kText,
            FontWeight::Bold);
      const std::string owner = trimTo(boat->owner.empty() ? "UNKNOWN" : boat->owner, 16);
      label(owner, panel.x + panel.width - 12.0f -
                       measureText(owner, 11.0f, FontWeight::SemiBold).x,
            panel.y + 9.0f, 11.0f, kBot, FontWeight::SemiBold);

      label(trimTo(boat->name, 30), panel.x + 12.0f, panel.y + 36.0f, 16.0f, kAccent,
            FontWeight::Bold);

      char position[64];
      std::snprintf(position, sizeof(position), "POS  %.3f, %.3f", boat->position.lat,
                    boat->position.lng);
      label(position, panel.x + 12.0f, panel.y + 58.0f, 10.0f, kMuted);

      const float healthFraction =
          boat->maxHealth > 0.0f ? boat->health / boat->maxHealth : 0.0f;
      const std::string hull = "HULL " + number(boat->health) + " / " + number(boat->maxHealth);
      label(hull, panel.x + 12.0f, panel.y + 76.0f, 11.0f, kGreen, FontWeight::Bold);
      const std::string defense = "DEF " + number(boat->defense);
      label(defense, panel.x + panel.width - 12.0f -
                         measureText(defense, 11.0f, FontWeight::Bold).x,
            panel.y + 76.0f, 11.0f, kSteel, FontWeight::Bold);
      healthBar({panel.x + 12.0f, panel.y + 90.0f, panel.width - 24.0f, 5.0f},
                healthFraction);

      label("ARMAMENT", panel.x + 12.0f, panel.y + 103.0f, 11.0f, kMuted,
            FontWeight::SemiBold);
      float rowY = panel.y + 120.0f;
      if (listed == 0) {
        label("NONE FITTED", panel.x + 12.0f, rowY, 12.0f, kMuted);
      }
      for (int i = 0; i < listed; ++i) {
        const Armament& weapon = boat->armament[static_cast<size_t>(i)];
        const MountState& mount = i < static_cast<int>(boat->mounts.size())
                                      ? boat->mounts[static_cast<size_t>(i)]
                                      : MountState{};
        const std::string name = trimTo(weapon.name.empty() ? weapon.type : weapon.name, 22);
        label(name, panel.x + 12.0f, rowY, 12.0f, kText, FontWeight::SemiBold);

        // Only a dry magazine is reloading in the interesting sense; the short timer between
        // two rounds is the gun cycling, not a magazine change.
        std::string state;
        Color stateColor = kGreen;
        if (mount.ammo <= 0.0f && mount.reload > 0.0f) {
          char reloadText[32];
          std::snprintf(reloadText, sizeof(reloadText), "RELOAD %.1fs", mount.reload);
          state = reloadText;
          stateColor = kGold;
        } else if (mount.ammo <= 0.0f) {
          state = "EMPTY";
          stateColor = kRed;
        } else {
          state = number(mount.ammo) + " / " + number(std::max(1.0f, weapon.ammo));
          stateColor = mount.reload > 0.0f ? kText : kGreen;
        }
        label(state, panel.x + panel.width - 12.0f -
                         measureText(state, 11.0f, FontWeight::Bold).x,
              rowY, 11.0f, stateColor, FontWeight::Bold);
        rowY += 18.0f;
      }
      if (more) {
        label("+" + std::to_string(boat->armament.size() - static_cast<size_t>(listed)) +
                  " MORE MOUNTS",
              panel.x + 12.0f, rowY, 10.0f, kMuted);
      }
    }
  }

  // The order marker, fading a moment after a move is issued.
  if (nowMs_ < moveMarkerUntilMs_) {
    const Vector2 marker = map_.project(moveMarker_);
    const float pulse = 7.0f + static_cast<float>(moveMarkerUntilMs_ - nowMs_) * 0.01f;
    DrawCircleLines(static_cast<int>(marker.x), static_cast<int>(marker.y), pulse, kGold);
    DrawCircle(static_cast<int>(marker.x), static_cast<int>(marker.y), 2.0f, kGold);
  }

  // The drag rectangle while a box selection is being pulled.
  if (boxSelecting_) {
    const Rectangle box{std::min(boxStart_.x, boxCurrent_.x),
                        std::min(boxStart_.y, boxCurrent_.y),
                        std::fabs(boxCurrent_.x - boxStart_.x),
                        std::fabs(boxCurrent_.y - boxStart_.y)};
    DrawRectangleRec(box, Color{92, 176, 92, 40});
    DrawRectangleLinesEx(box, 1.0f, kGreen);
  }

  // --- Map input: selection and move orders ---------------------------------
  // The action panel is drawn after the map, so its bounds are tested here too: a click that
  // lands on a button must not also fall through to the water underneath it.
  const bool overNavalPanel =
      !selectedNaval_.empty() && CheckCollisionPointRec(mouse, navalPanelBounds());

  if (mapHovered && !navigationOpen_ && shipyardPortId_ == 0 && !overNavalPanel) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      const bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
      const int unitId = navalUnitAtScreen(mouse, 18.0f);
      const Port* hitPort =
          unitId == 0 ? map_.portAtScreen(catalog_.ports(), mouse, 14.0f) : nullptr;

      if (unitId != 0) {
        if (additive) {
          toggleNavalSelection(unitId);
        } else {
          selectNavalUnit(unitId);
        }
        selectedPort_ = nullptr;
        clickConsumed_ = true;
      } else if (hitPort != nullptr) {
        selectedPort_ = hitPort;
        if (!additive) selectedNaval_.clear();
        clickConsumed_ = true;
      } else {
        // Empty water: begin a box selection so a whole task force can be swept up at once.
        if (!additive) selectedNaval_.clear();
        boxSelecting_ = true;
        boxStart_ = mouse;
        boxCurrent_ = mouse;
      }
    }

    if (boxSelecting_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) boxCurrent_ = mouse;
    if (boxSelecting_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      boxSelecting_ = false;
      selectNavalInRect(boxStart_, boxCurrent_);
    }

    // A right click that was not a pan is an order for everything selected: on an enemy hull
    // it is an attack order, on open water a move order. This is the RTS rule - the cursor
    // decides what kind of order the player is giving.
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && !rightDragged_ && !selectedNaval_.empty()) {
      const int boatId = botBoatAtScreen(mouse, 26.0f);
      const BotBoat* boat = boatId != 0 ? game_.botBoatById(boatId) : nullptr;

      if (boat != nullptr) {
        std::string error;
        const int ordered = game_.orderNavalUnitsAttack(selectedNaval_, boatId, error);
        if (ordered > 0) {
          attackMarkerBoatId_ = boatId;
          attackMarkerUntilMs_ = nowMs_ + 2200.0;
          showToast("Fleet engaging " + boat->name + ".");
        } else {
          showToast(error.empty() ? "That hull cannot be engaged." : error);
        }
      } else {
        // A harbour the player does not own is a target rather than a piece of water: the
        // right click sends the selection to bombard it, which is the only way to take one.
        const Port* hitPort = map_.portAtScreen(catalog_.ports(), mouse, 14.0f);
        if (hitPort != nullptr && !game_.portOwned(hitPort->id)) {
          std::string error;
          const int ordered = game_.orderNavalUnitsAttackPort(selectedNaval_, hitPort->id, error);
          if (ordered > 0) {
            showToast("Fleet bombarding " + hitPort->name + ".");
          } else {
            showToast(error.empty() ? "That harbour cannot be attacked." : error);
          }
        } else {
          const LatLng target = map_.unproject(mouse);
          const int ordered = game_.orderNavalUnits(selectedNaval_, target);
          if (ordered > 0) {
            moveMarker_ = target;
            moveMarkerUntilMs_ = nowMs_ + 1400.0;
          } else {
            // Say why nothing moved rather than swallowing the click.
            showToast("No course to that point.");
          }
        }
      }
      clickConsumed_ = true;
    }
  } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    boxSelecting_ = false;
  }

  if (selectedPort_ != nullptr) drawPortPanel();
  drawVoyagePanel();
}

std::vector<PortModules> generateAllModules() {
  //need to adapt price everywhere btw
  std::vector<PortModules> modules;
  PortModules streamJammer;
  streamJammer.module_id = 1;
  streamJammer.moduleCost = 600;
  streamJammer.moduleDescription = "Jams communication between ports";
  streamJammer.moduleName = "Stream Jammer";
  modules.push_back(streamJammer);

  PortModules shipyard;
  shipyard.module_id = 2;
  shipyard.moduleCost = 200;
  shipyard.moduleDescription = "This port is now able to build any naval ship you unlocked";
  shipyard.moduleName = "Shipyard";
  modules.push_back(shipyard);

    PortModules commercialPort;
  commercialPort.module_id = 3;
  commercialPort.moduleCost = 500;
  commercialPort.moduleDescription = "+15% income for every transaction navigation going out from this port";
  commercialPort.moduleName = "Commercial Port";
  modules.push_back(commercialPort);

    PortModules artilleryBattery;
  artilleryBattery.module_id = 4;
  artilleryBattery.moduleCost = 700;
  artilleryBattery.moduleDescription = "Artillery Battery deals more damage to ennemy ships if attacked";
  artilleryBattery.moduleName = "Artillery Battery";
  modules.push_back(artilleryBattery);

    PortModules steel_shielding;
  steel_shielding.module_id = 5;
  steel_shielding.moduleCost = 650;
  steel_shielding.moduleDescription = "Great reduction of damage by ennemy submarines and ships";
  steel_shielding.moduleName = "Steel Shielding";
  modules.push_back(steel_shielding);

  PortModules controlTower;
  controlTower.module_id = 6;
  controlTower.moduleCost = 450;
  controlTower.moduleDescription = "Give the capacity of ports to detect submarines and ships from further";
  controlTower.moduleName = "Control Tower";
  modules.push_back(controlTower);

  PortModules torpedoStriker;
  torpedoStriker.module_id = 7;
  torpedoStriker.moduleCost = 550;
  torpedoStriker.moduleDescription = "Ports deal more damage to submarine and slightly more to ships";
  torpedoStriker.moduleName = "Torpedo Striker";
  modules.push_back(torpedoStriker);
  return modules;
}

void App::drawPortPanel() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  const Port& port = *selectedPort_;
  // A selection change or a closed panel dismisses the fit-a-module list and the armament
  // screen too.
  if (modulePopupPortId_ != 0 && modulePopupPortId_ != port.id) modulePopupPortId_ = 0;
  if (armamentPortId_ != 0 && armamentPortId_ != port.id) armamentPortId_ = 0;

  const Rectangle panel{width - 344.0f, kTopBar + 12.0f, 332.0f,
                        height - kTopBar - kBottomBar - 24.0f};
  raisedPanel(panel);
  const Rectangle header{panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 26.0f};
  headerStrip(header);
  label("PORT INFORMATION", header.x + 22.0f, header.y + 6.0f, 14.0f, kText, FontWeight::Bold);

  float y = panel.y + 40.0f;
  label(trimTo(port.name, 30), panel.x + 14.0f, y, 22.0f, kAccent, FontWeight::Bold);
  y += 28.0f;


  const Texture2D* flag = flagForOrigin(catalog_.countryNameFor(port.country));
  // std::cout << port.country << std::endl;
  if (flag != nullptr) drawFlag(*flag, {panel.x + 14.0f, panel.y + 66.0f, 30.0f, 20.0f});

  // Who holds the harbour: the player, a rival power, or nobody. A rival's name is printed in
  // the hostile red its marker and its shipping use, so the panel and the map agree.
  const std::string portOwner = game_.factionNameForPort(port.id);
  if (game_.portOwned(port.id)) {
    label("  -  OWNED", panel.x + 56.0f, y, 16.0f, kGreen);
  } else if (!portOwner.empty()) {
    label("  -  " + portOwner, panel.x + 56.0f, y, 16.0f, kRed);
  }
  y += 26.0f;

  auto detail = [&](const std::string& key, const std::string& value) {
    label(key, panel.x + 14.0f, y, 15.0f, kMuted);
    label(trimTo(value, 26), panel.x + 150.0f, y, 15.0f, kText);
    y += 20.0f;
  };

  detail("Coordinates", std::to_string(port.position.lat).substr(0, 7) + ", " +
                            std::to_string(port.position.lng).substr(0, 8));
  detail("Harbor size", harborSizeLabel(port.harborSize));
  detail("Max vessel", port.maxVessel.empty() ? "-" : port.maxVessel);
  detail("Repair code", port.repairCode.empty() ? "-" : port.repairCode);
  detail("Dry dock", port.drydock.empty() ? "-" : port.drydock);
  detail("Fixed crane", port.craneFixed == "Y" ? "Yes" : "No");
  detail("Fuel / diesel", (port.fuelOil == "Y" || port.diesel == "Y") ? "Yes" : "No");
  detail("Air link", port.commAir == "Y" ? "Yes" : "No");
  detail("Rail link", port.commRail == "Y" ? "Yes" : "No");
  detail("Claim cost", moneyText(portCost(port)));

  y += 8.0f;

  // --- Combat stats ----------------------------------------------------------
  // Health, defense and attack were once drawn as three bare numbers stacked on the same
  // spot, so only the last survived. They are now a labelled, colour-coded card row.
  label("COMBAT STATS", panel.x + 14.0f, y, 14.0f, kAccent, FontWeight::Bold);
  y += 20.0f;

  struct StatCard {
    const char* caption;
    int value;
    Color color;
  };
  const StatCard stats[3] = {
      {"HEALTH", port.health, kGreen},
      {"DEFENSE", port.def, kBlue},
      {"ATTACK", port.attack, kRed},
  };
  const float statGap = 8.0f;
  const float statWidth = (panel.width - 28.0f - statGap * 2.0f) / 3.0f;
  const float statHeight = 48.0f;
  for (int i = 0; i < 3; ++i) {
    const Rectangle card{panel.x + 14.0f + static_cast<float>(i) * (statWidth + statGap), y,
                         statWidth, statHeight};
    sunkenPanel(card);
    // A colour-coded top rule lets the three cards read apart at a glance.
    DrawRectangle(static_cast<int>(card.x + 1.0f), static_cast<int>(card.y + 1.0f),
                  static_cast<int>(card.width - 2.0f), 3, stats[i].color);
    labelCentered(stats[i].caption, {card.x, card.y + 9.0f, card.width, 14.0f}, 11.0f, kMuted,
                  FontWeight::SemiBold);
    labelCentered(std::to_string(stats[i].value), {card.x, card.y + 24.0f, card.width, 20.0f},
                  21.0f, stats[i].color, FontWeight::Bold);
  }
  y += statHeight + 10.0f;



  if (!game_.portOwned(port.id)) {
    // A harbour is not bought off the map. Its battery has to be broken first, by a squadron
    // sent to bombard it; the bar is the siege's progress and the button only opens the
    // purchase once the defences are down.
    const double defence = game_.portDefence(port.id);
    const double defenceMax = std::max(1.0, game_.portDefenceMax(port.id));
    const bool broken = game_.portDefeated(port.id);

    label(broken ? "HARBOUR DEFENCES BROKEN" : "HARBOUR DEFENCES", panel.x + 14.0f, y, 14.0f,
          broken ? kGreen : kAccent, FontWeight::Bold);
    const std::string pool = number(defence) + " / " + number(defenceMax);
    label(pool, panel.x + panel.width - 14.0f - measureText(pool, 13.0f, FontWeight::Bold).x, y,
          13.0f, broken ? kGreen : kText, FontWeight::Bold);
    y += 18.0f;
    healthBar({panel.x + 14.0f, y, panel.width - 28.0f, 6.0f},
              static_cast<float>(defence / defenceMax));
    y += 14.0f;

    if (broken) {
      const bool affordable = game_.money() >= portCost(port);
      if (button({panel.x + 14.0f, y, panel.width - 28.0f, 34.0f},
                 "Buy for " + moneyText(portCost(port)), affordable, true)) {
        std::string error;
        if (game_.claimPort(port.id, error)) showToast("Claimed " + port.name);
        else showToast(error);
      }
    } else {
      button({panel.x + 14.0f, y, panel.width - 28.0f, 34.0f}, "Defeat the harbour first", false,
             true);
      y += 40.0f;
      label("Send warships to bombard it: right click", panel.x + 14.0f, y, 12.0f, kMuted);
      y += 15.0f;
      label("the port with your naval force selected.", panel.x + 14.0f, y, 12.0f, kMuted);
      y += 15.0f;
    }
    y += 42.0f;
  } else {

    // if(button({panel.x + 14.0f, closeY, actionWidth, 32.0f}, "Armement", true)){

    // }
    // Each upgrade costs twice the one before it, and raises the level that sets how many
    // module slots the port has, so the row reports both. The price comes from the game
    // state (base claim price doubled per level), not a fixed multiple of portCost.
    const int level = game_.portLevel(port.id);
    label("You own this port.  LEVEL " + std::to_string(level) + " / " +
              std::to_string(MAX_PORT_LEVEL),
          panel.x + 14.0f, y, 16.0f, kGreen);
    y += 22.0f;

    const double upgradeCost = game_.portUpgradeCost(port.id);
    if (upgradeCost < 0.0) {
      button({panel.x + 14.0f, y, panel.width - 28.0f, 34.0f}, "Maximum level reached", false,
             true);
    } else if (button({panel.x + 14.0f, y, panel.width - 28.0f, 34.0f},
                      "Upgrade for " + moneyText(upgradeCost), game_.money() >= upgradeCost,
                      true)) {
      std::string error;
      if (game_.upgradePort(port.id, error)) {
        showToast("Upgraded " + port.name + " to level " +
        std::to_string(game_.portLevel(port.id)));
        // upgrade stats
        Port* portM = catalog_.portByIdM(port.id);
        portM->attack *= 1.15;
        portM->def *= 1.15;
        portM->health *= 1.15;
        portM->totalAvailableSlotArmement ++;

      } else {
        showToast(error);
      }
    }
    y += 42.0f;
  }
  y += 6.0f;

  const CountryTrade* trade = catalog_.tradeFor(port.country);
  // Anchor the module slots above the Close button, then let the trade lists take whatever
  // room is left. On a short window the lists shrink rather than running into the slots.
  const float closeY = panel.y + panel.height - 44.0f;
  const float boxSize = 56.0f;
  const float boxGap = 6.0f;
  const float modulesBoxY = closeY - 10.0f - boxSize;
  const float modulesLabelY = modulesBoxY - 20.0f;

  if (trade != nullptr) {
    const int maxRows = std::max(0, static_cast<int>((modulesLabelY - y - 54.0f) / 18.0f));
    const int exports = std::min({static_cast<int>(trade->exports.size()), 5, maxRows});
    const int imports = std::max(
        0, std::min({static_cast<int>(trade->imports.size()), 5, maxRows - exports}));

    if (exports > 0) {
      label("TOP EXPORTS", panel.x + 14.0f, y, 14.0f, kAccent, FontWeight::Bold);
      y += 20.0f;
      for (int index = 0; index < exports; ++index) {
        const TradeGood& good = trade->exports[static_cast<size_t>(index)];
        label(trimTo(good.description, 24), panel.x + 14.0f, y, 15.0f, kText);
        const std::string value = number(good.valueK) + "k";
        label(value, panel.x + panel.width - 14.0f - measureText(value, 15.0f).x, y, 15.0f,
              kMuted);
        y += 18.0f;
      }
      y += 8.0f;
    }
    if (imports > 0) {
      label("TOP IMPORTS", panel.x + 14.0f, y, 14.0f, kAccent, FontWeight::Bold);
      y += 20.0f;
      for (int index = 0; index < imports; ++index) {
        const TradeGood& good = trade->imports[static_cast<size_t>(index)];
        label(trimTo(good.description, 24), panel.x + 14.0f, y, 15.0f, kText);
        const std::string value = number(good.valueK) + "k";
        label(value, panel.x + panel.width - 14.0f - measureText(value, 15.0f).x, y, 15.0f,
              kMuted);
        y += 18.0f;
      }
    }
  }

  // --- Module slots ----------------------------------------------------------
  // One box per slot the port's level grants: a fitted module fills its box (hover it for
  // the name and description), own ports leave the rest as clickable empty slots that open
  // the fit-a-module list.
  const std::vector<PortModules> allModules = generateAllModules();
  const bool owned = game_.portOwned(port.id);
  const std::vector<int>& fitted = game_.portModules(port.id);

  y = modulesLabelY;
  label("MODULES  " + std::to_string(fitted.size()) + " / " +
            std::to_string(game_.portSlotCount(port.id)),
        panel.x + 14.0f, y, 14.0f, kAccent, FontWeight::Bold);
  y += 20.0f;
  const PortModules* hoveredModule = nullptr;
  Rectangle hoveredBox{};

  for (int slot = 0; slot < game_.portSlotCount(port.id); ++slot) {
    const Rectangle box{panel.x + 14.0f + static_cast<float>(slot) * (boxSize + boxGap), y,
                        boxSize, boxSize};
    const bool hovered =
        modulePopupPortId_ == 0 && CheckCollisionPointRec(GetMousePosition(), box);

    if (slot < static_cast<int>(fitted.size())) {
      const PortModules* module = nullptr;
      for (const PortModules& candidate : allModules) {
        if (candidate.module_id == fitted[static_cast<size_t>(slot)]) {
          module = &candidate;
          break;
        }
      }

      raisedPanel(box);
      sunkenPanel({box.x + 8.0f, box.y + 8.0f, box.width - 16.0f, box.height - 16.0f});
      const std::string badge = moduleInitials(module != nullptr ? module->moduleName : "?");
      const Vector2 size = measureText(badge, 22.0f, FontWeight::Bold);
      label(badge, box.x + (box.width - size.x) * 0.5f, box.y + (box.height - size.y) * 0.5f,
            22.0f, kAccent, FontWeight::Bold);

      if (hovered && module != nullptr) {
        hoveredModule = module;
        hoveredBox = box;
      }
    } else {
      sunkenPanel(box);
      const std::string glyph = owned ? "+" : "-";
      const float glyphSize = owned ? 26.0f : 22.0f;
      const Vector2 size = measureText(glyph, glyphSize, FontWeight::Bold);
      if (owned && hovered) DrawRectangleLinesEx(box, 1.0f, kAccent);
      label(glyph, box.x + (box.width - size.x) * 0.5f, box.y + (box.height - size.y) * 0.5f,
            glyphSize, owned && hovered ? kAccent : kMuted, FontWeight::Bold);
      if (owned && hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        modulePopupPortId_ = port.id;
        modulePopupJustOpened_ = true;
        clickConsumed_ = true;
      }
    }
  }

  //later put tabs like modules, shipyard...
  // The bottom row holds the screens this port opens. Armament is only offered once the port
  // is yours, and every button gets an equal share of the row so none of them overlaps.
  const float actionGap = 8.0f;
  const bool hasShipyard = game_.portHasModule(port.id, 2);
  const int actionCount = owned ? (hasShipyard ? 3 : 2) : 1;
  const float actionWidth =
      (panel.width - 28.0f - actionGap * static_cast<float>(actionCount - 1)) /
      static_cast<float>(actionCount);
  float actionX = panel.x + 14.0f;

  if (owned) {
    if (button({actionX, closeY, actionWidth, 32.0f}, "Armement", true)) {
      armamentPortId_ = port.id;
      armamentJustOpened_ = true;
      modulePopupPortId_ = 0;
      shipyardPortId_ = 0;
    }
    actionX += actionWidth + actionGap;
  }
  if (hasShipyard) {  // shipyard
    if (button({actionX, closeY, actionWidth, 32.0f}, "Shipyard", true)) {
      shipyardPortId_ = port.id;
      shipyardJustOpened_ = true;
      modulePopupPortId_ = 0;
      armamentPortId_ = 0;
      shipyardQuery_.clear();
      shipyardScroll_ = 0.0f;
    }
    actionX += actionWidth + actionGap;
  }

  if (button({actionX, closeY, actionWidth, 32.0f}, "Close", true)) {
    selectedPort_ = nullptr;
    modulePopupPortId_ = 0;
    armamentPortId_ = 0;
    shipyardPortId_ = 0;
  }

  // Hovering a fitted module: a little card with its name and what it does.
  if (hoveredModule != nullptr) {
    const float tipWidth = 260.0f;
    const std::vector<std::string> lines =
        wrapText(hoveredModule->moduleDescription, 14.0f, tipWidth - 24.0f);
    const float tipHeight = 44.0f + static_cast<float>(lines.size()) * 17.0f + 10.0f;
    const float tipX = std::max(8.0f, panel.x - tipWidth - 10.0f);
    const Rectangle tip{tipX, hoveredBox.y, tipWidth, tipHeight};
    raisedPanel(tip);
    headerStrip({tip.x + 4.0f, tip.y + 4.0f, tip.width - 8.0f, 22.0f});
    label(trimTo(hoveredModule->moduleName, 28), tip.x + 12.0f, tip.y + 7.0f, 15.0f, kText,
          FontWeight::Bold);

    float lineY = tip.y + 32.0f;
    for (const std::string& line : lines) {
      label(line, tip.x + 12.0f, lineY, 14.0f, kMuted);
      lineY += 17.0f;
    }
  }

  if (modulePopupPortId_ == port.id) drawModulePopup();
  if (armamentPortId_ == port.id) drawArmementPopup();
}

// ---------------------------------------------------------------------------
// Port armament
// ---------------------------------------------------------------------------
// Opened from an owned port's Armement button. A port arms one gun mount per level; a mount
// takes any weapon whose research is unlocked, and every fitted mount adds its power to the
// port's attack rating. The list is modal so a click on a row never also reaches the map.
void App::drawArmementPopup() {
  if (armamentPortId_ == 0) return;
  Port* port = catalog_.portByIdM(armamentPortId_);
  if (port == nullptr || !game_.portOwned(port->id)) {
    armamentPortId_ = 0;
    return;
  }

  const std::vector<Armament> available = portArmamentCatalog();

  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  // The level the player has bought decides how many mounts the port may arm, so an upgraded
  // harbour can fit a second weapon beside the machine gun it was founded with.
  const int mounts = portArmamentSlotCountForLevel(game_.portLevel(port->id));
  const int fitted = static_cast<int>(port->equippedArmement.size());

  const float panelWidth = 560.0f;
  const float rowHeight = 74.0f;
  const float panelHeight = 96.0f + rowHeight * static_cast<float>(available.size()) + 50.0f;
  const Rectangle panel{width * 0.5f - panelWidth * 0.5f,
                        height * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};

  DrawRectangle(0, 0, static_cast<int>(width), static_cast<int>(height), Color{0, 0, 0, 150});
  raisedPanel(panel);

  // The click that opened the screen is still "pressed" this frame, so swallow it rather than
  // letting the outside-panel test dismiss the screen on the very same click.
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    if (armamentJustOpened_) {
      armamentJustOpened_ = false;
    } else if (CheckCollisionPointRec(GetMousePosition(), panel)) {
      clickConsumed_ = true;
    } else {
      armamentPortId_ = 0;
      return;
    }
  }

  headerStrip({panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 30.0f});
  label("ARMEMENT  -  " + trimTo(port->name, 24), panel.x + 22.0f, panel.y + 8.0f, 16.0f,
        kText, FontWeight::Bold);
  label(std::to_string(fitted) + " of " + std::to_string(mounts) + " mounts armed",
        panel.x + 14.0f, panel.y + 42.0f, 14.0f, fitted < mounts ? kMuted : kGold);
  label("PORT ATTACK  " + std::to_string(port->attack), panel.x + panel.width - 14.0f -
            measureText("PORT ATTACK  " + std::to_string(port->attack), 14.0f).x,
        panel.y + 42.0f, 14.0f, kRed);

  float y = panel.y + 82.0f;
  for (const Armament& weapon : available) {
    int count = 0;
    for (const Armament& armed : port->equippedArmement) {
      if (armed.id == weapon.id) ++count;
    }
    const int freeMounts = std::max(0, mounts - static_cast<int>(port->equippedArmement.size()));

    const Rectangle row{panel.x + 14.0f, y, panel.width - 28.0f, rowHeight - 8.0f};
    sunkenPanel(row);
    label(trimTo(weapon.name, 24) + (count > 0 ? "  x" + std::to_string(count) : ""),
          row.x + 10.0f, row.y + 6.0f, 16.0f, count > 0 ? kGreen : kText,
          FontWeight::SemiBold);
    label(weapon.type + "  -  " + weapon.getCategory(static_cast<int>(weapon.category)),
          row.x + 10.0f, row.y + 27.0f, 13.0f, kMuted);
    label("DMG " + std::to_string(static_cast<int>(weapon.damage)) + "   POW " +
              std::to_string(static_cast<int>(weapon.power)) + "   MAG " +
              std::to_string(static_cast<int>(weapon.ammo)) + "   RELOAD " +
              std::to_string(static_cast<int>(weapon.reload_time)) + "s",
          row.x + 10.0f, row.y + 45.0f, 13.0f, kMuted);

    const Rectangle action{row.x + row.width - 146.0f, row.y + 7.0f, 136.0f,
                           row.height - 14.0f};
    if (!game_.isTechAcquired(weapon.name)) {
      button(action, "Unlock it in research", false, true);
    } else if (freeMounts == 0) {
      button(action, "No free mount", false, true);
    } else if (button(action, "Fit", true, true)) {
      port->equippedArmement.push_back(weapon);
      port->attack += static_cast<int>(weapon.power + 0.5f);
      showToast(weapon.name + " fitted at " + port->name);
    }
    y += rowHeight;
  }

  if (button({panel.x + panel.width - 120.0f, panel.y + panel.height - 40.0f, 104.0f, 30.0f},
             "Close", true)) {
    armamentPortId_ = 0;
  }
}

void App::drawModulePopup() {
  if (modulePopupPortId_ == 0) return;
  const Port* port = catalog_.portById(modulePopupPortId_);
  if (port == nullptr || !game_.portOwned(port->id)) {
    modulePopupPortId_ = 0;
    return;
  }

  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  const std::vector<PortModules> modules = generateAllModules();

  const std::vector<int>& fitted = game_.portModules(port->id);
  const int slots = game_.portSlotCount(port->id);
  const int freeSlots = std::max(0, slots - static_cast<int>(fitted.size()));

  const float panelWidth = 520.0f;
  const float rowHeight = 58.0f;
  const float panelHeight = 66.0f + rowHeight * static_cast<float>(modules.size()) + 46.0f;
  const Rectangle panel{width * 0.5f - panelWidth * 0.5f,
                        height * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};

  DrawRectangle(0, 0, static_cast<int>(width), static_cast<int>(height), Color{0, 0, 0, 150});
  raisedPanel(panel);

  // The list is modal: a click inside it belongs to the list, a click outside dismisses it,
  // so one click never both closes the list and picks a port on the map behind it.
  // The click that opened the list is still "pressed" this frame, so swallow it rather
  // than letting the outside-panel test below dismiss the list on the very same click.
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    if (modulePopupJustOpened_) {
      modulePopupJustOpened_ = false;
    } else if (CheckCollisionPointRec(GetMousePosition(), panel)) {
      clickConsumed_ = true;
    } else {
      modulePopupPortId_ = 0;
    }
  }

  headerStrip({panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 30.0f});
  label("FIT A MODULE  -  " + trimTo(port->name, 24), panel.x + 22.0f, panel.y + 8.0f, 16.0f,
        kText, FontWeight::Bold);
  label(std::to_string(freeSlots) + " of " + std::to_string(slots) + " slots free",
        panel.x + 14.0f, panel.y + 40.0f, 14.0f, freeSlots > 0 ? kMuted : kGold);

  float y = panel.y + 62.0f;
  std::unordered_map<std::string, std::string> u = {{"Stream Jammer", "jammer"},
  {"Control Tower","control_tower"}};
  for (const PortModules& module : modules){
    if (!u[module.moduleName].empty() && game_.isTechAcquired(u[module.moduleName]) || u[module.moduleName].empty()){


    // game_.technologies_->
    const bool already = game_.portHasModule(port->id, module.module_id);
    const bool canEquip = freeSlots > 0 && !already && game_.money() >= module.moduleCost;

    const Rectangle row{panel.x + 14.0f, y, panel.width - 28.0f, rowHeight - 8.0f};
    sunkenPanel(row);
    label(trimTo(module.moduleName, 26), row.x + 10.0f, row.y + 5.0f, 16.0f,
          already ? kGreen : kText, FontWeight::SemiBold);
    label(trimTo(module.moduleDescription, 44), row.x + 10.0f, row.y + 25.0f, 13.0f, kMuted);

    const Rectangle action{row.x + row.width - 146.0f, row.y + 7.0f, 136.0f, row.height - 14.0f};
    if (already) {
      button(action, "Fitted", false, true);
    } else if (button(action, "Equip  " + moneyText(module.moduleCost), canEquip, true)) {
      std::string error;
      if (game_.installPortModule(port->id, module.module_id, module.moduleCost, error)) {
        showToast(module.moduleName + " fitted at " + port->name);
        modulePopupPortId_ = 0;
      } else {
        showToast(error);
      }
    }
  }else{
    const Rectangle row{panel.x + 14.0f, y, panel.width - 28.0f, rowHeight - 8.0f};
    const Rectangle action{row.x + row.width - 146.0f, row.y + 7.0f, 136.0f, row.height - 14.0f};
    sunkenPanel(row);
    label(trimTo(module.moduleName, 26), row.x + 10.0f, row.y + 5.0f, 16.0f, kText, FontWeight::SemiBold);
    label(trimTo(module.moduleDescription, 44), row.x + 10.0f, row.y + 25.0f, 13.0f, kMuted);
    button(action, "Unlock it in research", false, false);
  }
  y += rowHeight;

    if (button({panel.x + panel.width - 120.0f, panel.y + panel.height - 40.0f, 104.0f, 30.0f},
              "Close", true)) {
      modulePopupPortId_ = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Shipyard
// ---------------------------------------------------------------------------
// Opened from an owned port's Shipyard button. It is the build screen for that port: a
// searchable list of naval classes on the left (ships and submarines) and a detail card on
// the right with a reserved slot for the rotating 3D model. Classes of the port's own nation
// are highlighted as the advantage: they are the ones this yard lays down without surcharge.
void App::drawShipyardPanel() {
  if (shipyardPortId_ == 0) return;
  const Port* port = catalog_.portById(shipyardPortId_);
  // The screen only makes sense while the port is still yours and still has the shipyard.
  if (port == nullptr || !game_.portOwned(port->id) || !game_.portHasModule(port->id, 2)) {
    shipyardPortId_ = 0;
    return;
  }

  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  const float panelWidth = std::min(940.0f, width - 80.0f);
  const float panelHeight = std::min(620.0f, height - kTopBar - kBottomBar - 32.0f);
  const Rectangle panel{width * 0.5f - panelWidth * 0.5f,
                        kTopBar + (height - kTopBar - kBottomBar) * 0.5f - panelHeight * 0.5f,
                        panelWidth, panelHeight};

  DrawRectangle(0, 0, static_cast<int>(width), static_cast<int>(height), Color{0, 0, 0, 150});
  raisedPanel(panel);
  headerStrip({panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 30.0f});
  label("SHIPYARD  -  " + trimTo(port->name, 24), panel.x + 22.0f, panel.y + 8.0f, 16.0f, kText,
        FontWeight::Bold);

  // The list is modal: the click that opened it belongs to the port panel button, so swallow
  // that frame rather than letting the outside-panel test dismiss the screen on the same
  // click. Any later click outside dismisses it.
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    if (shipyardJustOpened_) {
      shipyardJustOpened_ = false;
    } else if (CheckCollisionPointRec(GetMousePosition(), panel)) {
      clickConsumed_ = true;
    } else {
      shipyardPortId_ = 0;
      return;
    }
  }

  const std::string homeNation = catalog_.countryNameFor(port->country);

  // --- Filter the naval catalog ----------------------------------------------
  const std::vector<NavalVessel>& all = catalog_.navalVessels();
  std::vector<const NavalVessel*> shown;
  shown.reserve(all.size());
  int sameNation = 0;
  for (const NavalVessel& vessel : all) {
    if (navalFromNation(vessel, port->country, homeNation)) ++sameNation;

    if (!match(shipyardQuery_, {vessel.shipName, vessel.type, vessel.origin,
                                vessel.manufacturer, vessel.navies}) ) {
      continue;
    }
    //check that this type of boat is unlocked
    if(vessel.type == "Unmanned vessel" || vessel.type == "Patrol vessel"){
      shown.push_back(&vessel);
    }else if(vessel.type == "Corvette" && game_.isTechAcquired("corvette")){
      shown.push_back(&vessel);
    }else if(vessel.type == "Frigate" && game_.isTechAcquired("frigate")){
      shown.push_back(&vessel);
    }else if(vessel.type == "Submarine" && game_.isTechAcquired("submarine")){
      shown.push_back(&vessel);
    }else if(vessel.type == "Destroyer" && game_.isTechAcquired("destroyer")){
      shown.push_back(&vessel);
    }else if(vessel.type == "Cruiser" && game_.isTechAcquired("cruiser")){
      shown.push_back(&vessel);
    }
  }

  const float contentBottom = panel.y + panel.height - 46.0f;
  const float leftWidth = (panel.width - 48.0f) * 0.52f;
  const float rightX = panel.x + 16.0f + leftWidth + 16.0f;
  const float rightWidth = panel.x + panel.width - 16.0f - rightX;

  // --- Left: search and the class list ---------------------------------------
  textField(20, {panel.x + 16.0f, panel.y + 40.0f, leftWidth, 30.0f}, shipyardQuery_,
            "Search ships and submarines...");
  label(std::to_string(shown.size()) + " classes  |  " + std::to_string(sameNation) +
            " from " + (homeNation.empty() ? port->country : homeNation),
        panel.x + 16.0f, panel.y + 74.0f, 13.0f, kMuted);

  const Rectangle list{panel.x + 16.0f, panel.y + 92.0f, leftWidth,
                       contentBottom - (panel.y + 92.0f)};
  sunkenPanel(list);
  const float itemHeight = 54.0f;
  const float total = static_cast<float>(shown.size()) * itemHeight;
  if (CheckCollisionPointRec(GetMousePosition(), list)) {
    shipyardScroll_ -= GetMouseWheelMove() * 60.0f;
    shipyardScroll_ = std::max(0.0f, std::min(std::max(0.0f, total - list.height), shipyardScroll_));
  }

  const Vector2 mouse = GetMousePosition();
  const bool overList = CheckCollisionPointRec(mouse, list);
  const NavalVessel* hovered = nullptr;

  BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width),
                   static_cast<int>(list.height));
  for (size_t index = 0; index < shown.size(); ++index) {
    const float rowY = list.y + static_cast<float>(index) * itemHeight - shipyardScroll_;
    if (rowY > list.y + list.height || rowY + itemHeight < list.y) continue;

    const NavalVessel& vessel = *shown[index];
    const bool fromHome = navalFromNation(vessel, port->country, homeNation);
    const Rectangle row{list.x, rowY, list.width, itemHeight - 4.0f};
    const bool rowHovered = overList && CheckCollisionPointRec(mouse, row);

    DrawRectangleRec(row, fromHome ? (rowHovered ? Color{72, 92, 64, 245} : Color{50, 64, 46, 235})
                                   : (rowHovered ? Color{60, 67, 74, 245} : Color{42, 46, 51, 235}));
    if (fromHome) DrawRectangleLinesEx(row, 1.0f, kGreen);

    // Every row starts with the class's national flag, so a wall of names can be scanned by
    // country. The name and meta shift right to make room for it.
    const Texture2D* flag = flagForOrigin(vessel.origin);
    const float textX = row.x + (flag != nullptr ? 48.0f : 10.0f);
    if (flag != nullptr) drawFlag(*flag, {row.x + 10.0f, row.y + 8.0f, 30.0f, 20.0f});

    label(trimTo(vessel.shipName, flag != nullptr ? 28 : 32), textX, row.y + 5.0f, 16.0f,
          fromHome ? kGreen : kText);
    const std::string meta =
        (vessel.type.empty() ? "Unknown class" : vessel.type) + "  |  " +
        (vessel.origin.empty() ? "unknown origin" : vessel.origin);
    label(trimTo(meta, flag != nullptr ? 40 : 44), textX, row.y + 26.0f, 13.0f, kMuted);
    if (fromHome) {
      label("NATION BONUS", row.x + row.width - 104.0f, row.y + 8.0f, 12.0f, kGreen,
            FontWeight::Bold);
    }

    if (rowHovered) {
      hovered = &vessel;
      // Clicking a class lays one down: it spends the (nation-discounted) hull price and the
      // new unit appears in the water beside the port, ready for orders on the map.
      if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const double cost = navalUnitCost(vessel, fromHome);
        std::string error;
        if (game_.buildNavalUnit(vessel.id, port->id, cost, error)) {
          showToast(trimTo(vessel.shipName, 24) + " laid down at " + trimTo(port->name, 16) +
                    "  -  " + moneyText(cost));
        } else {
          showToast(error);
        }
        clickConsumed_ = true;
      }
    }
  }
  EndScissorMode();
  if (shown.empty()) {
    label("No class matches that search.", list.x + 10.0f, list.y + 10.0f, 14.0f, kMuted);
  }

  // --- Right: detail card for the hovered class ------------------------------
  const Rectangle detail{rightX, panel.y + 40.0f, rightWidth, contentBottom - (panel.y + 40.0f)};
  sunkenPanel(detail);

  // The 3D well: the hovered class's hull, baked from its model, turning slowly in place.
  const float previewHeight = std::max(96.0f, std::min(168.0f, detail.height * 0.44f));
  const Rectangle preview{detail.x + 10.0f, detail.y + 10.0f, detail.width - 20.0f, previewHeight};
  sunkenPanel(preview);
  label("3D PREVIEW", preview.x + 8.0f, preview.y + 5.0f, 12.0f, kMuted, FontWeight::Bold);

  if (hovered == nullptr) {
    labelCentered("rotating hull model",
                  {preview.x, preview.y + preview.height * 0.5f - 8.0f, preview.width, 18.0f},
                  13.0f, kMuted);
    labelCentered("Hover a class to inspect it.",
                  {detail.x, preview.y + preview.height, detail.width,
                   detail.y + detail.height - preview.y - preview.height},
                  15.0f, kMuted);
    return;
  }

  const NavalModelKind previewKind = navalModelKindFor(*hovered);
  if (naval_.ready(previewKind)) {
    naval_.drawPreview(previewKind, preview, std::fmod(nowMs_ * 0.045, 360.0));
  } else {
    labelCentered("Loading...",
                  {preview.x, preview.y + preview.height * 0.5f - 8.0f, preview.width, 18.0f},
                  13.0f, kMuted);
  }

  const NavalVessel& vessel = *hovered;
  const bool fromHome = navalFromNation(vessel, port->country, homeNation);
  float y = preview.y + preview.height + 8.0f;
  const Texture2D* flag = flagForOrigin(vessel.origin);
  if (flag != nullptr) {
    drawFlag(*flag, {detail.x + detail.width - 54.0f, y - 2.0f, 42.0f, 26.0f});
  }
  label(trimTo(vessel.shipName, flag != nullptr ? 26 : 34), detail.x + 12.0f, y, 18.0f, kAccent,
        FontWeight::Bold);
  y += 24.0f;
  if (fromHome) {
    label("NATION BONUS  -  built at " + trimTo(port->name, 16), detail.x + 12.0f, y, 12.0f,
          kGreen, FontWeight::SemiBold);
    y += 18.0f;
  }

  // The spec sheet runs in two columns, so the vertical room is kept for the combat readouts
  // rather than spent on one key/value pair per line.
  const float columnStep = detail.width * 0.5f;
  auto spec = [&](float x, const std::string& key, const std::string& value) {
    label(key, x, y, 12.0f, kMuted);
    label(trimTo(value.empty() ? "-" : value, 18), x + 78.0f, y, 12.0f, kText);
  };
  spec(detail.x + 12.0f, "Type", vessel.type);
  spec(detail.x + 12.0f + columnStep, "Origin", vessel.origin);
  y += 16.0f;
  spec(detail.x + 12.0f, "Builder", vessel.manufacturer);
  spec(detail.x + 12.0f + columnStep, "Commissioned", vessel.commissioned);
  y += 16.0f;
  spec(detail.x + 12.0f, "Displacement", vessel.displacement);
  spec(detail.x + 12.0f + columnStep, "Max speed", vessel.maxSpeed);
  y += 16.0f;
  spec(detail.x + 12.0f, "Build cost", moneyText(navalUnitCost(vessel, fromHome)));
  y += 26.0f;

  // --- Combat stats ----------------------------------------------------------
  // Four colour-coded cards, the same language the port panel uses, so a class's strength
  // reads at a glance instead of as a column of loose numbers.
  struct StatCard {
    const char* caption;
    float value;
    Color color;
  };
  const StatCard stats[4] = {
      {"HEALTH", vessel.health, kGreen},
      {"DEFENSE", vessel.defense, kBlue},
      {"POWER", vessel.totalPower(), kGold},
      {"ATTACK", vessel.totalDamage(), kRed},
  };
  const float statGap = 6.0f;
  const float statWidth = (detail.width - 24.0f - statGap * 3.0f) / 4.0f;
  const float statHeight = 46.0f;
  for (int i = 0; i < 4; ++i) {
    const Rectangle card{detail.x + 12.0f + static_cast<float>(i) * (statWidth + statGap), y,
                         statWidth, statHeight};
    sunkenPanel(card);
    DrawRectangle(static_cast<int>(card.x + 1.0f), static_cast<int>(card.y + 1.0f),
                  static_cast<int>(card.width - 2.0f), 3, stats[i].color);
    labelCentered(stats[i].caption, {card.x, card.y + 9.0f, card.width, 12.0f}, 10.0f, kMuted,
                  FontWeight::SemiBold);
    labelCentered(number(stats[i].value), {card.x, card.y + 24.0f, card.width, 18.0f}, 17.0f,
                  stats[i].color, FontWeight::Bold);
  }
  y += statHeight + 12.0f;

  // --- Armament --------------------------------------------------------------
  // One compact row per weapon: a category colour chip, the name, then its damage and power.
  // The list is capped to the room left above the operator/summary footer and any overflow is
  // summarised, so a heavily armed class never pushes the rest of the card off the plate.
  const int weaponCount = static_cast<int>(vessel.armament.size());
  label("ARMAMENT  " + std::to_string(weaponCount), detail.x + 12.0f, y, 13.0f, kAccent,
        FontWeight::Bold);
  y += 18.0f;

  const float footerTop = detail.y + detail.height - 84.0f;
  const int rowsFit = std::max(1, static_cast<int>((footerTop - y) / 18.0f));
  int rowsShown = std::min(weaponCount, rowsFit);
  if (weaponCount > rowsShown && rowsShown > 0) --rowsShown;  // free a row for the overflow note

  if (weaponCount == 0) {
    label("No armament recorded", detail.x + 12.0f, y, 12.0f, kMuted);
    y += 18.0f;
  }
  for (int i = 0; i < rowsShown; ++i) {
    const Armament& weapon = vessel.armament[static_cast<size_t>(i)];
    Color category = kMuted;
    if (weapon.category == FireCategory::light) category = kGreen;
    else if (weapon.category == FireCategory::medium) category = kGold;
    else if (weapon.category == FireCategory::heavy) category = kRed;

    const Rectangle row{detail.x + 12.0f, y - 2.0f, detail.width - 24.0f, 17.0f};
    sunkenPanel(row);
    DrawRectangle(static_cast<int>(row.x + 1.0f), static_cast<int>(row.y + 1.0f), 3,
                  static_cast<int>(row.height - 2.0f), category);
    label(trimTo(weapon.name.empty() ? weapon.type : weapon.name, 19), row.x + 9.0f,
          row.y + 2.0f, 12.0f, kText);
    // The rate is the mount's own cadence, not the type's flat estimate, so two classes that
    // both list a "gun" can be told apart on the card: a 30 mm mount and a 5-inch turret do not
    // shoot at the same speed.
    const std::string stat = "D" + number(weapon.damage) + "  P" + number(weapon.power) + "  " +
                             std::to_string(static_cast<int>(std::lround(
                                 fireRateRoundsPerMinute(weapon)))) +
                             "/m";
    label(stat, row.x + row.width - 8.0f - measureText(stat, 12.0f).x, row.y + 2.0f, 12.0f,
          category, FontWeight::SemiBold);
    y += 18.0f;
  }
  if (weaponCount > rowsShown) {
    label("+" + std::to_string(weaponCount - rowsShown) + " more weapons", detail.x + 12.0f, y,
          12.0f, kMuted);
    y += 18.0f;
  }

  y += 4.0f;
  label("OPERATORS", detail.x + 12.0f, y, 13.0f, kAccent, FontWeight::Bold);
  y += 18.0f;
  for (const std::string& line : wrapText(
           vessel.navies.empty() ? "No recorded operator" : vessel.navies, 13.0f,
           detail.width - 24.0f)) {
    if (y > detail.y + detail.height - 14.0f) break;
    label(line, detail.x + 12.0f, y, 13.0f, kMuted);
    y += 16.0f;
  }

  y += 6.0f;
  if (y < detail.y + detail.height - 20.0f) {
    label("SUMMARY", detail.x + 12.0f, y, 13.0f, kAccent, FontWeight::Bold);
    y += 18.0f;
    for (const std::string& line : wrapText(vessel.summary, 12.0f, detail.width - 24.0f)) {
      if (y > detail.y + detail.height - 14.0f) break;
      label(line, detail.x + 12.0f, y, 12.0f, kText);
      y += 15.0f;
    }
  }
}

void App::drawVoyagePanel() {
  const std::vector<ActiveVoyage>& voyages = game_.voyages();
  if (voyages.empty()) return;

  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  const Rectangle panel{14.0f, height - kBottomBar - (34.0f + 60.0f * voyages.size()), 430.0f,
                        24.0f + 60.0f * voyages.size()};
  raisedPanel(panel);
  const Rectangle header{panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 22.0f};
  headerStrip(header);
  label(voyages.size() == 1 ? "VOYAGE IN PROGRESS"
                            : std::to_string(voyages.size()) + " VOYAGES IN PROGRESS",
        header.x + 22.0f, header.y + 4.0f, 13.0f, kText, FontWeight::Bold);

  float y = panel.y + 34.0f;
  for (const ActiveVoyage& voyage : voyages) {
    const Port* from = catalog_.portById(voyage.fromPortId);
    const Port* to = catalog_.portById(voyage.toPortId);
    const Vessel* vessel = catalog_.vesselById(voyage.vesselId);

    label((from ? from->name : "?") + " -> " + (to ? to->name : "?"), panel.x + 12.0f, y, 16.0f,
          kText);
    label((vessel ? vessel->companyName : "vessel") + " - " + trimTo(voyage.cargo.description, 22),
          panel.x + 12.0f, y + 18.0f, 14.0f, kMuted);
    label(std::to_string(static_cast<int>(voyage.progress)) + "%  -  " +
              number(voyage.distanceKm * (1.0 - voyage.progress / 100.0)) + " km to run",
          panel.x + 12.0f, y + 34.0f, 14.0f, kGold);

    if (button({panel.x + panel.width - 100.0f, y + 12.0f, 86.0f, 28.0f}, "Cancel", true)) {
      game_.cancelVoyage(voyage.id);
    }
    y += 60.0f;
  }
}

// ---------------------------------------------------------------------------
// Naval units
// ---------------------------------------------------------------------------
// The map is a real-time layer: built ships are steered with the mouse. Left click selects a
// unit (shift adds, a drag sweeps a box), right click sends the selection to that point, and
// this panel is where the standing and, later, combat orders live.

namespace {

/// One line of the action panel's armament summary: every mount of a kind pooled, with what is
/// left in the magazines and whether the battery can actually shoot right now.
struct ArmamentSummary {
  Delivery delivery = Delivery::Gun;
  const char* icon = nullptr;
  int count = 0;
  float ammo = 0.0f;
  float rpm = 0.0f;  // rounds a minute the whole group can put out, mounts added up
  int reloading = 0;
  int empty = 0;
  int unaffordable = 0;
};

/// The icon in data/icon that stands for a mount. The artwork is per weapon family; a mount
/// whose name is a Harpoon flies the Harpoon, every other missile the generic anti-ship round.
const char* weaponIconPath(const Armament& weapon) {
  switch (deliveryFor(weapon)) {
    case Delivery::Torpedo:
      return "icon/anti_submarine.png";
    case Delivery::Rocket:
      return "icon/rocket.png";
    case Delivery::Missile:
      return lower(weapon.name).find("harpoon") != std::string::npos ? "icon/harpoon.png"
                                                                   : "icon/anti_ship_missile.png";
    case Delivery::MachineGun:
    case Delivery::Gun:
      return "icon/machine_gun.png";
  }
  return "icon/machine_gun.png";
}

/// Groups a hull's mounts by the kind of weapon they are, so four twin turrets read as one
/// line ("x4 Gun") instead of four. Ammo, readiness and rate of fire are pooled the same way:
/// the row is reloading if any of its mounts still is, dry only when a magazine is empty and
/// its reload has already finished, and its rate is what the whole group throws in a minute.
std::vector<ArmamentSummary> summariseArmament(const NavalUnit& unit, const NavalVessel& klass,
                                               double money) {
  std::vector<ArmamentSummary> rows;
  for (size_t i = 0; i < klass.armament.size() && i < unit.mounts.size(); ++i) {
    const Armament& weapon = klass.armament[i];
    const MountState& mount = unit.mounts[i];
    const Delivery delivery = deliveryFor(weapon);

    ArmamentSummary* row = nullptr;
    for (ArmamentSummary& candidate : rows) {
      if (candidate.delivery == delivery) {
        row = &candidate;
        break;
      }
    }
    if (row == nullptr) {
      rows.push_back(ArmamentSummary{});
      row = &rows.back();
      row->delivery = delivery;
      row->icon = weaponIconPath(weapon);
    }

    row->count += 1;
    row->ammo += mount.ammo;
    row->rpm += fireRateRoundsPerMinute(weapon);
    // An empty magazine is normally a mount that is changing it, not a dry one: the game
    // refills the mount when its reload ends. Only a mount with no magazine at all counts as
    // empty here, so a reloading row still reads as armed rather than as out of ammunition.
    if (mount.ammo <= 0.0f) {
      if (mount.reload <= 0.0f) row->empty += 1;
    } else if (!roundsAffordable(weapon, money)) {
      row->unaffordable += 1;
    }
    if (mount.reload > 0.0f) row->reloading += 1;
  }
  return rows;
}

/// Two weapon kinds per row, and never more than three rows, so the action panel cannot grow
/// past the space the map can spare.
int armamentRows(int groups) {
  return std::max(1, std::min(3, (groups + 1) / 2));
}

}  // namespace

Rectangle App::navalPanelBounds() const {
  const float height = static_cast<float>(GetScreenHeight());
  const float voyages = static_cast<float>(game_.voyages().size());
  const float voyageH = voyages == 0.0f ? 0.0f : (34.0f + 60.0f * voyages + 8.0f);

  // The panel grows by one row of armament chips per pair of weapon kinds on the lead hull,
  // so a gunboat does not reserve the height of a battleship's battery.
  int groups = 0;
  const NavalUnit* lead =
      selectedNaval_.empty() ? nullptr : game_.navalUnitById(selectedNaval_.front());
  if (lead != nullptr) {
    const NavalVessel* klass = catalog_.navalVesselById(lead->classId);
    if (klass != nullptr) {
      groups = static_cast<int>(summariseArmament(*lead, *klass, game_.money()).size());
    }
  }

  const float panelHeight = 322.0f + 34.0f * static_cast<float>(armamentRows(groups));
  return Rectangle{14.0f, height - kBottomBar - voyageH - panelHeight - 8.0f, 436.0f,
                   panelHeight};
}

int App::navalUnitAtScreen(Vector2 screen, float radius) const {
  int best = 0;
  float bestDistance = radius;
  for (const NavalUnit& unit : game_.navalUnits()) {
    if (unit.sunk()) continue;
    const Vector2 position = map_.project(unit.position);
    const float distance = std::hypot(position.x - screen.x, position.y - screen.y);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = unit.id;
    }
  }
  return best;
}

int App::botBoatAtScreen(Vector2 screen, float radius) const {
  int best = 0;
  float bestDistance = radius;
  for (const BotBoat& boat : game_.botBoats()) {
    if (boat.sunk()) continue;
    const Vector2 position = map_.project(boat.position);
    const float distance = std::hypot(position.x - screen.x, position.y - screen.y);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = boat.id;
    }
  }
  return best;
}

bool App::navalSelected(int unitId) const {
  return std::find(selectedNaval_.begin(), selectedNaval_.end(), unitId) != selectedNaval_.end();
}

void App::selectNavalUnit(int unitId) {
  selectedNaval_.clear();
  selectedNaval_.push_back(unitId);
  // Clicking any ship of a fleet selects the fleet. That is the Stellaris rule: the admiral
  // commands a formation, not a single hull, so orders and the action panel follow the whole
  // squadron as one unit.
  const Fleet* fleet = game_.fleetForUnit(unitId);
  if (fleet == nullptr) return;
  for (const int mateId : fleet->unitIds) {
    if (mateId == unitId) continue;
    const NavalUnit* mate = game_.navalUnitById(mateId);
    if (mate == nullptr || mate->sunk()) continue;
    if (!navalSelected(mateId)) selectedNaval_.push_back(mateId);
  }
}

void App::toggleNavalSelection(int unitId) {
  const auto found = std::find(selectedNaval_.begin(), selectedNaval_.end(), unitId);
  if (found != selectedNaval_.end()) {
    selectedNaval_.erase(found);
    return;
  }
  selectedNaval_.push_back(unitId);
  const Fleet* fleet = game_.fleetForUnit(unitId);
  if (fleet == nullptr) return;
  for (const int mateId : fleet->unitIds) {
    const NavalUnit* mate = game_.navalUnitById(mateId);
    if (mate == nullptr || mate->sunk() || navalSelected(mateId)) continue;
    selectedNaval_.push_back(mateId);
  }
}

void App::selectNavalInRect(Vector2 a, Vector2 b) {
  const Rectangle box{std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x),
                      std::fabs(b.y - a.y)};
  // A plain click leaves a zero-size box, which should select nothing rather than everything
  // at one pixel; anything smaller than a few pixels is treated as a click.
  if (box.width < 4.0f && box.height < 4.0f) return;

  for (const NavalUnit& unit : game_.navalUnits()) {
    const Vector2 position = map_.project(unit.position);
    if (CheckCollisionPointRec(position, box) && !navalSelected(unit.id)) {
      selectedNaval_.push_back(unit.id);
    }
  }

  // Sweeping any part of a fleet picks up its other ships too, so a box around one hull is
  // still a command over the formation.
  const std::vector<int> swept = selectedNaval_;
  for (const int unitId : swept) {
    const Fleet* fleet = game_.fleetForUnit(unitId);
    if (fleet == nullptr) continue;
    for (const int mateId : fleet->unitIds) {
      const NavalUnit* mate = game_.navalUnitById(mateId);
      if (mate == nullptr || mate->sunk() || navalSelected(mateId)) continue;
      selectedNaval_.push_back(mateId);
    }
  }
}

void App::drawNavalPanel() {
  if (selectedNaval_.empty()) return;

  const Rectangle panel = navalPanelBounds();
  raisedPanel(panel);
  headerStrip({panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 24.0f});
  // Pooled fleet readout, ammo state and the lead ship's target. Every number is read from
  // the live mounts, so a shot fired this frame moves the readout in the same frame.
  const NavalUnit* lead = game_.navalUnitById(selectedNaval_.front());
  const Fleet* leadFleet = lead != nullptr ? game_.fleetForUnit(lead->id) : nullptr;
  const Admiral* leadAdmiral =
      leadFleet != nullptr ? game_.admiralById(leadFleet->admiralId) : nullptr;
  const std::string panelTitle =
      (leadFleet != nullptr ? leadFleet->name : std::string("NAVAL FORCE")) + "  -  " +
      std::to_string(selectedNaval_.size()) + " SELECTED";
  label(trimTo(panelTitle, 40), panel.x + 14.0f, panel.y + 6.0f, 15.0f, kText,
        FontWeight::Bold);
  if (leadAdmiral != nullptr) {
    drawAdmiralPortrait(*leadAdmiral, {panel.x + panel.width - 56.0f, panel.y + 30.0f, 42.0f,
                                       42.0f});
  }
  float fleetHealth = 0.0f;
  float fleetMaxHealth = 0.0f;
  float fleetPower = 0.0f;
  float ammo = 0.0f;
  float ammoCapacity = 0.0f;
  float nextReload = 0.0f;
  int reloading = 0;
  int mounts = 0;
  for (const int id : selectedNaval_) {
    const NavalUnit* unit = game_.navalUnitById(id);
    if (unit == nullptr) continue;
    fleetHealth += unit->health;
    fleetMaxHealth += unit->maxHealth;
    const NavalVessel* klass = catalog_.navalVesselById(unit->classId);
    if (klass == nullptr) continue;
    fleetPower += klass->totalPower();
    for (size_t i = 0; i < unit->mounts.size(); ++i) {
      ammo += unit->mounts[i].ammo;
      mounts += 1;
      if (i < klass->armament.size()) {
        ammoCapacity += std::max(0.0f, klass->armament[i].ammo);
      }
      if (unit->mounts[i].reload > 0.0f) {
        ++reloading;
        if (nextReload == 0.0f || unit->mounts[i].reload < nextReload) {
          nextReload = unit->mounts[i].reload;
        }
      }
    }
  }

  const BotBoat* leadTarget =
      lead != nullptr && lead->targetBoatId != 0 ? game_.botBoatById(lead->targetBoatId) : nullptr;
  if (lead != nullptr) {
    label(trimTo(lead->className, leadAdmiral != nullptr ? 30 : 36), panel.x + 14.0f,
          panel.y + 32.0f, 16.0f, kAccent, FontWeight::SemiBold);
    std::string state = lead->moving() ? "under way" : "holding station";
    if (leadTarget != nullptr && !leadTarget->sunk()) state += "  -  engaging " + leadTarget->name;
    label(trimTo(lead->type.empty() ? "naval vessel" : lead->type, 22) + "  -  " + state,
          panel.x + 14.0f, panel.y + 51.0f, 13.0f, lead->moving() ? kGold : kMuted);
  }
  if (leadFleet != nullptr) {
    const std::string flagLine =
        leadAdmiral != nullptr
            ? leadFleet->name + "   ADM " + trimTo(leadAdmiral->fullName(), 24) + "   " +
                  admiralBuffLabel(leadAdmiral->buffKind()) + " +" +
                  std::to_string(static_cast<int>(leadAdmiral->bonus() * 100.0f)) + "%"
            : leadFleet->name + "   no admiral embarked";
    label(trimTo(flagLine, 56), panel.x + 14.0f, panel.y + 69.0f, 12.0f,
          leadAdmiral != nullptr ? kGold : kMuted, FontWeight::SemiBold);
  }

  const float readoutY = panel.y + 88.0f;
  label("FLEET  " + std::to_string(selectedNaval_.size()) + " ships", panel.x + 14.0f, readoutY,
        12.0f, kMuted, FontWeight::SemiBold);
  const std::string hpText = "HP " + number(fleetHealth);
  label(hpText, panel.x + 196.0f, readoutY, 12.0f, kGreen, FontWeight::Bold);
  const std::string pwrText = "PWR " + number(fleetPower);
  label(pwrText, panel.x + panel.width - 14.0f - measureText(pwrText, 12.0f, FontWeight::Bold).x,
        readoutY, 12.0f, kGold, FontWeight::Bold);
  healthBar({panel.x + 14.0f, readoutY + 18.0f, panel.width - 28.0f, 6.0f},
            fleetMaxHealth > 0.0f ? fleetHealth / fleetMaxHealth : 0.0f);

  // Ammo is the rounds left in the current magazines. A fleet that is changing magazines says
  // so; because every mount reloads itself, it is never permanently out of ammunition.
  const float ordnanceY = panel.y + 118.0f;
  std::string ordnance;
  Color ordnanceColor = kGreen;
  if (mounts == 0) {
    ordnance = "NO ARMAMENT FITTED";
    ordnanceColor = kMuted;
  } else if (ammo <= 0.0f) {
    ordnance = reloading > 0 ? "RELOADING MAGAZINES" : "MAGAZINES EMPTY";
    ordnanceColor = reloading > 0 ? kGold : kRed;
  } else {
    ordnance = "MAGAZINES " + number(ammo) + " / " + number(ammoCapacity);
    if (ammoCapacity > 0.0f && ammo / ammoCapacity < 0.2f) ordnanceColor = kRed;
  }
  label(ordnance, panel.x + 14.0f, ordnanceY, 12.0f, ordnanceColor, FontWeight::SemiBold);
  const std::string reloadText =
      reloading > 0 ? std::to_string(reloading) + " RELOADING  " + twoDecimals(nextReload) + "s"
                    : "GUNS READY";
  label(reloadText, panel.x + panel.width - 14.0f -
                        measureText(reloadText, 12.0f, FontWeight::Bold).x,
        ordnanceY, 12.0f, reloading > 0 ? kGold : kGreen, FontWeight::Bold);
  if (mounts > 0) {
    healthBar({panel.x + 14.0f, ordnanceY + 16.0f, panel.width - 28.0f, 4.0f},
              ammoCapacity > 0.0f ? ammo / ammoCapacity : 0.0f);
  }

  // Armament summary: what the lead hull is actually carrying, pooled by kind so a battery of
  // four twin turrets reads as one line instead of four. The chips read the live mounts, so a
  // round fired this frame moves the numbers in the same frame, and a mount the treasury
  // cannot pay for says NO FUNDS instead of leaving the player wondering why the ship is
  // silent.
  std::vector<ArmamentSummary> battery;
  if (lead != nullptr) {
    const NavalVessel* leadClass = catalog_.navalVesselById(lead->classId);
    if (leadClass != nullptr) battery = summariseArmament(*lead, *leadClass, game_.money());
  }

  const float armamentY = panel.y + 144.0f;
  label(selectedNaval_.size() > 1 ? "ARMAMENT  -  LEAD SHIP" : "ARMAMENT", panel.x + 14.0f,
        armamentY, 12.0f, kMuted, FontWeight::SemiBold);
  if (lead != nullptr && !lead->mounts.empty()) {
    const std::string fitted = std::to_string(lead->mounts.size()) + " MOUNTS";
    label(fitted, panel.x + panel.width - 14.0f - measureText(fitted, 12.0f, FontWeight::Bold).x,
          armamentY, 12.0f, kSteel, FontWeight::Bold);
  }

  for (size_t i = 0; i < battery.size() && i < 6; ++i) {
    const ArmamentSummary& row = battery[i];
    const Rectangle chip{panel.x + 14.0f + 202.0f * static_cast<float>(i % 2),
                         armamentY + 17.0f + 34.0f * static_cast<float>(i / 2), 196.0f, 30.0f};
    sunkenPanel(chip);

    // A round fired by this battery lights the chip for a moment, fading back to the recessed
    // metal. Only the lead hull's mounts are shown, so another ship's gun must not light it.
    const int deliveryIndex = static_cast<int>(row.delivery);
    if (lead != nullptr && deliveryIndex >= 0 && deliveryIndex < kDeliveryKinds &&
        armamentFlash_[deliveryIndex].unitId == lead->id &&
        nowMs_ < armamentFlash_[deliveryIndex].untilMs) {
      const float remaining = static_cast<float>(
          (armamentFlash_[deliveryIndex].untilMs - nowMs_) / kArmamentFlashMs);
      DrawRectangleRec(chip, Color{232, 163, 61,
                                   static_cast<unsigned char>(25.0f + 55.0f * remaining)});
      DrawRectangleLinesEx(chip, 1.0f, kAccent);
    }

    // A lighter well behind the icon, so the dark artwork of a machine gun reads against the
    // dark chip instead of blending into it.
    const Rectangle iconWell{chip.x + 3.0f, chip.y + 2.0f, 32.0f, 26.0f};
    DrawRectangleRec(iconWell, Color{58, 64, 70, 255});
    const Texture2D* icon = row.icon != nullptr ? cachedPhoto(row.icon) : nullptr;
    if (icon != nullptr) {
      // Fit the artwork inside the well instead of stretching it: the icons are not all the
      // same shape, and a squashed missile reads as a bug. The well is deliberately larger
      // than the old chip icon: these are the readouts that tell a captain what is firing.
      const float scale = std::min(30.0f / static_cast<float>(icon->width),
                                   24.0f / static_cast<float>(icon->height));
      const float width = static_cast<float>(icon->width) * scale;
      const float height = static_cast<float>(icon->height) * scale;
      DrawTexturePro(*icon,
                     {0.0f, 0.0f, static_cast<float>(icon->width),
                      static_cast<float>(icon->height)},
                     {iconWell.x + (iconWell.width - width) * 0.5f,
                      iconWell.y + (iconWell.height - height) * 0.5f, width, height},
                     {0.0f, 0.0f}, 0.0f, WHITE);
    }
    // The chip names the group and its pooled cadence, so two hulls with the same icon can be
    // compared at a glance: a 30 mm gunboat and a 5-inch destroyer both read "Gun", but one
    // carries "112rpm" underneath and the other "12rpm".
    label("x" + std::to_string(row.count) + " " + deliveryName(row.delivery),
          chip.x + 40.0f, chip.y + 5.0f, 12.0f, kText, FontWeight::SemiBold);
    const std::string cadence =
        std::to_string(static_cast<int>(std::lround(row.rpm))) + "rpm";
    label(cadence, chip.x + 40.0f, chip.y + 17.0f, 10.0f, kMuted, FontWeight::SemiBold);

    std::string state;
    Color stateColor = kGreen;
    if (row.empty == row.count) {
      state = "EMPTY";
      stateColor = kRed;
    } else if (row.empty + row.unaffordable == row.count) {
      state = "NO FUNDS";
      stateColor = kRed;
    } else if (row.reloading == row.count) {
      state = "RELOADING";
      stateColor = kGold;
    } else {
      state = number(row.ammo);
      stateColor = row.reloading > 0 ? kGold : kGreen;
    }
    label(state, chip.x + chip.width - 6.0f - measureText(state, 11.0f, FontWeight::Bold).x,
          chip.y + 8.0f, 11.0f, stateColor, FontWeight::Bold);

    // A readiness rule along the foot of the chip: the whole bar is the battery, the lit part
    // is what can shoot now. An all-reloading row is dark with an amber number, so the chip
    // never reads as "nothing fitted". It is the same bar language as the health and
    // ammunition readouts above, at chip scale.
    const float ready =
        static_cast<float>(row.count - row.reloading - row.empty) / static_cast<float>(row.count);
    const Rectangle rule{chip.x + 1.0f, chip.y + chip.height - 3.0f, chip.width - 2.0f, 2.0f};
    DrawRectangle(static_cast<int>(rule.x), static_cast<int>(rule.y),
                  static_cast<int>(rule.width), 2, Color{16, 20, 24, 230});
    if (ready > 0.0f) {
      DrawRectangle(static_cast<int>(rule.x), static_cast<int>(rule.y),
                    static_cast<int>(rule.width * std::min(1.0f, ready)), 2, stateColor);
    }
  }

  // Standing orders. Steering and target selection are on the map (right click); these cover
  // the rest. The panel's height is the armament rows plus this fixed block, so the buttons
  // stay under the chips however many weapon kinds the lead hull carries.
  const float rowY =
      armamentY + 17.0f + 34.0f * static_cast<float>(armamentRows(static_cast<int>(battery.size()))) + 5.0f;

  if (button({panel.x + 14.0f, rowY, 118.0f, 28.0f}, "Hold Position", true)) {
    for (const int id : selectedNaval_) game_.stopNavalUnit(id);
  }
  if (button({panel.x + 140.0f, rowY, 128.0f, 28.0f}, "Return to Port", true)) {
    int ordered = 0;
    std::string error;
    for (const int id : selectedNaval_) {
      const NavalUnit* unit = game_.navalUnitById(id);
      if (unit != nullptr && game_.orderNavalUnitToPort(id, unit->homePortId, error)) ++ordered;
    }
    showToast(ordered > 0 ? "Returning to port to rearm." : "No home port to return to.");
  }
  if (button({panel.x + 276.0f, rowY, 146.0f, 28.0f}, "Engage Nearest", true)) {
    int ordered = 0;
    for (const int id : selectedNaval_) {
      if (game_.engageNearestEnemy(id)) ++ordered;
    }
    showToast(ordered > 0 ? "Engaging the nearest enemy." : "No enemy boats on the map.");
  }

  // Fire control: which categories of mount the selection may open up with. The game keeps
  // the order as a bit set, so these read the order the whole selection shares and set it on
  // every selected ship. A mixed selection highlights none until one is picked.
  label("TYPE OF ATTACK", panel.x + 14.0f, rowY + 32.0f, 12.0f, kMuted, FontWeight::SemiBold);

  bool haveOrder = false;
  bool mixedOrder = false;
  unsigned order = 0;
  for (const int id : selectedNaval_) {
    const NavalUnit* unit = game_.navalUnitById(id);
    if (unit == nullptr) continue;
    if (!haveOrder) {
      order = unit->fireOrder;
      haveOrder = true;
    } else if (unit->fireOrder != order) {
      mixedOrder = true;
    }
  }

  const unsigned kOrders[4] = {kFireLight, kFireMedium, kFireHeavy, kFireAuto};
  const char* const kOrderNames[4] = {"Light", "Medium", "Heavy", "Auto"};
  const float attackY = rowY + 50.0f;
  for (int i = 0; i < 4; ++i) {
    const Rectangle bounds{panel.x + 14.0f + 104.0f * i, attackY, 96.0f, 26.0f};
    const bool active = haveOrder && !mixedOrder && order == kOrders[i];
    if (button(bounds, kOrderNames[i], true, active)) {
      game_.setFireOrder(selectedNaval_, kOrders[i]);
      order = kOrders[i];
      haveOrder = true;
      mixedOrder = false;
    }
  }

  // Repair. A hull only patches up alongside one of the player's own quays, so the button is
  // dark until a selected ship is both damaged and in dock. The game spreads the work over a
  // few steps of its own, and the map's medical cross shows which hulls still need the yard.
  const float healY = attackY + 36.0f;
  bool canHeal = false;
  for (const int id : selectedNaval_) {
    if (game_.canHealShip(id)) {
      canHeal = true;
      break;
    }
  }
  if (button({panel.x + 14.0f, healY, 146.0f, 26.0f}, "Heal Ship", canHeal)) {
    int started = 0;
    std::string error;
    for (const int id : selectedNaval_) {
      if (game_.canHealShip(id) && game_.HealShip(id, error)) ++started;
    }
    showToast(started > 0 ? "Repair crews are patching the hull."
                          : (error.empty() ? "No selected ship can be repaired here." : error));
  }

  // label("POSTURE", panel.x + 14.0f, attackY + 32.0f, 12.0f, kMuted, FontWeight::SemiBold);

  // // Standing posture, read and set the same way as the firing order above. Offensive ships
  // // shoot at anything that comes into their own reach, defensive ships only answer fire, and
  // // aggressive ships chase the nearest boat as soon as it enters their patrol radius.
  // int posture = kPostureOffensive;
  // bool havePosture = false;
  // bool mixedPosture = false;
  // for (const int id : selectedNaval_) {
  //   const NavalUnit* unit = game_.navalUnitById(id);
  //   if (unit == nullptr) continue;
  //   if (!havePosture) {
  //     posture = unit->posture;
  //     havePosture = true;
  //   } else if (unit->posture != posture) {
  //     mixedPosture = true;
  //   }
  // }

  // const int kPostures[3] = {kPostureOffensive, kPostureDefensive, kPostureAggressive};
  // const char* const kPostureNames[3] = {"Offensive", "Defensive", "Aggressive"};
  // const float postureY = attackY + 50.0f;
  // for (int i = 0; i < 3; ++i) {
  //   const Rectangle bounds{panel.x + 14.0f + 132.0f * i, postureY, 122.0f, 26.0f};
  //   const bool active = havePosture && !mixedPosture && posture == kPostures[i];
  //   if (button(bounds, kPostureNames[i], true, active)) {
  //     game_.setPosture(selectedNaval_, kPostures[i]);
  //     posture = kPostures[i];
  //     havePosture = true;
  //     mixedPosture = false;
  //   }
  // }

  // label("Right-click sea: move.  Right-click an enemy hull: attack.  Shift-click or drag to add.",
  //       panel.x + 14.0f, postureY + 34.0f, 12.0f, kMuted);
}

void App::drawNavalOverlays(const Rectangle& viewport) {
  const std::vector<NavalUnit>& units = game_.navalUnits();
  const size_t count = units.size();
  if (count == 0) return;

  std::vector<Vector2> screen(count);
  std::vector<int> parent(count, -1);
  for (size_t i = 0; i < count; ++i) {
    screen[i] = map_.project(units[i].position);
    // A sunk hull is drawn by the combat view as a wreck; it must not keep a live health
    // bar or get swept into a fleet banner.
    parent[i] = units[i].sunk() ? -1 : static_cast<int>(i);
  }

  auto findRoot = [&](int node) {
    while (parent[node] != node) {
      parent[node] = parent[parent[node]];
      node = parent[node];
    }
    return node;
  };

  // Merge hulls that sit within a screen-space radius, so zooming in splits a fleet back into
  // its individual ships exactly the way Stellaris drops from a fleet banner to ship bars.
  const float kMergeRadius = 54.0f;
  for (size_t i = 0; i < count; ++i) {
    if (parent[i] < 0) continue;
    for (size_t j = i + 1; j < count; ++j) {
      if (parent[j] < 0) continue;
      const float dx = screen[i].x - screen[j].x;
      const float dy = screen[i].y - screen[j].y;
      if (dx * dx + dy * dy > kMergeRadius * kMergeRadius) continue;
      const int a = findRoot(static_cast<int>(i));
      const int b = findRoot(static_cast<int>(j));
      if (a != b) parent[b] = a;
    }
  }

  std::unordered_map<int, std::vector<int>> groups;
  for (size_t i = 0; i < count; ++i) {
    if (parent[i] < 0) continue;
    groups[findRoot(static_cast<int>(i))].push_back(static_cast<int>(i));
  }

  const float hullLength = (map_.scale() > 8.0f ? 34.0f : 24.0f) * kUiIconScale;
  // At world zoom a bar under every hull would be visual noise, so a lone ship only labels
  // itself once the map is deep enough - or when it is selected, which always earns a readout.
  const bool zoomedIn = map_.scale() > 6.0f;

  // Where a damaged hull's medical cross goes. They are collected first and drawn on top of
  // every bar and nameplate at the end, so the warning is never buried under its own readout.
  std::vector<Vector2> repairMarks;

  for (const auto& entry : groups) {
    const std::vector<int>& members = entry.second;

    Vector2 center{0.0f, 0.0f};
    float top = viewport.y + viewport.height;
    float health = 0.0f;
    float maxHealth = 0.0f;
    float power = 0.0f;
    bool selected = false;
    int statted = 0;
    for (const int index : members) {
      const size_t slot = static_cast<size_t>(index);
      center.x += screen[slot].x;
      center.y += screen[slot].y;
      top = std::min(top, screen[slot].y);
      if (navalSelected(units[slot].id)) selected = true;
      const NavalVessel* klass = catalog_.navalVesselById(units[slot].classId);
      if (klass == nullptr) continue;
      health += units[slot].health;
      maxHealth += units[slot].maxHealth;
      power += klass->totalPower();
      ++statted;
    }
    if (statted == 0) continue;
    center.x /= static_cast<float>(members.size());
    center.y /= static_cast<float>(members.size());

    if (center.x < viewport.x - 90.0f || center.x > viewport.x + viewport.width + 90.0f ||
        center.y < viewport.y - 90.0f || center.y > viewport.y + viewport.height + 90.0f) {
      continue;
    }

    // A group only flies a fleet name and admiral when every visible hull belongs to the same
    // fleet. A mixed brawl keeps its neutral pooled banner, so the flag never lies.
    const Fleet* groupFleet = nullptr;
    if (!members.empty()) {
      groupFleet = game_.fleetForUnit(units[static_cast<size_t>(members.front())].id);
      for (const int index : members) {
        if (game_.fleetForUnit(units[static_cast<size_t>(index)].id) != groupFleet) {
          groupFleet = nullptr;
          break;
        }
      }
    }
    const Admiral* groupAdmiral =
        groupFleet != nullptr ? game_.admiralById(groupFleet->admiralId) : nullptr;

    // A hull below its full hit points is one the yard can still save, so it flies the cross at
    // any zoom: the mark is a warning, not a readout.
    const bool damaged = maxHealth > 0.0f && health < maxHealth;

    if (members.size() == 1) {
      if (!zoomedIn && !selected) {
        if (damaged) repairMarks.push_back({center.x, top - 20.0f});
        continue;
      }
      const float barWidth = std::max(30.0f, hullLength * 1.6f);
      const Rectangle bar{center.x - barWidth * 0.5f, center.y + hullLength * 0.5f + 7.0f,
                          barWidth, 4.0f};
      healthBar(bar, maxHealth > 0.0f ? health / maxHealth : 0.0f);
      const std::string value = number(power);
      const Vector2 size = measureText(value, 11.0f, FontWeight::Bold);
      label(value, bar.x + bar.width + 5.0f, bar.y + bar.height * 0.5f - size.y * 0.5f, 11.0f,
            selected ? kGold : Color{220, 190, 130, 220}, FontWeight::Bold);

      // A lone fleet ship still wears its command: a compact Stellaris-style nameplate with
      // the admiral's portrait rides above the hull, so a spread-out squadron reads as
      // commanded even before it closes up into one banner.
      Rectangle nameplate{};
      if (groupFleet != nullptr && (selected || zoomedIn)) {
        const std::string title = trimTo(groupFleet->name, 20);
        const std::string subtitle = groupAdmiral != nullptr
                                         ? "ADM " + trimTo(groupAdmiral->fullName(), 20)
                                         : "no admiral embarked";
        const float textWidth = std::max(measureText(title, 11.0f, FontWeight::Bold).x,
                                         measureText(subtitle, 10.0f, FontWeight::SemiBold).x);
        const float plateWidth =
            std::max(148.0f, textWidth + (groupAdmiral != nullptr ? 46.0f : 18.0f));
        const Rectangle plate{center.x - plateWidth * 0.5f, top - 44.0f, plateWidth, 36.0f};
        nameplate = plate;
        raisedPanel(plate);
        if (selected) {
          DrawRectangleLinesEx({plate.x - 1.0f, plate.y - 1.0f, plate.width + 2.0f,
                                plate.height + 2.0f},
                               1.0f, kAccent);
        }
        const float textX = groupAdmiral != nullptr ? plate.x + 40.0f : plate.x + 8.0f;
        if (groupAdmiral != nullptr) {
          drawAdmiralPortrait(*groupAdmiral, {plate.x + 5.0f, plate.y + 3.0f, 30.0f, 30.0f});
        }
        label(title, textX, plate.y + 3.0f, 11.0f, kText, FontWeight::Bold);
        label(subtitle, textX, plate.y + 18.0f, 10.0f,
              groupAdmiral != nullptr ? kGold : kMuted, FontWeight::SemiBold);
      }
      // Beside the nameplate when the ship flies one, otherwise over the masts, so the cross
      // never lands on the flag's own text.
      if (damaged) {
        repairMarks.push_back(nameplate.width > 0.0f
                                  ? Vector2{nameplate.x - 13.0f,
                                            nameplate.y + nameplate.height * 0.5f}
                                  : Vector2{center.x, top - 20.0f});
      }
      continue;
    }

    // A group: one banner pooling the ships, so a dozen hulls in a harbour read as a single
    // compact stack of name, health, power and a shared bar instead of a pile of overlaps.
    const int ships = static_cast<int>(members.size());
    const std::string title = groupFleet != nullptr ? trimTo(groupFleet->name, 24)
                                                   : std::to_string(ships) + " SHIPS";
    const std::string subtitle =
        groupAdmiral != nullptr ? "ADM " + trimTo(groupAdmiral->fullName(), 24) : "";
    const std::string hpText = "HP " + number(health);
    const std::string pwrText = "PWR " + number(power);
    const float titleWidth = measureText(title, 12.0f, FontWeight::Bold).x;
    const float hpWidth = measureText(hpText, 12.0f, FontWeight::Bold).x;
    const float pwrWidth = measureText(pwrText, 12.0f, FontWeight::Bold).x;
    const float portraitWidth = groupAdmiral != nullptr ? 34.0f : 0.0f;
    const float plateWidth = std::max(
        172.0f, portraitWidth + std::max(titleWidth, measureText(subtitle, 10.0f).x) +
                    hpWidth + pwrWidth + 62.0f);
    const float plateHeight = groupAdmiral != nullptr ? 48.0f : 36.0f;
    const Rectangle plate{center.x - plateWidth * 0.5f, top - plateHeight - 14.0f, plateWidth,
                          plateHeight};
    raisedPanel(plate);
    if (selected) {
      DrawRectangleLinesEx({plate.x - 1.0f, plate.y - 1.0f, plate.width + 2.0f, plate.height + 2.0f},
                           1.0f, kAccent);
    }
    if (groupAdmiral != nullptr) {
      drawAdmiralPortrait(*groupAdmiral, {plate.x + 5.0f, plate.y + 7.0f, 34.0f, 34.0f});
    }
    const float textX = plate.x + (groupAdmiral != nullptr ? 46.0f : 10.0f);
    label(title, textX, plate.y + 5.0f, 12.0f, kText, FontWeight::Bold);
    if (!subtitle.empty()) {
      label(subtitle, textX, plate.y + 21.0f, 10.0f, kGold, FontWeight::SemiBold);
    }
    label(hpText, plate.x + plate.width - 10.0f - pwrWidth - 12.0f - hpWidth, plate.y + 6.0f,
          12.0f, kGreen, FontWeight::Bold);
    label(pwrText, plate.x + plate.width - 10.0f - pwrWidth, plate.y + 6.0f, 12.0f, kGold,
          FontWeight::Bold);
    healthBar({plate.x + 10.0f, plate.y + plate.height - 12.0f, plate.width - 20.0f, 7.0f},
              maxHealth > 0.0f ? health / maxHealth : 0.0f);

    // A squadron with even one damaged hull wears a single cross beside its banner: the pooled
    // bar already shows how badly hurt the formation is.
    if (damaged) {
      repairMarks.push_back({plate.x - 13.0f, plate.y + plate.height * 0.5f});
    }
  }

  // The medical cross: a white cross on a red disc, bobbing and pulsing so it reads as a
  // request for the repair yard rather than another status plate. Drawn last, above every
  // other ship readout.
  for (const Vector2& at : repairMarks) {
    const float phase = static_cast<float>(nowMs_) * 0.005f;
    const float pulse = 0.82f + 0.18f * std::sin(phase * 2.0f);
    const float radius = 7.5f * pulse;
    const float thickness = radius * 0.64f;
    const Vector2 center{at.x, at.y + 2.0f * std::sin(phase * 1.3f)};
    const unsigned char alpha = static_cast<unsigned char>(195.0f + 60.0f * pulse);
    DrawCircleV(center, radius + 1.5f, Color{18, 14, 14, 200});
    DrawCircleV(center, radius, Color{204, 62, 56, alpha});
    const Color ink{246, 244, 240, alpha};
    DrawRectangleRec(
        {center.x - radius * 0.78f, center.y - thickness * 0.5f, radius * 1.56f, thickness}, ink);
    DrawRectangleRec(
        {center.x - thickness * 0.5f, center.y - radius * 0.78f, thickness, radius * 1.56f}, ink);
  }
}

// A developer screen: every baked frame of every hull, laid out in a strip so the sprites and
// their compass alignment can be eyeballed from a screenshot.
void App::drawNavalDebugScreen() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  DrawRectangle(0, 0, static_cast<int>(width), static_cast<int>(height), Color{10, 14, 20, 255});
  label("HULL SPRITE SHEETS  -  frame 0 = north (up-screen), 24 frames of 15 degrees", 20.0f,
        12.0f, 18.0f, kText, FontWeight::Bold);

  const NavalModelKind kinds[] = {NavalModelKind::other,
NavalModelKind::aircraft_carrier,
NavalModelKind::submarine,
NavalModelKind::corvette,
NavalModelKind::cruiser,
NavalModelKind::destroyer,
NavalModelKind::frigate,
NavalModelKind::patrol
                                  };
  const char* names[] = {"SHIP", "SMALL SHIP", "SUBMARINE", "AUXILIARY"};

  const double samples[] = {0.0, 90.0, 180.0, 270.0};
  const char* compass[] = {"N", "E", "S", "W"};

  float y = 46.0f;
  for (int k = 0; k < 4; ++k) {
    label(names[k], 20.0f, y + 56.0f, 15.0f, kAccent, FontWeight::Bold);
    for (int s = 0; s < 4; ++s) {
      const float x = 150.0f + static_cast<float>(s) * 140.0f;
      const RenderTexture2D* frame = naval_.frameFor(kinds[k], samples[s]);
      if (frame == nullptr) continue;
      // A light plate under each hull: dark models would otherwise vanish into the screen
      // background, both here and in an ASCII read-back.
      DrawRectangle(static_cast<int>(x), static_cast<int>(y), 130, 130, Color{205, 205, 205, 255});
      const Rectangle source{0.0f, 0.0f, static_cast<float>(frame->texture.width),
                             -static_cast<float>(frame->texture.height)};
      DrawTexturePro(frame->texture, source, {x, y, 130.0f, 130.0f}, {0.0f, 0.0f}, 0.0f, WHITE);
      label(compass[s], x + 58.0f, y + 130.0f, 13.0f, kText, FontWeight::Bold);
    }
    y += 158.0f;
  }

  // A reference arrow: north is up-screen on the map, so a hull whose bow points up on the N
  // sample is aligned correctly.
  (void)width;
  (void)height;
}

namespace {

/// One image from data/, decoded once and kept for the run. Flags and the vessel photograph
/// both come through here, so each file is loaded at most once; a missing file caches a null
/// entry instead of retrying every frame. The map owns its entries for the process lifetime,
/// which means an open/close cycle can never leave a panel pointing at a freed texture.
const Texture2D* cachedPhoto(const std::string& path) {
  static std::unordered_map<std::string, Texture2D> cache;
  const auto found = cache.find(path);
  if (found != cache.end()) return found->second.id != 0 ? &found->second : nullptr;

  Texture2D texture{};
  Image image = LoadImage(resolveDataFile(path).c_str());
  if (image.data != nullptr) {
    texture = LoadTextureFromImage(image);
    if (texture.id != 0) SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
  }
  UnloadImage(image);

  const auto inserted = cache.emplace(path, texture);
  return inserted.first->second.id != 0 ? &inserted.first->second : nullptr;
}

/// The flag asset for a naval class's origin. A few classes were built for states that no
/// longer exist; they fly the flag of their principal successor rather than showing nothing.

void drawAdmiralPortrait(const Admiral& admiral, Rectangle box) {
  DrawRectangleRec(box, Color{18, 22, 26, 255});
  const Texture2D* photo = cachedPhoto(admiral.pathPhoto);
  if (photo != nullptr && photo->width > 0 && photo->height > 0) {
    // Cover-crop instead of letterboxing: a leader portrait should fill its frame, and the
    // source photos have different aspect ratios.
    const float imageAspect = static_cast<float>(photo->width) / static_cast<float>(photo->height);
    const float boxAspect = box.width / box.height;
    Rectangle source{0.0f, 0.0f, static_cast<float>(photo->width),
                     static_cast<float>(photo->height)};
    if (imageAspect > boxAspect) {
      source.width = source.height * boxAspect;
      source.x = (static_cast<float>(photo->width) - source.width) * 0.5f;
    } else {
      source.height = source.width / boxAspect;
      source.y = (static_cast<float>(photo->height) - source.height) * 0.5f;
    }
    DrawTexturePro(*photo, source, box, {0.0f, 0.0f}, 0.0f, WHITE);
  } else {
    std::string initials;
    if (!admiral.name.empty()) initials.push_back(admiral.name.front());
    if (!admiral.surname.empty()) initials.push_back(admiral.surname.front());
    const int size = static_cast<int>(std::max(14.0f, box.height * 0.38f));
    const int width = MeasureText(initials.c_str(), size);
    DrawText(initials.c_str(), static_cast<int>(box.x + (box.width - width) * 0.5f),
             static_cast<int>(box.y + (box.height - size) * 0.5f), size, kMuted);
  }
  DrawRectangleLinesEx(box, 1.0f, kBevelLo);
}

const Texture2D* flagForISO(const std::string& origin) {
  return cachedPhoto("flag/" + origin + ".png");
}
const Texture2D* flagForOrigin(const std::string& origin) {
  static const std::unordered_map<std::string, std::string> kSuccessors = {
      {"Ex-USSR", "Russia"},
      {"Ex-East Germany", "Germany"},
      {"Ex-Yugoslavia", "Serbia"},
  };
  if (origin.empty()) return nullptr;
  const auto successor = kSuccessors.find(origin);
  const std::string& name = successor == kSuccessors.end() ? origin : successor->second;
  return cachedPhoto("flag/" + name + ".png");
}

/// Draw a flag inside a fixed box, fitted to its own aspect ratio. The flags in data/flag are
/// not all the same shape, so they are letterboxed on a dark backing instead of stretched.
void drawFlag(const Texture2D& flag, Rectangle box) {
  // Flags are icons, so they get the same enlargement as the type around them.
  box.width *= kUiIconScale;
  box.height *= kUiIconScale;
  DrawRectangleRec(box, Color{18, 22, 26, 255});
  const float scale = std::min(box.width / static_cast<float>(flag.width),
                               box.height / static_cast<float>(flag.height));
  const float width = static_cast<float>(flag.width) * scale;
  const float height = static_cast<float>(flag.height) * scale;
  DrawTexturePro(flag,
                 {0.0f, 0.0f, static_cast<float>(flag.width),
                  static_cast<float>(flag.height)},
                 {box.x + (box.width - width) * 0.5f, box.y + (box.height - height) * 0.5f,
                  width, height},
                 {0.0f, 0.0f}, 0.0f, WHITE);
  DrawRectangleLinesEx(box, 1.0f, kBevelLo);
}

}  // namespace

// ---------------------------------------------------------------------------
// Fleet screen
// ---------------------------------------------------------------------------
void App::drawVesselInfoModal() {
  if (infoVessel_ == nullptr) return;
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  const Vessel& v = *infoVessel_;

  DrawRectangle(0, 0, static_cast<int>(width), static_cast<int>(height), Color{0, 0, 0, 150});

  // Wide enough for the spec sheet on the left and the photograph on the right. The photo is
  // drawn inside the plate, so opening the modal no longer drops an image across the text.
  const Rectangle panel{width * 0.5f - 380.0f, height * 0.5f - 175.0f, 760.0f, 350.0f};
  raisedPanel(panel);
  const Rectangle header{panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 30.0f};
  headerStrip(header);
  label(trimTo(v.companyName, 40), header.x + 22.0f, header.y + 6.0f, 18.0f, kText,
        FontWeight::Bold);

  const float columnX = panel.x + 20.0f;
  float y = panel.y + 56.0f;
  auto row = [&](const std::string& k, const std::string& val) {
    label(k, columnX, y, 15.0f, kMuted);
    label(trimTo(val, 24), columnX + 130.0f, y, 15.0f, kText);
    y += 26.0f;
  };
  row("Ship name", v.shipName);
  row("Size", number(v.gt) + " GT");
  row("Dimensions", std::to_string(static_cast<int>(v.length)) + " x " +
                        std::to_string(static_cast<int>(v.width)) + " m");
  row("Built", std::to_string(v.builtYear));
  row("Fleet nation", v.fleetNation.empty() ? "-" : v.fleetNation);

  // The photograph fills a recessed well on the right, scaled down to fit without distorting
  // the aspect ratio, or omitted when the asset is missing.
  const Texture2D* photo = cachedPhoto("requin.jpg");
  const Rectangle frame{panel.x + panel.width - 336.0f, panel.y + 56.0f, 316.0f, 220.0f};
  sunkenPanel(frame);
  if (photo != nullptr) {
    const float scale = std::min(frame.width / static_cast<float>(photo->width),
                                 frame.height / static_cast<float>(photo->height));
    const float drawWidth = static_cast<float>(photo->width) * scale;
    const float drawHeight = static_cast<float>(photo->height) * scale;
    DrawTexturePro(*photo,
                   {0.0f, 0.0f, static_cast<float>(photo->width),
                    static_cast<float>(photo->height)},
                   {frame.x + (frame.width - drawWidth) * 0.5f,
                    frame.y + (frame.height - drawHeight) * 0.5f, drawWidth, drawHeight},
                   {0.0f, 0.0f}, 0.0f, WHITE);
  }

  if (button({panel.x + panel.width - 130.0f, panel.y + panel.height - 46.0f, 112.0f, 32.0f},
             "Close", true, true)) {
    infoVessel_ = nullptr;
  }
}

void App::drawFleetScreen() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  const Rectangle area{20.0f, kTopBar + 14.0f, width - 40.0f, height - kTopBar - kBottomBar - 28.0f};

  raisedPanel(area);
  const Rectangle header{area.x + 4.0f, area.y + 4.0f, area.width - 8.0f, 30.0f};
  headerStrip(header);
  label("TRANSPORTATION STRUCTURE DIRECTORY", header.x + 22.0f, header.y + 8.0f, 18.0f, kText,
        FontWeight::Bold);

  // The query lives on the app so what you type survives the frame and the filter below
  // can actually see it.
  textField(10, {area.x + 16.0f, area.y + 46.0f, 360.0f, 30.0f}, fleetQuery_,
            "Search owned, catalog and naval fleets...");

  const std::vector<FleetEntry> fleet = game_.fleet();
  const std::vector<NavalVessel>& naval = catalog_.navalVessels();
  std::string summary = std::to_string(fleet.size()) + " owned  |  " +
                        std::to_string(catalog_.vessels().size()) + " in the catalog  |  " +
                        std::to_string(naval.size()) + " naval classes";
  label(summary, area.x + 16.0f, area.y + 84.0f, 15.0f, kMuted);

  // One scrollable list: owned vessels first, then the rest of the catalog for sale, then
  // the naval fleets. The search box filters all three.
  struct Entry {
    const Vessel* vessel = nullptr;
    const NavalVessel* naval = nullptr;
    bool owned = false;
    bool busy = false;
    const Port* destination = nullptr;
    bool header = false;
    std::string headerText;
  };
  std::vector<Entry> entries;
  entries.reserve(fleet.size() + catalog_.vessels().size() + naval.size() + 1);

  for (const FleetEntry& entry : fleet) {
    if (entry.vessel == nullptr) continue;
    if (!match(fleetQuery_,
               {entry.vessel->companyName, entry.vessel->shipName, entry.vessel->fleetNation})) {
      continue;
    }
    Entry row;
    row.vessel = entry.vessel;
    row.owned = true;
    row.busy = entry.busy;
    row.destination = entry.toPort;
    entries.push_back(row);
  }
  for (const Vessel& vessel : catalog_.vessels()) {
    if (game_.vesselOwned(vessel.id)) continue;
    if (!match(fleetQuery_, {vessel.companyName, vessel.shipName, vessel.fleetNation})) continue;
    Entry row;
    row.vessel = &vessel;
    entries.push_back(row);
  }

  std::vector<Entry> navalRows;
  navalRows.reserve(naval.size());
  for (const NavalVessel& vessel : naval) {
    if (!match(fleetQuery_, {vessel.shipName, vessel.type, vessel.origin, vessel.manufacturer,
                             vessel.navies})) {
      continue;
    }
    Entry row;
    row.naval = &vessel;
    navalRows.push_back(row);
  }
  if (!navalRows.empty()) {
    Entry header;
    header.header = true;
    header.headerText = "NAVAL FLEETS  -  " + std::to_string(navalRows.size()) + " classes";
    entries.push_back(header);
    entries.insert(entries.end(), navalRows.begin(), navalRows.end());
  }

  const Rectangle list{area.x + 12.0f, area.y + 108.0f, area.width - 24.0f,
                       area.height - 120.0f};
  sunkenPanel(list);
  const float itemHeight = 56.0f;
  const float total = static_cast<float>(entries.size()) * itemHeight;

  const bool hovered = CheckCollisionPointRec(GetMousePosition(), list);
  if (hovered) {
    fleetScroll_ -= GetMouseWheelMove() * 60.0f;
    fleetScroll_ = std::max(0.0f, std::min(std::max(0.0f, total - list.height), fleetScroll_));
  }

  BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width),
                   static_cast<int>(list.height));

  int firstVisible = static_cast<int>(fleetScroll_ / itemHeight);
  int lastVisible = std::min(static_cast<int>(entries.size()) - 1,
                             firstVisible + static_cast<int>(list.height / itemHeight) + 2);

  for (int index = std::max(0, firstVisible); index <= lastVisible; ++index) {
    const Entry& entry = entries[static_cast<size_t>(index)];
    const float y = list.y + index * itemHeight - fleetScroll_;
    const Rectangle row{list.x, y, list.width, itemHeight - 4.0f};

    if (entry.header) {
      DrawRectangleRec(row, Color{54, 60, 66, 255});
      DrawLineEx({row.x, row.y}, {row.x + row.width, row.y}, 1.0f, kBevelHi);
      label(entry.headerText, row.x + 12.0f, row.y + 16.0f, 16.0f, kAccent, FontWeight::Bold);
      continue;
    }

    if (entry.naval != nullptr) {
      const NavalVessel& vessel = *entry.naval;
      DrawRectangleRec(row, Color{40, 44, 60, 235});

      const Texture2D* flag = flagForOrigin(vessel.origin);
      const float textX = row.x + (flag != nullptr ? 50.0f : 12.0f);
      if (flag != nullptr) drawFlag(*flag, {row.x + 12.0f, row.y + 8.0f, 30.0f, 20.0f});


      label(trimTo(vessel.shipName, flag != nullptr ? 36 : 40), textX, row.y + 6.0f, 18.0f,
            kText);

      std::ostringstream navalMeta;
      navalMeta << (vessel.type.empty() ? "-" : vessel.type) << "  |  "
                << (vessel.origin.empty() ? "-" : vessel.origin) << "  |  "
                << (vessel.displacement.empty() ? "-" : vessel.displacement) << "  |  "
                << (vessel.maxSpeed.empty() ? "-" : vessel.maxSpeed) << "  |  commissioned "
                << (vessel.commissioned.empty() ? "-" : vessel.commissioned);
      label(trimTo(navalMeta.str(), flag != nullptr ? 92 : 96), textX, row.y + 30.0f, 14.0f,
            kMuted);
      label(trimTo(vessel.navies.empty() ? "No recorded operator" : vessel.navies, 44),
            row.x + row.width - 360.0f, row.y + 18.0f, 15.0f, kBlue);
      continue;
    }

    const char vesselClassChar = vesselClass(entry.vessel->length);
    const double price = hullPrice(entry.vessel->gt);

    DrawRectangleRec(row, entry.owned ? Color{38, 50, 42, 235} : Color{44, 48, 53, 235});

    label(trimTo(entry.vessel->companyName, 34), row.x + 12.0f, row.y + 6.0f, 18.0f,
          entry.owned ? kGreen : kText);
    std::ostringstream meta;
    meta << entry.vessel->shipName << "  |  " << (vesselClassChar == '\0' ? '?' : vesselClassChar)
         << " class  |  " << number(entry.vessel->gt) << " GT  |  " << static_cast<int>(entry.vessel->length)
         << " x " << static_cast<int>(entry.vessel->width) << " m  |  " << entry.vessel->builtYear
         << "  |  " << (entry.vessel->fleetNation.empty() ? "-" : entry.vessel->fleetNation);
    label(trimTo(meta.str(), 96), row.x + 12.0f, row.y + 30.0f, 14.0f, kMuted);

  if (button({row.x + row.width - 400, row.y + 8.0f, 178.0f, 36.0f}, "Vessel info", true)) {
    infoVessel_ = entry.vessel;
  }

    if (entry.owned) {
      const std::string status =
          entry.busy ? "At sea -> " + (entry.destination ? entry.destination->name : "?")
                     : "Ready to sail";
      label(status, row.x + row.width - 500.0f, row.y + 18.0f, 15.0f, entry.busy ? kGold : kGreen);
    } else {
      const bool affordable = game_.money() >= price;
      if (button({row.x + row.width - 190.0f, row.y + 8.0f, 178.0f, 36.0f},
                 "Buy - " + moneyText(price), affordable, true)) {
        std::string error;
        if (game_.buildVessel(entry.vessel->id, error)) {
          showToast("Bought " + entry.vessel->companyName);
        } else {
          showToast(error);
        }
      }
    }
  }

  EndScissorMode();
}

// ---------------------------------------------------------------------------
// Fleet Command / Admiralty
// ---------------------------------------------------------------------------
void App::drawFleetCommandScreen() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());
  const Rectangle area{20.0f, kTopBar + 14.0f, width - 40.0f,
                       height - kTopBar - kBottomBar - 28.0f};

  raisedPanel(area);
  const Rectangle header{area.x + 4.0f, area.y + 4.0f, area.width - 8.0f, 30.0f};
  headerStrip(header);
  label("FLEET COMMAND  -  ADMIRALTY", header.x + 22.0f, header.y + 8.0f, 18.0f, kText,
        FontWeight::Bold);

  const FleetLimits limits = game_.fleetLimits();
  const float tabY = area.y + 42.0f;
  const char* const kTabs[2] = {"FLEETS", "ADMIRALTY"};
  for (int i = 0; i < 2; ++i) {
    const Rectangle tab{area.x + 16.0f + 132.0f * static_cast<float>(i), tabY, 124.0f, 28.0f};
    if (button(tab, kTabs[i], true, fleetCommandTab_ == i)) fleetCommandTab_ = i;
  }

  const std::string summary =
      "FLEETS " + std::to_string(game_.fleets().size()) + "/" + std::to_string(limits.maxFleets) +
      "   |   SHIPS PER FLEET " + std::to_string(limits.maxShipsPerFleet) +
      "   |   ADMIRALS " + std::to_string(game_.hiredAdmiralCount()) + "/" +
      std::to_string(limits.maxAdmirals);
  const float summaryWidth = measureText(summary, 14.0f, FontWeight::SemiBold).x;
  label(summary, area.x + area.width - 18.0f - summaryWidth, tabY + 6.0f, 14.0f, kMuted,
        FontWeight::SemiBold);

  // Keep the selection pointing at a fleet that still exists. A fleet can be disbanded from
  // this screen, which would otherwise leave the detail panel reading a stale id.
  if (game_.fleetById(fleetCommandSelected_) == nullptr && !game_.fleets().empty()) {
    fleetCommandSelected_ = game_.fleets().front().id;
    fleetCommandRenameFor_ = 0;
  }
  if (game_.fleetById(admiralAssignTarget_) == nullptr && !game_.fleets().empty()) {
    admiralAssignTarget_ = game_.fleets().front().id;
  }

  auto fleetStatus = [](const FleetRecap& recap) {
    if (recap.shipCount == 0) return std::string("EMPTY");
    if (recap.sunk == recap.shipCount) return std::string("WRECKED");
    if (recap.engaged > 0) return std::string("IN COMBAT");
    if (recap.atSea > 0) return std::string("AT SEA");
    if (recap.damaged > 0) return std::string("DAMAGED");
    if (recap.docked > 0) return std::string("IN PORT");
    return std::string("READY");
  };
  auto fleetStatusColor = [](const FleetRecap& recap) {
    if (recap.shipCount == 0 || recap.sunk == recap.shipCount) return kRed;
    if (recap.engaged > 0) return kRed;
    if (recap.atSea > 0 || recap.damaged > 0) return kGold;
    return kGreen;
  };

  const std::vector<FleetRecap> recaps = game_.fleetRecaps();
  auto recapFor = [&](int fleetId) -> const FleetRecap* {
    for (const FleetRecap& recap : recaps) {
      if (recap.fleetId == fleetId) return &recap;
    }
    return nullptr;
  };

  const float contentY = area.y + 80.0f;
  const float contentHeight = area.height - 96.0f;

  if (fleetCommandTab_ == 0) {
    // --- Left column: raise a fleet and list every fleet ---------------------
    const float leftWidth = 402.0f;
    const Rectangle left{area.x + 14.0f, contentY, leftWidth, contentHeight};
    const Rectangle right{left.x + left.width + 12.0f, contentY,
                          area.width - left.width - 40.0f, contentHeight};

    sunkenPanel(left);
    textField(20, {left.x + 12.0f, left.y + 12.0f, 240.0f, 30.0f}, fleetCommandNewName_,
              "New fleet name...");
    if (button({left.x + 260.0f, left.y + 12.0f, 128.0f, 30.0f}, "Raise Fleet",
               static_cast<int>(game_.fleets().size()) < limits.maxFleets, true)) {
      std::string error;
      if (game_.createFleet(fleetCommandNewName_, error)) {
        fleetCommandNewName_.clear();
        fleetCommandSelected_ = game_.fleets().back().id;
        fleetCommandRenameFor_ = 0;
        admiralAssignTarget_ = fleetCommandSelected_;
        showToast("Fleet raised: " + game_.fleets().back().name);
      } else {
        showToast(error);
      }
    }

    const Rectangle list{left.x + 8.0f, left.y + 50.0f, left.width - 16.0f,
                         left.height - 58.0f};
    const float cardHeight = 96.0f;
    const float total = static_cast<float>(recaps.size()) * cardHeight;
    const float viewHeight = list.height;
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
      fleetCommandListScroll_ -= GetMouseWheelMove() * 60.0f;
      fleetCommandListScroll_ =
          std::max(0.0f, std::min(std::max(0.0f, total - viewHeight), fleetCommandListScroll_));
    }

    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y),
                     static_cast<int>(list.width), static_cast<int>(list.height));
    if (recaps.empty()) {
      label("No fleets raised. Name one above and press Raise Fleet.", list.x + 10.0f,
            list.y + 14.0f, 15.0f, kMuted);
    }
    for (size_t index = 0; index < recaps.size(); ++index) {
      const FleetRecap& recap = recaps[index];
      const float y = list.y + static_cast<float>(index) * cardHeight - fleetCommandListScroll_;
      if (y + cardHeight < list.y - 4.0f || y > list.y + list.height + 4.0f) continue;
      const Rectangle card{list.x + 2.0f, y + 4.0f, list.width - 8.0f, cardHeight - 8.0f};
      const bool selected = recap.fleetId == fleetCommandSelected_;
      DrawRectangleRec(card, selected ? Color{54, 52, 42, 245} : Color{40, 44, 49, 245});
      DrawRectangleLinesEx(card, 1.0f, selected ? kAccent : kBevelLo);

      const Admiral* admiral = game_.admiralById(recap.admiralId);
      const float textX = card.x + 72.0f;
      if (admiral != nullptr) {
        drawAdmiralPortrait(*admiral, {card.x + 8.0f, card.y + 8.0f, 56.0f, 56.0f});
      } else {
        const Rectangle emptyPortrait{card.x + 8.0f, card.y + 8.0f, 56.0f, 56.0f};
        DrawRectangleRec(emptyPortrait, Color{22, 26, 30, 255});
        DrawRectangleLinesEx(emptyPortrait, 1.0f, kBevelLo);
        label("NO ADM", emptyPortrait.x + 8.0f, emptyPortrait.y + 20.0f, 10.0f, kMuted,
              FontWeight::SemiBold);
      }
      label(trimTo(recap.name, 30), textX, card.y + 8.0f, 18.0f, selected ? kText : kSteel,
            FontWeight::Bold);
      const std::string status = fleetStatus(recap);
      const float statusWidth = measureText(status, 12.0f, FontWeight::Bold).x;
      label(status, card.x + card.width - 12.0f - statusWidth, card.y + 11.0f, 12.0f,
            fleetStatusColor(recap), FontWeight::Bold);

      const std::string admiralText =
          admiral != nullptr ? "ADM " + trimTo(admiral->fullName(), 26) : "No admiral embarked";
      label(admiralText, textX, card.y + 32.0f, 13.0f, admiral != nullptr ? kGold : kMuted,
            FontWeight::SemiBold);
      const std::string ships = std::to_string(recap.shipCount) + "/" +
                                std::to_string(recap.capacity) + " ships   PWR " +
                                number(recap.power);
      label(ships, textX, card.y + 52.0f, 13.0f, kMuted);
      healthBar({card.x + 12.0f, card.y + card.height - 17.0f, card.width - 24.0f, 5.0f},
                recap.maxHealth > 0.0f ? recap.health / recap.maxHealth : 0.0f);

      if (CheckCollisionPointRec(GetMousePosition(), card) &&
          IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        fleetCommandSelected_ = recap.fleetId;
        fleetCommandRenameFor_ = 0;
        clickConsumed_ = true;
      }
    }
    EndScissorMode();

    // --- Right column: the selected fleet's detail --------------------------
    sunkenPanel(right);
    const Fleet* fleet = game_.fleetById(fleetCommandSelected_);
    const FleetRecap* recap = fleet != nullptr ? recapFor(fleet->id) : nullptr;
    if (fleet == nullptr || recap == nullptr) {
      label("Select a fleet on the left to inspect and manage it.", right.x + 16.0f,
            right.y + 18.0f, 16.0f, kMuted);
      return;
    }

    const Admiral* admiral = game_.admiralById(fleet->admiralId);
    label(trimTo(fleet->name, 42), right.x + 16.0f, right.y + 12.0f, 20.0f, kText,
          FontWeight::Bold);
    const std::string status = fleetStatus(*recap);
    const float statusWidth = measureText(status, 13.0f, FontWeight::Bold).x;
    label(status, right.x + right.width - 16.0f - statusWidth, right.y + 16.0f, 13.0f,
          fleetStatusColor(*recap), FontWeight::Bold);
    const std::string stats =
        std::to_string(recap->shipCount) + "/" + std::to_string(recap->capacity) +
        " ships   |   HP " + number(recap->health) + "/" + number(recap->maxHealth) +
        "   |   PWR " + number(recap->power) +
        (admiral != nullptr ? "   |   " + admiral->fullName() + " +" +
                                  std::to_string(static_cast<int>(admiral->bonus() * 100.0f)) +
                                  "% " + lower(admiralBuffLabel(admiral->buffKind()))
                            : "   |   no admiral");
    label(trimTo(stats, 118), right.x + 16.0f, right.y + 40.0f, 13.0f, kMuted);
    healthBar({right.x + 16.0f, right.y + 60.0f, right.width - 32.0f, 5.0f},
              recap->maxHealth > 0.0f ? recap->health / recap->maxHealth : 0.0f);

    if (fleetCommandRenameFor_ != fleet->id) {
      fleetCommandRename_ = fleet->name;
      fleetCommandRenameFor_ = fleet->id;
    }
    textField(21, {right.x + 16.0f, right.y + 74.0f, 250.0f, 28.0f}, fleetCommandRename_,
              "Fleet name");
    if (button({right.x + 274.0f, right.y + 74.0f, 96.0f, 28.0f}, "Rename", true)) {
      std::string error;
      if (game_.renameFleet(fleet->id, fleetCommandRename_, error)) {
        showToast("Fleet renamed.");
      } else {
        showToast(error);
      }
    }
    if (button({right.x + 378.0f, right.y + 74.0f, 112.0f, 28.0f}, "Disband", true)) {
      std::string error;
      if (game_.disbandFleet(fleet->id, error)) {
        showToast("Fleet disbanded; its ships are independent again.");
        fleetCommandSelected_ = 0;
        fleetCommandRenameFor_ = 0;
        return;
      }
      showToast(error);
    }

    // Admiral slot. Direct assignment and deassignment live here, next to the fleet they
    // affect, while the Admiralty tab handles engagement and the full roster.
    const Rectangle admiralBox{right.x + 16.0f, right.y + 110.0f, right.width - 32.0f, 96.0f};
    sunkenPanel(admiralBox);
    label("FLAG OFFICER", admiralBox.x + 10.0f, admiralBox.y + 6.0f, 12.0f, kMuted,
          FontWeight::SemiBold);
    if (admiral != nullptr) {
      drawAdmiralPortrait(*admiral, {admiralBox.x + 8.0f, admiralBox.y + 20.0f, 68.0f, 68.0f});
      const float flagX = admiralBox.x + 86.0f;
      label(trimTo(admiral->fullName(), 30), flagX, admiralBox.y + 20.0f, 18.0f, kGold,
            FontWeight::Bold);
      label("Level " + std::to_string(admiral->level) + "   " +
                admiralBuffLabel(admiral->buffKind()) + " +" +
                std::to_string(static_cast<int>(admiral->bonus() * 100.0f)) + "%",
            flagX, admiralBox.y + 44.0f, 13.0f, kText);
      label(trimTo(admiral->generalDescription, 88), flagX, admiralBox.y + 64.0f, 12.0f, kMuted);
      if (button({admiralBox.x + admiralBox.width - 118.0f, admiralBox.y + 22.0f, 104.0f, 28.0f},
                 "Dismiss", true)) {
        std::string error;
        if (game_.dismissAdmiralFromFleet(fleet->id, error)) {
          showToast(admiral->fullName() + " released from command.");
        } else {
          showToast(error);
        }
      }
    } else {
      label("No admiral embarked. Recruit an officer to lead this fleet.",
            admiralBox.x + 10.0f, admiralBox.y + 20.0f, 14.0f, kText);
      const bool admiralSlotFree =
          game_.hiredAdmiralCount() < game_.fleetLimits().maxAdmirals;
      float buttonX = admiralBox.x + 10.0f;
      int offered = 0;
      for (const Admiral& candidate : game_.admirals()) {
        if (candidate.fleetId != 0) continue;
        const bool recruit = !candidate.hired;
        const bool enabled =
            recruit ? admiralSlotFree && game_.money() >= static_cast<double>(candidate.buyCost)
                    : true;
        if (button({buttonX, admiralBox.y + 46.0f, 152.0f, 28.0f},
                   (recruit ? "Recruit " : "Assign ") + candidate.surname, enabled,
                   recruit)) {
          std::string error;
          const bool ok = recruit
                              ? game_.recruitAdmiralToFleet(candidate.id, fleet->id, error)
                              : game_.assignAdmiralToFleet(candidate.id, fleet->id, error);
          if (ok) {
            showToast(candidate.fullName() + " takes command of " + fleet->name + ".");
          } else {
            showToast(error);
          }
        }
        buttonX += 158.0f;
        ++offered;
        if (offered >= 5) break;
      }
      if (offered == 0) {
        label("No officer is free to take command. Release or recruit one in the Admiralty tab.",
              admiralBox.x + 10.0f, admiralBox.y + 50.0f, 12.0f, kMuted);
      } else {
        label("Engagement fee 500.  Recruiting posts the officer straight to this fleet.",
              admiralBox.x + 10.0f, admiralBox.y + 78.0f, 11.0f, kMuted);
      }
    }

    // Two lists side by side: ships already in the fleet, and every independent hull that
    // could join it. Both scroll independently, so a large navy never pushes the admiral block
    // off the panel.
    const float listsY = admiralBox.y + admiralBox.height + 10.0f;
    const float listsHeight = right.y + right.height - listsY - 10.0f;
    const float columnWidth = (right.width - 42.0f) * 0.5f;
    const Rectangle memberList{right.x + 16.0f, listsY, columnWidth, listsHeight};
    const Rectangle availableList{memberList.x + columnWidth + 10.0f, listsY, columnWidth,
                                 listsHeight};

    auto drawShipList = [&](const Rectangle& box, const std::string& title,
                            const std::vector<int>& ids, bool members) {
      sunkenPanel(box);
      label(title + "  -  " + std::to_string(ids.size()), box.x + 10.0f, box.y + 5.0f, 12.0f,
            kMuted, FontWeight::SemiBold);
      const Rectangle rows{box.x + 2.0f, box.y + 24.0f, box.width - 4.0f, box.height - 28.0f};
      float& scroll = members ? fleetCommandMemberScroll_ : fleetCommandAvailableScroll_;
      const float rowHeight = 40.0f;
      const float totalRows = static_cast<float>(ids.size()) * rowHeight;
      if (CheckCollisionPointRec(GetMousePosition(), rows)) {
        scroll -= GetMouseWheelMove() * 50.0f;
        scroll = std::max(0.0f, std::min(std::max(0.0f, totalRows - rows.height), scroll));
      }
      BeginScissorMode(static_cast<int>(rows.x), static_cast<int>(rows.y),
                       static_cast<int>(rows.width), static_cast<int>(rows.height));
      if (ids.empty()) {
        label(members ? "No ships assigned yet." : "No independent ships available.",
              rows.x + 8.0f, rows.y + 8.0f, 13.0f, kMuted);
      }
      for (size_t index = 0; index < ids.size(); ++index) {
        const NavalUnit* unit = game_.navalUnitById(ids[index]);
        if (unit == nullptr) continue;
        const float y = rows.y + static_cast<float>(index) * rowHeight - scroll;
        if (y + rowHeight < rows.y - 2.0f || y > rows.y + rows.height + 2.0f) continue;
        const Rectangle row{rows.x + 2.0f, y + 2.0f, rows.width - 6.0f, rowHeight - 4.0f};
        DrawRectangleRec(row, Color{40, 44, 49, 240});
        DrawRectangleLinesEx(row, 1.0f, kBevelLo);
        const NavalVessel* klass = catalog_.navalVesselById(unit->classId);
        label(trimTo(unit->className, 28), row.x + 8.0f, row.y + 4.0f, 14.0f,
              unit->sunk() ? kRed : kText, FontWeight::SemiBold);
        const std::string meta = "HP " + number(unit->health) + "/" + number(unit->maxHealth) +
                                 (klass != nullptr ? "   PWR " + number(klass->totalPower()) : "") +
                                 "   #" + std::to_string(unit->id);
        label(meta, row.x + 8.0f, row.y + 21.0f, 11.0f, unit->sunk() ? kRed : kMuted);
        if (button({row.x + row.width - 78.0f, row.y + 6.0f, 68.0f, 24.0f},
                   members ? "Remove" : "Add", !unit->sunk())) {
          std::string error;
          const bool ok = members ? game_.removeUnitFromFleet(unit->id, error)
                                  : game_.assignUnitToFleet(unit->id, fleet->id, error);
          showToast(ok ? (members ? "Ship detached from " + fleet->name + "."
                                 : "Ship joined " + fleet->name + ".")
                       : error);
        }
      }
      EndScissorMode();
    };

    std::vector<int> memberIds;
    for (const int id : fleet->unitIds) {
      if (game_.navalUnitById(id) != nullptr) memberIds.push_back(id);
    }

    //all units
    std::vector<int> availableIds;
    for (const NavalUnit& unit : game_.navalUnits()) {
      if (unit.sunk()) continue;
      if (game_.fleetForUnit(unit.id) != nullptr) continue;
      availableIds.push_back(unit.id);
    }
    drawShipList(memberList, "SHIPS IN FLEET", memberIds, true);
    drawShipList(availableList, "INDEPENDENT SHIPS", availableIds, false);
    return;
  }

  // --- Admiralty tab: engage officers and post them to a fleet ----------------
  const Rectangle roster{area.x + 14.0f, contentY, area.width - 28.0f, contentHeight};
  sunkenPanel(roster);
  label("Recruit / assign to", roster.x + 12.0f, roster.y + 12.0f, 13.0f, kMuted,
        FontWeight::SemiBold);
  float targetX = roster.x + 132.0f;
  for (const Fleet& fleet : game_.fleets()) {
    const bool active = fleet.id == admiralAssignTarget_;
    if (button({targetX, roster.y + 8.0f, 142.0f, 28.0f}, trimTo(fleet.name, 18), true, active)) {
      admiralAssignTarget_ = fleet.id;
    }
    targetX += 148.0f;
    if (targetX > roster.x + roster.width - 160.0f) break;
  }
  if (game_.fleets().empty()) {
    label("Raise a fleet in the Fleets tab before posting an admiral.", targetX,
          roster.y + 13.0f, 13.0f, kRed);
  }

  const Fleet* targetFleet = game_.fleetById(admiralAssignTarget_);
  const float gridTop = roster.y + 48.0f;
  const float cardGap = 12.0f;
  const float cardWidth = (roster.width - 36.0f - cardGap) * 0.5f;
  const float cardHeight = 178.0f;
  const int columns = 2;
  const std::vector<Admiral>& admirals = game_.admirals();
  const float total = static_cast<float>((admirals.size() + 1) / 2) * (cardHeight + cardGap);
  if (CheckCollisionPointRec(GetMousePosition(), roster)) {
    admiraltyScroll_ -= GetMouseWheelMove() * 60.0f;
    admiraltyScroll_ =
        std::max(0.0f, std::min(std::max(0.0f, total - (roster.height - 56.0f)),
                                admiraltyScroll_));
  }

  BeginScissorMode(static_cast<int>(roster.x + 2.0f), static_cast<int>(gridTop),
                   static_cast<int>(roster.width - 4.0f),
                   static_cast<int>(roster.height - 54.0f));
  for (size_t index = 0; index < admirals.size(); ++index) {
    const Admiral& admiral = admirals[index];
    const int col = static_cast<int>(index) % columns;
    const int row = static_cast<int>(index) / columns;
    const Rectangle card{roster.x + 12.0f + static_cast<float>(col) * (cardWidth + cardGap),
                         gridTop + static_cast<float>(row) * (cardHeight + cardGap) -
                             admiraltyScroll_,
                         cardWidth, cardHeight};
    if (card.y + card.height < gridTop - 4.0f || card.y > roster.y + roster.height + 4.0f) {
      continue;
    }
    DrawRectangleRec(card, Color{40, 44, 49, 245});
    DrawRectangleLinesEx(card, 1.0f, admiral.fleetId != 0 ? kGold : kBevelLo);
    drawAdmiralPortrait(admiral, {card.x + 12.0f, card.y + 20.0f, 78.0f, 78.0f});
    const float textX = card.x + 102.0f;
    label(trimTo(admiral.fullName(), 30), textX, card.y + 8.0f, 18.0f, kText,
          FontWeight::Bold);
    const std::string standing =
        !admiral.hired ? "NOT ENGAGED"
                       : admiral.fleetId != 0
                             ? "COMMANDS " + std::string(
                                   game_.fleetById(admiral.fleetId) != nullptr
                                       ? game_.fleetById(admiral.fleetId)->name
                                       : "A FLEET")
                             : "AWAITING COMMAND";
    const Color standingColor = !admiral.hired ? kMuted : (admiral.fleetId != 0 ? kGold : kGreen);
    const float standingWidth = measureText(standing, 11.0f, FontWeight::Bold).x;
    label(standing, card.x + card.width - 12.0f - standingWidth, card.y + 11.0f, 11.0f,
          standingColor, FontWeight::Bold);

    const std::string meta = trimTo(admiral.nationality, 18) + "   |   Level " +
                             std::to_string(admiral.level) + "   |   " +
                             admiralBuffLabel(admiral.buffKind()) + " +" +
                             std::to_string(static_cast<int>(admiral.bonus() * 100.0f)) + "%";
    label(meta, textX, card.y + 30.0f, 12.0f, kSteel, FontWeight::SemiBold);
    label(trimTo(admiral.descriptionBuff, 72), textX, card.y + 48.0f, 11.0f, kMuted);

    const std::vector<std::string> bio = wrapText(admiral.generalDescription, 11.0f,
                                                  card.width - 116.0f);
    for (size_t line = 0; line < bio.size() && line < 2; ++line) {
      label(trimTo(bio[line], 90), textX, card.y + 64.0f + 13.0f * static_cast<float>(line),
            11.0f, kText);
    }
    label("Engagement fee " + moneyText(static_cast<double>(admiral.buyCost)),
          textX, card.y + 94.0f, 11.0f, kMuted);

    if (!admiral.hired) {
      const bool canRecruit = targetFleet != nullptr &&
                              game_.hiredAdmiralCount() < game_.fleetLimits().maxAdmirals &&
                              game_.money() >= static_cast<double>(admiral.buyCost);
      if (button({card.x + 12.0f, card.y + card.height - 44.0f, 220.0f, 30.0f},
                 targetFleet != nullptr
                     ? "Recruit to " + trimTo(targetFleet->name, 16)
                     : "Select a fleet first",
                 canRecruit, true)) {
        std::string error;
        if (game_.recruitAdmiralToFleet(admiral.id, targetFleet->id, error)) {
          showToast(admiral.fullName() + " takes command of " + targetFleet->name + ".");
        } else {
          showToast(error);
        }
      }
    } else if (admiral.fleetId != 0) {
      if (button({card.x + 12.0f, card.y + card.height - 44.0f, 132.0f, 30.0f},
                 "Dismiss", true)) {
        std::string error;
        if (game_.dismissAdmiralFromFleet(admiral.fleetId, error)) {
          showToast(admiral.fullName() + " released from command.");
        } else {
          showToast(error);
        }
      }
      if (targetFleet != nullptr && targetFleet->id != admiral.fleetId &&
          button({card.x + 152.0f, card.y + card.height - 44.0f, 152.0f, 30.0f},
                 "Move to " + trimTo(targetFleet->name, 11), true)) {
        std::string error;
        if (game_.assignAdmiralToFleet(admiral.id, targetFleet->id, error)) {
          showToast(admiral.fullName() + " takes command of " + targetFleet->name + ".");
        } else {
          showToast(error);
        }
      }
    } else {
      if (button({card.x + 12.0f, card.y + card.height - 44.0f, 210.0f, 30.0f},
                 targetFleet != nullptr ? "Assign to " + trimTo(targetFleet->name, 20)
                                        : "Select a fleet first",
                 targetFleet != nullptr, true)) {
        std::string error;
        if (game_.assignAdmiralToFleet(admiral.id, targetFleet->id, error)) {
          showToast(admiral.fullName() + " takes command of " + targetFleet->name + ".");
        } else {
          showToast(error);
        }
      }
      if (button({card.x + 232.0f, card.y + card.height - 44.0f, 108.0f, 30.0f}, "Release",
                 true)) {
        std::string error;
        if (game_.releaseAdmiral(admiral.id, error)) {
          showToast(admiral.fullName() + " leaves the admiralty; the slot is free.");
        } else {
          showToast(error);
        }
      }
    }
  }
  EndScissorMode();
}

// ---------------------------------------------------------------------------
// Research screen
// ---------------------------------------------------------------------------

void App::drawResearchScreen() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());

  const std::vector<Technology>& table = technologies();
  const Research& levels = game_.researchLevels();
  const int count = static_cast<int>(table.size());

  // --- Layered dependency graph ---------------------------------------------
  // A technology sits one column to the right of its deepest parent. The rows inside a
  // column are ordered by barycentre sweeps, which pulls each node toward its parents and
  // children and keeps most links from crossing. The graph is fitted to the viewport on
  // entry, so no branch is born offscreen.
  std::unordered_map<std::string, int> indexOf;
  for (int i = 0; i < count; ++i) indexOf[table[i].id] = i;

  std::vector<std::vector<int>> parents(static_cast<size_t>(count));
  std::vector<std::vector<int>> children(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    for (const std::string& parentId : table[i].parentTechRequired) {
      const auto found = indexOf.find(parentId);
      if (found == indexOf.end()) continue;
      parents[static_cast<size_t>(i)].push_back(found->second);
      children[static_cast<size_t>(found->second)].push_back(i);
    }
  }

  std::vector<int> depth(static_cast<size_t>(count), 0);
  for (int pass = 0; pass < count; ++pass) {
    bool changed = false;
    for (int i = 0; i < count; ++i) {
      for (const int parent : parents[static_cast<size_t>(i)]) {
        const int candidate = depth[static_cast<size_t>(parent)] + 1;
        if (candidate > depth[static_cast<size_t>(i)]) {
          depth[static_cast<size_t>(i)] = candidate;
          changed = true;
        }
      }
    }
    if (!changed) break;
  }

  const int maxDepth = count > 0 ? *std::max_element(depth.begin(), depth.end()) : 0;
  std::vector<std::vector<int>> columns(static_cast<size_t>(maxDepth + 1));
  for (int i = 0; i < count; ++i) {
    columns[static_cast<size_t>(depth[static_cast<size_t>(i)])].push_back(i);
  }
  for (auto& column : columns) std::stable_sort(column.begin(), column.end());
  size_t maxRows = 1;
  for (const auto& column : columns) maxRows = std::max(maxRows, column.size());

  // Barycentre ordering: a forward sweep sorts each column by the average row of its
  // parents, a backward sweep by the average row of its children. Repeating both a few
  // times is the cheap Sugiyama recipe for a readable tree.
  std::vector<int> rowOf(static_cast<size_t>(count), 0);
  for (const auto& column : columns) {
    for (size_t row = 0; row < column.size(); ++row) {
      rowOf[static_cast<size_t>(column[row])] = static_cast<int>(row);
    }
  }

  auto orderColumn = [&](size_t columnIndex, bool byParents) {
    std::vector<int>& column = columns[columnIndex];
    std::vector<std::pair<double, int>> order;
    order.reserve(column.size());
    for (const int node : column) {
      const std::vector<int>& neighbours =
          byParents ? parents[static_cast<size_t>(node)] : children[static_cast<size_t>(node)];
      double sum = 0.0;
      int seen = 0;
      for (const int neighbour : neighbours) {
        sum += rowOf[static_cast<size_t>(neighbour)];
        ++seen;
      }
      const double barycentre = seen > 0 ? sum / seen : rowOf[static_cast<size_t>(node)];
      order.push_back({barycentre, node});
    }
    std::stable_sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
      if (a.first != b.first) return a.first < b.first;
      return rowOf[static_cast<size_t>(a.second)] < rowOf[static_cast<size_t>(b.second)];
    });
    for (size_t row = 0; row < order.size(); ++row) {
      column[row] = order[row].second;
      rowOf[static_cast<size_t>(order[row].second)] = static_cast<int>(row);
    }
  };

  for (int pass = 0; pass < 6; ++pass) {
    for (size_t column = 1; column < columns.size(); ++column) orderColumn(column, true);
    for (int column = static_cast<int>(columns.size()) - 2; column >= 0; --column) {
      orderColumn(static_cast<size_t>(column), false);
    }
  }

  constexpr float kNodeW = 140.0f;
  constexpr float kNodeH = 72.0f;
  constexpr float kColGap = 36.0f;
  constexpr float kRowGap = 18.0f;
  constexpr float kWorldPad = 16.0f;
  const float colStep = kNodeW + kColGap;
  const float rowStep = kNodeH + kRowGap;

  std::vector<Rectangle> nodes(static_cast<size_t>(count));
  for (size_t col = 0; col < columns.size(); ++col) {
    const float columnHeight = columns[col].size() * rowStep;
    const float top = (maxRows * rowStep - columnHeight) * 0.5f;
    for (size_t row = 0; row < columns[col].size(); ++row) {
      nodes[static_cast<size_t>(columns[col][row])] = {
          kWorldPad + static_cast<float>(col) * colStep,
          kWorldPad + top + static_cast<float>(row) * rowStep, kNodeW, kNodeH};
    }
  }
  const float worldW =
      kWorldPad * 2.0f +
      (columns.empty() ? 0.0f : static_cast<float>(columns.size() - 1) * colStep) + kNodeW;
  const float worldH =
      kWorldPad * 2.0f + (maxRows > 0 ? static_cast<float>(maxRows - 1) * rowStep : 0.0f) +
      kNodeH;
  const Rectangle world{0.0f, 0.0f, worldW, worldH};

  // --- Viewport and the side detail panel ----------------------------------
  const float detailWidth = std::min(360.0f, width * 0.32f);
  const Rectangle view{0.0f, kTopBar, width - detailWidth, height - kTopBar - kBottomBar};
  const Rectangle detail{width - detailWidth, kTopBar, detailWidth,
                         height - kTopBar - kBottomBar};

  if (researchNeedsCenter_) {
    constexpr float kFitMargin = 12.0f;
    const float fitX =
        world.width > 0.0f ? (view.width - kFitMargin * 2.0f) / world.width : 1.0f;
    const float fitY =
        world.height > 0.0f ? (view.height - kFitMargin * 2.0f) / world.height : 1.0f;
    researchZoom_ = std::clamp(std::min(fitX, fitY), 0.5f, 1.0f);
    researchPan_.x = view.x + (view.width - world.width * researchZoom_) * 0.5f;
    researchPan_.y = view.y + (view.height - world.height * researchZoom_) * 0.5f;
    researchNeedsCenter_ = false;
  }

  auto project = [&](Vector2 point) {
    return Vector2{researchPan_.x + point.x * researchZoom_,
                   researchPan_.y + point.y * researchZoom_};
  };
  auto unproject = [&](Vector2 point) {
    return Vector2{(point.x - researchPan_.x) / researchZoom_,
                   (point.y - researchPan_.y) / researchZoom_};
  };
  auto screenRect = [&](Rectangle r) {
    const Vector2 origin = project({r.x, r.y});
    return Rectangle{origin.x, origin.y, r.width * researchZoom_, r.height * researchZoom_};
  };

  const Vector2 mouse = GetMousePosition();
  const bool overView = CheckCollisionPointRec(mouse, view) && !navigationOpen_ &&
                        infoVessel_ == nullptr && shipyardPortId_ == 0;

  // --- View controls: wheel zooms at the cursor, drag pans, arrow keys pan ----
  if (overView) {
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
      const Vector2 anchor = unproject(mouse);
      researchZoom_ = std::clamp(researchZoom_ + wheel * 0.15f, 0.35f, 2.0f);
      researchPan_.x = mouse.x - anchor.x * researchZoom_;
      researchPan_.y = mouse.y - anchor.y * researchZoom_;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
        IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) ||
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
      researchPanning_ = true;
      researchDragged_ = false;
      researchDragStart_ = mouse;
      researchPanOrigin_ = researchPan_;
    }
  }
  if (researchPanning_) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) ||
        IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
      if (std::hypot(mouse.x - researchDragStart_.x, mouse.y - researchDragStart_.y) > 5.0f) {
        researchDragged_ = true;
      }
      if (researchDragged_) {
        researchPan_ = {researchPanOrigin_.x + (mouse.x - researchDragStart_.x),
                        researchPanOrigin_.y + (mouse.y - researchDragStart_.y)};
      }
    } else {
      // A press that never travelled is a click: select the node under the cursor.
      if (!researchDragged_ && overView) {
        for (int i = count - 1; i >= 0; --i) {
          if (CheckCollisionPointRec(mouse, screenRect(nodes[i]))) {
            researchSelectedId_ = table[i].id;
            clickConsumed_ = true;
            break;
          }
        }
      }
      researchPanning_ = false;
      researchDragged_ = false;
    }
  }

  const float keyStep = 520.0f * GetFrameTime();
  if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) researchPan_.x += keyStep;
  if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) researchPan_.x -= keyStep;
  if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) researchPan_.y += keyStep;
  if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) researchPan_.y -= keyStep;
  if (IsKeyPressed(KEY_F)) researchNeedsCenter_ = true;

  // Keep the graph on screen: fit it when it is smaller than the viewport, otherwise let
  // the player pan but always leave a generous strip of the tree reachable.
  {
    const float contentW = world.width * researchZoom_;
    const float contentH = world.height * researchZoom_;
    if (contentW <= view.width - 32.0f) {
      researchPan_.x = std::clamp(researchPan_.x, view.x + 16.0f,
                                  view.x + view.width - contentW - 16.0f);
    } else {
      researchPan_.x = std::clamp(researchPan_.x,
                                  view.x + view.width - contentW - 40.0f, view.x + 40.0f);
    }
    if (contentH <= view.height - 32.0f) {
      researchPan_.y = std::clamp(researchPan_.y, view.y + 16.0f,
                                  view.y + view.height - contentH - 16.0f);
    } else {
      researchPan_.y = std::clamp(researchPan_.y,
                                  view.y + view.height - contentH - 40.0f, view.y + 40.0f);
    }
  }

  // --- The tree itself, clipped to the viewport -----------------------------
  BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y),
                   static_cast<int>(view.width), static_cast<int>(view.height));

  // A faint grid so panning reads as movement even over empty space.
  {
    const float grid = 44.0f * researchZoom_;
    if (grid > 6.0f) {
      const float gridX = std::fmod(researchPan_.x - view.x, grid);
      for (float x = view.x + gridX; x < view.x + view.width; x += grid) {
        DrawLineV({x, view.y}, {x, view.y + view.height}, Color{28, 31, 35, 255});
      }
      const float gridY = std::fmod(researchPan_.y - view.y, grid);
      for (float y = view.y + gridY; y < view.y + view.height; y += grid) {
        DrawLineV({view.x, y}, {view.x + view.width, y}, Color{28, 31, 35, 255});
      }
    }
  }

  // Connections first, so nodes sit on top of their lines. A link brightens as its parent is
  // researched, which is what makes the tree read as a route rather than a static diagram.
  const float linkWidth = std::max(1.0f, 2.0f * researchZoom_);
  for (int i = 0; i < count; ++i) {
    const Rectangle child = screenRect(nodes[i]);
    for (const std::string& parentId : table[i].parentTechRequired) {
      const auto found = indexOf.find(parentId);
      if (found == indexOf.end()) continue;
      const Rectangle parent = screenRect(nodes[found->second]);
      const Vector2 a{parent.x + parent.width, parent.y + parent.height * 0.5f};
      const Vector2 b{child.x, child.y + child.height * 0.5f};
      const float midX = (a.x + b.x) * 0.5f;
      const Color color = techAcquired(levels, table[i].id)
                              ? kGreen
                              : (techAcquired(levels, parentId) ? kAccent
                                                                : Color{60, 66, 72, 255});
      DrawLineEx(a, {midX, a.y}, linkWidth, color);
      DrawLineEx({midX, a.y}, {midX, b.y}, linkWidth, color);
      DrawLineEx({midX, b.y}, b, linkWidth, color);
      const float head = std::max(3.0f, 5.0f * researchZoom_);
      DrawTriangle({b.x, b.y}, {b.x - head * 1.7f, b.y - head},
                   {b.x - head * 1.7f, b.y + head}, color);
    }
  }

  for (int i = 0; i < count; ++i) {
    const Rectangle r = screenRect(nodes[i]);
    if (r.x > view.x + view.width || r.x + r.width < view.x || r.y > view.y + view.height ||
        r.y + r.height < view.y) {
      continue;
    }

    const Technology& tech = table[i];
    const int level = techLevel(levels, tech.id);
    const int cost = nextLevelCost(levels, tech.id);
    const bool acquired = level > 0;
    const bool maxed = cost < 0;
    const bool unlocked = techUnlocked(levels, tech.id);
    const bool affordable = cost >= 0 && game_.researchPoints() >= cost;
    const bool selected = researchSelectedId_ == tech.id;
    const bool hovered = overView && CheckCollisionPointRec(mouse, r);

    Color top{58, 64, 70, 255};
    Color bottom{38, 42, 46, 255};
    Color edge{86, 94, 102, 255};
    if (maxed) {
      top = {42, 74, 48, 255};
      bottom = {24, 46, 30, 255};
      edge = kGreen;
    } else if (!unlocked) {
      top = {40, 44, 48, 255};
      bottom = {28, 31, 34, 255};
      edge = {58, 62, 66, 255};
    } else if (acquired) {
      top = {60, 72, 58, 255};
      bottom = {38, 48, 38, 255};
      edge = kGreen;
    } else if (affordable) {
      top = {76, 66, 44, 255};
      bottom = {46, 41, 28, 255};
      edge = kGold;
    }

    gradientV(r, top, bottom);
    DrawRectangleLinesEx(r, selected ? 2.0f : 1.0f, selected ? kAccent : edge);
    if (hovered && !selected) {
      DrawRectangleLinesEx({r.x - 1.0f, r.y - 1.0f, r.width + 2.0f, r.height + 2.0f}, 1.0f,
                           kBevelHi);
    }
    if (acquired) {
      DrawRectangle(static_cast<int>(r.x), static_cast<int>(r.y),
                    static_cast<int>(std::max(2.0f, 3.0f * researchZoom_)),
                    static_cast<int>(r.height), kGreen);
    }

    const float pad = std::max(5.0f, 8.0f * researchZoom_);
    const float nameSize = std::max(9.0f, 14.0f * researchZoom_);
    const std::vector<std::string> nameLines =
        wrapText(tech.name, nameSize, r.width - pad * 2.0f);
    float textY = r.y + pad;
    for (size_t line = 0; line < nameLines.size() && line < 2; ++line) {
      label(nameLines[line], r.x + pad, textY, nameSize, unlocked ? kText : kMuted,
            FontWeight::Bold);
      textY += nameSize + 1.0f;
    }

    // Level pips bottom-left and status bottom-right, so the name gets the full card width.
    const float pipSize = std::max(3.0f, 6.0f * researchZoom_);
    const float bottomY = r.y + r.height - pad;
    const float pipY = bottomY - pipSize;
    for (int pip = 0; pip < kMaxTechLevel; ++pip) {
      const Rectangle box{r.x + pad + pip * (pipSize + 2.0f * researchZoom_), pipY, pipSize,
                          pipSize};
      DrawRectangleRec(box, pip < level ? kGreen : Color{44, 48, 52, 255});
      DrawRectangleLinesEx(box, 1.0f, kBevelLo);
    }

    // Status: the price when it can be bought, otherwise why it cannot.
    const float statusSize = std::max(8.0f, 12.0f * researchZoom_);
    const float statusY = bottomY - statusSize;
    std::string status;
    Color statusColor = kMuted;
    if (maxed) {
      status = "RESEARCHED";
      statusColor = kGreen;
    } else if (!unlocked) {
      status = "LOCKED";
    } else {
      status = std::to_string(cost) + " pts";
      statusColor = affordable ? kGold : kRed;
    }
    const std::string statusText = trimTo(status, 10);
    const Vector2 statusWidth = measureText(statusText, statusSize, FontWeight::SemiBold);
    label(statusText, r.x + r.width - pad - statusWidth.x, statusY, statusSize, statusColor,
          FontWeight::SemiBold);
  }

  EndScissorMode();

  // --- Detail panel for the selected technology -----------------------------
  raisedPanel(detail);
  const Rectangle header{detail.x + 4.0f, detail.y + 4.0f, detail.width - 8.0f, 30.0f};
  headerStrip(header);
  label("TECHNOLOGY TREE", header.x + 22.0f, header.y + 6.0f, 18.0f, kText, FontWeight::Bold);
  if (button({header.x + header.width - 74.0f, header.y + 4.0f, 64.0f, 22.0f}, "FIT", true)) {
    researchNeedsCenter_ = true;
  }

  const Technology* selectedTech = nullptr;
  for (const Technology& tech : table) {
    if (tech.id == researchSelectedId_) {
      selectedTech = &tech;
      break;
    }
  }
  if (selectedTech == nullptr && !table.empty()) selectedTech = &table.front();

  float y = detail.y + 42.0f;
  label("POINTS  " + std::to_string(game_.researchPoints()), detail.x + 16.0f, y, 14.0f,
        kGreen, FontWeight::SemiBold);
  y += 26.0f;
  if (selectedTech == nullptr) {
    label("No technologies loaded.", detail.x + 16.0f, y, 16.0f, kMuted);
  } else {
    const Technology& tech = *selectedTech;
    const int level = techLevel(levels, tech.id);
    const int cost = nextLevelCost(levels, tech.id);
    const bool maxed = cost < 0;
    const bool unlocked = techUnlocked(levels, tech.id);
    const bool affordable = cost >= 0 && game_.researchPoints() >= cost;

    label(trimTo(tech.name, 26), detail.x + 16.0f, y, 20.0f, kText, FontWeight::Bold);
    y += 26.0f;
    label("LEVEL " + std::to_string(level) + " / " + std::to_string(kMaxTechLevel),
          detail.x + 16.0f, y, 15.0f, kGold, FontWeight::SemiBold);
    y += 22.0f;
    const float buttonY = detail.y + detail.height - 52.0f;

    for (const std::string& line : wrapText(tech.summary, 14.0f, detail.width - 32.0f)) {
      label(line, detail.x + 16.0f, y, 14.0f, kMuted);
      y += 17.0f;
    }
    y += 6.0f;
    const std::string effect = techEffectText(tech.id, level);
    if (!effect.empty()) {
      for (const std::string& line : wrapText(effect, 14.0f, detail.width - 32.0f)) {
        label(line, detail.x + 16.0f, y, 14.0f, kText);
        y += 17.0f;
      }
      y += 6.0f;
    }

    if (!tech.parentTechRequired.empty()) {
      label("PREREQUISITES", detail.x + 16.0f, y, 13.0f, kAccent, FontWeight::Bold);
      y += 19.0f;
      for (const std::string& parentId : tech.parentTechRequired) {
        const bool done = techAcquired(levels, parentId);
        std::string parentName = parentId;
        for (const Technology& candidate : table) {
          if (candidate.id == parentId) {
            parentName = candidate.name;
            break;
          }
        }
        label((done ? "[x] " : "[ ] ") + trimTo(parentName, 26), detail.x + 20.0f, y, 14.0f,
              done ? kGreen : kText);
        y += 18.0f;
      }
      y += 4.0f;
    }

    int selectedIndex = -1;
    for (int i = 0; i < count; ++i) {
      if (table[static_cast<size_t>(i)].id == tech.id) {
        selectedIndex = i;
        break;
      }
    }
    if (selectedIndex >= 0 && !children[static_cast<size_t>(selectedIndex)].empty()) {
      label("UNLOCKS", detail.x + 16.0f, y, 13.0f, kAccent, FontWeight::Bold);
      y += 19.0f;
      for (const int childIndex : children[static_cast<size_t>(selectedIndex)]) {
        if (y + 18.0f > buttonY - 34.0f) break;
        const bool done = techAcquired(levels, table[static_cast<size_t>(childIndex)].id);
        label((done ? "[x] " : "[ ] ") + trimTo(table[static_cast<size_t>(childIndex)].name, 26),
              detail.x + 20.0f, y, 14.0f, done ? kGreen : kText);
        y += 18.0f;
      }
      y += 4.0f;
    }

    if (maxed) {
      label("Fully researched.", detail.x + 16.0f, buttonY + 8.0f, 16.0f, kGreen);
    } else {
      label("Cost: " + std::to_string(cost) + " points", detail.x + 16.0f,
            std::min(y, buttonY - 22.0f), 15.0f, kText);
      std::string buttonText;
      if (!unlocked) {
        std::string missing;
        for (const std::string& parentId : missingParents(levels, tech.id)) {
          if (!missing.empty()) missing += ", ";
          for (const Technology& candidate : table) {
            if (candidate.id == parentId) {
              missing += candidate.name;
              break;
            }
          }
        }
        buttonText = "Requires " + trimTo(missing, 20);
      } else if (affordable) {
        buttonText = "Research level " + std::to_string(level + 1);
      } else {
        buttonText = "Need " + std::to_string(cost) + " pts";
      }
      if (button({detail.x + 16.0f, buttonY, detail.width - 32.0f, 34.0f}, buttonText,
                 unlocked && affordable, true)) {
        std::string error;
        if (game_.buyTechnology(tech.id, error)) {
          showToast(tech.name + " researched.");
        } else {
          showToast(error);
        }
      }
    }

    label("Drag to pan  |  Wheel to zoom  |  F to fit  |  Click a technology",
          detail.x + 16.0f, detail.y + detail.height - 14.0f, 12.0f, kMuted);
  }
}

// ---------------------------------------------------------------------------
// Navigation planner
// ---------------------------------------------------------------------------

void App::openNavigation() {
  if (!dataLoaded_) {
    showToast("No game data loaded.");
    return;
  }
  navigationOpen_ = true;
  modulePopupPortId_ = 0;
  focusField_ = -1;
  navCargoCode_.clear();
}

void App::drawNavigationPanel() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());

  const Rectangle panel{width - 800.0f, kTopBar + 8.0f, 788.0f, height - kTopBar - kBottomBar - 16.0f};
  DrawRectangle(0, 0, static_cast<int>(width), static_cast<int>(height), Color{0, 0, 0, 120});
  raisedPanel(panel);
  const Rectangle header{panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 30.0f};
  headerStrip(header);
  label("NEW ROUTE", header.x + 22.0f, header.y + 6.0f, 20.0f, kText, FontWeight::Bold);
  label("Sails from a port you own, around the continents.", panel.x + 16.0f, panel.y + 40.0f,
        14.0f, kMuted);

  const Port* departure =
      navDeparture_ != 0 ? catalog_.portById(navDeparture_) : nullptr;
  const Port* arrival = navArrival_ != 0 ? catalog_.portById(navArrival_) : nullptr;
  const Vessel* vessel = navVessel_ != 0 ? catalog_.vesselById(navVessel_) : nullptr;

  if (departure == nullptr || departure != catalog_.portById(navDepartureCachedField_)) {
    navField_.reset();
    navDepartureCachedField_ = navDeparture_;
  }
  if (departure != nullptr && !navField_) navField_ = router_.field(departure->position);

  auto distanceFor = [&](const Port& port) {
    double distance = 0.0;
    if (navField_ && navField_->distanceKmTo(port.position, distance) && distance > 0.0) {
      return distance;
    }
    return haversineKm(departure->position, port.position);
  };

  const float columnWidth = (panel.width - 48.0f) / 3.0f;
  const float listHeight = 210.0f;
  const float top = panel.y + 66.0f;

  // --- Column 1: departure port (owned only) -------------------------------
  const Rectangle depColumn{panel.x + 16.0f, top, columnWidth, listHeight + 46.0f};
  label("DEPARTURE PORT", depColumn.x, depColumn.y, 15.0f, kAccent, FontWeight::Bold);
  textField(1, {depColumn.x, depColumn.y + 20.0f, depColumn.width, 30.0f}, queryDeparture_,
            "Search your ports...");

  {
    std::vector<Row> rows;
    for (int id : game_.ownedPortIds()) {
      const Port* port = catalog_.portById(id);
      if (port == nullptr) continue;
      if (!match(queryDeparture_, {port->name, port->country})) continue;
      rows.push_back({port->id, port->name + ", " + port->country,
                      harborSizeLabel(port->harborSize) + " berth", "", false});
      if (rows.size() >= 60) break;
    }

    const Rectangle list{depColumn.x, depColumn.y + 56.0f, depColumn.width, listHeight};
    sunkenPanel(list);
    const float itemHeight = 34.0f;
    const float total = rows.size() * itemHeight;
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
      scrollDeparture_ -= GetMouseWheelMove() * 40.0f;
      scrollDeparture_ = std::max(0.0f, std::min(std::max(0.0f, total - list.height), scrollDeparture_));
    }
    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width),
                     static_cast<int>(list.height));
    for (size_t index = 0; index < rows.size(); ++index) {
      const float y = list.y + index * itemHeight - scrollDeparture_;
      if (y > list.y + list.height || y + itemHeight < list.y) continue;
      const Rectangle row{list.x, y, list.width, itemHeight - 2.0f};
      const bool selected = rows[index].id == navDeparture_;
      const bool isHovered = CheckCollisionPointRec(GetMousePosition(), row);
      DrawRectangleRec(row, selected ? Color{104, 78, 34, 245}
                                     : (isHovered ? Color{66, 73, 80, 245} : Color{38, 42, 47, 235}));
      label(trimTo(rows[index].title, 30), row.x + 8.0f, row.y + 4.0f, 15.0f, kText);
      label(trimTo(rows[index].meta, 30), row.x + 8.0f, row.y + 19.0f, 12.0f, kMuted);
      if (isHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        navDeparture_ = rows[index].id;
        clickConsumed_ = true;
      }
    }
    EndScissorMode();
    if (rows.empty()) {
      label(game_.ownedPortCount() == 0 ? "You do not own a port yet."
                                        : "No owned port matches that search.",
            list.x + 8.0f, list.y + 8.0f, 14.0f, kMuted);
    }
  }

  // --- Column 2: arrival port (any) ----------------------------------------
  const Rectangle arrColumn{panel.x + 32.0f + columnWidth, top, columnWidth, listHeight + 46.0f};
  label("ARRIVAL PORT", arrColumn.x, arrColumn.y, 15.0f, kAccent, FontWeight::Bold);
  textField(2, {arrColumn.x, arrColumn.y + 20.0f, arrColumn.width, 30.0f}, queryArrival_,
            departure ? "Search, or browse nearest..." : "Search 3630 ports...");

  {
    std::vector<Row> rows;
    if (queryArrival_.empty() && departure != nullptr) {
      // Browse nearest destinations, priced off one sea search.
      std::vector<std::pair<double, const Port*>> nearest;
      nearest.reserve(catalog_.ports().size());
      for (const Port& port : catalog_.ports()) {
        if (!port.hasPosition() || port.id == navDeparture_) continue;
        nearest.emplace_back(distanceFor(port), &port);
      }
      const size_t want = std::min<size_t>(60, nearest.size());
      std::partial_sort(nearest.begin(), nearest.begin() + static_cast<long>(want), nearest.end(),
                        [](const auto& a, const auto& b) { return a.first < b.first; });
      for (size_t index = 0; index < want; ++index) {
        const Port* port = nearest[index].second;
        rows.push_back({port->id, port->name + ", " + port->country,
                        number(nearest[index].first) + " km by sea", "", false});
      }
    } else {
      for (const Port& port : catalog_.ports()) {
        if (!port.hasPosition() || port.id == navDeparture_) continue;
        if (!match(queryArrival_, {port.name, port.country})) continue;
        rows.push_back({port.id, port.name + ", " + port.country,
                        harborSizeLabel(port.harborSize) + " berth", "", false});
        if (rows.size() >= 60) break;
      }
    }

    const Rectangle list{arrColumn.x, arrColumn.y + 56.0f, arrColumn.width, listHeight};
    sunkenPanel(list);
    const float itemHeight = 34.0f;
    const float total = rows.size() * itemHeight;
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
      scrollArrival_ -= GetMouseWheelMove() * 40.0f;
      scrollArrival_ = std::max(0.0f, std::min(std::max(0.0f, total - list.height), scrollArrival_));
    }
    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width),
                     static_cast<int>(list.height));
    for (size_t index = 0; index < rows.size(); ++index) {
      const float y = list.y + index * itemHeight - scrollArrival_;
      if (y > list.y + list.height || y + itemHeight < list.y) continue;
      const Rectangle row{list.x, y, list.width, itemHeight - 2.0f};
      const bool selected = rows[index].id == navArrival_;
      const bool isHovered = CheckCollisionPointRec(GetMousePosition(), row);
      DrawRectangleRec(row, selected ? Color{104, 78, 34, 245}
                                     : (isHovered ? Color{66, 73, 80, 245} : Color{38, 42, 47, 235}));
      label(trimTo(rows[index].title, 30), row.x + 8.0f, row.y + 4.0f, 15.0f, kText);
      label(trimTo(rows[index].meta, 30), row.x + 8.0f, row.y + 19.0f, 12.0f, kMuted);
      if (isHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        navArrival_ = rows[index].id;
        clickConsumed_ = true;
      }
    }
    EndScissorMode();
  }

  // --- Column 3: vessel -----------------------------------------------------
  const Rectangle vesselColumn{panel.x + 48.0f + columnWidth * 2.0f, top, columnWidth,
                               listHeight + 46.0f};
  label("VESSEL", vesselColumn.x, vesselColumn.y, 15.0f, kAccent, FontWeight::Bold);
  textField(3, {vesselColumn.x, vesselColumn.y + 20.0f, vesselColumn.width, 30.0f}, queryVessel_,
            "Search by name, owner or class...");

  {
    std::vector<Row> rows;
    std::vector<const Vessel*> candidates;
    // Owned and ready first, then the rest of the catalog by price.
    for (const Vessel* owned : game_.readyVessels()) candidates.push_back(owned);
    for (const Vessel& entry : catalog_.vessels()) {
      if (game_.vesselOwned(entry.id)) continue;
      if (!match(queryVessel_, {entry.shipName, entry.companyName, entry.fleetNation})) continue;
      candidates.push_back(&entry);
    }

    for (const Vessel* entry : candidates) {
      if (game_.vesselOwned(entry->id) &&
          !match(queryVessel_, {entry->shipName, entry->companyName, entry->fleetNation})) {
        continue;
      }
      const char vesselClassChar = vesselClass(entry->length);
      const bool owned = game_.vesselOwned(entry->id);
      const bool fits = departure == nullptr || canBerth(*departure, entry->length, game_.researchLevels());
      std::ostringstream meta;
      meta << (vesselClassChar == '\0' ? '?' : vesselClassChar) << " class  |  " << entry->shipName
           << "  |  " << number(entry->gt) << " GT  |  "
           << (owned ? "owned" : moneyText(hullPrice(entry->gt)));
      rows.push_back({entry->id, entry->companyName, meta.str(),
                      fits ? "" : "Needs a deeper berth", !fits});
      if (rows.size() >= 60) break;
    }

    const Rectangle list{vesselColumn.x, vesselColumn.y + 56.0f, vesselColumn.width, listHeight};
    sunkenPanel(list);
    const float itemHeight = 34.0f;
    const float total = rows.size() * itemHeight;
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
      scrollVessel_ -= GetMouseWheelMove() * 40.0f;
      scrollVessel_ = std::max(0.0f, std::min(std::max(0.0f, total - list.height), scrollVessel_));
    }
    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width),
                     static_cast<int>(list.height));
    for (size_t index = 0; index < rows.size(); ++index) {
      const float y = list.y + index * itemHeight - scrollVessel_;
      if (y > list.y + list.height || y + itemHeight < list.y) continue;
      const Rectangle row{list.x, y, list.width, itemHeight - 2.0f};
      const bool selected = rows[index].id == navVessel_;
      const bool isHovered = CheckCollisionPointRec(GetMousePosition(), row);
      DrawRectangleRec(row, selected ? Color{104, 78, 34, 245}
                                     : (isHovered ? Color{66, 73, 80, 245} : Color{38, 42, 47, 235}));
      label(trimTo(rows[index].title, 26), row.x + 8.0f, row.y + 4.0f, 15.0f,
            rows[index].disabled ? kMuted : kText);
      label(trimTo(rows[index].warning.empty() ? rows[index].meta : rows[index].warning, 40),
            row.x + 8.0f, row.y + 19.0f, 12.0f, rows[index].disabled ? kRed : kMuted);
      if (isHovered && !rows[index].disabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        navVessel_ = rows[index].id;
        clickConsumed_ = true;
      }
    }
    EndScissorMode();
  }

  // --- Goods ---------------------------------------------------------------
  const float goodsTop = top + listHeight + 62.0f;
  label("GOODS", panel.x + 16.0f, goodsTop, 15.0f, kAccent, FontWeight::Bold);
  textField(4, {panel.x + 16.0f, goodsTop + 20.0f, 300.0f, 30.0f}, queryCargo_, "Search goods...");

  const std::vector<CargoOption> cargoList = cargoOptions(
      catalog_, departure != nullptr ? departure->country : "", arrival != nullptr ? arrival->country : "");
  const CargoOption* cargo = nullptr;
  for (const CargoOption& option : cargoList) {
    if (option.productCode == navCargoCode_) {
      cargo = &option;
      break;
    }
  }

  {
    const Rectangle list{panel.x + 16.0f, goodsTop + 56.0f, 300.0f, listHeight};
    sunkenPanel(list);
    std::vector<const CargoOption*> shown;
    for (const CargoOption& option : cargoList) {
      if (!match(queryCargo_, {option.description, option.productCode})) continue;
      shown.push_back(&option);
      if (shown.size() >= 120) break;
    }

    const float itemHeight = 30.0f;
    const float total = shown.size() * itemHeight;
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
      scrollCargo_ -= GetMouseWheelMove() * 40.0f;
      scrollCargo_ = std::max(0.0f, std::min(std::max(0.0f, total - list.height), scrollCargo_));
    }
    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width),
                     static_cast<int>(list.height));
    for (size_t index = 0; index < shown.size(); ++index) {
      const float y = list.y + index * itemHeight - scrollCargo_;
      if (y > list.y + list.height || y + itemHeight < list.y) continue;
      const Rectangle row{list.x, y, list.width, itemHeight - 2.0f};
      const bool selected = shown[index]->productCode == navCargoCode_;
      const bool isHovered = CheckCollisionPointRec(GetMousePosition(), row);
      DrawRectangleRec(row, selected ? Color{104, 78, 34, 245}
                                     : (isHovered ? Color{66, 73, 80, 245} : Color{38, 42, 47, 235}));
      label(trimTo(shown[index]->description, 30), row.x + 8.0f, row.y + 6.0f, 15.0f, kText);
      const std::string meta = shown[index]->realTrade
                                   ? "real trade  +5%"
                                   : number(shown[index]->valueK) + "k";
      label(meta, row.x + row.width - measureText(meta, 13.0f).x - 8.0f, row.y + 7.0f, 13.0f,
            shown[index]->realTrade ? kGreen : kMuted);
      if (isHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        navCargoCode_ = shown[index]->productCode;
        clickConsumed_ = true;
      }
    }
    EndScissorMode();

    if (departure == nullptr) {
      label("Cargo depends on the departure port.", list.x + 8.0f, list.y + 8.0f, 14.0f, kMuted);
    } else if (shown.empty()) {
      label("No good matches that search.", list.x + 8.0f, list.y + 8.0f, 14.0f, kMuted);
    } else {
      label(std::to_string(cargoList.size()) + " goods " + departure->country + " exports",
            panel.x + 330.0f, goodsTop + 26.0f, 14.0f, kMuted);
    }
  }

  // --- Summary --------------------------------------------------------------
  const Rectangle summary{panel.x + 330.0f, goodsTop + 56.0f, panel.width - 346.0f, listHeight};
  sunkenPanel(summary);

  double distanceKm = 0.0;
  bool seaReachable = false;
  if (departure != nullptr && arrival != nullptr) {
    distanceKm = distanceFor(*arrival);
    seaReachable = navField_ && navField_->distanceKmTo(arrival->position, distanceKm);
  }

  const bool vesselOwned = vessel != nullptr && game_.vesselOwned(vessel->id);
  const double vesselCost = vessel != nullptr && !vesselOwned ? hullPrice(vessel->gt) : 0.0;
  const bool vesselBusy =
      vessel != nullptr && std::any_of(game_.voyages().begin(), game_.voyages().end(),
                                       [&](const ActiveVoyage& v) { return v.vesselId == vessel->id; });

  VoyageOutcome preview;
  bool hasPreview = false;
  if (departure != nullptr && arrival != nullptr && cargo != nullptr && distanceKm > 0.0) {
    preview = resolveVoyage(cargo->valueK, distanceKm, game_.researchLevels(), 100.0,
                            cargo->realTrade);
    hasPreview = true;
  }

  float sy = summary.y + 10.0f;
  auto summaryRow = [&](const std::string& key, const std::string& value, Color color) {
    label(key, summary.x + 12.0f, sy, 15.0f, kMuted);
    label(trimTo(value, 40), summary.x + 200.0f, sy, 15.0f, color);
    sy += 22.0f;
  };

  summaryRow("Departure", departure ? departure->name : "-", kText);
  summaryRow("Destination", arrival ? arrival->name : "-", kText);
  summaryRow("Distance", departure && arrival ? number(distanceKm) + " km" : "-", kText);
  summaryRow("Course", departure && arrival
                           ? (seaReachable ? "By sea" : "Direct line")
                           : "-",
             kText);
  summaryRow("Crossing", hasPreview ? formatDuration(preview.durationMs) : "-", kText);
  summaryRow("Est. freight", hasPreview ? moneyText(preview.reward) : "-", kGreen);
  summaryRow("Research", hasPreview ? "+" + std::to_string(preview.points) + " pts" : "-", kGold);
  summaryRow("Loss risk", std::to_string(static_cast<int>(lossRiskPercent(game_.researchLevels()))) + "%", kText);
  summaryRow("Vessel", vessel ? vessel->companyName : "-", vesselOwned ? kGreen : kText);
  if (cargo != nullptr) {
    summaryRow("Cargo", cargo->description, cargo->realTrade ? kGreen : kText);
  }

  // --- Blocked reason -------------------------------------------------------
  std::string blocked;
  if (game_.ownedPortCount() == 0) blocked = "You need to own a port before you can sail from one.";
  else if (departure == nullptr) blocked = "Choose a departure port.";
  else if (arrival == nullptr) blocked = "Choose an arrival port.";
  else if (vessel == nullptr) blocked = "Choose a vessel.";
  else if (vesselBusy) blocked = vessel->companyName + " is already at sea.";
  else if (!canBerth(*departure, vessel->length, game_.researchLevels()))
    blocked = departure->name + " cannot berth a " + std::to_string(static_cast<int>(vessel->length)) +
              " m hull. Research Deepwater Dredging.";
  else if (!canBerth(*arrival, vessel->length, game_.researchLevels()))
    blocked = arrival->name + " cannot berth that hull. Pick a deeper arrival port.";
  else if (cargo == nullptr) blocked = "Choose goods to trade.";
  else if (vesselCost > game_.money())
    blocked = "Not enough money for this hull (" + moneyText(vesselCost) + " needed).";
  else if (departure->id == arrival->id) blocked = "Departure and arrival are the same port.";

  const float footerY = panel.y + panel.height - 46.0f;
  if (!blocked.empty()) label(trimTo(blocked, 92), panel.x + 16.0f, footerY - 20.0f, 15.0f, kRed);

  const std::string startLabel =
      vesselCost > 0.0 ? "Buy hull & sail (" + moneyText(vesselCost) + ")" : "Start navigation";
  if (button({panel.x + 16.0f, footerY, 260.0f, 34.0f}, startLabel, blocked.empty(), true)) {
    dispatchVoyage();
  }
  if (button({panel.x + 288.0f, footerY, 120.0f, 34.0f}, "Close", true)) {
    navigationOpen_ = false;
  }
}

void App::dispatchVoyage() {
  const Port* departure = catalog_.portById(navDeparture_);
  const Port* arrival = catalog_.portById(navArrival_);
  const Vessel* vessel = catalog_.vesselById(navVessel_);

  const std::vector<CargoOption> options =
      cargoOptions(catalog_, departure != nullptr ? departure->country : "",
                   arrival != nullptr ? arrival->country : "");
  const CargoOption* cargo = nullptr;
  for (const CargoOption& option : options) {
    if (option.productCode == navCargoCode_) {
      cargo = &option;
      break;
    }
  }

  if (departure == nullptr || arrival == nullptr || vessel == nullptr || cargo == nullptr) {
    showToast("The route is not complete.");
    return;
  }

  std::string error;
  if (!game_.startVoyage(departure->id, arrival->id, vessel->id, *cargo, nowMs_, error)) {
    showToast(error);
    return;
  }

  showToast(vessel->companyName + " is sailing to " + arrival->name + ".");
  navVessel_ = 0;  // pointing the next pick at a vessel still in port
}

// ---------------------------------------------------------------------------
// Scripted smoke test
//
// The screenshot test drives the real UI code paths (claim, plot, dispatch)
// headlessly, so rendering and the voyage loop are exercised without a human
// at the keyboard. Set SEA_BUILDER_DEMO to run it.
// ---------------------------------------------------------------------------

void App::debugDemo() {
  if (!dataLoaded_) return;
  // Scripted captures run without a person to click Play.
  startMenu_ = false;

  const Vessel* starter = catalog_.vesselById(kStarterVesselId);

  // The harbour the player was handed, plus one more taken by storm, are the scripted run's
  // two berths. A harbour that still fights is broken first, which is what a player would do
  // with a squadron; the capture only needs the result.
  std::vector<int> claims;
  if (game_.portOwned(kStarterPortId)) claims.push_back(kStarterPortId);
  for (const char* band : {"V", "S", "M", "L"}) {
    if (claims.size() >= 2) break;
    for (const Port& port : catalog_.ports()) {
      if (port.harborSize != band || !port.hasPosition() || game_.portOwned(port.id)) continue;
      if (starter != nullptr && !canBerth(port, starter->length, game_.researchLevels())) continue;
      std::string error;
      if (!game_.stormPort(port.id, error)) continue;
      claims.push_back(port.id);
      break;
    }
  }
  if (claims.empty()) return;

  const Port* departure = catalog_.portById(claims.front());
  if (departure == nullptr) return;

  // The furthest port by sea that the starter hull can still enter.
  const std::shared_ptr<const SeaField> field = router_.field(departure->position);
  int arrival = 0;
  double bestKm = 0.0;
  for (const Port& port : catalog_.ports()) {
    if (port.id == departure->id || !port.hasPosition()) continue;
    if (starter != nullptr && !canBerth(port, starter->length, game_.researchLevels())) continue;
    double km = 0.0;
    if (!field || !field->distanceKmTo(port.position, km) || km <= bestKm) continue;
    bestKm = km;
    arrival = port.id;
  }
  if (arrival == 0) return;

  openNavigation();
  navDeparture_ = departure->id;
  navArrival_ = arrival;
  navVessel_ = kStarterVesselId;

  const Port* arrivalPort = catalog_.portById(arrival);
  const std::vector<CargoOption> options =
      cargoOptions(catalog_, departure->country, arrivalPort != nullptr ? arrivalPort->country : "");
  if (!options.empty()) navCargoCode_ = options.front().productCode;
}

void App::debugStartVoyage() { dispatchVoyage(); }

void App::debugShowScreen(const std::string& name) {
  startMenu_ = false;
  navigationOpen_ = false;
  selectedPort_ = nullptr;
  screen_ = Screen::Map;
  debugSprites_ = false;
  fleetCommandTab_ = 0;

  if (name == "nav") {
    navigationOpen_ = true;
  } else if (name == "port" || name == "modules") {
    const Port* port = catalog_.portById(navDeparture_);
    if (port == nullptr && !catalog_.ports().empty()) port = &catalog_.ports().front();
    selectedPort_ = port;
    if (port != nullptr) {
      map_.centerOn(port->position, 6.0f);
      // Exercise the upgrade and module path so a captured port panel shows a fitted module
      // next to the empty slots, not just the empty layout. The "modules" screen also opens
      // the fit-a-module list, which a screenshot cannot click.
      if (game_.portOwned(port->id)) {
        std::string error;
        game_.upgradePort(port->id, error);
        game_.installPortModule(port->id, 1, 600, error);
        if (name == "modules") modulePopupPortId_ = port->id;
      }
    }
  } else if (name == "shipyard") {
    const Port* port = catalog_.portById(navDeparture_);
    if (port == nullptr && !catalog_.ports().empty()) port = &catalog_.ports().front();
    selectedPort_ = port;
    if (port != nullptr) {
      map_.centerOn(port->position, 6.0f);
      if (game_.portOwned(port->id)) {
        std::string error;
        game_.upgradePort(port->id, error);
        game_.installPortModule(port->id, 2, 200, error);
        shipyardPortId_ = port->id;
        // Park the cursor on the first class so the detail card (and the 3D well inside it)
        // is populated in a capture: with no hover it only shows its placeholder text.
        const float width = static_cast<float>(GetScreenWidth());
        const float height = static_cast<float>(GetScreenHeight());
        const float panelWidth = std::min(940.0f, width - 80.0f);
        const float panelHeight = std::min(620.0f, height - kTopBar - kBottomBar - 32.0f);
        const float panelX = width * 0.5f - panelWidth * 0.5f;
        const float panelY =
            kTopBar + (height - kTopBar - kBottomBar) * 0.5f - panelHeight * 0.5f;
        SetMousePosition(static_cast<int>(panelX + 90.0f), static_cast<int>(panelY + 118.0f));
      }
    }
  } else if (name == "armement") {
    // Open an owned port's armament screen: claiming the harbour has already mounted its basic
    // machine gun, so a capture shows a fitted row, the mount it costs and the attack rating
    // it produces.
    const Port* port = catalog_.portById(navDeparture_);
    if (port == nullptr && !catalog_.ports().empty()) port = &catalog_.ports().front();
    selectedPort_ = port;
    if (port != nullptr) {
      map_.centerOn(port->position, 6.0f);
      if (game_.portOwned(port->id)) armamentPortId_ = port->id;
    }
  } else if (name == "fleet") {
    screen_ = Screen::Fleet;
  } else if (name == "admiralty" || name == "fleetcommand") {
    // A ready-made command: claim a yard, lay down a small squadron, raise a fleet, engage an
    // admiral and put them in command, so a capture shows every part of the window populated.
    const Port* port = catalog_.portById(navDeparture_);
    if (port == nullptr && !catalog_.ports().empty()) port = &catalog_.ports().front();
    if (port != nullptr) {
      game_.changeMoney(50000.0);
      std::string error;
      if (!game_.portOwned(port->id)) game_.stormPort(port->id, error);
      game_.upgradePort(port->id, error);
      game_.installPortModule(port->id, 2, 0, error);

      int built = 0;
      for (const NavalVessel& vessel : catalog_.navalVessels()) {
        if (built >= 3) break;
        const std::string type = lower(vessel.type);
        if (type.find("submarine") == std::string::npos &&
            type.find("destroyer") == std::string::npos &&
            type.find("patrol") == std::string::npos) {
          continue;
        }
        if (game_.buildNavalUnit(vessel.id, port->id, navalUnitCost(vessel, false), error)) {
          ++built;
        }
      }

      if (game_.createFleet("Home Squadron", error)) {
        for (const NavalUnit& unit : game_.navalUnits()) {
          game_.assignUnitToFleet(unit.id, game_.fleets().back().id, error);
        }
      }
      for (const Admiral& admiral : game_.admirals()) {
        if (game_.fleets().empty()) break;
        if (!game_.recruitAdmiralToFleet(admiral.id, game_.fleets().front().id, error)) continue;
        break;
      }
      if (!game_.fleets().empty()) {
        fleetCommandSelected_ = game_.fleets().front().id;
        admiralAssignTarget_ = fleetCommandSelected_;
      }
    }
    screen_ = Screen::FleetCommand;
    if (name == "admiralty") fleetCommandTab_ = 1;
  } else if (name == "vessel") {
    // Opens the vessel info modal on the first catalog hull, so the panel (and the photograph
    // inside it) can be captured without a click.
    screen_ = Screen::Fleet;
    if (!catalog_.vessels().empty()) infoVessel_ = &catalog_.vessels().front();
  } else if (name == "navy") {
    // Claim a yard, fit the shipyard module and lay down a small task force, so a capture
    // shows several hulls on the map with the action panel open.
    const Port* port = catalog_.portById(navDeparture_);
    if (port == nullptr && !catalog_.ports().empty()) port = &catalog_.ports().front();
    if (port != nullptr) {
      map_.centerOn(port->position, 14.0f);
      game_.changeMoney(50000.0);
      std::string error;
      if (!game_.portOwned(port->id)) game_.stormPort(port->id, error);
      game_.upgradePort(port->id, error);
      game_.installPortModule(port->id, 2, 200, error);

      int built = 0;
      for (const NavalVessel& vessel : catalog_.navalVessels()) {
        if (built >= 3) break;
        const std::string type = lower(vessel.type);
        if (type.find("submarine") == std::string::npos &&
            type.find("destroyer") == std::string::npos &&
            type.find("patrol") == std::string::npos) {
          continue;
        }
        if (game_.buildNavalUnit(vessel.id, port->id, navalUnitCost(vessel, false), error)) {
          ++built;
        }
      }

      // Put the force under way, fanned out, so a capture shows hulls at different headings
      // with their courses drawn instead of three sprites stacked on the quay. Each hull gets
      // the first open-water point found on a ring around the yard, so the orders are always
      // accepted even when the port sits against a coast.
      selectedNaval_.clear();
      int ring = 0;
      for (const NavalUnit& unit : game_.navalUnits()) {
        const double angle = static_cast<double>(ring) * 2.0 + 0.6;
        const double span = 4.0 + 3.0 * static_cast<double>(ring);
        for (double reach = span; reach <= span + 24.0; reach += 2.0) {
          const LatLng target{port->position.lat + std::sin(angle) * reach,
                              port->position.lng + std::cos(angle) * reach};
          if (router_.isLandAt(target.lat, target.lng)) continue;
          std::string order;
          if (game_.orderNavalUnit(unit.id, target, order)) break;
        }
        selectedNaval_.push_back(unit.id);
        ++ring;
      }

      // Give the task force a flag so the capture exercises the map's fleet nameplates, the
      // admiral portrait in the action panel and the admiral's fleet-wide buffs.
      std::string fleetError;
      if (game_.createFleet("Home Squadron", fleetError) || !game_.fleets().empty()) {
        const int fleetId = game_.fleets().front().id;
        for (const NavalUnit& unit : game_.navalUnits()) {
          game_.assignUnitToFleet(unit.id, fleetId, fleetError);
        }
        for (const Admiral& admiral : game_.admirals()) {
          if (game_.recruitAdmiralToFleet(admiral.id, fleetId, fleetError)) break;
        }
      }
    }
  } else if (name == "siege") {
    // Scripted siege for a capture: the home yard lays down a hull, the nearest small neutral
    // harbour is put under bombardment and the camera sits on it, so a frame shows the harbour
    // under attack - its defence bar on the map, and a port panel that refuses to sell it.
    const Port* yard = catalog_.portById(kStarterPortId);
    if (yard != nullptr) {
      game_.changeMoney(50000.0);
      std::string error;
      if (!game_.portOwned(yard->id)) game_.stormPort(yard->id, error);
      game_.installPortModule(yard->id, 2, 0, error);

      const std::shared_ptr<const SeaField> field = router_.field(yard->position);
      const Port* target = nullptr;
      double bestKm = 1.0e18;
      for (const Port& port : catalog_.ports()) {
        if (port.id == yard->id || !port.hasPosition() || port.harborSize != "V") continue;
        double km = 0.0;
        if (!field || !field->distanceKmTo(port.position, km)) continue;
        if (km < bestKm) {
          bestKm = km;
          target = &port;
        }
      }

      if (target != nullptr) {
        for (const NavalVessel& vessel : catalog_.navalVessels()) {
          if (vessel.type.find("Destroyer") == std::string::npos || vessel.armament.empty()) {
            continue;
          }
          if (game_.buildNavalUnit(vessel.id, yard->id, navalUnitCost(vessel, true), error)) break;
        }

        std::vector<int> force;
        for (const NavalUnit& unit : game_.navalUnits()) force.push_back(unit.id);
        game_.orderNavalUnitsAttackPort(force, target->id, error);
        selectedNaval_ = force;
        selectedPort_ = target;
        map_.centerOn(target->position, 6.0f);
      }
    }
  } else if (name == "combat") {
    // Scripted battle for a capture: pick a rival hull with a yard within missile range,
    // lay down a hull that carries several kinds of weapon, and order it to attack. The
    // frame then shows the attack order, the target marker, tracers and guided trails.
    const BotBoat* target = nullptr;
    const Port* yard = nullptr;
    double bestGap = 1.0e18;
    for (const BotBoat& boat : game_.botBoats()) {
      if (boat.sunk()) continue;
      for (const Port& candidate : catalog_.ports()) {
        if (!candidate.hasPosition()) continue;
        const double gap = haversineKm(boat.position, candidate.position);
        if (gap < 250.0 || gap > 1500.0) continue;
        if (gap < bestGap) {
          bestGap = gap;
          target = &boat;
          yard = &candidate;
        }
      }
    }

    if (target != nullptr && yard != nullptr) {
      game_.changeMoney(1.0e6);
      std::string error;
      if (!game_.portOwned(yard->id)) game_.stormPort(yard->id, error);
      game_.installPortModule(yard->id, 2, 0, error);

      const NavalVessel* bestClass = nullptr;
      size_t bestTypes = 0;
      for (const NavalVessel& vessel : catalog_.navalVessels()) {
        bool seen[5] = {false, false, false, false, false};
        size_t types = 0;
        for (const Armament& weapon : vessel.armament) {
          if (!weapon.is_efficient_against_ships || weapon.ammo <= 0.0f) continue;
          const size_t kind = static_cast<size_t>(deliveryFor(weapon));
          if (!seen[kind]) {
            seen[kind] = true;
            ++types;
          }
        }
        if (types > bestTypes) {
          bestTypes = types;
          bestClass = &vessel;
        }
      }

      if (bestClass != nullptr &&
          game_.buildNavalUnit(bestClass->id, yard->id, 0.0, error) &&
          !game_.navalUnits().empty()) {
        const NavalUnit& unit = game_.navalUnits().back();
        if (game_.orderNavalUnitAttack(unit.id, target->id, error)) {
          selectedNaval_ = {unit.id};
          attackMarkerBoatId_ = target->id;
          attackMarkerUntilMs_ = nowMs_ + 60000.0;
          const LatLng middle{(unit.position.lat + target->position.lat) * 0.5,
                              (unit.position.lng + target->position.lng) * 0.5};
          map_.centerOn(middle, 90.0f);
        }
      }
    }
  } else if (name == "sprites") {
    debugSprites_ = true;
  } else if (name == "research") {
    screen_ = Screen::Research;
    researchNeedsCenter_ = true;
  } else {
    map_.resetView(2.2f);  // whole world, so a plotted route is visible end to end
  }
}

std::string App::debugStatus() const {
  std::ostringstream out;
  const char* screen = screen_ == Screen::Map       ? "map"
                       : screen_ == Screen::Fleet   ? "fleet"
                       : screen_ == Screen::FleetCommand ? "admiralty"
                                                    : "research";
  out << "screen=" << screen << " navOpen=" << (navigationOpen_ ? 1 : 0)
      << " selectedPort=" << (selectedPort_ != nullptr ? selectedPort_->id : 0) << " "
      << "money=" << static_cast<long long>(game_.money())
      << " pts=" << game_.researchPoints()
      << " ports=" << game_.ownedPortCount() << " vessels=" << game_.ownedVesselCount()
      << " naval=" << game_.navalUnitCount() << " fleets=" << game_.fleets().size()
      << " admirals=" << game_.hiredAdmiralCount() << " selected=" << selectedNaval_.size()
      << " killFeed=" << killFeed_.size()
      << " voyages=" << game_.voyages().size() << " departure=" << navDeparture_
      << " arrival=" << navArrival_ << " goods=" << navCargoCode_;
  if (!game_.voyages().empty()) {
    const ActiveVoyage& voyage = game_.voyages().front();
    out << " progress=" << voyage.progress << " km=" << voyage.distanceKm;
  }
  out << " projectiles=" << game_.projectiles().size();
  // The rival powers, one line each: what they hold, what they have and what is afloat. It is
  // how a scripted run can be checked for the opponents' economy actually running.
  out << " rivals=" << game_.factions().size() << " merchants=" << game_.merchantRuns().size();
  for (size_t index = 0; index < game_.factions().size(); ++index) {
    const AIPlayer& power = game_.factions()[index];
    out << " [" << power.name << ":port=" << game_.factionHomePort(static_cast<int>(index))
        << ",money=" << static_cast<long long>(power.money)
        << ",pts=" << power.researchPoints << ",ships=" << game_.factionShipCount(index) << "]";
  }
  if (!game_.navalUnits().empty()) {
    const NavalUnit& unit = game_.navalUnits().front();
    const Vector2 screen = map_.project(unit.position);
    out << " unit0=" << unit.id << "@{lat=" << unit.position.lat << ",lng=" << unit.position.lng
        << ",screen=" << static_cast<int>(screen.x) << "," << static_cast<int>(screen.y)
        << ",hdg=" << static_cast<int>(unit.headingDeg) << ",hp=" << unit.health
        << ",target=" << unit.targetBoatId;
    for (const MountState& mount : unit.mounts) out << ",ammo=" << mount.ammo;
    out << "}";
  }
  return out.str();
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

void App::drainMessages() {
  for (std::string& message : game_.takeMessages()) inbox_.push_back(std::move(message));
}

void App::recordKillFeed(const std::vector<CombatEvent>& events) {
  for (const CombatEvent& event : events) {
    if (event.kind != CombatEvent::Kind::Kill) continue;

    KillFeedEntry entry;
    entry.attacker = event.attackerName.empty() ? "Unknown" : event.attackerName;
    entry.victim = event.victimName.empty() ? "Unknown" : event.victimName;
    entry.attackerIsAlly = event.attackerIsAlly;
    entry.victimIsAlly = event.victimIsAlly;
    entry.expiresAtMs = nowMs_ + 7000.0;
    // Newest first, like a shooter's kill feed: the latest kill sits at the top and the older
    // lines slide down until they time out.
    killFeed_.insert(killFeed_.begin(), std::move(entry));
  }

  if (killFeed_.size() > 5) killFeed_.resize(5);
  killFeed_.erase(
      std::remove_if(killFeed_.begin(), killFeed_.end(),
                     [&](const KillFeedEntry& entry) { return entry.expiresAtMs <= nowMs_; }),
      killFeed_.end());
}

void App::drawKillFeed() {
  // A full-screen management plate would sit across the feed; leave it to the map and the
  // fleet screens rather than stacking HUD text on a modal.
  if (killFeed_.empty() || shipyardPortId_ != 0 || modulePopupPortId_ != 0) return;

  const float width = static_cast<float>(GetScreenWidth());
  // Sit clear of whichever right-hand panel is open. The navigation planner is the widest,
  // so it decides the edge; otherwise the port information plate does.
  float rightEdge = width - 14.0f;
  if (navigationOpen_) {
    rightEdge = width - 812.0f;
  } else if (selectedPort_ != nullptr) {
    rightEdge = width - 356.0f;
  }

  constexpr float kNameSize = 17.0f;
  constexpr float kPad = 10.0f;
  constexpr float kGap = 5.0f;
  const std::string verb = "Destroyed";
  const Vector2 verbSize = measureText(verb, kNameSize, FontWeight::SemiBold);
  const float lineHeight = measureText("Ag", kNameSize, FontWeight::Bold).y;
  const float boxHeight = std::max(30.0f, lineHeight + 10.0f);

  float y = kTopBar + 12.0f;
  for (const KillFeedEntry& entry : killFeed_) {
    const Color attackerColor = entry.attackerIsAlly ? kBlue : kRed;
    const Color victimColor = entry.victimIsAlly ? kBlue : kRed;

    const Vector2 attackerSize = measureText(entry.attacker, kNameSize, FontWeight::Bold);
    const Vector2 victimSize = measureText(entry.victim, kNameSize, FontWeight::Bold);
    const float contentWidth = attackerSize.x + verbSize.x + victimSize.x + kGap * 2.0f;
    const float boxWidth = contentWidth + kPad * 2.0f;
    const float x = std::max(8.0f, rightEdge - boxWidth);

    // Fade the oldest lines out over their last second instead of letting them pop away.
    const float remaining = static_cast<float>(entry.expiresAtMs - nowMs_);
    const float alpha = std::clamp(remaining / 1000.0f, 0.0f, 1.0f);
    Color background{8, 12, 16, static_cast<unsigned char>(205.0f * alpha)};
    Color textAlpha = WHITE;
    textAlpha.a = static_cast<unsigned char>(255.0f * alpha);

    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(boxWidth),
                  static_cast<int>(boxHeight), background);
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), 3, static_cast<int>(boxHeight),
                  Fade(attackerColor, alpha));

    const float textY = y + (boxHeight - lineHeight) * 0.5f;
    float textX = x + kPad;
    Color attackerTint = attackerColor;
    attackerTint.a = textAlpha.a;
    labelCentered(entry.attacker, {textX, textY, attackerSize.x, lineHeight}, kNameSize,
                  attackerTint, FontWeight::Bold);
    textX += attackerSize.x + kGap;

    Color verbTint = kMuted;
    verbTint.a = textAlpha.a;
    labelCentered(verb, {textX, textY, verbSize.x, lineHeight}, kNameSize, verbTint,
                  FontWeight::SemiBold);
    textX += verbSize.x + kGap;

    Color victimTint = victimColor;
    victimTint.a = textAlpha.a;
    labelCentered(entry.victim, {textX, textY, victimSize.x, lineHeight}, kNameSize, victimTint,
                  FontWeight::Bold);

    y += boxHeight + 4.0f;
  }
}

void App::drawMessages() {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());

  if (!inbox_.empty()) {
    const Rectangle panel{width * 0.5f - 260.0f, height * 0.5f - 150.0f, 520.0f, 300.0f};
    raisedPanel(panel);
    const Rectangle header{panel.x + 4.0f, panel.y + 4.0f, panel.width - 8.0f, 30.0f};
    headerStrip(header);
    label("DELIVERY", header.x + 22.0f, header.y + 6.0f, 18.0f, kGreen, FontWeight::Bold);

    // Show the first message, wrapped.
    std::istringstream words(inbox_.front());
    std::string word;
    std::string line;
    float y = panel.y + 52.0f;
    while (words >> word) {
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (measureText(candidate, 16.0f).x > panel.width - 36.0f) {
        label(line, panel.x + 18.0f, y, 16.0f, kText);
        y += 20.0f;
        line = word;
      } else {
        line = candidate;
      }
    }
    if (!line.empty()) label(line, panel.x + 18.0f, y, 16.0f, kText);

    if (button({panel.x + panel.width - 130.0f, panel.y + panel.height - 46.0f, 112.0f, 32.0f},
               "Continue", true, true)) {
      inbox_.erase(inbox_.begin());
    }
  }

  if (!toast_.empty()) {
    const Vector2 measured = measureText(toast_, 16.0f);
    const Rectangle box{width * 0.5f - measured.x * 0.5f - 16.0f, kTopBar + 10.0f,
                        measured.x + 32.0f, 34.0f};
    raisedPanel(box);
    DrawLineEx({box.x + 1.0f, box.y + 1.0f}, {box.x + 1.0f, box.y + box.height - 2.0f}, 3.0f,
               kAccent);
    labelCentered(toast_, box, 16.0f, kText);
  }
}

void App::showToast(const std::string& text) {
  toast_ = text;
  toastUntilMs_ = nowMs_ + 4000.0;
}

}  // namespace sea

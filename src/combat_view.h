#pragma once

// The look of a fight. Everything here is presentation: the game decides where a round is and
// what it hit; this turns a mount firing, a round in flight and a hit landing into muzzle
// flashes, tracers, rocket trails, torpedo wakes, fireballs, smoke, splashes, debris and a hull
// going down.
//
// A particle is world-anchored and pixel-sized: it remembers the latitude and longitude it was
// born at and carries its own motion in screen pixels. Anchoring to the world means panning and
// zooming never leave an explosion hanging in the wrong place, while keeping the motion in
// pixels means a fireball reads the same size at every zoom instead of collapsing to nothing
// when the map is pulled back.

#include <random>
#include <unordered_map>
#include <vector>

#include "game.h"
#include "map_view.h"
#include "naval_view.h"
#include "raylib.h"

namespace sea {

class CombatView {
 public:
  /// Clears every particle and wreck, for a new game or a load.
  void reset();

  /// Turns this frame's combat events into new effects and ages everything already alive.
  /// The game is read for the rounds still in flight and for which hulls are hurt enough to be
  /// trailing smoke.
  void update(float deltaSeconds, const std::vector<CombatEvent>& events, const GameState& game);

  /// Draws the wrecks, the rounds in flight and the particles. `naval` supplies the hull
  /// sprites, so a sinking ship is the same hull the map drew a moment ago.
  void draw(const MapView& map, const NavalView& naval, Rectangle viewport,
            const GameState& game) const;

 private:
  /// The shape a particle is drawn with, which is also how it ages: a spark gutters and dies, a
  /// fireball swells and blackens into smoke, a smoke puff drifts and thins.
  enum class Form { Flash, Fire, Smoke, Splash, Debris, Shock, Spray };

  struct Particle {
    LatLng anchor;
    Vector2 offset{0.0f, 0.0f};    // pixels from the anchor, updated every frame
    Vector2 velocity{0.0f, 0.0f};  // pixels per second
    float drag = 1.0f;             // velocity retained per second
    float age = 0.0f;
    float life = 1.0f;
    float size = 1.0f;    // screen radius now, in pixels
    float growth = 0.0f;  // pixels of radius added per second
    Color from{255, 255, 255, 255};
    Color to{255, 255, 255, 0};
    Form form = Form::Fire;
    bool additively = true;  // fire and flashes blend additively; smoke and water do not
  };

  /// A hull that has gone down: the map stops drawing it, so this keeps it on the surface for a
  /// few seconds and takes it under.
  struct Wreck {
    LatLng position;
    NavalModelKind kind = NavalModelKind::other;
    double headingDeg = 0.0;
    float age = 0.0f;
    float life = 7.0f;
  };

  void add(const Particle& particle);
  void emitMuzzle(const LatLng& at, FireCategory category, Delivery delivery, float headingDeg,
                  int mountIndex);
  void emitImpact(const LatLng& at, FireCategory category, Delivery delivery, bool onWater);
  void emitProjectileTrail(const Projectile& shot, float deltaSeconds);
  void emitHullSmoke(const LatLng& at, float damageFraction);

  float random(float low, float high);

  std::vector<Particle> particles_;
  std::vector<Wreck> wrecks_;
  std::unordered_map<int, float> smokeTimers_;  // hull id -> seconds to its next smoke puff
  std::unordered_map<int, float> trailTimers_;  // projectile id -> seconds to its next puff
  std::mt19937 rng_{std::random_device{}()};
};

}  // namespace sea

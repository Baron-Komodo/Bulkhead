#pragma once

// The naval units' artwork: one 3D hull model per class kind, rendered once at load into a
// sheet of sprites, one per 15 degrees of heading. That is the classic RTS trick - the map
// stays a fast 2D draw, but every unit is real 3D geometry looked at from a fixed aerial
// camera, so a hull turns properly as it comes about instead of a flat icon rotating.
//
// Everything here is presentation. The game rules never look at it.

#include <vector>

#include "game.h"
#include "raylib.h"

namespace sea {

class NavalView {
 public:
  /// Loads the hull models and bakes their rotation sheets. Call once, after the window
  /// exists (rendering offscreen needs a GL context). A missing model just leaves that kind
  /// without sprites; the map then draws its fallback marker.
  void load();

  /// Frees every render target. Safe to call more than once.
  void unload();

  /// Whether a kind has baked sprites.
  bool ready(NavalModelKind kind) const;

  /// The baked sprite for a heading, degrees clockwise from north, or nullptr when the kind
  /// has no model.
  const RenderTexture2D* frameFor(NavalModelKind kind, double headingDeg) const;

  /// Draws a unit's hull centred on `center`, scaled to `length` pixels long and tinted.
  void drawHull(NavalModelKind kind, double headingDeg, Vector2 center, float length,
                Color tint) const;

  /// Draws a spinning preview of a kind, fitted into `bounds` (the shipyard's 3D well).
  void drawPreview(NavalModelKind kind, Rectangle bounds, double spinDeg) const;

 private:
  struct Sheet {
    std::vector<RenderTexture2D> frames;  // one per heading step, index 0 = north
    bool loaded = false;
  };

  Sheet sheets_[8];
};

}  // namespace sea

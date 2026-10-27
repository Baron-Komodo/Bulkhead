#pragma once

// The game's typeface.
//
// raylib's built-in font is a 10-pixel bitmap: drawing it at any other size magnifies the
// pixels, which is where the blocky text came from. The bundled typeface is instead
// rasterised on demand, once per weight and pixel size, so every label is drawn at the
// resolution it was rendered at and comes out smooth.
//
// The face is Barlow Condensed (SIL Open Font License, see data/fonts/OFL.txt), a narrow
// grotesque in the same family as the ones Grand-strategy and sim UIs use. Swapping it is a
// matter of dropping other .ttf files in data/fonts and renaming them below.

#include <map>
#include <string>

#include "raylib.h"

namespace sea {

enum class FontWeight { Regular = 0, SemiBold = 1, Bold = 2 };

class FontBook {
 public:
  /// Resolves the font files. Safe to call when they are missing: every lookup then falls
  /// back to raylib's built-in font.
  void load();

  /// The font rasterised at `pixelSize`. Loaded on first use and cached, so a screen full of
  /// labels in the same size shares one texture.
  const Font& get(FontWeight weight, float pixelSize);

  Vector2 measure(const std::string& text, float pixelSize,
                  FontWeight weight = FontWeight::Regular);
  void draw(const std::string& text, float x, float y, float pixelSize, Color color,
            FontWeight weight = FontWeight::Regular);

 private:
  std::string path_[3];
  std::map<int, Font> cache_;
};

}  // namespace sea

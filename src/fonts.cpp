#include "fonts.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "data.h"  // resolveDataFile

namespace sea {
namespace {

const char* kFiles[3] = {"fonts/BarlowCondensed-Regular.ttf",
                         "fonts/BarlowCondensed-SemiBold.ttf",
                         "fonts/BarlowCondensed-Bold.ttf"};

/// Letter spacing, in pixels: the gap raylib's own default font uses.
constexpr float kSpacing = 1.0f;

/// Glyph sizes are rounded to whole pixels so a label is always rasterised at exactly the
/// size it is drawn at. Scaling the atlas is what makes text look soft or blocky.
int snappedSize(float pixelSize) {
  if (!std::isfinite(pixelSize)) return 16;
  return std::clamp(static_cast<int>(std::lround(pixelSize)), 8, 200);
}

/// ASCII, Latin-1 and Latin Extended-A (the accents in port and country names), plus the
/// punctuation and symbols the trade tables use.
std::vector<int>& codepoints() {
  static std::vector<int> list = [] {
    std::vector<int> built;
    built.reserve(360);
    for (int c = 0x20; c <= 0x7E; ++c) built.push_back(c);
    for (int c = 0xA0; c <= 0x17F; ++c) built.push_back(c);
    for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
                  0x20AC, 0x2264, 0x2265}) {
      built.push_back(c);
    }
    return built;
  }();
  return list;
}

}  // namespace

void FontBook::load() {
  for (int index = 0; index < 3; ++index) {
    path_[index] = resolveDataFile(kFiles[index]);
    if (GetFileLength(path_[index].c_str()) <= 0) {
      TraceLog(LOG_WARNING, "FONT: %s is missing; using the built-in font", path_[index].c_str());
      path_[index].clear();
    }
  }
}

const Font& FontBook::get(FontWeight weight, float pixelSize) {
  const int index = static_cast<int>(weight);
  const int size = snappedSize(pixelSize);
  const int key = index * 1000 + size;

  const auto cached = cache_.find(key);
  if (cached != cache_.end()) return cached->second;

  Font font{};
  if (!path_[index].empty()) {
    font = LoadFontEx(path_[index].c_str(), size, codepoints().data(),
                      static_cast<int>(codepoints().size()));
  }

  const Font builtIn = GetFontDefault();
  if (font.texture.id == 0 || font.texture.id == builtIn.texture.id) {
    // Either the file could not be read or the rasteriser rejected it, and raylib handed
    // back its built-in font. That one is owned by raylib, so it is never unloaded.
    font = builtIn;
  } else {
    // Smooth sampling. At this point the glyphs are already at their final size, so this
    // only softens the edges of the quads, it does not rescale the letters.
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
  }

  return cache_.emplace(key, font).first->second;
}

Vector2 FontBook::measure(const std::string& text, float pixelSize, FontWeight weight) {
  return MeasureTextEx(get(weight, pixelSize), text.c_str(), pixelSize, kSpacing);
}

void FontBook::draw(const std::string& text, float x, float y, float pixelSize, Color color,
                    FontWeight weight) {
  DrawTextEx(get(weight, pixelSize), text.c_str(), {x, y}, pixelSize, kSpacing, color);
}

}  // namespace sea

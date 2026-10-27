// Checks a frame captured by the game's scripted screenshot mode.
//
// The agent and CI have no display, so "did it draw anything" is answered by loading the
// PNG back with raylib and summarising the pixels.
//
// Text quality is checked the same way, since typography cannot be eyeballed here:
//   * `antialias` counts how many distinct shades appear in the content area. A bitmap font
//     scaled up lands on two or three shades; a font rasterised at its final size has a
//     coverage value per edge pixel.
//   * `flat rows` is the share of pixel rows identical to the row above. Magnified bitmap
//     text repeats rows in blocks, smooth text does not.
//   * `--ascii x y w h` prints a region as luminance characters, so a glyph can be looked at
//     directly instead of only measured.
//
// Note on HiDPI: raylib captures the whole GL framebuffer, which can be larger than the
// logical window (e.g. 1440x860 drawn into a 1800x1075 buffer). The uncovered margin is
// pure black, so the report first finds the content bounding box (non-black pixels) and
// then summarises that region only.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "raylib.h"

namespace {

struct Bucket {
  unsigned color;
  int count;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf(
        "usage: frame_report <frame.png> [--ascii x y w h] [--crop x y w h] "
        "[--count RRGGBB[:label] ...]\n");
    return 2;
  }

  int cropX = -1, cropY = -1, cropW = 0, cropH = 0;
  bool ascii = false;
  std::vector<unsigned> counts;  // colours asked about with --count, from the command line
  std::vector<std::string> countNames;
  for (int i = 2; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--count") {
      if (i + 1 >= argc) {
        std::printf("--count needs RRGGBB, optionally :label\n");
        return 2;
      }
      const std::string value = argv[++i];
      const size_t colon = value.find(':');
      counts.push_back(static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 16)));
      countNames.push_back(colon == std::string::npos ? value.substr(0, 6)
                                                      : value.substr(colon + 1));
      continue;
    }
    if (flag != "--ascii" && flag != "--crop") continue;
    if (i + 4 >= argc) {
      std::printf("%s needs four numbers\n", flag.c_str());
      return 2;
    }
    if (flag == "--ascii") {
      ascii = true;
      cropX = std::atoi(argv[++i]);
      cropY = std::atoi(argv[++i]);
      cropW = std::atoi(argv[++i]);
      cropH = std::atoi(argv[++i]);
    } else {
      i += 4;
    }
  }

  Image image = LoadImage(argv[1]);
  if (image.data == nullptr || image.width <= 0) {
    std::printf("%s: FAILED to load\n", argv[1]);
    return 1;
  }
  ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  auto* pixels = static_cast<unsigned char*>(image.data);

  auto colorAt = [&](int x, int y) -> unsigned {
    const int index = (y * image.width + x) * 4;
    return (static_cast<unsigned>(pixels[index]) << 16) |
           (static_cast<unsigned>(pixels[index + 1]) << 8) | pixels[index + 2];
  };
  auto lumaAt = [&](int x, int y) -> int {
    const unsigned color = colorAt(x, y);
    return (299 * static_cast<int>((color >> 16) & 0xFF) +
            587 * static_cast<int>((color >> 8) & 0xFF) + 114 * static_cast<int>(color & 0xFF)) /
           1000;
  };

  // Content bounding box: everything that is not pure black.
  int minX = image.width, minY = image.height, maxX = -1, maxY = -1;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      if (colorAt(x, y) == 0u) continue;
      minX = std::min(minX, x);
      maxX = std::max(maxX, x);
      minY = std::min(minY, y);
      maxY = std::max(maxY, y);
    }
  }

  if (maxX < 0) {
    std::printf("%s: %dx%d  ALL BLACK\n", argv[1], image.width, image.height);
    UnloadImage(image);
    return 1;
  }

  std::map<unsigned, int> histogram;
  long long sumR = 0, sumG = 0, sumB = 0, counted = 0;
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const unsigned color = colorAt(x, y);
      histogram[color]++;
      sumR += (color >> 16) & 0xFF;
      sumG += (color >> 8) & 0xFF;
      sumB += color & 0xFF;
      ++counted;
    }
  }

  std::vector<Bucket> buckets;
  buckets.reserve(histogram.size());
  for (const auto& entry : histogram) buckets.push_back({entry.first, entry.second});
  std::sort(buckets.begin(), buckets.end(),
            [](const Bucket& a, const Bucket& b) { return a.count > b.count; });

  // Text quality over the content area: how many shades, and how often a row repeats the
  // one above it (magnified bitmap glyphs do, rasterised ones do not).
  // Exact colour census: how many pixels carry each colour asked about with --count.
  std::vector<long long> colourCounts(counts.size(), 0);

  long long sameRowPairs = 0, rowPairs = 0;
  for (int y = minY + 1; y <= maxY; ++y) {
    bool identical = true;
    for (int x = minX; x <= maxX; ++x) {
      const unsigned color = colorAt(x, y);
      for (size_t i = 0; i < counts.size(); ++i) {
        if (color == counts[i]) colourCounts[i]++;
      }
      identical = identical && color == colorAt(x, y - 1);
    }
    sameRowPairs += identical ? 1 : 0;
    ++rowPairs;
  }

  for (size_t i = 0; i < counts.size(); ++i) {
    std::printf("  count %s (#%06X): %lld px\n", countNames[i].c_str(), counts[i], colourCounts[i]);
  }

  const long long contentArea = static_cast<long long>(maxX - minX + 1) * (maxY - minY + 1);
  std::printf("%s\n  frame %dx%d, content %dx%d at (%d,%d), covering %.0f%% of the buffer\n",
              argv[1], image.width, image.height, maxX - minX + 1, maxY - minY + 1, minX, minY,
              100.0 * contentArea / (static_cast<double>(image.width) * image.height));
  std::printf("  content: distinct=%zu  mean=(%lld,%lld,%lld)\n", histogram.size(), sumR / counted,
              sumG / counted, sumB / counted);
  for (size_t i = 0; i < buckets.size() && i < 6; ++i) {
    std::printf("    #%06X  %.2f%%\n", buckets[i].color, 100.0 * buckets[i].count / counted);
  }
  std::printf("  text:    flat rows=%.2f%%  (lower is smoother)\n",
              100.0 * sameRowPairs / static_cast<double>(rowPairs));

  if (ascii) {
    cropW = std::min(cropW, image.width - cropX);
    cropH = std::min(cropH, image.height - cropY);
    const std::string ramp = " .:-=+*#%@";
    std::printf("  --ascii (%d,%d) %dx%d --\n", cropX, cropY, cropW, cropH);
    for (int y = cropY; y < cropY + cropH; ++y) {
      std::string line;
      for (int x = cropX; x < cropX + cropW; ++x) {
        const int luma = std::clamp(lumaAt(x, y), 0, 255);
        line.push_back(ramp[static_cast<size_t>(luma * (ramp.size() - 1) / 255)]);
      }
      std::printf("  |%s|\n", line.c_str());
    }
  }

  UnloadImage(image);
  // A frame with real UI on it has many colours; a blank one has one or two.
  return histogram.size() < 8 ? 1 : 0;
}

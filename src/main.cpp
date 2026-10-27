// Ocean Builder, desktop edition: a C++ / raylib port of the maritime trading game.
//
// The game is single-player and offline. It loads the same data the web build ships
// (ports, vessels, the sea-routing grid, trade flows and country outlines) and keeps its
// progress in a local save file instead of an account.

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <iostream>
#include "raylib.h"
#include "rlgl.h"
#include <vector>
#include "ui.h"

namespace {

int envInt(const char* name, int fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return fallback;
  return std::atoi(value);
}

}  // namespace


std::string random(){
  std::srand(std::time(0));
  std::vector<std::string> sentences = {"Now on MS DOS", "75 20 6E 65 72 64", "Admin is doing it sideways! ", "There is nothing we can do", "another 2000 hour of joy", "Duke fishron approved", "好久没见"};
  return sentences.at(std::rand() % sentences.size());
}

int main() {
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);

  // The window size can be overridden so the screenshot automation can run on a display
  // smaller than the default 1440x860 (a drawable smaller than the window would clip the
  // right-hand panels out of every capture).
  InitWindow(envInt("SEA_BUILDER_WIDTH", 1440), envInt("SEA_BUILDER_HEIGHT", 860),
             ("Ocean Builder - " + random()).c_str());
  SetTargetFPS(60);

  sea::App app;
  app.init();

  // Scripted smoke test: SEA_BUILDER_DEMO claims ports and plots a route, and
  // SEA_BUILDER_SCREENSHOT saves a frame to disk, so the game can be exercised without a
  // person at the keyboard.
  const bool demo = std::getenv("SEA_BUILDER_DEMO") != nullptr;
  const bool startVoyage = std::getenv("SEA_BUILDER_START_VOYAGE") != nullptr;
  const char* screen = std::getenv("SEA_BUILDER_SCREEN");
  const char* screenshot = std::getenv("SEA_BUILDER_SCREENSHOT");
  const int captureFrame = envInt("SEA_BUILDER_FRAME", 40);
  const int stopFrame = envInt("SEA_BUILDER_FRAMES", -1);

  int frame = 0;
  double secondTimer = 0.0;

    Image cursor = LoadImage(sea::App::cursor.c_str());
    Texture2D texture = LoadTextureFromImage(cursor);
    UnloadImage(cursor);
    HideCursor();
    // Vector2 ballPosition = { -100.0f, -100.0f };
    // Color ballColor = DARKBLUE;



      // InitAudioDevice();              // Initialize audio device
    //  Music music = LoadMusicStream("data/audio/Rule Britannia.mp3");
    //  PlayMusicStream(music);
    //  float volume = 0.8f;            // Default audio volume [0.0f..1.0f]
    //  SetMusicVolume(music, volume);



  while (!app.shouldClose()) {
    // UpdateMusicStream(music);  // no stream until the audio device is initialised above


    // Elapsed time since the window opened, in milliseconds: the game clock every voyage
    // is measured against.
    const double nowMs = GetTime() * 1000.0;



    if (demo && frame == 2) app.debugDemo();
    if (startVoyage && demo && frame == 6) app.debugStartVoyage();
    if (screen != nullptr && frame == 8) app.debugShowScreen(screen);

    BeginDrawing();
    // std::cout << std::to_string(GetFrameTime()) << " " << std::to_string(nowMs) << std::endl;



    app.update(GetFrameTime(), nowMs);

    secondTimer += GetFrameTime();
    // Trigger each second, but only once the start menu has handed over to the game.
    if (secondTimer >= 1.0) {
        secondTimer -= 1.0;
        if (app.isPlaying()) app.game_.changeMoney(app.game_.passiveIncomePerSecond());
    }

    app.draw();
    if (screenshot != nullptr && frame == captureFrame) {
      // TakeScreenshot reads the framebuffer directly and raylib only flushes its draw
      // batch in EndDrawing, so force the flush first or the capture comes back empty.
      rlDrawRenderBatchActive();
      TakeScreenshot(screenshot);
    }

    //cursor
    const Vector2 mouse = GetMousePosition();

    // The pointer is part of the shell, so it is drawn at the same enlarged scale as the
    // rest of the icon art.
    const float cursorScale = 1.0f;
    // DrawTextureEx places the texture's top-left corner at the position, which leaves the
    // art hanging down-right of the real pointer. Shift by half the scaled size so the art
    // is centred on the mouse.
    const Vector2 cursorOffset = {texture.width * cursorScale * 0.38f,
                                  texture.height * cursorScale * 0.31f};
    DrawTextureEx(texture, {mouse.x - cursorOffset.x, mouse.y - cursorOffset.y}, 0.0f,
                  cursorScale, WHITE);

    // DrawCircleV(GetMousePosition(), 40, ballColor);

    EndDrawing();


    if (demo && (frame == 3 || frame == captureFrame || (frame % 600) == 0)) {
      std::printf("[demo] frame %d: %s\n", frame, app.debugStatus().c_str());
      std::fflush(stdout);
    }

    ++frame;



    if (screenshot != nullptr && frame > captureFrame) break;
    if (stopFrame > 0 && frame > stopFrame) break;
  }

  CloseWindow();
  return 0;
}

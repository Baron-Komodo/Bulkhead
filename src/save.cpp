#include "save.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sea {
namespace {

std::string executableDir() {
#if defined(_WIN32)
  char buffer[4096];
  const unsigned long length = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
  if (length == 0) return "";
  std::string path(buffer, length);
  const size_t slash = path.find_last_of("\\/");
  return slash == std::string::npos ? "" : path.substr(0, slash);
#else
  char buffer[4096];
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) return "";
  std::string path(buffer, static_cast<size_t>(length));
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? "" : path.substr(0, slash);
#endif
}

std::string homeDir() {
  if (const char* home = std::getenv("USERPROFILE")) return home;
  if (const char* home = std::getenv("HOME")) return home;
  return ".";
}

bool writable(const std::string& path) {
  std::ofstream probe(path, std::ios::app);
  return static_cast<bool>(probe);
}

}  // namespace

std::string defaultSavePath() {
  const std::string exeDir = executableDir();
  if (!exeDir.empty()) {
    const std::string candidate = exeDir + "/savegame.json";
    if (writable(candidate)) return candidate;
  }
  return homeDir() + "/sea-builder-save.json";
}

bool saveGame(const GameState& game, const std::string& path) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return false;
  file << game.saveToJson();
  return static_cast<bool>(file);
}

bool loadGame(GameState& game, const std::string& path, std::string& error, double nowMs) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "No save file at " + path;
    return false;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return game.loadFromJson(buffer.str(), error, nowMs);
}

}  // namespace sea

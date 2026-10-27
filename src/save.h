#pragma once

// Local persistence: a single JSON save file, replacing the web game's backend account.

#include <string>

#include "game.h"

namespace sea {

/// The default save location: beside the executable when that is writable, otherwise the
/// user's home directory.
std::string defaultSavePath();

bool saveGame(const GameState& game, const std::string& path);
bool loadGame(GameState& game, const std::string& path, std::string& error, double nowMs);

}  // namespace sea

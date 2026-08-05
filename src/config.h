// config.h - persists per-fan settings to %APPDATA%\MacFanCtl\config.ini

#pragma once

#include "controller.h"
#include <string>
#include <vector>

namespace app {

std::wstring configPath();

// Returns an empty vector if there is nothing saved yet.
std::vector<FanConfig> loadConfig();

bool saveConfig(const std::vector<FanConfig>& cfg);

} // namespace app

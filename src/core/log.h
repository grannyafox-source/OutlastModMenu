// Thread-safe log file plus an in-memory ring buffer that the in-game
// Diagnostics tab displays.
#pragma once

#include "common.h"

#include <string>
#include <vector>

namespace omm::log {

enum class Level { Debug = 0, Info, Warn, Error };

void Init(const std::string& path);
void Shutdown();
void SetMinLevel(Level lvl);
void Write(Level lvl, const char* fmt, ...) OMM_PRINTF(2, 3);
std::vector<std::string> Recent(size_t maxLines = 400);
const std::string& FilePath();

}  // namespace omm::log

#define LOGD(...) ::omm::log::Write(::omm::log::Level::Debug, __VA_ARGS__)
#define LOGI(...) ::omm::log::Write(::omm::log::Level::Info, __VA_ARGS__)
#define LOGW(...) ::omm::log::Write(::omm::log::Level::Warn, __VA_ARGS__)
#define LOGE(...) ::omm::log::Write(::omm::log::Level::Error, __VA_ARGS__)

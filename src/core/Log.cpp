#include "btai/core/Log.hpp"
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace btai {
namespace {
std::mutex mutex;
const char* levelName(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
  }
  return "UNKNOWN";
}
}

void Log::write(LogLevel level, std::string_view message) {
  std::lock_guard lock(mutex);
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::clog << '[' << now << "] [" << levelName(level) << "] ["
            << std::this_thread::get_id() << "] " << message << '\n';
}
} // namespace btai

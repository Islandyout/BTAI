#pragma once

#include <string_view>

namespace btai {

enum class LogLevel { Debug, Info, Warning, Error };

class Log final {
public:
  static void write(LogLevel level, std::string_view message);
};

} // namespace btai

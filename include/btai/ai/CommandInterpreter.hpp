#pragma once

#include "btai/ecs/Registry.hpp"
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace btai::ai {

struct CommandResult {
  bool ok = false;
  ecs::Entity entity{};
  std::string error;
};

class CommandInterpreter final {
public:
  explicit CommandInterpreter(ecs::Registry& registry) noexcept : registry_(registry) {}
  CommandResult execute(std::string_view json);
  CommandResult execute(const nlohmann::json& command);
private:
  CommandResult fail(std::string message) const;
  bool entity(const nlohmann::json& value, ecs::Entity& out) const noexcept;
  static bool vec3(const nlohmann::json& value, ecs::Vec3& out) noexcept;
  ecs::Registry& registry_;
};

} // namespace btai::ai

#pragma once

#include "btai/ecs/Registry.hpp"
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace btai::project { class Project; }

namespace btai::ai {

struct CommandResult {
  bool ok = false;
  ecs::Entity entity{};
  std::string error;
  // Populated by read-only/query commands (list_entities, describe_entity).
  // Write commands leave this as JSON null.
  nlohmann::json data;
};

// Executes small JSON commands against an ecs::Registry. This is the primary
// surface an external tool, script, or AI agent uses to build and inspect a
// scene without touching C++ — see docs/AI_INTERFACE.md for the full command
// reference, including save_scene/load_scene which round-trip through
// project::Scene.
class CommandInterpreter final {
public:
  explicit CommandInterpreter(ecs::Registry& registry) noexcept : registry_(registry) {}
  CommandResult execute(std::string_view json);
  CommandResult execute(const nlohmann::json& command);
  // Without this exact overload, a raw string/char* literal (e.g.
  // ai.execute("{...}")) is ambiguous: it converts equally well to
  // std::string_view and, via nlohmann::json's implicit string constructor,
  // to `const nlohmann::json&`. An exact-match const char* overload wins
  // over both conversions and forwards to the string_view parser.
  CommandResult execute(const char* json) { return execute(std::string_view(json)); }

  // save_scene/load_scene resolve their "path" argument through this project's
  // sandboxed Project::resolve, so must be set before those commands are used.
  // Not required for any other command. The pointer is not owned; only
  // Project's const methods (resolve()) are ever called through it.
  void setProject(const project::Project* project) noexcept { project_ = project; }

private:
  CommandResult fail(std::string message) const;
  bool entity(const nlohmann::json& value, ecs::Entity& out) const noexcept;
  static bool vec3(const nlohmann::json& value, ecs::Vec3& out) noexcept;
  ecs::Registry& registry_;
  const project::Project* project_ = nullptr;
};

} // namespace btai::ai

#pragma once

#include "btai/ecs/Registry.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>

// Generic scene (de)serialization for the ECS registry.
//
// This is the persistence half of the AI-authoring loop: btai::ai::CommandInterpreter
// lets a script or an AI agent build up a scene entity-by-entity; Scene lets that
// result be written to disk as plain JSON and reloaded later, either by the engine
// or by tooling that never touches C++ at all. See docs/AI_INTERFACE.md.
namespace btai::project {

// Dumps every component attached to `entity` into a JSON object keyed by
// component name, e.g. {"Transform":{"position":[0,1,0]},"Name":{"value":"Player"}}.
// Returns an empty object for an entity with no recognized components.
nlohmann::json dumpEntity(const ecs::Registry& registry, ecs::Entity entity);

// Serializes every alive entity in `registry` to a JSON scene document:
// {"format":1,"entities":[{"components":{...}}, ...]}.
nlohmann::json serializeScene(const ecs::Registry& registry);

// Recreates entities/components described by `scene` (as produced by
// serializeScene) into `registry`. New entities are created fresh — indices
// and generations from the original scene are not preserved, since no
// component in this engine references another entity by id. If `clearFirst` is
// true (the default) every existing entity in `registry` is destroyed first,
// so the registry ends up containing exactly the scene's contents.
// Throws std::runtime_error on a malformed document.
void deserializeScene(ecs::Registry& registry, const nlohmann::json& scene, bool clearFirst = true);

// Convenience wrappers that read/write the JSON document as a file.
// Throws std::runtime_error if the file can't be opened or parsed.
void saveScene(const ecs::Registry& registry, const std::filesystem::path& path);
void loadScene(ecs::Registry& registry, const std::filesystem::path& path, bool clearFirst = true);

} // namespace btai::project

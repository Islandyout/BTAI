#include "btai/project/Scene.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace btai::project {
namespace {

constexpr std::array<const char*, 7> kAiStateNames{"Idle", "Walking", "Running", "Driving", "Fleeing", "Chasing", "Dead"};
constexpr std::array<const char*, 2> kColliderTypeNames{"AABB", "Sphere"};

nlohmann::json vec3ToJson(const ecs::Vec3& v) { return nlohmann::json::array({v.x, v.y, v.z}); }

ecs::Vec3 vec3FromJson(const nlohmann::json& j) {
  if (!j.is_array() || j.size() != 3) throw std::runtime_error("expected a [x,y,z] array");
  return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()};
}

// --- per-component (de)serialization -------------------------------------
// Explicit, one-branch-per-type dispatch, matching the style already used by
// ai::CommandInterpreter's attach_component/remove_component handlers.

void dumpComponents(const ecs::Registry& registry, ecs::Entity e, nlohmann::json& out) {
  if (const auto* c = registry.get<ecs::Transform>(e)) out["Transform"] = {{"position", vec3ToJson(c->position)}};
  if (const auto* c = registry.get<ecs::Rotation>(e)) out["Rotation"] = {{"euler", vec3ToJson(c->euler)}};
  if (const auto* c = registry.get<ecs::Scale>(e)) out["Scale"] = {{"value", vec3ToJson(c->value)}};
  if (const auto* c = registry.get<ecs::Velocity>(e)) out["Velocity"] = {{"value", vec3ToJson(c->value)}};
  if (const auto* c = registry.get<ecs::Acceleration>(e)) out["Acceleration"] = {{"value", vec3ToJson(c->value)}};
  if (const auto* c = registry.get<ecs::RigidBody>(e)) out["RigidBody"] = {{"mass", c->mass}, {"dynamic", c->dynamic}};
  if (const auto* c = registry.get<ecs::Collider>(e)) {
    out["Collider"] = {{"type", kColliderTypeNames[static_cast<std::size_t>(c->type)]},
                        {"halfExtents", vec3ToJson(c->halfExtents)},
                        {"radius", c->radius}};
  }
  if (const auto* c = registry.get<ecs::Health>(e)) out["Health"] = {{"current", c->current}, {"maximum", c->maximum}};
  if (const auto* c = registry.get<ecs::AIState>(e)) out["AIState"] = {{"state", kAiStateNames[static_cast<std::size_t>(c->state)]}};
  if (const auto* c = registry.get<ecs::Pedestrian>(e)) out["Pedestrian"] = {{"archetype", c->archetype}};
  if (const auto* c = registry.get<ecs::Vehicle>(e)) out["Vehicle"] = {{"archetype", c->archetype}};
  if (const auto* c = registry.get<ecs::AnimationState>(e)) {
    out["AnimationState"] = {{"clip", c->clip}, {"time", c->time}, {"looping", c->looping}};
  }
  if (const auto* c = registry.get<ecs::Renderable>(e)) {
    out["Renderable"] = {{"mesh", c->mesh}, {"material", c->material}, {"visible", c->visible}};
  }
  if (const auto* c = registry.get<ecs::Name>(e)) out["Name"] = {{"value", c->value}};
}

void loadComponents(ecs::Registry& registry, ecs::Entity e, const nlohmann::json& components) {
  if (!components.is_object()) throw std::runtime_error("entity 'components' must be an object");
  if (const auto it = components.find("Transform"); it != components.end())
    registry.add<ecs::Transform>(e, vec3FromJson(it->at("position")));
  if (const auto it = components.find("Rotation"); it != components.end())
    registry.add<ecs::Rotation>(e, vec3FromJson(it->at("euler")));
  if (const auto it = components.find("Scale"); it != components.end())
    registry.add<ecs::Scale>(e, vec3FromJson(it->at("value")));
  if (const auto it = components.find("Velocity"); it != components.end())
    registry.add<ecs::Velocity>(e, vec3FromJson(it->at("value")));
  if (const auto it = components.find("Acceleration"); it != components.end())
    registry.add<ecs::Acceleration>(e, vec3FromJson(it->at("value")));
  if (const auto it = components.find("RigidBody"); it != components.end()) {
    ecs::RigidBody body;
    body.mass = it->value("mass", 1.0f);
    body.dynamic = it->value("dynamic", true);
    body.inverseMass = body.dynamic && body.mass > 0.0f ? 1.0f / body.mass : 0.0f;
    registry.add<ecs::RigidBody>(e, body);
  }
  if (const auto it = components.find("Collider"); it != components.end()) {
    ecs::Collider collider;
    const auto typeName = it->value("type", std::string{"AABB"});
    const auto match = std::find(kColliderTypeNames.begin(), kColliderTypeNames.end(), typeName);
    if (match == kColliderTypeNames.end()) throw std::runtime_error("unknown Collider type: " + typeName);
    collider.type = static_cast<ecs::Collider::Type>(std::distance(kColliderTypeNames.begin(), match));
    if (const auto he = it->find("halfExtents"); he != it->end()) collider.halfExtents = vec3FromJson(*he);
    collider.radius = it->value("radius", 0.5f);
    registry.add<ecs::Collider>(e, collider);
  }
  if (const auto it = components.find("Health"); it != components.end()) {
    ecs::Health health;
    health.current = it->value("current", 100.0f);
    health.maximum = it->value("maximum", 100.0f);
    registry.add<ecs::Health>(e, health);
  }
  if (const auto it = components.find("AIState"); it != components.end()) {
    const auto stateName = it->value("state", std::string{"Idle"});
    const auto match = std::find(kAiStateNames.begin(), kAiStateNames.end(), stateName);
    if (match == kAiStateNames.end()) throw std::runtime_error("unknown AIState: " + stateName);
    ecs::AIState state;
    state.state = static_cast<ecs::AIState::State>(std::distance(kAiStateNames.begin(), match));
    registry.add<ecs::AIState>(e, state);
  }
  if (const auto it = components.find("Pedestrian"); it != components.end())
    registry.add<ecs::Pedestrian>(e, ecs::Pedestrian{it->value("archetype", 0u)});
  if (const auto it = components.find("Vehicle"); it != components.end())
    registry.add<ecs::Vehicle>(e, ecs::Vehicle{it->value("archetype", 0u)});
  if (const auto it = components.find("AnimationState"); it != components.end()) {
    ecs::AnimationState anim;
    anim.clip = it->value("clip", 0u);
    anim.time = it->value("time", 0.0f);
    anim.looping = it->value("looping", true);
    registry.add<ecs::AnimationState>(e, anim);
  }
  if (const auto it = components.find("Renderable"); it != components.end()) {
    ecs::Renderable renderable;
    renderable.mesh = it->value("mesh", 0u);
    renderable.material = it->value("material", 0u);
    renderable.visible = it->value("visible", true);
    registry.add<ecs::Renderable>(e, renderable);
  }
  if (const auto it = components.find("Name"); it != components.end())
    registry.add<ecs::Name>(e, ecs::Name{it->value("value", std::string{})});
}

} // namespace

nlohmann::json dumpEntity(const ecs::Registry& registry, ecs::Entity entity) {
  nlohmann::json components = nlohmann::json::object();
  dumpComponents(registry, entity, components);
  return components;
}

nlohmann::json serializeScene(const ecs::Registry& registry) {
  nlohmann::json entities = nlohmann::json::array();
  registry.eachAlive([&](ecs::Entity e) {
    entities.push_back({{"components", dumpEntity(registry, e)}});
  });
  return {{"format", 1}, {"entities", std::move(entities)}};
}

void deserializeScene(ecs::Registry& registry, const nlohmann::json& scene, bool clearFirst) {
  if (!scene.is_object() || scene.value("format", 0) != 1) throw std::runtime_error("unsupported scene format");
  const auto entities = scene.find("entities");
  if (entities == scene.end() || !entities->is_array()) throw std::runtime_error("scene is missing an 'entities' array");
  if (clearFirst) registry.clear();
  for (const auto& entry : *entities) {
    if (!entry.is_object()) throw std::runtime_error("scene entity must be an object");
    const auto e = registry.create();
    const auto components = entry.find("components");
    if (components != entry.end()) loadComponents(registry, e, *components);
  }
}

void saveScene(const ecs::Registry& registry, const std::filesystem::path& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write scene file: " + path.string());
  out << serializeScene(registry).dump(2) << '\n';
}

void loadScene(ecs::Registry& registry, const std::filesystem::path& path, bool clearFirst) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open scene file: " + path.string());
  nlohmann::json json;
  try {
    in >> json;
  } catch (const std::exception& e) {
    throw std::runtime_error("invalid scene file " + path.string() + ": " + e.what());
  }
  deserializeScene(registry, json, clearFirst);
}

} // namespace btai::project

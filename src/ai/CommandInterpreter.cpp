#include "btai/ai/CommandInterpreter.hpp"
#include <array>
#include <exception>

namespace btai::ai {

CommandResult CommandInterpreter::fail(std::string message) const { return {false, {}, std::move(message)}; }

bool CommandInterpreter::vec3(const nlohmann::json& value, ecs::Vec3& out) noexcept {
  if (!value.is_array() || value.size() != 3) return false;
  for (std::size_t i=0;i<3;++i) if (!value[i].is_number()) return false;
  out={value[0].get<float>(),value[1].get<float>(),value[2].get<float>()};
  return true;
}

bool CommandInterpreter::entity(const nlohmann::json& value, ecs::Entity& out) const noexcept {
  if (!value.is_object() || !value.contains("index") || !value.contains("generation") ||
      !value["index"].is_number_unsigned() || !value["generation"].is_number_unsigned()) return false;
  out={value["index"].get<std::uint32_t>(),value["generation"].get<std::uint32_t>()};
  return registry_.alive(out);
}

CommandResult CommandInterpreter::execute(std::string_view text) {
  try { return execute(nlohmann::json::parse(text)); }
  catch (const std::exception& e) { return fail(std::string("invalid JSON: ")+e.what()); }
}

CommandResult CommandInterpreter::execute(const nlohmann::json& c) {
  if (!c.is_object() || !c.contains("command") || !c["command"].is_string()) return fail("command must be a string");
  const std::string op=c["command"].get<std::string>();
  try {
    if (op=="spawn_entity") {
      const auto e=registry_.create();
      if (c.contains("transform")) { ecs::Vec3 v; if (!vec3(c["transform"],v)) return fail("transform must be [x,y,z]"); registry_.add<ecs::Transform>(e,v); }
      if (c.contains("velocity")) { ecs::Vec3 v; if (!vec3(c["velocity"],v)) return fail("velocity must be [x,y,z]"); registry_.add<ecs::Velocity>(e,v); }
      if (c.value("physics",false)) { registry_.add<ecs::RigidBody>(e); registry_.add<ecs::Collider>(e); }
      if (c.contains("name")) { if (!c["name"].is_string()) return fail("name must be a string"); registry_.add<ecs::Name>(e,c["name"].get<std::string>()); }
      return {true,e,{}};
    }
    ecs::Entity e;
    if (!c.contains("entity") || !entity(c["entity"],e)) return fail("invalid or missing entity");
    if (op=="destroy_entity") return {registry_.destroy(e),e,registry_.alive(e)?"destroy failed":""};
    if (op=="set_transform") { ecs::Vec3 v; if (!vec3(c.value("position",nlohmann::json{}),v)) return fail("position must be [x,y,z]"); if(auto* t=registry_.get<ecs::Transform>(e)) t->position=v; else registry_.add<ecs::Transform>(e,v); return {true,e,{}}; }
    if (op=="set_velocity") { ecs::Vec3 v; if (!vec3(c.value("velocity",nlohmann::json{}),v)) return fail("velocity must be [x,y,z]"); if(auto* x=registry_.get<ecs::Velocity>(e)) x->value=v; else registry_.add<ecs::Velocity>(e,v); return {true,e,{}}; }
    if (op=="set_physics") { if (!c.contains("dynamic") || !c["dynamic"].is_boolean()) return fail("dynamic must be boolean"); auto* r=registry_.get<ecs::RigidBody>(e); if(!r) r=&registry_.add<ecs::RigidBody>(e); r->dynamic=c["dynamic"].get<bool>(); if(c.contains("mass")){if(!c["mass"].is_number()||c["mass"].get<float>()<=0) return fail("mass must be positive");r->mass=c["mass"].get<float>();r->inverseMass=1.0f/r->mass;} registry_.has<ecs::Collider>(e)||registry_.add<ecs::Collider>(e); return {true,e,{}}; }
    if (op=="set_ai_state") { if(!c.contains("state")||!c["state"].is_string()) return fail("state must be a string"); static constexpr std::array names{"Idle","Walking","Running","Driving","Fleeing","Chasing","Dead"}; const auto s=c["state"].get<std::string>(); auto it=std::find(names.begin(),names.end(),s); if(it==names.end()) return fail("unknown AI state"); auto* a=registry_.get<ecs::AIState>(e); if(!a)a=&registry_.add<ecs::AIState>(e); a->state=static_cast<ecs::AIState::State>(std::distance(names.begin(),it)); return {true,e,{}}; }
    if (op=="trigger_animation") { if(!c.contains("clip")||!c["clip"].is_number_unsigned()) return fail("clip must be unsigned"); auto* a=registry_.get<ecs::AnimationState>(e); if(!a)a=&registry_.add<ecs::AnimationState>(e); a->clip=c["clip"].get<std::uint32_t>(); a->time=0.0f; a->looping=c.value("looping",true); return {true,e,{}}; }
    if (op=="attach_component") { if(!c.contains("type")||!c["type"].is_string()) return fail("type must be a string"); const auto t=c["type"].get<std::string>(); if(t=="Transform") registry_.add<ecs::Transform>(e); else if(t=="Velocity") registry_.add<ecs::Velocity>(e); else if(t=="RigidBody") registry_.add<ecs::RigidBody>(e); else if(t=="Collider") registry_.add<ecs::Collider>(e); else if(t=="AIState") registry_.add<ecs::AIState>(e); else if(t=="Pedestrian") registry_.add<ecs::Pedestrian>(e); else if(t=="Vehicle") registry_.add<ecs::Vehicle>(e); else if(t=="AnimationState") registry_.add<ecs::AnimationState>(e); else if(t=="Renderable") registry_.add<ecs::Renderable>(e); else if(t=="Name") registry_.add<ecs::Name>(e); else return fail("unsupported component: "+t); return {true,e,{}}; }
    if (op=="remove_component") { if(!c.contains("type")||!c["type"].is_string()) return fail("type must be a string"); const auto t=c["type"].get<std::string>(); bool ok=false; if(t=="Transform")ok=registry_.remove<ecs::Transform>(e); else if(t=="Velocity")ok=registry_.remove<ecs::Velocity>(e); else if(t=="RigidBody")ok=registry_.remove<ecs::RigidBody>(e); else if(t=="Collider")ok=registry_.remove<ecs::Collider>(e); else if(t=="AIState")ok=registry_.remove<ecs::AIState>(e); else if(t=="Pedestrian")ok=registry_.remove<ecs::Pedestrian>(e); else if(t=="Vehicle")ok=registry_.remove<ecs::Vehicle>(e); else if(t=="AnimationState")ok=registry_.remove<ecs::AnimationState>(e); else if(t=="Renderable")ok=registry_.remove<ecs::Renderable>(e); else if(t=="Name")ok=registry_.remove<ecs::Name>(e); else return fail("unsupported component: "+t); return {ok,e,ok?"":"component not present"}; }
    return fail("unknown command: "+op);
  } catch (const std::exception& e) { return fail(std::string("command failed: ")+e.what()); }
}

} // namespace btai::ai

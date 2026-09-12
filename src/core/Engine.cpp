#include "btai/core/Engine.hpp"
#include "btai/platform/Window.hpp"
#include "btai/render/VulkanRenderer.hpp"
#include <chrono>
#include <exception>
#include <memory>
#include <thread>
#include <utility>

namespace btai {
Engine::Engine(EngineConfig config):config_(std::move(config)){}
Engine::~Engine(){shutdown();}
bool Engine::initialize(){
  if(running_)return true;
  try {
    project_=std::make_unique<project::Project>(project::Project::open(config_.projectManifest));
    window_=std::make_unique<Window>(config_.width,config_.height,config_.title);
    if(!window_->valid())throw std::runtime_error("Failed to create BTAI window");
    renderer_=std::make_unique<VulkanRenderer>(*window_);
    if(!renderer_->initialize())throw std::runtime_error("Failed to initialize Vulkan renderer");
    const auto modelPath=project_->resolve("Assets/Models/sample.gltf");
    if(const auto model=assets_.load(modelPath))renderer_->setModel(model);
  } catch(const std::exception& e) {
    Log::write(LogLevel::Error,e.what());
    shutdown();
    return false;
  }
  running_=true;
  simulation_=std::jthread([this](std::stop_token t){simulationLoop(t);});
  return true;
}
void Engine::simulationLoop(std::stop_token token){
  using clock=std::chrono::steady_clock;constexpr auto step=std::chrono::microseconds(16667);auto next=clock::now();
  const auto entity=registry_.create();
  registry_.add<ecs::Transform>(entity,ecs::Vec3{0,2,0});
  registry_.add<ecs::Velocity>(entity,ecs::Vec3{0,0,0.5f});
  registry_.add<ecs::RigidBody>(entity);
  registry_.add<ecs::Collider>(entity);
  registry_.add<ecs::Rotation>(entity);
  registry_.add<ecs::Scale>(entity);
  registry_.add<ecs::Renderable>(entity);
  const auto aiResult=ai_.execute(nlohmann::json{{"command","set_ai_state"},{"entity",{{"index",entity.index},{"generation",entity.generation}}},{"state","Walking"}});
  if(!aiResult.ok)Log::write(LogLevel::Error,aiResult.error);
  std::uint64_t tick=0;
  while(!token.stop_requested()&&running_){
    next+=step;
    physics_.step(registry_,1.0f/60.0f);
    if(const auto* transform=registry_.get<ecs::Transform>(entity))streamer_.update(transform->position);
    streamer_.tick();
    auto snapshot=std::make_shared<render::RenderSnapshot>();
    snapshot->tick=++tick;
    render::extract(registry_,*snapshot);
    latestSnapshot_.store(std::shared_ptr<const render::RenderSnapshot>(std::move(snapshot)),std::memory_order_release);
    std::this_thread::sleep_until(next);
    if(clock::now()>next+step*4)next=clock::now();
  }
}
int Engine::run(){
  if(!running_&&!initialize())return 1;
  while(running_&&!window_->shouldClose()){
    window_->poll();
    const auto snapshot=latestSnapshot_.load(std::memory_order_acquire);
    if(snapshot)renderer_->draw(*snapshot);else renderer_->draw();
  }
  shutdown();
  return 0;
}
void Engine::shutdown()noexcept{running_=false;if(simulation_.joinable()){simulation_.request_stop();simulation_.join();}if(renderer_)renderer_->shutdown();renderer_.reset();window_.reset();project_.reset();assets_.clear();jobs_.stop();latestSnapshot_.store({},std::memory_order_release);}
}

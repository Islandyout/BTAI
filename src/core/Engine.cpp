#include "btai/core/Engine.hpp"
#include "btai/platform/Window.hpp"
#include "btai/render/VulkanRenderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
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
    ai_.setProject(project_.get());
    window_=std::make_unique<Window>(config_.width,config_.height,config_.title);
    if(!window_->valid())throw std::runtime_error("Failed to create BTAI window");
    if(!input_.initialize(window_->native()))throw std::runtime_error("Failed to initialize input");
    renderer_=std::make_unique<VulkanRenderer>(*window_);
    if(!renderer_->initialize())throw std::runtime_error("Failed to initialize Vulkan renderer");
    editor_=std::make_unique<editor::Editor>(*window_->native(),*project_,registry_,paused_);
    if(!editor_->initialize(renderer_->imguiContext()))throw std::runtime_error("Failed to initialize BTAI editor");
    renderer_->setEditor(editor_.get());
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
  registry_.add<ecs::Name>(entity,ecs::Name{"Player"});
  const auto aiResult=ai_.execute(nlohmann::json{{"command","set_ai_state"},{"entity",{{"index",entity.index},{"generation",entity.generation}}},{"state","Walking"}});
  if(!aiResult.ok)Log::write(LogLevel::Error,aiResult.error);
  std::uint64_t tick=0;
  while(!token.stop_requested()&&running_){
    next+=step;
    if(paused_.load(std::memory_order_acquire)){std::this_thread::sleep_for(std::chrono::milliseconds(10));next=clock::now();continue;}
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
  lastFrame_=std::chrono::steady_clock::now();
  while(running_&&!window_->shouldClose()){
    window_->poll();
    input_.poll();
    if(input_.quitRequested())break;
    const auto now=std::chrono::steady_clock::now();
    const float dt=std::min(std::chrono::duration<float>(now-lastFrame_).count(),0.1f);
    lastFrame_=now;
    if(editor_){editor_->newFrame();editor_->draw();}
    ecs::Vec3 movement{};
    if(input_.key(GLFW_KEY_W))movement.z+=1.0f;
    if(input_.key(GLFW_KEY_S))movement.z-=1.0f;
    if(input_.key(GLFW_KEY_D))movement.x+=1.0f;
    if(input_.key(GLFW_KEY_A))movement.x-=1.0f;
    if(input_.key(GLFW_KEY_SPACE))movement.y+=1.0f;
    if(input_.key(GLFW_KEY_LEFT_CONTROL))movement.y-=1.0f;
    static bool cameraCapture=false;
    const bool rightMouse=input_.mouseButton(GLFW_MOUSE_BUTTON_RIGHT);
    if(rightMouse&&!cameraCapture){glfwSetInputMode(window_->native(),GLFW_CURSOR,GLFW_CURSOR_DISABLED);cameraCapture=true;}
    if(!rightMouse&&cameraCapture){glfwSetInputMode(window_->native(),GLFW_CURSOR,GLFW_CURSOR_NORMAL);cameraCapture=false;}
    if(cameraCapture&&!editor_->wantsKeyboardCapture()&&!editor_->wantsMouseCapture())camera_.update(movement,input_.mouseDeltaX(),input_.mouseDeltaY(),dt);
    const auto snapshot=latestSnapshot_.load(std::memory_order_acquire);
    if(snapshot)renderer_->draw(*snapshot,camera_);else renderer_->draw(render::RenderSnapshot{},camera_);
  }
  shutdown();
  return 0;
}
void Engine::shutdown()noexcept{running_=false;if(simulation_.joinable()){simulation_.request_stop();simulation_.join();}if(editor_)editor_->shutdown();if(renderer_)renderer_->setEditor(nullptr);editor_.reset();if(renderer_)renderer_->shutdown();renderer_.reset();window_.reset();project_.reset();assets_.clear();jobs_.stop();latestSnapshot_.store({},std::memory_order_release);}
}

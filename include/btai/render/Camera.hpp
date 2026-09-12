#pragma once

#include "btai/ecs/Components.hpp"

namespace btai::render {

class Camera final {
public:
  ecs::Vec3 position{0.0f,2.0f,6.0f};
  float yaw = -90.0f;
  float pitch = -12.0f;
  float moveSpeed = 8.0f;
  float mouseSensitivity = 0.12f;
  float fov = 70.0f;
  float nearPlane = 0.05f;
  float farPlane = 20000.0f;

  void update(const ecs::Vec3& movement, float mouseDx, float mouseDy, float dt);
  ecs::Vec3 forward() const noexcept;
  ecs::Vec3 right() const noexcept;
};

} // namespace btai::render

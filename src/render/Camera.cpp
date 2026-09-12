#include "btai/render/Camera.hpp"
#include <algorithm>
#include <cmath>

namespace btai::render {
namespace {
constexpr float Pi=3.14159265358979323846f;
ecs::Vec3 mul(ecs::Vec3 v,float s) noexcept { return {v.x*s,v.y*s,v.z*s}; }
ecs::Vec3 add(ecs::Vec3 a,ecs::Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
ecs::Vec3 normalize(ecs::Vec3 v) noexcept { const float d=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); return d>1e-6f?mul(v,1.0f/d):ecs::Vec3{0,0,-1}; }
}

void Camera::update(const ecs::Vec3& movement,float mouseDx,float mouseDy,float dt) {
  yaw += mouseDx*mouseSensitivity;
  pitch = std::clamp(pitch-mouseDy*mouseSensitivity,-89.0f,89.0f);
  const ecs::Vec3 f=forward();
  const ecs::Vec3 r=right();
  const ecs::Vec3 horizontal=normalize({f.x,0.0f,f.z});
  position=add(position,mul(add(add(mul(r,movement.x),mul(horizontal,movement.z)),{0.0f,movement.y,0.0f}),moveSpeed*std::max(dt,0.0f)));
}

ecs::Vec3 Camera::forward() const noexcept {
  const float y=yaw*Pi/180.0f,p=pitch*Pi/180.0f;
  return normalize({std::cos(y)*std::cos(p),std::sin(p),std::sin(y)*std::cos(p)});
}

ecs::Vec3 Camera::right() const noexcept {
  const ecs::Vec3 f=forward();
  return normalize({f.z,0.0f,-f.x});
}

} // namespace btai::render

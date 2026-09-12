#pragma once

#include "btai/ecs/Components.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <mutex>

namespace btai::ecs {

struct Entity {
  std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t generation = 0;
  constexpr bool valid() const noexcept { return index != std::numeric_limits<std::uint32_t>::max(); }
  friend constexpr bool operator==(Entity a, Entity b) noexcept { return a.index == b.index && a.generation == b.generation; }
};

class Registry final {
  template<class T>
  class Pool {
  public:
    static constexpr std::uint32_t npos = std::numeric_limits<std::uint32_t>::max();

    bool has(std::uint32_t index) const noexcept { return index < sparse_.size() && sparse_[index] != npos; }
    T& add(std::uint32_t index, T value = {}) {
      ensure(index);
      if (has(index)) {
        data_[sparse_[index]] = std::move(value);
        return data_[sparse_[index]];
      }
      sparse_[index] = static_cast<std::uint32_t>(entities_.size());
      entities_.push_back(index);
      data_.push_back(std::move(value));
      return data_.back();
    }
    void remove(std::uint32_t index) noexcept {
      if (!has(index)) return;
      const auto dense = sparse_[index];
      const auto last = static_cast<std::uint32_t>(entities_.size() - 1);
      if (dense != last) {
        entities_[dense] = entities_[last];
        data_[dense] = std::move(data_[last]);
        sparse_[entities_[dense]] = dense;
      }
      entities_.pop_back();
      data_.pop_back();
      sparse_[index] = npos;
    }
    T* get(std::uint32_t index) noexcept { return has(index) ? &data_[sparse_[index]] : nullptr; }
    const T* get(std::uint32_t index) const noexcept { return has(index) ? &data_[sparse_[index]] : nullptr; }
    const std::vector<std::uint32_t>& entities() const noexcept { return entities_; }
    std::size_t size() const noexcept { return entities_.size(); }
  private:
    void ensure(std::uint32_t index) {
      if (index >= sparse_.size()) sparse_.resize(static_cast<std::size_t>(index) + 1, npos);
    }
    std::vector<std::uint32_t> sparse_;
    std::vector<std::uint32_t> entities_;
    std::vector<T> data_;
  };

  using Pools = std::tuple<Pool<Transform>, Pool<Rotation>, Pool<Scale>, Pool<Velocity>, Pool<Acceleration>,
                        Pool<RigidBody>, Pool<Collider>, Pool<Health>, Pool<AIState>, Pool<Pedestrian>,
                        Pool<Vehicle>, Pool<AnimationState>, Pool<Renderable>, Pool<Name>>;

  template<class T>
  Pool<T>& pool() { return std::get<Pool<T>>(pools_); }
  template<class T>
  const Pool<T>& pool() const { return std::get<Pool<T>>(pools_); }

  template<class... T>
  void removeAll(std::uint32_t index) noexcept { (pool<T>().remove(index), ...); }

  template<class... T>
  bool hasAll(Entity entity) const noexcept { return (has<T>(entity) && ...); }

public:
  explicit Registry(std::uint32_t reserveEntities = 10000) {
    generations_.reserve(reserveEntities);
    alive_.reserve(reserveEntities);
    free_.reserve(reserveEntities);
  }

  Entity create() {
    std::lock_guard lock(mutex_);
    std::uint32_t index;
    if (free_.empty()) {
      index = static_cast<std::uint32_t>(generations_.size());
      generations_.push_back(0);
      alive_.push_back(true);
    } else {
      index = free_.back();
      free_.pop_back();
      alive_[index] = true;
    }
    return {index, generations_[index]};
  }

  bool destroy(Entity entity) noexcept {
    std::lock_guard lock(mutex_);
    if (!validUnlocked(entity)) return false;
    removeAll<Transform, Rotation, Scale, Velocity, Acceleration, RigidBody, Collider, Health, AIState,
              Pedestrian, Vehicle, AnimationState, Renderable, Name>(entity.index);
    alive_[entity.index] = false;
    ++generations_[entity.index];
    free_.push_back(entity.index);
    return true;
  }

  bool alive(Entity entity) const noexcept {
    std::lock_guard lock(mutex_);
    return validUnlocked(entity);
  }

  std::size_t size() const noexcept {
    std::lock_guard lock(mutex_);
    return aliveCount_;
  }

  template<class T, class... Args>
  T& add(Entity entity, Args&&... args) {
    std::lock_guard lock(mutex_);
    if (!validUnlocked(entity)) throw std::invalid_argument("invalid entity");
    auto& result = pool<T>().add(entity.index, T{std::forward<Args>(args)...});
    return result;
  }

  template<class T>
  bool remove(Entity entity) noexcept {
    std::lock_guard lock(mutex_);
    if (!validUnlocked(entity)) return false;
    pool<T>().remove(entity.index);
    return true;
  }

  template<class T>
  bool has(Entity entity) const noexcept {
    std::lock_guard lock(mutex_);
    return validUnlocked(entity) && pool<T>().has(entity.index);
  }

  template<class T>
  T* get(Entity entity) noexcept {
    std::lock_guard lock(mutex_);
    return validUnlocked(entity) ? pool<T>().get(entity.index) : nullptr;
  }

  template<class T>
  const T* get(Entity entity) const noexcept {
    std::lock_guard lock(mutex_);
    return validUnlocked(entity) ? pool<T>().get(entity.index) : nullptr;
  }

  template<class... T, class F>
  void each(F&& function) {
    std::lock_guard lock(mutex_);
    if constexpr (sizeof...(T) == 0) return;
    const auto& entities = pool<std::tuple_element_t<0, std::tuple<T...>>>().entities();
    for (const auto index : entities) {
      if (!alive_[index] || !hasAllUnlocked<T...>(index)) continue;
      std::invoke(function, Entity{index, generations_[index]}, *pool<T>().get(index)...);
    }
  }

private:
  bool validUnlocked(Entity entity) const noexcept {
    return entity.valid() && entity.index < alive_.size() && alive_[entity.index] && generations_[entity.index] == entity.generation;
  }
  template<class... T>
  bool hasAllUnlocked(std::uint32_t index) const noexcept { return (pool<T>().has(index) && ...); }

  mutable std::mutex mutex_;
  std::vector<std::uint32_t> generations_;
  std::vector<bool> alive_;
  std::vector<std::uint32_t> free_;
  std::size_t aliveCount_ = 0;
  Pools pools_;
};

} // namespace btai::ecs

#pragma once

#include "btai/ecs/Components.hpp"
#include "btai/jobs/JobSystem.hpp"
#include <cstdint>
#include <future>
#include <memory>
#include <unordered_map>
#include <vector>

namespace btai::world {

struct ChunkCoord {
  std::int32_t x=0,z=0;
  bool operator==(const ChunkCoord&) const noexcept = default;
};
struct ChunkCoordHash {
  std::size_t operator()(ChunkCoord c) const noexcept;
};

enum class ChunkState : std::uint8_t { Unloaded, Requested, Loading, Ready, Activating, Active, Deactivating, Unloading };

struct StaticObject { std::uint32_t mesh=0; ecs::Vec3 position{}; ecs::Vec3 scale{1,1,1}; };
struct Spawn { std::uint32_t archetype=0; ecs::Vec3 position{}; };
struct ChunkData {
  ChunkCoord coord{};
  float terrainHeight=0.0f;
  std::vector<StaticObject> objects;
  std::vector<Spawn> spawns;
};

class Streamer final {
public:
  struct Config { float chunkSize=128.0f; std::int32_t radius=2; std::size_t maxActive=64; };

  // NOTE: the default argument spells out Config's field values explicitly
  // (matching Config's own default member initializers below) rather than
  // using `Config config = {}`. A bare `{}` default argument would need to
  // evaluate Config's default member initializers, but per [class.mem] those
  // aren't usable until the *enclosing* class (Streamer) is complete — which
  // it isn't yet at this point in its own body. GCC correctly rejects
  // `= {}` here with "default member initializer ... required before the
  // end of its enclosing class"; spelling out the values sidesteps it.
  explicit Streamer(JobSystem& jobs, Config config = Config{128.0f, 2, 64});
  ~Streamer();
  Streamer(const Streamer&) = delete;
  Streamer& operator=(const Streamer&) = delete;

  void update(ecs::Vec3 center);
  void tick();
  ChunkState state(ChunkCoord coord) const;
  std::vector<ChunkCoord> activeChunks() const;
  static ChunkData generate(ChunkCoord coord);
  Config config() const noexcept { return config_; }

private:
  struct Entry { ChunkState state=ChunkState::Unloaded; std::shared_ptr<ChunkData> data; std::future<ChunkData> load; std::uint64_t request=0; };
  static std::int64_t distanceSq(ChunkCoord a, ChunkCoord b) noexcept;
  void request(ChunkCoord coord);
  void unload(ChunkCoord coord);

  JobSystem& jobs_;
  Config config_;
  std::unordered_map<ChunkCoord,Entry,ChunkCoordHash> chunks_;
  ChunkCoord center_{};
  std::uint64_t generation_=0;
};

} // namespace btai::world

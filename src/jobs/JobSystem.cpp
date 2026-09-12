#include "btai/jobs/JobSystem.hpp"
#include <algorithm>
#include <thread>

namespace btai {

JobSystem::JobSystem(std::size_t workerCount) {
  if (workerCount == 0) workerCount = std::max<std::size_t>(1, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1);
  workers_.reserve(workerCount);
  for (std::size_t i = 0; i < workerCount; ++i) workers_.emplace_back([this](std::stop_token token) { worker(token); });
}

JobSystem::~JobSystem() { stop(); }

void JobSystem::worker(std::stop_token token) {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, token, [this] { return stopping_ || !queue_.empty(); });
      if ((stopping_ || token.stop_requested()) && queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop_front();
      ++active_;
    }
    try { task(); } catch (...) {}
    {
      std::lock_guard lock(mutex_);
      --active_;
      if (queue_.empty() && active_ == 0) idleCondition_.notify_all();
    }
  }
}

void JobSystem::waitIdle() {
  std::unique_lock lock(mutex_);
  idleCondition_.wait(lock, [this] { return queue_.empty() && active_ == 0; });
}

void JobSystem::stop() noexcept {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
    queue_.clear();
  }
  for (auto& worker : workers_) worker.request_stop();
  condition_.notify_all();
  workers_.clear();
  {
    std::lock_guard lock(mutex_);
    active_ = 0;
  }
  idleCondition_.notify_all();
}

} // namespace btai

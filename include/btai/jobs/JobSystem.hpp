#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace btai {

class JobSystem final {
public:
  explicit JobSystem(std::size_t workerCount = 0);
  ~JobSystem();

  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  template<class F, class... Args>
  auto submit(F&& function, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using R = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<R()>>(
        std::bind(std::forward<F>(function), std::forward<Args>(args)...));
    auto future = task->get_future();
    {
      std::lock_guard lock(mutex_);
      if (stopping_) throw std::runtime_error("JobSystem is stopped");
      queue_.emplace_back([task] { (*task)(); });
    }
    condition_.notify_one();
    return future;
  }

  void waitIdle();
  void stop() noexcept;
  std::size_t workerCount() const noexcept { return workers_.size(); }

private:
  void worker(std::stop_token token);

  mutable std::mutex mutex_;
  std::condition_variable_any condition_;
  std::condition_variable idleCondition_;
  std::deque<std::function<void()>> queue_;
  std::vector<std::jthread> workers_;
  std::size_t active_ = 0;
  bool stopping_ = false;
};

} // namespace btai

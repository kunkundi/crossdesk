#include "task_queue_lock_free.h"

#include <concurrentqueue.h>

#include <exception>
#include <new>
#include <stdexcept>
#include <utility>

namespace crossdesk {

struct TaskQueueLockFree::QueueStorage {
  moodycamel::ConcurrentQueue<TaskItem> tasks;
  // All submissions share one producer so different caller threads keep FIFO.
  moodycamel::ProducerToken producer{tasks};
};

TaskQueueLockFree::TaskQueueLockFree()
    : queue_(std::make_unique<QueueStorage>()),
      worker_([this] { WorkerThread(); }) {}

TaskQueueLockFree::~TaskQueueLockFree() { Stop(); }

std::future<void> TaskQueueLockFree::PostTask(std::function<void()> task) {
  TaskItem item;
  item.run = std::move(task);
  auto result = item.completion.get_future();
  {
    std::lock_guard lock(mutex_);
    if (stopping_) {
      throw std::runtime_error("Cannot post to a stopped task queue");
    }

    // Count before publishing: the worker may dequeue as soon as enqueue returns.
    ++pending_tasks_;
    if (!queue_->tasks.enqueue(queue_->producer, std::move(item))) {
      --pending_tasks_;
      throw std::bad_alloc();
    }
  }
  wake_.notify_one();
  return result;
}

void TaskQueueLockFree::Stop() {
  // Concurrent Stop callers all wait for the same worker shutdown to finish.
  std::call_once(stop_once_, [this] {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    worker_.join();
  });
}

void TaskQueueLockFree::WorkerThread() {
  while (true) {
    TaskItem item;
    while (queue_->tasks.try_dequeue(item)) {
      --pending_tasks_;

      std::exception_ptr error;
      try {
        item.run();
      } catch (...) {
        error = std::current_exception();
      }

      // Release captured resources before marking the task complete.
      item.run = nullptr;
      if (error) {
        item.completion.set_exception(error);
      } else {
        item.completion.set_value();
      }
    }

    std::unique_lock lock(mutex_);
    wake_.wait(lock, [this] { return stopping_ || pending_tasks_ > 0; });
    // A stop request must not discard tasks submitted while the worker was idle.
    if (stopping_ && pending_tasks_ == 0) {
      return;
    }
  }
}

}  // namespace crossdesk

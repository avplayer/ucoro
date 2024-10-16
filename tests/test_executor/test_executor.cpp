#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <functional>
#include <vector>
#include <atomic>
#include <condition_variable>

#include "ucoro/awaitable.hpp"

class executor_service
{
public:
    using Task = std::function<void()>;

    executor_service()
        : abort_(false)
    {
    }

    void enqueue(Task task)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        task_queue_.push(task);
        condition_.notify_one();
    }

    void worker_thread()
    {
        while (!abort_)
        {
            Task task;

            {
                std::unique_lock<std::mutex> lock(mutex_);

                condition_.wait(lock, [this]()
                    {
                        return abort_ || !task_queue_.empty();
                    });

                if (abort_ && task_queue_.empty())
                    return;

                task = std::move(task_queue_.front());
                task_queue_.pop();
            }

            task();
        }
    }

    void start_workers(size_t thread_count)
    {
        for (size_t i = 0; i < thread_count; ++i)
            workers_.emplace_back(&executor_service::worker_thread, this);
    }

    void stop()
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            abort_ = true;
        }

        condition_.notify_all();
    }

    void join_all()
    {
        for (auto& worker : workers_)
        {
            if (worker.joinable())
                worker.join();
        }
    }

private:
    std::vector<std::thread> workers_;
    std::condition_variable condition_;
    std::mutex mutex_;
    std::queue<Task> task_queue_;
    std::atomic<bool> abort_;
};


ucoro::awaitable<int> coro_compute_int(int value)
{
    executor_service* executor = co_await ucoro::local_storage_t<executor_service*>();

    auto ret = co_await callback_awaitable<int>([executor, value](auto handle)
        {
            executor->enqueue([value, handle = std::move(handle)]() mutable
                {
                    std::cout << value << " value\n";
                    handle(value * 100);
                });
        });

    co_return (value + ret);
}

ucoro::awaitable<void> coro_compute()
{
    for (auto i = 0; i < 100; i++)
    {
        co_await coro_compute_int(i);
    }

    executor_service* executor = co_await ucoro::local_storage_t<executor_service*>();
    executor->stop();
}

int main(int argc, char** argv)
{
    executor_service srv;
    srv.start_workers(4);

    coro_start(coro_compute(), &srv);

    srv.join_all();

    return 0;
}

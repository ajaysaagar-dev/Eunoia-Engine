#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <memory>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#include <malloc.h>
#endif

// ============================================================================
// EngineCore::JobSystem — Wicked Engine-style Cooperative Task & Job System
// ============================================================================

namespace EngineCore {

struct JobArgs {
    uint32_t jobIndex{0};          // job index relative to dispatch (0..jobCount-1)
    uint32_t groupID{0};           // group index relative to dispatch (0..groupCount-1)
    uint32_t groupIndex{0};        // job index inside group (0..groupSize-1)
    bool isFirstJobInGroup{false}; // is this the first job in the group?
    bool isLastJobInGroup{false};  // is this the last job in the group?
    void* sharedmemory{nullptr};   // optional group-shared memory
};

struct JobContext {
    std::atomic<uint32_t> counter{0};
};

class JobSystem {
public:
    static JobSystem& Get() {
        static JobSystem instance;
        return instance;
    }

    explicit JobSystem(size_t numThreads = 0) {
        if (numThreads == 0) {
            numThreads = std::max(1u, std::thread::hardware_concurrency());
            if (numThreads > 1) numThreads -= 1; // Leave 1 core for main/render thread
        }
        m_workers.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        m_condition.wait(lock, [this]() {
                            return m_stop.load() || !m_tasks.empty();
                        });
                        if (m_stop.load() && m_tasks.empty()) {
                            return;
                        }
                        task = std::move(m_tasks.front());
                        m_tasks.pop();
                        m_activeJobs++;
                    }

                    try {
                        task();
                    } catch (...) {
                        // Suppress job exceptions from terminating worker threads
                    }

                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        m_activeJobs--;
                    }
                    m_waitCondition.notify_all();
                }
            });
        }
    }

    ~JobSystem() {
        Stop();
    }

    // Wicked Engine cooperative dispatch:
    // jobCount: total iterations
    // groupSize: how many iterations are processed serially per task unit
    // task: function receiving JobArgs
    void Dispatch(JobContext& ctx, uint32_t jobCount, uint32_t groupSize, const std::function<void(JobArgs)>& task, size_t sharedmemory_size = 0) {
        if (jobCount == 0 || groupSize == 0) return;

        uint32_t groupCount = (jobCount + groupSize - 1) / groupSize;
        ctx.counter.fetch_add(groupCount, std::memory_order_relaxed);

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            if (m_stop.load()) return;

            for (uint32_t groupID = 0; groupID < groupCount; ++groupID) {
                uint32_t groupJobOffset = groupID * groupSize;
                uint32_t groupJobEnd = std::min(groupJobOffset + groupSize, jobCount);

                m_tasks.emplace([&ctx, task, groupID, groupJobOffset, groupJobEnd, sharedmemory_size]() {
                    JobArgs args;
                    args.groupID = groupID;
                    std::vector<uint8_t> sharedMemBuffer;
                    if (sharedmemory_size > 0) {
                        sharedMemBuffer.resize(sharedmemory_size);
                        args.sharedmemory = sharedMemBuffer.data();
                    } else {
                        args.sharedmemory = nullptr;
                    }

                    for (uint32_t j = groupJobOffset; j < groupJobEnd; ++j) {
                        args.jobIndex = j;
                        args.groupIndex = j - groupJobOffset;
                        args.isFirstJobInGroup = (j == groupJobOffset);
                        args.isLastJobInGroup = (j == groupJobEnd - 1);
                        task(args);
                    }

                    ctx.counter.fetch_sub(1, std::memory_order_release);
                });
            }
        }

        m_condition.notify_all();
    }

    void Execute(JobContext& ctx, const std::function<void(JobArgs)>& task) {
        Dispatch(ctx, 1, 1, task);
    }

    bool IsBusy(const JobContext& ctx) const {
        return ctx.counter.load(std::memory_order_acquire) > 0;
    }

    // Cooperative wait: current thread actively helps finish queued tasks instead of idling!
    void Wait(const JobContext& ctx) {
        while (IsBusy(ctx)) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (!m_tasks.empty()) {
                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                    m_activeJobs++;
                }
            }

            if (task) {
                try {
                    task();
                } catch (...) {}

                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    m_activeJobs--;
                }
                m_waitCondition.notify_all();
            } else {
                // If queue is empty but counter > 0, other threads are still finishing tasks
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (IsBusy(ctx)) {
                    m_waitCondition.wait_for(lock, std::chrono::microseconds(100), [&ctx, this]() {
                        return !IsBusy(ctx) || !m_tasks.empty();
                    });
                }
            }
        }
    }

    // Submit a task with return value via std::future
    template<typename F, typename... Args>
    auto Enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            if (m_stop.load()) {
                throw std::runtime_error("JobSystem is stopped");
            }
            m_tasks.emplace([task]() { (*task)(); });
        }
        m_condition.notify_one();
        return res;
    }

    // Fire-and-forget submission
    void Execute(std::function<void()> job) {
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            if (m_stop.load()) return;
            m_tasks.push(std::move(job));
        }
        m_condition.notify_one();
    }

    // Wait until all queued jobs finish (with cooperative waiting)
    void WaitAll() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (!m_tasks.empty()) {
                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                    m_activeJobs++;
                } else if (m_activeJobs == 0) {
                    break;
                }
            }

            if (task) {
                try {
                    task();
                } catch (...) {}

                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    m_activeJobs--;
                }
                m_waitCondition.notify_all();
            } else {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (m_tasks.empty() && m_activeJobs == 0) break;
                m_waitCondition.wait_for(lock, std::chrono::microseconds(100), [this]() {
                    return (!m_tasks.empty()) || (m_tasks.empty() && m_activeJobs == 0);
                });
            }
        }
    }

    void Stop() {
        bool expected = false;
        if (!m_stop.compare_exchange_strong(expected, true)) return;
        m_condition.notify_all();
        m_waitCondition.notify_all();
        for (std::thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
    }

    size_t WorkerCount() const { return m_workers.size(); }

private:
    std::vector<std::thread>          m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex                        m_queueMutex;
    std::condition_variable           m_condition;
    std::condition_variable           m_waitCondition;
    std::atomic<bool>                 m_stop{false};
    size_t                            m_activeJobs{0};
};

} // namespace EngineCore

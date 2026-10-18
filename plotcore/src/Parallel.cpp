// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Parallel.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace plotcore {
namespace {

/// One call of parallelFor(): its pieces, handed out by a counter, and how
/// many of them have come back.
struct Job
{
    const std::function<void(long long)>* piece = nullptr;
    long long pieces = 0;
    std::atomic<long long> next{0};
    std::atomic<long long> finished{0};
    std::mutex mutex;
    std::condition_variable done;
};

/// Take pieces of `job` until there are none left to take.
void take(Job& job)
{
    for (;;) {
        const long long i = job.next.fetch_add(1);
        if (i >= job.pieces) {
            return;
        }
        (*job.piece)(i);
        if (job.finished.fetch_add(1) + 1 == job.pieces) {
            // Under the job's lock, so the caller cannot test the count, find
            // it short, and then miss this on its way into the wait.
            const std::lock_guard<std::mutex> held(job.mutex);
            job.done.notify_all();
        }
    }
}

class Pool
{
public:
    /// Never destroyed. The workers sleep in a wait until the process ends;
    /// joining them from a static destructor races whatever else is being torn
    /// down, and a pool of idle threads costs nothing to leave behind.
    static Pool& instance()
    {
        static Pool* pool = new Pool;
        return *pool;
    }

    [[nodiscard]] int threads() const { return threads_; }

    void run(long long pieces, const std::function<void(long long)>& piece)
    {
        if (pieces <= 0) {
            return;
        }
        if (pieces == 1 || threads_ <= 1) {
            for (long long i = 0; i < pieces; ++i) {
                piece(i);
            }
            return;
        }
        const auto job = std::make_shared<Job>();
        job->piece = &piece;
        job->pieces = pieces;
        {
            const std::lock_guard<std::mutex> held(mutex_);
            queue_.push_back(job);
        }
        wake_.notify_all();

        // The caller is a thread of the pool for as long as it waits, which is
        // what makes a fold inside a fold safe: the pieces get done whether or
        // not any worker is free to take them.
        take(*job);
        {
            std::unique_lock<std::mutex> held(job->mutex);
            job->done.wait(held, [&] { return job->finished.load() == job->pieces; });
        }
        const std::lock_guard<std::mutex> held(mutex_);
        const auto at = std::find(queue_.begin(), queue_.end(), job);
        if (at != queue_.end()) {
            queue_.erase(at);
        }
    }

private:
    Pool() : threads_(std::max(1, static_cast<int>(std::thread::hardware_concurrency())))
    {
        for (int i = 1; i < threads_; ++i) {
            std::thread([this] { work(); }).detach();
        }
    }

    void work()
    {
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> held(mutex_);
                wake_.wait(held, [&] { return !queue_.empty(); });
                job = queue_.front();
                if (job->next.load() >= job->pieces) {
                    // Every piece is taken; the ones still running belong to
                    // whoever took them, and nothing here waits on those.
                    queue_.pop_front();
                    continue;
                }
            }
            take(*job);
        }
    }

    int threads_ = 1;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<Job>> queue_;
};

} // namespace

int poolThreads()
{
    return Pool::instance().threads();
}

void parallelFor(long long pieces, const std::function<void(long long)>& piece)
{
    Pool::instance().run(pieces, piece);
}

} // namespace plotcore

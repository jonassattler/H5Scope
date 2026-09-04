// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "H5Session.hpp"

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <cstdint>
#include <functional>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace gui {

/// A caller's outstanding requests, and the means of disowning them.
///
/// A reply comes back one or more event-loop turns after the request, by which
/// time the thing that asked may be gone or may no longer care -- the reader
/// has clicked another dataset, the file has been closed, the model has been
/// destroyed. Both are the normal case rather than the exceptional one, so
/// neither may be a crash and neither may be a stale answer painted over a
/// fresh one.
///
/// Every requester owns one of these. `reset()` disowns everything asked so
/// far, and the destructor does the same permanently. A reply whose ticket no
/// longer matches is dropped without its continuation being called at all, so
/// a continuation may capture `this` freely: it does not run unless the thing
/// it captured is still there and still asking the same question.
///
/// The check and the continuation both happen on the requester's own thread,
/// which is what makes this safe without a lock: nothing on the HDF5 thread
/// ever looks at a requester.
class H5Requests
{
public:
    H5Requests() = default;
    ~H5Requests();

    H5Requests(const H5Requests&) = delete;
    H5Requests& operator=(const H5Requests&) = delete;
    H5Requests(H5Requests&&) = delete;
    H5Requests& operator=(H5Requests&&) = delete;

    /// Disown every request made so far. Their replies, when they arrive, are
    /// dropped. Call it when the answer being waited on has stopped being the
    /// answer that is wanted.
    void reset();

private:
    friend class H5Thread;

    /// What a reply carries back so it can tell whether it is still wanted.
    /// Held by shared_ptr so it outlives the requester: a reply in flight must
    /// be able to ask the question even when the answer is "no".
    struct Ticket {
        std::atomic<bool> alive{true};
        std::atomic<std::uint64_t> generation{0};
    };

    std::shared_ptr<Ticket> ticket_ = std::make_shared<Ticket>();
};

/// The one thread this application makes HDF5 calls on.
///
/// HDF5 is not thread-safe in the pinned build and cannot be made concurrent by
/// one that is -- see `h5core::thread`, which is where the rule is enforced.
/// This is where the rule is *kept*: a single QThread, claimed once, with a job
/// queue in front of it. Nothing else in the application opens a file, reads a
/// window or asks a type what it is.
///
/// Two ways to use it, and the difference is the whole point:
///
///   submit()  the request returns immediately and the continuation runs later,
///             on the caller's thread. The UI stays live while the filesystem
///             answers. This is what everything on the drawing path uses.
///   invoke()  blocks the caller until the job has run. Correct, and never
///             what the GUI thread should be doing; it is here for tests, for
///             start-up before there is a window, and for shutdown.
///
/// The queue is strictly ordered. Two jobs submitted in order run in that
/// order, which is what lets a caller submit "open this file" and then "list
/// its root" without waiting in between.
class H5Thread : public QObject
{
    Q_OBJECT

public:
    /// The one instance. Created on first use, on whatever thread first asks --
    /// which in this application is the GUI thread, before any window exists.
    static H5Thread& instance();

    /// Stop the thread and wait for it. Idempotent. Called from main() on the
    /// way out, so the worker is gone before static destruction starts.
    static void shutdown();

    ~H5Thread() override;

    H5Thread(const H5Thread&) = delete;
    H5Thread& operator=(const H5Thread&) = delete;
    H5Thread(H5Thread&&) = delete;
    H5Thread& operator=(H5Thread&&) = delete;

    /// Run `job` on the HDF5 thread; run `then` with its result afterwards, on
    /// the thread that called this, unless `requests` has since been reset or
    /// destroyed.
    ///
    /// `job` is handed the session -- the open file, the dataset being drawn --
    /// because that is the only way to reach any of it. It must return plain
    /// data: never an open identifier, never a pointer into anything the
    /// session owns. That one rule is what makes the result safe to read on the
    /// other side, and it is why h5core answers in structs rather than handles.
    template<typename Job, typename Then>
    void submit(H5Requests& requests, Job job, Then then)
    {
        auto ticket = requests.ticket_;
        const std::uint64_t generation = ticket->generation.load();
        using Result = decltype(job(session_));
        began();
        postJob([this, ticket = std::move(ticket), generation, job = std::move(job),
                 then = std::move(then)]() mutable {
            // On the HDF5 thread. Nothing here reads the requester.
            //
            // The catch is not defensive tidiness. An exception let out of here
            // would unwind into Qt's event loop on this thread, where there is
            // nothing above it to catch anything, and std::terminate is what
            // happens next -- so h5core's ordinary way of reporting that a
            // dataset will not read would take the process down.
            std::shared_ptr<Result> result;
            QString failure;
            try {
                result = std::make_shared<Result>(job(session_));
            } catch (const std::exception& error) {
                failure = QString::fromUtf8(error.what());
            } catch (...) {
                failure = QStringLiteral("unknown failure on the HDF5 thread");
            }
            reply([this, ticket = std::move(ticket), generation,
                   result = std::move(result), then = std::move(then),
                   failure]() mutable {
                if (!ticket->alive.load()
                    || ticket->generation.load() != generation) {
                    return;
                }
                if (result == nullptr) {
                    emit jobFailed(failure);
                    return;
                }
                then(std::move(*result));
            });
        });
    }

    /// The same, for a job with nothing to hand back.
    template<typename Job, typename Then>
    void submitVoid(H5Requests& requests, Job job, Then then)
    {
        submit(
            requests,
            [job = std::move(job)](H5Session& session) mutable {
                job(session);
                return std::monostate{};
            },
            [then = std::move(then)](std::monostate) mutable { then(); });
    }

    /// Run `job` on the HDF5 thread and wait for it. Returns what it returned.
    ///
    /// Blocking, deliberately and visibly. Every use of this on the GUI thread
    /// is a stall the reader can feel, so there should be none; the compiler
    /// cannot enforce that, but the name can at least make it obvious in a
    /// diff.
    template<typename Job>
    auto invoke(Job job) -> decltype(job(std::declval<H5Session&>()))
    {
        using Result = decltype(job(std::declval<H5Session&>()));
        if (QThread::currentThread() == worker_) {
            return job(session_); // already there: a job may call this on itself
        }
        // Caught there and rethrown here, so a caller that waits sees the same
        // exception it would have seen when this ran on its own thread.
        std::optional<Result> slot;
        std::exception_ptr failure;
        invokeBlocking([&] {
            try {
                slot.emplace(job(session_));
            } catch (...) {
                failure = std::current_exception();
            }
        });
        if (failure) {
            std::rethrow_exception(failure);
        }
        return std::move(*slot);
    }

    /// How many jobs have been submitted and not yet had their reply delivered.
    /// Zero means the HDF5 thread has nothing to do and the UI is showing
    /// everything it asked for.
    [[nodiscard]] int outstanding() const;
    [[nodiscard]] bool busy() const { return outstanding() > 0; }

    /// Run this thread's event loop until nothing is outstanding, or the
    /// deadline passes. Returns whether it went quiet.
    ///
    /// For tests and for shutdown. A UI must never call it: spinning the event
    /// loop inside a call that came *from* the event loop is how a repaint ends
    /// up nested inside a model update.
    bool drain(int timeoutMilliseconds = 30000);

signals:
    /// Emitted when outstanding() moves between zero and non-zero, so the
    /// window can show that the file is being read without polling.
    void busyChanged();
    /// A submitted job threw. Delivered on the requester's thread, in place of
    /// its continuation, and only while the requester is still asking -- so a
    /// failure is reported exactly where the answer would have gone.
    void jobFailed(const QString& message);

private:
    H5Thread();

    void postJob(std::function<void()> job);
    void invokeBlocking(const std::function<void()>& job);
    /// Called on the HDF5 thread to hand a continuation back to the requester's
    /// thread. Posted to this object, which outlives every requester, so there
    /// is never a dead target.
    void reply(std::function<void()> continuation);
    void began();
    void finished();

    QThread* worker_ = nullptr;
    QObject* runner_ = nullptr; ///< lives on `worker_`; the queue's receiver
    /// Touched only from inside a job, which is to say only on `worker_`.
    H5Session session_;
    std::atomic<int> outstanding_{0};
};

} // namespace gui

// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "H5Thread.hpp"

#include "h5core/Thread.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QMetaObject>
#include <QSemaphore>

namespace gui {
namespace {

/// The instance, and whether it has been shut down.
///
/// A raw pointer rather than a function-local static: the thread has to be
/// stopped and joined *before* static destruction runs, or the worker is still
/// executing while the objects it uses are being torn down. main() calls
/// shutdown(), and this is what lets a second call to instance() after that
/// build a fresh one rather than hand back a corpse.
H5Thread* the = nullptr;

} // namespace

H5Requests::~H5Requests()
{
    ticket_->alive.store(false);
}

void H5Requests::reset()
{
    ticket_->generation.fetch_add(1);
}

H5Thread::H5Thread()
{
    worker_ = new QThread;
    worker_->setObjectName(QStringLiteral("hdf5"));

    // The runner is what jobs are posted to. It lives on the worker, so a
    // queued invocation on it runs there and nowhere else.
    runner_ = new QObject;
    runner_->moveToThread(worker_);

    worker_->start();

    // Claim on the worker itself, and wait for it: every h5core call after
    // this point asserts that it is on the claiming thread, so the claim has
    // to be in place before anything can be submitted.
    QSemaphore claimed;
    QMetaObject::invokeMethod(
        runner_,
        [&claimed] {
            h5core::thread::claim();
            claimed.release();
        },
        Qt::QueuedConnection);
    claimed.acquire();
}

H5Thread::~H5Thread()
{
    if (worker_ == nullptr) {
        return;
    }
    // Close the file and release the claim on the worker, then stop it.
    //
    // Closing there is not tidiness. `session_` is a member of this object and
    // this object is destroyed on the GUI thread, so an open file left in it
    // would call H5Fclose from the wrong thread -- which is precisely the
    // violation the guard aborts on, reached on the way out of a program that
    // did everything else right. Emptying it here leaves nothing for the
    // member's own destructor to do.
    QMetaObject::invokeMethod(
        runner_,
        [this] {
            session_.close();
            h5core::thread::release();
        },
        Qt::BlockingQueuedConnection);
    runner_->deleteLater();
    worker_->quit();
    worker_->wait();
    delete worker_;
    worker_ = nullptr;
    runner_ = nullptr;
}

H5Thread& H5Thread::instance()
{
    if (the == nullptr) {
        the = new H5Thread;
    }
    return *the;
}

void H5Thread::shutdown()
{
    delete the;
    the = nullptr;
}

void H5Thread::postJob(std::function<void()> job)
{
    QMetaObject::invokeMethod(runner_, std::move(job), Qt::QueuedConnection);
}

void H5Thread::invokeBlocking(const std::function<void()>& job)
{
    QMetaObject::invokeMethod(runner_, job, Qt::BlockingQueuedConnection);
}

void H5Thread::reply(std::function<void()> continuation)
{
    // Posted to this object rather than to whoever asked. This outlives every
    // requester in the application, so the target is never dangling; whether
    // the *answer* is still wanted is the ticket's question, asked inside the
    // continuation once it is safely back on the requester's thread.
    QMetaObject::invokeMethod(
        this,
        [this, continuation = std::move(continuation)]() mutable {
            continuation();
            finished();
        },
        Qt::QueuedConnection);
}

void H5Thread::began()
{
    if (outstanding_.fetch_add(1) == 0) {
        emit busyChanged();
    }
}

void H5Thread::finished()
{
    if (outstanding_.fetch_sub(1) == 1) {
        emit busyChanged();
    }
}

int H5Thread::outstanding() const
{
    return outstanding_.load();
}

bool H5Thread::drain(int timeoutMilliseconds)
{
    QDeadlineTimer deadline(timeoutMilliseconds);
    for (;;) {
        // Events first, always. Work is not always submitted the moment it is
        // asked for -- the tree batches a frame's worth of row requests behind
        // a zero-delay timer -- so a queue that looks empty may simply not have
        // been filled yet, and a drain that checked the counter first would
        // return before the batch had even been sent.
        QCoreApplication::processEvents();
        if (outstanding_.load() == 0) {
            // A reply can queue more work: a listing arrives, rows appear,
            // and the rows want describing. Settle only when a pass over the
            // queue leaves nothing behind it.
            QCoreApplication::processEvents();
            if (outstanding_.load() == 0) {
                return true;
            }
        }
        if (deadline.hasExpired()) {
            return false;
        }
        // WaitForMoreEvents so this does not spin a core while the filesystem
        // answers; the 10 ms cap keeps the deadline honest when the queue goes
        // quiet without an event to wake us.
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 10);
    }
}

} // namespace gui

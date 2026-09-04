// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The rule that HDF5 is touched from one thread only, and the machinery that
// keeps it.
//
// This suite is about the machinery rather than about any file: that jobs run
// where they are supposed to, in the order they were given, and that a reply to
// a question nobody is asking any more is dropped rather than delivered. Those
// three are what every asynchronous model in the application is built on, and
// each of them is a crash or a stale readout when it is wrong.

#include "gui/H5Thread.hpp"
#include "support/TestFile.hpp"

#include "h5core/File.hpp"
#include "h5core/Thread.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QThread>

#if defined(__unix__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

/// The fixture file, written once for the suite.
struct Fixture {
    h5test::TempFile temp{"thread"};
    Fixture() { h5test::writeFixture(temp.path()); }
};

} // namespace

TEST_CASE("HDF5 runs on one thread and it is not this one", "[thread]")
{
    auto& h5 = gui::H5Thread::instance();

    SECTION("a job runs somewhere else")
    {
        const auto here = std::this_thread::get_id();
        const auto there = h5.invoke([](gui::H5Session&) {
            return std::this_thread::get_id();
        });
        CHECK(there != here);
    }

    SECTION("and every job runs on the same somewhere else")
    {
        // The point of the whole arrangement. A pool that happened to keep one
        // job in flight at a time would pass every other test here and still
        // be wrong: HDF5's global state belongs to a thread, not to a queue.
        std::vector<std::thread::id> seen;
        for (int i = 0; i < 32; ++i) {
            seen.push_back(h5.invoke(
                [](gui::H5Session&) { return std::this_thread::get_id(); }));
        }
        for (const auto& id : seen) {
            CHECK(id == seen.front());
        }
    }

    SECTION("and that thread is the one h5core has been told about")
    {
        CHECK(h5.invoke([](gui::H5Session&) { return h5core::thread::owned(); }));
        // ...which this one is not, now that something has claimed.
        CHECK(h5core::thread::claimed());
        CHECK_FALSE(h5core::thread::owned());
    }
}

#if defined(__unix__)
TEST_CASE("calling HDF5 from the wrong thread stops the process", "[thread][guard]")
{
    // The invariant, demonstrated rather than described.
    //
    // In a forked child, because the whole point of the guard is that it does
    // not return: HDF5's global state is one call away from being corrupted by
    // the time it fires, and an exception could be caught by a caller that then
    // carried on. What is asserted here is that a violation is loud and
    // immediate, which is the difference between a bug found by a test and a
    // bug found as a segfault in somebody's afternoon.
    //
    // Nothing in the application should ever reach this. That is what makes it
    // worth having a test for: an invariant nobody can see holding is one
    // nobody notices losing.
    auto& h5 = gui::H5Thread::instance();
    h5.invoke([](gui::H5Session&) { return 0; }); // make sure it has claimed
    REQUIRE(h5core::thread::claimed());
    REQUIRE_FALSE(h5core::thread::owned());

    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        // Catch2 installs a handler for the fatal signals so it can report
        // them; here that would turn a successful demonstration into a failed
        // test in a child nobody is reading. Put SIGABRT back to the default
        // so the abort is just an abort.
        ::signal(SIGABRT, SIG_DFL);
        // The child inherits the claim but not the thread that made it, which
        // is exactly the shape of the mistake.
        ::alarm(10); // never hang the suite if the guard is gone
        (void)h5core::File::isHDF5("/etc/hostname");
        ::_exit(0); // reached only if the guard did not fire
    }

    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    INFO("child exited " << (WIFEXITED(status) ? WEXITSTATUS(status) : -1)
                         << ", signal "
                         << (WIFSIGNALED(status) ? WTERMSIG(status) : 0));
    REQUIRE(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
}
#endif

TEST_CASE("submitted work comes back in order, and on the caller's thread",
          "[thread]")
{
    auto& h5 = gui::H5Thread::instance();
    gui::H5Requests requests;

    std::vector<int> order;
    std::vector<std::thread::id> repliedOn;
    for (int i = 0; i < 16; ++i) {
        h5.submit(
            requests, [i](gui::H5Session&) { return i; },
            [&order, &repliedOn](int value) {
                order.push_back(value);
                repliedOn.push_back(std::this_thread::get_id());
            });
    }

    REQUIRE(h5.drain());
    REQUIRE(order.size() == 16);
    for (int i = 0; i < 16; ++i) {
        CHECK(order[static_cast<std::size_t>(i)] == i);
    }
    for (const auto& id : repliedOn) {
        CHECK(id == std::this_thread::get_id());
    }
    CHECK(h5.outstanding() == 0);
}

TEST_CASE("a reply nobody is waiting for is dropped", "[thread]")
{
    auto& h5 = gui::H5Thread::instance();

    SECTION("because the question changed")
    {
        gui::H5Requests requests;
        bool delivered = false;
        h5.submit(
            requests, [](gui::H5Session&) { return 1; },
            [&delivered](int) { delivered = true; });
        // The reader clicked something else before the filesystem answered.
        requests.reset();
        REQUIRE(h5.drain());
        CHECK_FALSE(delivered);

        // ...and the next question is answered normally.
        bool second = false;
        h5.submit(
            requests, [](gui::H5Session&) { return 2; },
            [&second](int) { second = true; });
        REQUIRE(h5.drain());
        CHECK(second);
    }

    SECTION("because the asker is gone")
    {
        // The continuation captures a pointer to something that no longer
        // exists. It must not run at all -- checking inside it would be too
        // late.
        auto owner = std::make_unique<int>(7);
        auto requests = std::make_unique<gui::H5Requests>();
        bool delivered = false;
        h5.submit(
            *requests, [](gui::H5Session&) { return 0; },
            [raw = owner.get(), &delivered](int) {
                delivered = true;
                CHECK(*raw == 7); // never reached
            });
        requests.reset();
        owner.reset();
        REQUIRE(h5.drain());
        CHECK_FALSE(delivered);
    }
}

TEST_CASE_METHOD(Fixture, "the session owns the file and nothing else can reach it",
                 "[thread][session]")
{
    auto& h5 = gui::H5Thread::instance();

    const bool opened = h5.invoke([&](gui::H5Session& session) {
        session.open(temp.path());
        return session.isOpen();
    });
    REQUIRE(opened);

    SECTION("and answers about it, in plain data")
    {
        gui::H5Requests requests;
        std::vector<std::string> names;
        h5.submit(
            requests,
            [](gui::H5Session& session) {
                std::vector<std::string> found;
                for (const auto& child :
                     session.file()->children("/", h5core::File::Resolve::Links)) {
                    found.push_back(child.name);
                }
                return found;
            },
            [&names](std::vector<std::string> found) { names = std::move(found); });

        REQUIRE(h5.drain());
        CHECK(names.size() > 5);
        CHECK(std::find(names.begin(), names.end(), "matrix") != names.end());
    }

    SECTION("a dataset is opened once and kept")
    {
        const bool same = h5.invoke([](gui::H5Session& session) {
            const auto* first = session.dataset("/matrix");
            const auto* second = session.dataset("/matrix");
            return first != nullptr && first == second;
        });
        CHECK(same);

        // A different path replaces it rather than accumulating handles.
        const bool moved = h5.invoke([](gui::H5Session& session) {
            const auto* first = session.dataset("/matrix");
            const auto* other = session.dataset("/cube");
            return other != nullptr && other != first;
        });
        CHECK(moved);
    }

    SECTION("a path that is not a dataset answers with nothing, not a throw")
    {
        CHECK(h5.invoke([](gui::H5Session& session) {
            return session.dataset("/group") == nullptr;
        }));
    }

    h5.invoke([](gui::H5Session& session) {
        session.close();
        return 0;
    });
}

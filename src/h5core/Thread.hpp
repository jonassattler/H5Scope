// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace h5core {

/// Which thread this process makes its HDF5 calls on.
///
/// HDF5 is not thread-safe in this build -- `H5_HAVE_THREADSAFE` and
/// `H5_HAVE_CONCURRENCY` are both undefined in the pinned 2.2.0 -- and the
/// failure mode is not a deadlock or a wrong answer that a test would notice.
/// Two threads walking disjoint subtrees of the same file segfault within a
/// second, with a shared file handle and with one handle each alike, because
/// what they share is the library's own global state: the identifier tables,
/// the free lists, the error stack. The run that survives returns half the
/// objects.
///
/// So the rule is that exactly one thread ever calls into HDF5, and this is
/// how the rule is *checked* rather than merely intended. The owning thread
/// claims it once; every public entry point in h5core asserts it is the
/// caller. A violation aborts immediately, with a message naming the function,
/// instead of corrupting memory that crashes somewhere else an hour later.
///
/// Nothing claims by default. The test suites and the command-line tools are
/// single-threaded and never call claim(), so the check costs them one
/// relaxed atomic load and never fires. The application claims on the thread
/// `gui::H5Thread` owns, before it opens anything.
namespace thread {

/// Make the calling thread the one HDF5 may be used from. Idempotent for that
/// thread; claiming from a second thread while another still holds it is
/// itself the bug this exists to catch, and aborts.
void claim();

/// Give up the claim, so a later thread may take it. Called when the owning
/// thread shuts down. Safe to call unclaimed.
void release();

/// Whether a thread has claimed at all. False in the tools and the tests.
[[nodiscard]] bool claimed() noexcept;

/// Whether the calling thread is the owner, or nothing has claimed yet.
[[nodiscard]] bool owned() noexcept;

/// Abort unless owned(). `what` names the function, and is printed.
///
/// Deliberately live in release as well as debug: CI builds release only, so a
/// check compiled out of it would be a check that never ran. The cost is one
/// relaxed load and a comparison against a call into a library that reads
/// disk.
void check(const char* what);

} // namespace thread
} // namespace h5core

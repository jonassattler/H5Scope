// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Handle.hpp"

#include <hdf5.h>

#include <stdexcept>
#include <string>
#include <type_traits>

namespace h5core {

/// Exception carrying a message plus the HDF5 error stack at throw time.
///
/// The two are kept apart deliberately. `what()` is the whole thing and
/// belongs in a log; `summary()` is the one sentence that says what went
/// wrong, and is the only half fit to put in front of a reader -- an HDF5
/// stack trace in a status strip tells them nothing they can act on.
class H5Error : public std::runtime_error
{
public:
    explicit H5Error(std::string summary)
        : std::runtime_error(summary), summary_(std::move(summary))
    {
    }
    H5Error(const std::string& summary, const std::string& detail)
        : std::runtime_error(summary + ":\n" + detail), summary_(summary)
    {
    }

    [[nodiscard]] const std::string& summary() const noexcept { return summary_; }

private:
    std::string summary_;
};

/// Silence HDF5's default handler, which otherwise dumps its error stack
/// straight to stderr on every failure. Idempotent; call before any other
/// HDF5 use. Failures are surfaced as H5Error instead.
void initErrorHandling();

/// Format the current HDF5 error stack into a human-readable string.
/// Returns an empty string when the stack is empty.
std::string currentErrorStack();

/// Throw H5Error with `context` and the current HDF5 error stack appended.
[[noreturn]] void throwError(const std::string& context);

/// Whether an HDF5 call failed, forgetting what it said about it if it did.
///
/// For the failures this code answers in-band -- a filter it cannot name, a
/// link that will not resolve, an attribute that will not read -- which are
/// most of them: a viewer reports what a file will not give it and goes on. The
/// stack is cleared so the failure is not left behind for the next thing that
/// asks for it to report as its own. It used to be the test and then
/// `H5Eclear2(H5E_DEFAULT)` on the next line, some fifty times.
template<typename Status>
    requires std::is_integral_v<Status>
[[nodiscard]] bool failed(Status status) noexcept
{
    if (status < 0) {
        H5Eclear2(H5E_DEFAULT);
        return true;
    }
    return false;
}

/// The same question of a handle: whether the call that made it failed.
[[nodiscard]] inline bool failed(const Handle& handle) noexcept
{
    if (!handle.valid()) {
        H5Eclear2(H5E_DEFAULT);
        return true;
    }
    return false;
}

/// Throw unless `status` indicates success (HDF5 signals failure with < 0).
inline void check(herr_t status, const std::string& context)
{
    if (status < 0) {
        throwError(context);
    }
}

/// Throw unless `id` is a valid identifier, otherwise return it.
inline hid_t checkId(hid_t id, const std::string& context)
{
    if (id < 0) {
        throwError(context);
    }
    return id;
}

} // namespace h5core

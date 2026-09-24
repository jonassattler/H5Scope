// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Error.hpp"

#include <string>

namespace h5core {
namespace {

/// H5Ewalk2 callback. Appends one frame of the stack to the text.
///
/// Noexcept, and made so by catching rather than by promising: the appends
/// allocate, and an exception unwinding through H5Ewalk2's C frames is
/// undefined behaviour. A stack that could not be written out in full is still
/// a stack -- what was written so far goes into the message, and the walk
/// stops there.
herr_t walkCallback(unsigned n, const H5E_error2_t* err, void* clientData) noexcept
{
    auto* out = static_cast<std::string*>(clientData);
    if (err == nullptr || out == nullptr) {
        return 0;
    }

    try {
        if (!out->empty()) {
            out->append("\n");
        }
        out->append("  #").append(std::to_string(n)).append(": ");
        if (err->desc != nullptr) {
            out->append(err->desc);
        }
        if (err->func_name != nullptr) {
            out->append(" (in ").append(err->func_name).append(")");
        }
    } catch (...) {
        return -1;
    }
    return 0;
}

} // namespace

void initErrorHandling()
{
    // Suppress the automatic stderr dump; we read the stack ourselves.
    H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
}

std::string currentErrorStack()
{
    std::string stack;
    H5Ewalk2(H5E_DEFAULT, H5E_WALK_DOWNWARD, &walkCallback, &stack);
    return stack;
}

void throwError(const std::string& context)
{
    const std::string stack = currentErrorStack();
    H5Eclear2(H5E_DEFAULT);
    if (stack.empty()) {
        throw H5Error(context);
    }
    throw H5Error(context, stack);
}

} // namespace h5core

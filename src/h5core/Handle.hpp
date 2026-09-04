// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <hdf5.h>

#include <utility>

namespace h5core {

/// Move-only RAII owner for an HDF5 identifier.
///
/// HDF5 hands out `hid_t` integers that must be closed with the *matching*
/// close function; nothing about the value itself says which. Leaking them is
/// silent and eventually exhausts the library's id table, so every
/// `H5*open*`/`H5*create*` result in this codebase is wrapped here rather than
/// closed by hand.
class Handle
{
public:
    using Closer = herr_t (*)(hid_t);

    Handle() noexcept = default;

    Handle(hid_t id, Closer closer) noexcept : id_(id), closer_(closer) {}

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept
        : id_(std::exchange(other.id_, H5I_INVALID_HID)),
          closer_(std::exchange(other.closer_, nullptr))
    {
    }

    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other) {
            reset();
            id_ = std::exchange(other.id_, H5I_INVALID_HID);
            closer_ = std::exchange(other.closer_, nullptr);
        }
        return *this;
    }

    ~Handle() { reset(); }

    void reset() noexcept
    {
        if (valid() && closer_ != nullptr) {
            closer_(id_);
        }
        id_ = H5I_INVALID_HID;
        closer_ = nullptr;
    }

    [[nodiscard]] bool valid() const noexcept { return id_ >= 0; }
    [[nodiscard]] hid_t get() const noexcept { return id_; }
    explicit operator bool() const noexcept { return valid(); }

    /// Relinquish ownership without closing.
    [[nodiscard]] hid_t release() noexcept
    {
        closer_ = nullptr;
        return std::exchange(id_, H5I_INVALID_HID);
    }

private:
    hid_t id_ = H5I_INVALID_HID;
    Closer closer_ = nullptr;
};

} // namespace h5core

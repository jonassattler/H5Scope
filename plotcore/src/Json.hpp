// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Just enough JSON to write a view down and read it back.
//
// A saved view is a handful of settings and numbers, and a library that is
// handed to Python and Rust should not bring a JSON dependency with it to say
// them. Private to plotcore: what crosses the interface is the string.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plotcore::json {

struct Value
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;

    [[nodiscard]] static Value of(bool value);
    [[nodiscard]] static Value of(double value);
    [[nodiscard]] static Value of(std::string value);
    [[nodiscard]] static Value array();
    [[nodiscard]] static Value object();

    /// Add a member to an object; returns this, for chaining.
    Value& set(std::string key, Value value);
    /// The member called `key`, or nothing.
    [[nodiscard]] const Value* find(std::string_view key) const;

    /// What a member reads as, or `fallback` when it is missing or another
    /// kind -- the stance a saved view takes on a setting it cannot read: that
    /// setting is lost, not the view.
    [[nodiscard]] double numberOr(std::string_view key, double fallback) const;
    [[nodiscard]] bool boolOr(std::string_view key, bool fallback) const;
    [[nodiscard]] std::string textOr(std::string_view key, std::string fallback) const;
};

/// One line of JSON. A number that is not finite is written as null.
[[nodiscard]] std::string write(const Value& value);
/// The value `text` holds, or nothing when it is not JSON.
[[nodiscard]] std::optional<Value> parse(std::string_view text);

} // namespace plotcore::json

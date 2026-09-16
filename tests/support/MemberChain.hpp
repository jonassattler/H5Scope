// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/FieldDataset.hpp"
#include "h5core/Types.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace h5test {

/// A member chain built from names, for tests about the *read*.
///
/// The grammar that reads one of these out of text lives in postproc, and it is
/// tested there. Building one by hand here is what keeps a case about what
/// `/nested.samples` reads from also being a case about how ".samples" parses:
/// when one of them breaks, exactly one suite should go red.
inline h5core::MemberSelection chainOf(const h5core::TypeInfo& type,
                                       const std::vector<std::string>& names,
                                       std::optional<hsize_t> vlenIndex = std::nullopt)
{
    h5core::MemberSelection selection;

    // An array member's dimensions are axes of the result, and what it holds
    // is what a member is then looked up in -- the same unwrapping the reader
    // does. It has to happen *between* the links as well as after the last of
    // them: `.trail.x` goes through an array of structs, and a version that
    // only unwrapped at the end could not find `x` at all.
    const auto unwrap = [&selection](const h5core::TypeInfo* from) {
        while (from->cls == h5core::TypeClass::Array && from->base != nullptr) {
            selection.dims.insert(selection.dims.end(), from->arrayDims.begin(),
                                  from->arrayDims.end());
            from = from->base.get();
        }
        return from;
    };

    // A dataset whose own type is an array of compounds: those dimensions are
    // the result's too.
    const h5core::TypeInfo* level = unwrap(&type);
    for (const std::string& name : names) {
        const auto it = std::find_if(
            level->members.begin(), level->members.end(),
            [&name](const h5core::TypeMember& m) { return m.name == name; });
        if (it == level->members.end()) {
            return {}; // the caller's REQUIRE says it better than a throw here
        }
        selection.links.push_back(h5core::MemberLink{
            static_cast<unsigned>(it - level->members.begin()), name, std::nullopt});
        selection.text += "." + name;
        level = unwrap(&it->type);
    }
    selection.type = *level;

    // An index into a vlen is the one subscript that stays on the chain, so it
    // is set after the unwrapping rather than folded into a dimension.
    if (vlenIndex.has_value() && !selection.links.empty()) {
        selection.links.back().vlenIndex = vlenIndex;
        selection.text += "[" + std::to_string(*vlenIndex) + "]";
        if (selection.type.cls == h5core::TypeClass::VarLen
            && selection.type.base != nullptr) {
            selection.type = *selection.type.base;
        }
    }
    return selection;
}

} // namespace h5test

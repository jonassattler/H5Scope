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
    const h5core::TypeInfo* level = &type;
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
        level = &it->type;
    }

    // An array member's dimensions are axes of the result, and what it holds is
    // what one element of the result is -- the same unwrapping the reader does.
    selection.type = *level;
    while (selection.type.cls == h5core::TypeClass::Array
           && selection.type.base != nullptr) {
        selection.dims.insert(selection.dims.end(), selection.type.arrayDims.begin(),
                              selection.type.arrayDims.end());
        selection.type = *selection.type.base;
    }

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

// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Dataset.hpp"
#include "Types.hpp"

#include <hdf5.h>

#include <optional>
#include <string>
#include <vector>

namespace h5core {

/// One link of a resolved member chain: which member, at this level.
struct MemberLink {
    unsigned index = 0; ///< member index within the compound at this level
    std::string name;   ///< its name, which is what a partial memory type needs

    /// For a vlen member, which element of each record's list to take.
    ///
    /// The one subscript that cannot be an ordinary slice, and the reason it is
    /// here rather than in the slice line with every other one. An array member
    /// contributes a real dimension -- every record has the same four samples --
    /// so `.samples[2]` is `[..., 2]` over the derived shape and nothing special.
    /// A vlen's length differs from record to record, so it contributes no
    /// dimension at all, and an index into it has nowhere else to live.
    std::optional<hsize_t> vlenIndex;
};

/// A member chain, resolved against the datatype it applies to.
///
/// Deliberately not a parse tree, and deliberately free of Qt: this is what a
/// *read* needs and nothing more. The grammar that produces one lives in
/// postproc beside the subscript grammar it shares a line with, because they
/// are read off the same line and two parsers for one notation is two
/// notations the moment either is touched.
struct MemberSelection {
    std::vector<MemberLink> links;

    /// The axes this chain appends to the dataset's own shape: the dimensions
    /// of every array member it passes through, in the order it passes them.
    ///
    /// This is the whole of why `array[i1,i2,i3].b[i4]` and
    /// `(array[:,:,:].b)[i1,i2,i3,i4]` are the same selection. The member
    /// projection happens first and produces one shape, `dataset ++ dims`, and
    /// after that there is one ordinary slice over the whole of it. The short
    /// spelling is not a second rule; it is the same rule written closer.
    std::vector<hsize_t> dims;

    /// What one element of the result is, once `dims` have been taken off it:
    /// the float64 in "array[4] of float64", not the array.
    TypeInfo type;

    /// How the chain was written, ".position.x". What the result is called.
    std::string text;

    [[nodiscard]] bool empty() const { return links.empty(); }
};

/// One member of a compound dataset, presented as a dataset in its own right.
///
/// `/events` holds ten million structs and no view here can draw a struct.
/// `/events.energy` is a line of ten million floats and every view can draw
/// that -- so this is the whole of how compound data reaches the table, the
/// plot and a custom tab: they are handed one of these instead of a `Dataset`,
/// and not one of them has a branch for which it got. It is the same move
/// `postproc::ComputedDataset` makes, and for the same reason.
///
/// **It reads the member and not the struct.** The memory type is a compound
/// holding just the named member -- `H5Tcreate(H5T_COMPOUND)` plus one
/// `H5Tinsert`, nested once per link -- so HDF5 extracts that field during the
/// transfer and `.energy` over ten million records moves four bytes per record
/// rather than the ninety-six the struct occupies. Reading the whole struct and
/// throwing all but one field away would be the same picture at twenty times
/// the bytes, which is the kind of defect that looks like the file being slow.
///
/// **The member's own axes are selected in memory.** HDF5 cannot take a
/// hyperslab *inside* an `H5T_ARRAY` member, so `.samples[2]` transfers all
/// four samples of each record and drops three. That waste is bounded by the
/// member's own extent, which is small by nature -- three coordinates, four
/// samples -- and it is bounded per record rather than per dataset. It is
/// written down here rather than left to be discovered from a profile.
///
/// Derived from `Dataset` rather than holding one because a member chain
/// changes the datatype a read asks for and the shape it reports, and changes
/// nothing at all about which elements of the file are addressed: the leading
/// hyperslab is the same hyperslab, clamped by the same code. Note that
/// `info()` below answers about the *result* while the protected `info_` it
/// inherits goes on answering about the dataset on disk, which is what the
/// selection has to be clamped against.
class FieldDataset : public Dataset
{
public:
    /// Throws H5Error when the chain does not resolve against the dataset's
    /// datatype -- which the caller should have prevented, because the same
    /// walk answers on every keystroke without opening anything.
    FieldDataset(const File& file, const std::string& path, MemberSelection member);

    /// The derived dataset: `shape` is the dataset's own followed by the axes
    /// the chain appends, and `type` is what the chain lands on.
    [[nodiscard]] const DatasetInfo& info() const noexcept override { return derived_; }

    /// "/events.samples" -- the expression, so what a view shows as the name of
    /// what it is drawing is a line that can be typed back in.
    [[nodiscard]] const std::string& path() const noexcept override { return name_; }

    [[nodiscard]] DataWindow readWindow(const std::vector<hsize_t>& offset,
                                        const std::vector<hsize_t>& count) const override;

    [[nodiscard]] NumericWindow
    readNumericWindow(const std::vector<hsize_t>& offset,
                      const std::vector<hsize_t>& count) const override;

    [[nodiscard]] ElementValue
    readElement(const std::vector<hsize_t>& offset) const override;

    /// The chain this was built for.
    [[nodiscard]] const MemberSelection& member() const noexcept { return member_; }

private:
    /// A read of the leading hyperslab, with the member already extracted.
    ///
    /// `values` holds `leading` elements of the member, each `stride` bytes
    /// wide, in the type `elementType` describes. Both public reads are this
    /// followed by a walk over the member's own axes, which is the only thing
    /// that differs between them.
    struct Extract {
        std::vector<unsigned char> values;
        Handle memoryType; ///< kept: a vlen payload is reclaimed against it
        Handle memorySpace; ///< kept for the same reason
        Handle valueType;  ///< what one value is, for formatting it
        std::vector<hsize_t> leadCount;   ///< the dataset axes, clamped
        std::vector<hsize_t> trailOffset; ///< the member axes, clamped
        std::vector<hsize_t> trailCount;  ///< the member axes, clamped
        hsize_t leading = 0;    ///< dataset elements selected
        std::size_t stride = 0; ///< bytes per dataset element after extraction
        std::size_t width = 0;  ///< bytes per value within one of those
    };

    /// Build the nested partial-compound memory type. `asDouble` replaces the
    /// leaf with H5T_NATIVE_DOUBLE so the library converts on the way out,
    /// which is what gives the numeric read exactly one path like every other.
    [[nodiscard]] Handle memoryTypeFor(bool asDouble) const;

    [[nodiscard]] Extract extract(const std::vector<hsize_t>& offset,
                                  const std::vector<hsize_t>& count, bool asDouble) const;

    /// What one value of `memoryType` is, found by going down through the
    /// wrappers this built. An indexed vlen answers with what its list holds,
    /// because that is what the index picked out.
    [[nodiscard]] Handle valueTypeOf(hid_t memoryType) const;

    /// Where within one extracted element the value in `slot` sits, or null
    /// when this record's vlen is too short to have one.
    [[nodiscard]] const unsigned char* valueAt(const Extract& read, hsize_t element,
                                               hsize_t slot) const;

    MemberSelection member_;
    std::string name_;
    DatasetInfo derived_;
    /// How many of the reported axes are the dataset's own. Everything past it
    /// is the member's, and the two halves are selected differently.
    std::size_t originRank_ = 0;
};

} // namespace h5core

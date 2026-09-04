// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <hdf5.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace h5core {

/// What a link in the file hierarchy points at. This is the *resolved* kind:
/// a soft or external link that opens reports what it opens, because that is
/// what the reader is going to look at. How the name got there is `LinkType`.
enum class NodeKind {
    Group,
    Dataset,
    NamedDataType,
    Unresolved, ///< the link exists but its target does not open
    Unknown,
};

std::string toString(NodeKind kind);

/// How a name is attached to its object. A hard link *is* the object and has
/// nothing further to say; the other two store a path, which is information
/// the file carries and a reader needs -- especially when it resolves to
/// nothing.
enum class LinkType { Hard, Soft, External };

std::string toString(LinkType link);

/// Broad HDF5 datatype classes a viewer must render.
enum class TypeClass {
    Integer,
    Float,
    String,
    Compound,
    Enum,
    Array,
    VarLen,
    Bitfield,
    Opaque,
    Reference,
    Time,
    Complex, ///< HDF5 2.0's native complex numbers
    Unknown,
};

std::string toString(TypeClass cls);

/// Whether values of this class can be read as a `double`, which is what the
/// Data Viewer's plot and image presentations are made of. Deliberately narrow:
/// an enum has an ordinal but plotting it would draw a meaning HDF5 does not
/// give it, and a compound has no single value at all.
[[nodiscard]] constexpr bool isNumeric(TypeClass cls) noexcept
{
    return cls == TypeClass::Integer || cls == TypeClass::Float;
}

/// A rendered description of a datatype, kept as plain data so the GUI never
/// needs to hold an open HDF5 identifier.
struct TypeInfo {
    TypeClass cls = TypeClass::Unknown;
    std::string description; ///< e.g. "int32", "float64", "string (variable)"
    std::size_t size = 0;    ///< bytes per element in memory
    bool isSigned = false;
    bool isVariableLength = false;
    /// False when the library has no conversion path from this type to
    /// anything in memory. H5T_TIME is the case that occurs in practice: it is
    /// in the format specification and has never been implemented, so the
    /// bytes are there and no read of them can succeed.
    bool convertible = true;
    /// Member names for Compound, symbol names for Enum; empty otherwise.
    std::vector<std::string> memberNames;
};

/// One entry in the tree, as shown to the user.
struct NodeInfo {
    std::string name; ///< link name within its parent
    std::string path; ///< absolute path within the file
    NodeKind kind = NodeKind::Unknown;
    LinkType link = LinkType::Hard;
    /// What a soft or external link stores, verbatim and unresolved. Empty for
    /// a hard link, which has no target beyond the object itself.
    std::string linkTarget;
    /// The file an external link names. Empty for every other link type.
    std::string linkFile;
    /// Identity of the pointed-to object, used to break hard-link cycles.
    /// Absent for links that do not resolve to an object.
    std::optional<unsigned long> fileNumber;
    std::optional<haddr_t> address;
    /// How many attributes the object carries. Filled by the same object-header
    /// read that settles `kind`, because it is in the same header: asking for
    /// it separately is a second read of bytes already in hand.
    std::size_t attributeCount = 0;

    /// True when the name resolves to something that can be opened.
    [[nodiscard]] bool resolves() const { return kind != NodeKind::Unresolved; }
};

/// What a dataset says it is a picture of, per the HDF5 Image and Palette
/// Specification 1.2 -- the convention every HDF5 tool looks for, and the only
/// thing in a file that separates a raster from a two-dimensional array of
/// numbers that happens to have the same shape.
enum class ImageSubclass { Grayscale, Bitmap, Truecolor, Indexed };

std::string toString(ImageSubclass subclass);

/// Where the colour components of a truecolour image sit relative to the
/// pixels: interleaved with them, or in whole planes of their own.
enum class Interlace { Pixel, Plane };

std::string toString(Interlace interlace);

/// The Image spec's attributes, resolved to what the viewer has to do about
/// them. Present only on a dataset carrying CLASS="IMAGE".
struct ImageInfo {
    ImageSubclass subclass = ImageSubclass::Indexed;
    Interlace interlace = Interlace::Pixel;
    std::string version;
    /// "UL", "LL", "UR" or "LR", or empty when the file does not say. Only
    /// the default, UL, is drawn as the spec means it; see `originHonoured`.
    std::string displayOrigin;

    /// Which dimension is which, once the subclass and the interlace have been
    /// applied to the shape.
    std::size_t rowDim = 0;
    std::size_t columnDim = 1;
    /// The colour component axis, for a truecolour image. Absent for the
    /// single-channel subclasses, which have no such dimension.
    std::optional<std::size_t> channelDim;

    /// The display range the file asks for, if it gives one.
    std::optional<double> minimum;
    std::optional<double> maximum;
    /// IMAGE_WHITE_IS_ZERO: the darkest value is white, not black.
    bool whiteIsZero = false;

    /// False when the attributes claim a shape the dataset does not have -- a
    /// truecolour image of rank 2, say. The tag is still shown, because the
    /// file does say it is an image, but nothing is rearranged on the strength
    /// of an attribute that disagrees with the dataspace.
    bool shapeMatches = false;
    /// False when displayOrigin names a corner other than the default, which
    /// this viewer does not flip the raster to honour.
    bool originHonoured = true;
};

/// Storage layout of a dataset.
enum class Layout { Contiguous, Chunked, Compact, Virtual, Unknown };

std::string toString(Layout layout);

/// The three kinds of dataspace HDF5 distinguishes. Scalar and Null both have
/// rank 0 and no dimensions, and collapsing them is what makes a dataset
/// holding *nothing* report one element that will not read.
enum class Dataspace { Simple, Scalar, Null };

std::string toString(Dataspace space);

/// Everything the Info tab shows about a dataset.
struct DatasetInfo {
    Dataspace space = Dataspace::Simple;
    std::vector<hsize_t> shape;
    std::vector<hsize_t> maxShape;
    std::vector<hsize_t> chunk; ///< empty unless layout == Chunked
    TypeInfo type;
    Layout layout = Layout::Unknown;
    hsize_t storageSize = 0;
    /// Human-readable filter names in pipeline order, e.g. {"deflate (level 6)"}.
    std::vector<std::string> filters;
    /// Filters present in the file that this build cannot decode.
    std::vector<std::string> unavailableFilters;
    /// The subset of those the file marks mandatory. HDF5 skips an *optional*
    /// filter it does not have, on writing and on reading alike, so a dataset
    /// naming one still reads perfectly; only a mandatory one stops the data
    /// from being read at all.
    std::vector<std::string> blockingFilters;
    /// Files holding this dataset's raw data outside the container
    /// (H5Pset_external), in the order the file lists them.
    std::vector<std::string> externalFiles;
    /// One entry per mapping of a virtual dataset, as "file:/path".
    std::vector<std::string> virtualSources;
    /// Set when the dataset declares itself an image. What the Data Viewer
    /// opens on comes from here rather than from the shape alone.
    std::optional<ImageInfo> image;

    [[nodiscard]] std::size_t rank() const { return shape.size(); }
    [[nodiscard]] bool isScalar() const { return space == Dataspace::Scalar; }
    /// A null dataspace: no shape, and no elements either.
    [[nodiscard]] bool isNull() const { return space == Dataspace::Null; }
    [[nodiscard]] hsize_t elementCount() const;
    /// Whether the values can be read at all. A null dataspace is readable
    /// in this sense: reading it yields nothing, which is the right answer
    /// rather than a failure.
    [[nodiscard]] bool readable() const
    {
        return blockingFilters.empty() && type.convertible;
    }
    /// One sentence saying why the values cannot be read, or empty when they
    /// can. Stated once here so every surface that has to explain it says the
    /// same thing.
    [[nodiscard]] std::string unreadableReason() const;
    [[nodiscard]] bool isNumeric() const { return h5core::isNumeric(type.cls); }
};

/// The least a tree row needs to know about a dataset: what shape it is, and
/// whether the file calls it a picture.
///
/// Deliberately not DatasetInfo. That one carries the datatype description, the
/// storage layout, the filter pipeline and every external source -- four more
/// reads of the object header and its messages, none of which any row shows.
/// A file with thousands of datasets pays that difference once per visible row,
/// which is the difference between a tree that scrolls and one that does not.
struct DatasetOutline {
    Dataspace space = Dataspace::Simple;
    std::vector<hsize_t> shape;
    /// Whether the dataset declares itself an image, and which kind. Probed
    /// only when the object header says it has attributes at all, so a plain
    /// dataset never pays for the question.
    bool image = false;
    ImageSubclass subclass = ImageSubclass::Indexed;
};

/// One member of a compound element, already rendered.
///
/// A compound has no single value, so a grid cell can only ever show the whole
/// struct squeezed onto one line and elided. This is the other treatment: the
/// members named, typed and printed one to a row.
struct FieldValue {
    std::string name;
    std::string type;  ///< the member's datatype, as describeType renders it
    std::string value; ///< formatted exactly as a grid cell would show it
    std::string json;  ///< the same value as a JSON literal
};

/// A single attribute, already rendered to text.
struct AttributeInfo {
    std::string name;
    TypeInfo type;
    std::vector<hsize_t> shape;
    std::string value; ///< formatted; long arrays are elided
};

} // namespace h5core

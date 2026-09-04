// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Types.hpp"

#include <functional>
#include <numeric>

namespace h5core {

std::string toString(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Group:            return "Group";
    case NodeKind::Dataset:          return "Dataset";
    case NodeKind::NamedDataType:    return "Named datatype";
    case NodeKind::Unresolved:       return "Unresolved link";
    case NodeKind::Unknown:          break;
    }
    return "Unknown";
}

std::string toString(LinkType link)
{
    switch (link) {
    case LinkType::Hard:     return "Hard link";
    case LinkType::Soft:     return "Soft link";
    case LinkType::External: return "External link";
    }
    return "Link";
}

std::string toString(TypeClass cls)
{
    switch (cls) {
    case TypeClass::Integer:   return "Integer";
    case TypeClass::Float:     return "Float";
    case TypeClass::String:    return "String";
    case TypeClass::Compound:  return "Compound";
    case TypeClass::Enum:      return "Enum";
    case TypeClass::Array:     return "Array";
    case TypeClass::VarLen:    return "Variable-length";
    case TypeClass::Bitfield:  return "Bitfield";
    case TypeClass::Opaque:    return "Opaque";
    case TypeClass::Reference: return "Reference";
    case TypeClass::Time:      return "Time";
    case TypeClass::Complex:   return "Complex";
    case TypeClass::Unknown:   break;
    }
    return "Unknown";
}

std::string toString(ImageSubclass subclass)
{
    switch (subclass) {
    case ImageSubclass::Grayscale: return "Grayscale";
    case ImageSubclass::Bitmap:    return "Bitmap";
    case ImageSubclass::Truecolor: return "Truecolour";
    case ImageSubclass::Indexed:   return "Indexed";
    }
    return "Unknown";
}

std::string toString(Interlace interlace)
{
    switch (interlace) {
    case Interlace::Pixel: return "Pixel-interleaved";
    case Interlace::Plane: return "Plane-interleaved";
    }
    return "Unknown";
}

std::string toString(Layout layout)
{
    switch (layout) {
    case Layout::Contiguous: return "Contiguous";
    case Layout::Chunked:    return "Chunked";
    case Layout::Compact:    return "Compact";
    case Layout::Virtual:    return "Virtual";
    case Layout::Unknown:    break;
    }
    return "Unknown";
}

std::string toString(Dataspace space)
{
    switch (space) {
    case Dataspace::Simple: return "Simple";
    case Dataspace::Scalar: return "Scalar";
    case Dataspace::Null:   return "Null";
    }
    return "Unknown";
}

std::string DatasetInfo::unreadableReason() const
{
    if (!blockingFilters.empty()) {
        return "the data is compressed with " + blockingFilters.front()
               + ", which this build cannot decode";
    }
    if (!type.convertible) {
        return "this build of HDF5 cannot convert " + type.description
               + " to a value in memory";
    }
    return {};
}

hsize_t DatasetInfo::elementCount() const
{
    // A scalar dataspace has rank 0 but holds exactly one element; a null one
    // has rank 0 and holds none. The empty product below is right for the
    // first and wrong for the second, which is why the two are distinguished.
    if (space == Dataspace::Null) {
        return 0;
    }
    return std::accumulate(shape.begin(), shape.end(), static_cast<hsize_t>(1),
                           std::multiplies<>{});
}

} // namespace h5core

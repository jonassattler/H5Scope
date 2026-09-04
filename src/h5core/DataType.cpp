// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DataType.hpp"

#include "Thread.hpp"

#include "Error.hpp"
#include "Handle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

namespace h5core {
namespace {

TypeClass classOf(H5T_class_t cls)
{
    switch (cls) {
    case H5T_INTEGER:   return TypeClass::Integer;
    case H5T_FLOAT:     return TypeClass::Float;
    case H5T_STRING:    return TypeClass::String;
    case H5T_COMPOUND:  return TypeClass::Compound;
    case H5T_ENUM:      return TypeClass::Enum;
    case H5T_ARRAY:     return TypeClass::Array;
    case H5T_VLEN:      return TypeClass::VarLen;
    case H5T_BITFIELD:  return TypeClass::Bitfield;
    case H5T_OPAQUE:    return TypeClass::Opaque;
    case H5T_REFERENCE: return TypeClass::Reference;
    case H5T_TIME:      return TypeClass::Time;
    case H5T_COMPLEX:   return TypeClass::Complex;
    default:            return TypeClass::Unknown;
    }
}

std::string describeIntegerName(hid_t type, std::size_t size)
{
    const H5T_sign_t sign = H5Tget_sign(type);
    const char* prefix = (sign == H5T_SGN_NONE) ? "uint" : "int";
    const std::string name = std::format("{}{}", prefix, size * 8);

    // HDF5 lets an integer use fewer bits than its width -- which is the whole
    // point of the n-bit filter -- and the width alone would then misstate what
    // the file holds.
    const std::size_t precision = H5Tget_precision(type);
    if (precision > 0 && precision != size * 8) {
        return std::format("{} ({}-bit)", name, precision);
    }
    H5Eclear2(H5E_DEFAULT);
    return name;
}

std::string describeFloatName(std::size_t size)
{
    // HDF5 permits exotic float layouts, but sizes map to the familiar names
    // in every file a viewer realistically meets.
    switch (size) {
    case 2:  return "float16";
    case 4:  return "float32";
    case 8:  return "float64";
    case 16: return "float128";
    default: return std::format("float ({} bytes)", size);
    }
}

/// IEEE 754 binary16 to double, decoded by hand rather than through a
/// `_Float16` the standard does not have until C++23 and MSVC does not have
/// at all. HDF5 2.x reads and writes half precision, so a viewer meets it.
double halfToDouble(const void* data)
{
    std::uint16_t bits = 0;
    std::memcpy(&bits, data, sizeof(bits));

    const int sign = ((bits >> 15) != 0) ? -1 : 1;
    const int exponent = static_cast<int>((bits >> 10) & 0x1F);
    const int mantissa = bits & 0x3FF;

    if (exponent == 0x1F) {
        return (mantissa == 0)
                   ? sign * std::numeric_limits<double>::infinity()
                   : std::numeric_limits<double>::quiet_NaN();
    }
    if (exponent == 0) {
        // Subnormal: no implicit leading one, and a fixed exponent.
        return sign * std::ldexp(static_cast<double>(mantissa), -24);
    }
    return sign * std::ldexp(static_cast<double>(mantissa + 1024), exponent - 25);
}

/// Read a fixed- or variable-length HDF5 string element into std::string.
std::string readStringElement(hid_t type, const void* data)
{
    if (H5Tis_variable_str(type) > 0) {
        // Element is a char* owned by HDF5 until reclaimed.
        const char* ptr = *static_cast<const char* const*>(data);
        return (ptr != nullptr) ? std::string(ptr) : std::string{};
    }

    const std::size_t size = H5Tget_size(type);
    const char* chars = static_cast<const char*>(data);
    // Fixed-length strings are space- or null-padded; trim the padding.
    std::size_t length = size;
    while (length > 0 && (chars[length - 1] == '\0' || chars[length - 1] == ' ')) {
        --length;
    }
    return std::string(chars, length);
}

std::string formatInteger(hid_t type, const void* data)
{
    const std::size_t size = H5Tget_size(type);
    const bool isSigned = H5Tget_sign(type) != H5T_SGN_NONE;

    if (isSigned) {
        std::int64_t value = 0;
        switch (size) {
        case 1: value = *static_cast<const std::int8_t*>(data); break;
        case 2: value = *static_cast<const std::int16_t*>(data); break;
        case 4: value = *static_cast<const std::int32_t*>(data); break;
        case 8: value = *static_cast<const std::int64_t*>(data); break;
        default: return "<unsupported int width>";
        }
        return std::format("{}", value);
    }

    std::uint64_t value = 0;
    switch (size) {
    case 1: value = *static_cast<const std::uint8_t*>(data); break;
    case 2: value = *static_cast<const std::uint16_t*>(data); break;
    case 4: value = *static_cast<const std::uint32_t*>(data); break;
    case 8: value = *static_cast<const std::uint64_t*>(data); break;
    default: return "<unsupported int width>";
    }
    return std::format("{}", value);
}

std::string formatFloat(hid_t type, const void* data)
{
    switch (H5Tget_size(type)) {
    case 2:  return std::format("{}", halfToDouble(data));
    case 4:  return std::format("{}", *static_cast<const float*>(data));
    case 8:  return std::format("{}", *static_cast<const double*>(data));
    case 16: return std::format("{}", static_cast<double>(
                 *static_cast<const long double*>(data)));
    default: return "<unsupported float width>";
    }
}

/// A complex number is two floats of its base type laid end to end.
std::string formatComplex(hid_t type, const void* data)
{
    Handle base(H5Tget_super(type), &H5Tclose);
    if (!base.valid()) {
        H5Eclear2(H5E_DEFAULT);
        return "<complex>";
    }
    const std::size_t part = H5Tget_size(base.get());
    const std::string real = formatFloat(base.get(), data);
    const std::string imaginary =
        formatFloat(base.get(), static_cast<const unsigned char*>(data) + part);
    // The sign is already on the imaginary part unless it is positive.
    const char* separator = imaginary.starts_with('-') ? "" : "+";
    return std::format("{}{}{}i", real, separator, imaginary);
}

std::string formatEnum(hid_t type, const void* data)
{
    // H5Tenum_nameof needs the value in the enum's own base type.
    char name[256] = {};
    if (H5Tenum_nameof(type, data, name, sizeof(name)) >= 0) {
        return std::string(name);
    }
    H5Eclear2(H5E_DEFAULT);

    // Unnamed value: fall back to the underlying integer.
    Handle base(H5Tget_super(type), &H5Tclose);
    if (base.valid()) {
        return formatInteger(base.get(), data);
    }
    return "<enum>";
}

/// A JSON string literal: the text quoted, with the six escapes JSON defines
/// and \u00XX for the control characters it has no shorthand for. UTF-8 above
/// the ASCII range passes through, which JSON permits and which keeps a name
/// written in any script readable in the pane.
std::string quoteJson(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
    return out;
}

/// A double as a JSON number, or as a quoted word when JSON has no number for
/// it. Dropping a NaN to `null` would say the value is absent, which is a
/// different statement from the one the file makes.
std::string numberJson(double value)
{
    if (std::isnan(value)) {
        return "\"nan\"";
    }
    if (std::isinf(value)) {
        return (value > 0) ? "\"inf\"" : "\"-inf\"";
    }
    return std::format("{}", value);
}

std::string formatBytes(const void* data, std::size_t size)
{
    std::string out;
    out.reserve(size * 3);
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        if (i > 0) {
            out.push_back(' ');
        }
        out.append(std::format("{:02X}", bytes[i]));
    }
    return out;
}

} // namespace

TypeInfo describeType(hid_t type)
{
    thread::check(__func__);
    TypeInfo info;
    info.size = H5Tget_size(type);
    info.cls = classOf(H5Tget_class(type));

    // Asked once, here, rather than discovered by a read that fails: a type
    // with no conversion path is a property of the type, and every surface
    // that has to explain the failure needs it before any data is touched.
    Handle native(H5Tget_native_type(type, H5T_DIR_ASCEND), &H5Tclose);
    info.convertible = native.valid();
    if (!info.convertible) {
        H5Eclear2(H5E_DEFAULT);
    }

    switch (info.cls) {
    case TypeClass::Integer: {
        info.isSigned = H5Tget_sign(type) != H5T_SGN_NONE;
        info.description = describeIntegerName(type, info.size);
        break;
    }
    case TypeClass::Float: {
        info.isSigned = true;
        info.description = describeFloatName(info.size);
        break;
    }
    case TypeClass::String: {
        info.isVariableLength = H5Tis_variable_str(type) > 0;
        info.description = info.isVariableLength
                               ? "string (variable)"
                               : std::format("string ({} bytes)", info.size);
        break;
    }
    case TypeClass::Compound: {
        const int count = H5Tget_nmembers(type);
        std::ostringstream desc;
        desc << "compound {";
        for (int i = 0; i < count; ++i) {
            char* member = H5Tget_member_name(type, static_cast<unsigned>(i));
            if (member != nullptr) {
                info.memberNames.emplace_back(member);
                if (i > 0) {
                    desc << ", ";
                }
                desc << member;
                H5free_memory(member);
            }
        }
        desc << "}";
        info.description = desc.str();
        break;
    }
    case TypeClass::Enum: {
        const int count = H5Tget_nmembers(type);
        for (int i = 0; i < count; ++i) {
            char* member = H5Tget_member_name(type, static_cast<unsigned>(i));
            if (member != nullptr) {
                info.memberNames.emplace_back(member);
                H5free_memory(member);
            }
        }
        info.description = std::format("enum ({} values)", count);
        break;
    }
    case TypeClass::Array: {
        const int rank = H5Tget_array_ndims(type);
        std::vector<hsize_t> dims(static_cast<std::size_t>(std::max(rank, 0)));
        if (rank > 0) {
            H5Tget_array_dims2(type, dims.data());
        }
        Handle base(H5Tget_super(type), &H5Tclose);
        std::ostringstream desc;
        desc << "array";
        for (const hsize_t dim : dims) {
            desc << "[" << dim << "]";
        }
        if (base.valid()) {
            desc << " of " << describeType(base.get()).description;
        }
        info.description = desc.str();
        break;
    }
    case TypeClass::VarLen: {
        info.isVariableLength = true;
        Handle base(H5Tget_super(type), &H5Tclose);
        info.description =
            base.valid()
                ? std::format("vlen of {}", describeType(base.get()).description)
                : "vlen";
        break;
    }
    case TypeClass::Bitfield:
        info.description = std::format("bitfield ({} bytes)", info.size);
        break;
    case TypeClass::Opaque: {
        char* tag = H5Tget_tag(type);
        info.description = (tag != nullptr)
                               ? std::format("opaque ({})", tag)
                               : std::format("opaque ({} bytes)", info.size);
        if (tag != nullptr) {
            H5free_memory(tag);
        }
        break;
    }
    case TypeClass::Reference:
        info.description = "reference";
        break;
    case TypeClass::Time:
        info.description = std::format("time ({} bytes)", info.size);
        break;
    case TypeClass::Complex: {
        Handle base(H5Tget_super(type), &H5Tclose);
        info.description = base.valid()
                               ? std::format("complex{} ({} pair)", info.size * 8,
                                             describeType(base.get()).description)
                               : std::format("complex ({} bytes)", info.size);
        break;
    }
    case TypeClass::Unknown:
        info.description = "unknown";
        break;
    }

    return info;
}

std::string formatElement(hid_t type, const void* data)
{
    thread::check(__func__);
    switch (classOf(H5Tget_class(type))) {
    case TypeClass::Integer:
        return formatInteger(type, data);
    case TypeClass::Float:
        return formatFloat(type, data);
    case TypeClass::String:
        return readStringElement(type, data);
    case TypeClass::Enum:
        return formatEnum(type, data);
    case TypeClass::Compound: {
        const int count = H5Tget_nmembers(type);
        std::ostringstream out;
        out << "{";
        for (int i = 0; i < count; ++i) {
            const auto index = static_cast<unsigned>(i);
            Handle member(H5Tget_member_type(type, index), &H5Tclose);
            const std::size_t offset = H5Tget_member_offset(type, index);
            if (i > 0) {
                out << ", ";
            }
            char* name = H5Tget_member_name(type, index);
            if (name != nullptr) {
                out << name << "=";
                H5free_memory(name);
            }
            if (member.valid()) {
                out << formatElement(member.get(),
                                     static_cast<const unsigned char*>(data) + offset);
            }
        }
        out << "}";
        return out.str();
    }
    case TypeClass::Array: {
        const int rank = H5Tget_array_ndims(type);
        std::vector<hsize_t> dims(static_cast<std::size_t>(std::max(rank, 0)));
        if (rank > 0) {
            H5Tget_array_dims2(type, dims.data());
        }
        hsize_t total = 1;
        for (const hsize_t dim : dims) {
            total *= dim;
        }

        Handle base(H5Tget_super(type), &H5Tclose);
        if (!base.valid()) {
            return "<array>";
        }
        const std::size_t stride = H5Tget_size(base.get());

        std::ostringstream out;
        out << "[";
        for (hsize_t i = 0; i < total; ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << formatElement(base.get(),
                                 static_cast<const unsigned char*>(data) + i * stride);
        }
        out << "]";
        return out.str();
    }
    case TypeClass::VarLen: {
        const auto* vl = static_cast<const hvl_t*>(data);
        Handle base(H5Tget_super(type), &H5Tclose);
        if (!base.valid() || vl->p == nullptr) {
            return "[]";
        }
        const std::size_t stride = H5Tget_size(base.get());

        std::ostringstream out;
        out << "[";
        for (std::size_t i = 0; i < vl->len; ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << formatElement(base.get(),
                                 static_cast<const unsigned char*>(vl->p) + i * stride);
        }
        out << "]";
        return out.str();
    }
    case TypeClass::Complex:
        return formatComplex(type, data);
    case TypeClass::Reference:
        return "<reference>";
    case TypeClass::Bitfield:
    case TypeClass::Opaque:
    case TypeClass::Time:
    case TypeClass::Unknown:
        break;
    }
    return formatBytes(data, H5Tget_size(type));
}

std::vector<FieldValue> describeCompoundElement(hid_t type, const void* data)
{
    thread::check(__func__);
    std::vector<FieldValue> fields;
    if (classOf(H5Tget_class(type)) != TypeClass::Compound) {
        H5Eclear2(H5E_DEFAULT);
        return fields;
    }

    const int count = H5Tget_nmembers(type);
    fields.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        const auto index = static_cast<unsigned>(i);
        Handle member(H5Tget_member_type(type, index), &H5Tclose);
        const std::size_t offset = H5Tget_member_offset(type, index);

        FieldValue field;
        if (char* name = H5Tget_member_name(type, index); name != nullptr) {
            field.name = name;
            H5free_memory(name);
        }
        if (member.valid()) {
            const auto* at = static_cast<const unsigned char*>(data) + offset;
            field.type = describeType(member.get()).description;
            field.value = formatElement(member.get(), at);
            field.json = toJson(member.get(), at);
        }
        fields.push_back(std::move(field));
    }
    return fields;
}

std::string toJson(hid_t type, const void* data)
{
    thread::check(__func__);
    switch (classOf(H5Tget_class(type))) {
    case TypeClass::Integer:
        // Straight through: an integer is already a JSON number, and putting
        // it through a double would lose the top bits of a 64-bit one.
        return formatInteger(type, data);
    case TypeClass::Float: {
        switch (H5Tget_size(type)) {
        case 2:  return numberJson(halfToDouble(data));
        case 4:  return numberJson(*static_cast<const float*>(data));
        case 8:  return numberJson(*static_cast<const double*>(data));
        case 16: return numberJson(
            static_cast<double>(*static_cast<const long double*>(data)));
        default: return quoteJson(formatFloat(type, data));
        }
    }
    case TypeClass::String:
        return quoteJson(readStringElement(type, data));
    case TypeClass::Enum:
        // The symbol, not the ordinal: the name is what the file gave the
        // value, and it is the half that survives a change of base type.
        return quoteJson(formatEnum(type, data));
    case TypeClass::Compound: {
        const int count = H5Tget_nmembers(type);
        std::ostringstream out;
        out << "{";
        for (int i = 0; i < count; ++i) {
            const auto index = static_cast<unsigned>(i);
            Handle member(H5Tget_member_type(type, index), &H5Tclose);
            const std::size_t offset = H5Tget_member_offset(type, index);
            if (i > 0) {
                out << ", ";
            }
            char* name = H5Tget_member_name(type, index);
            out << quoteJson((name != nullptr) ? std::string_view(name)
                                               : std::string_view{})
                << ": ";
            if (name != nullptr) {
                H5free_memory(name);
            }
            out << (member.valid()
                        ? toJson(member.get(),
                                 static_cast<const unsigned char*>(data) + offset)
                        : std::string("null"));
        }
        out << "}";
        return out.str();
    }
    case TypeClass::Array: {
        const int rank = H5Tget_array_ndims(type);
        std::vector<hsize_t> dims(static_cast<std::size_t>(std::max(rank, 0)));
        if (rank > 0) {
            H5Tget_array_dims2(type, dims.data());
        }
        hsize_t total = 1;
        for (const hsize_t dim : dims) {
            total *= dim;
        }

        Handle base(H5Tget_super(type), &H5Tclose);
        if (!base.valid()) {
            H5Eclear2(H5E_DEFAULT);
            return "[]";
        }
        const std::size_t stride = H5Tget_size(base.get());

        std::ostringstream out;
        out << "[";
        for (hsize_t i = 0; i < total; ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << toJson(base.get(),
                          static_cast<const unsigned char*>(data) + i * stride);
        }
        out << "]";
        return out.str();
    }
    case TypeClass::VarLen: {
        const auto* vl = static_cast<const hvl_t*>(data);
        Handle base(H5Tget_super(type), &H5Tclose);
        if (!base.valid() || vl->p == nullptr) {
            H5Eclear2(H5E_DEFAULT);
            return "[]";
        }
        const std::size_t stride = H5Tget_size(base.get());

        std::ostringstream out;
        out << "[";
        for (std::size_t i = 0; i < vl->len; ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << toJson(base.get(),
                          static_cast<const unsigned char*>(vl->p) + i * stride);
        }
        out << "]";
        return out.str();
    }
    case TypeClass::Complex: {
        // An object rather than "1+2i": JSON's job is to be taken apart again,
        // and a reader pasting this somewhere wants two numbers.
        Handle base(H5Tget_super(type), &H5Tclose);
        if (!base.valid()) {
            H5Eclear2(H5E_DEFAULT);
            return quoteJson(formatElement(type, data));
        }
        const std::size_t part = H5Tget_size(base.get());
        return std::format(
            "{{\"re\": {}, \"im\": {}}}", toJson(base.get(), data),
            toJson(base.get(), static_cast<const unsigned char*>(data) + part));
    }
    case TypeClass::Bitfield:
    case TypeClass::Opaque:
    case TypeClass::Reference:
    case TypeClass::Time:
    case TypeClass::Unknown:
        break;
    }
    // Everything the format keeps as bytes. There is no JSON number for a
    // bitfield, and the hex is what the grid shows for it too.
    return quoteJson(formatElement(type, data));
}

VlenGuard::VlenGuard(hid_t type, hid_t space, void* buffer) noexcept
    : type_(type), space_(space), buffer_(buffer),
      needed_(H5Tdetect_class(type, H5T_VLEN) > 0 || H5Tdetect_class(type, H5T_STRING) > 0)
{
    // H5Tdetect_class can fail on an odd type; clear rather than leak the error.
    H5Eclear2(H5E_DEFAULT);
}

VlenGuard::~VlenGuard()
{
    if (needed_ && buffer_ != nullptr) {
        H5Treclaim(type_, space_, H5P_DEFAULT, buffer_);
        H5Eclear2(H5E_DEFAULT);
    }
}

} // namespace h5core

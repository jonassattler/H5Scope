// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Image.hpp"

#include "DataType.hpp"
#include "Handle.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace h5core {
namespace {

/// One attribute read as text, whatever string flavour the writer used.
/// Returns nothing when the attribute is absent or is not a string -- both of
/// which are states a file is allowed to be in, and neither of which is worth
/// an error.
std::optional<std::string> stringAttribute(hid_t object, const char* name)
{
    if (H5Aexists(object, name) <= 0) {
        H5Eclear2(H5E_DEFAULT);
        return std::nullopt;
    }
    Handle attribute(H5Aopen(object, name, H5P_DEFAULT), &H5Aclose);
    Handle type(attribute.valid() ? H5Aget_type(attribute.get()) : H5I_INVALID_HID,
                &H5Tclose);
    if (!attribute.valid() || !type.valid() || H5Tget_class(type.get()) != H5T_STRING) {
        H5Eclear2(H5E_DEFAULT);
        return std::nullopt;
    }

    Handle native(H5Tget_native_type(type.get(), H5T_DIR_ASCEND), &H5Tclose);
    Handle space(H5Aget_space(attribute.get()), &H5Sclose);
    if (!native.valid() || !space.valid()) {
        H5Eclear2(H5E_DEFAULT);
        return std::nullopt;
    }

    // A scalar string, which is what the spec asks for; anything longer is
    // read as its first element, which is the only one that could be meant.
    std::vector<unsigned char> buffer(H5Tget_size(native.get()));
    if (buffer.empty() || H5Aread(attribute.get(), native.get(), buffer.data()) < 0) {
        H5Eclear2(H5E_DEFAULT);
        return std::nullopt;
    }
    VlenGuard reclaim(native.get(), space.get(), buffer.data());
    return formatElement(native.get(), buffer.data());
}

/// The first `count` elements of a numeric attribute, widened to double.
std::vector<double> numericAttribute(hid_t object, const char* name, std::size_t count)
{
    if (H5Aexists(object, name) <= 0) {
        H5Eclear2(H5E_DEFAULT);
        return {};
    }
    Handle attribute(H5Aopen(object, name, H5P_DEFAULT), &H5Aclose);
    Handle space(attribute.valid() ? H5Aget_space(attribute.get()) : H5I_INVALID_HID,
                 &H5Sclose);
    if (!attribute.valid() || !space.valid()) {
        H5Eclear2(H5E_DEFAULT);
        return {};
    }

    const hssize_t elements = H5Sget_simple_extent_npoints(space.get());
    if (elements < static_cast<hssize_t>(count)) {
        H5Eclear2(H5E_DEFAULT);
        return {};
    }

    // H5T_NATIVE_DOUBLE as the memory type, so the library widens whatever
    // integer or float the writer chose and this file has no switch over them.
    std::vector<double> values(static_cast<std::size_t>(elements));
    if (H5Aread(attribute.get(), H5T_NATIVE_DOUBLE, values.data()) < 0) {
        H5Eclear2(H5E_DEFAULT);
        return {};
    }
    values.resize(count);
    return values;
}

ImageSubclass subclassFrom(const std::optional<std::string>& text)
{
    // The spec makes IMAGE_SUBCLASS optional and IMAGE_INDEXED its default.
    if (!text.has_value()) {
        return ImageSubclass::Indexed;
    }
    if (*text == "IMAGE_GRAYSCALE") {
        return ImageSubclass::Grayscale;
    }
    if (*text == "IMAGE_BITMAP") {
        return ImageSubclass::Bitmap;
    }
    if (*text == "IMAGE_TRUECOLOR") {
        return ImageSubclass::Truecolor;
    }
    return ImageSubclass::Indexed;
}

} // namespace

std::optional<ImageSubclass> readImageOutline(hid_t dataset)
{
    const auto declaredClass = stringAttribute(dataset, "CLASS");
    if (!declaredClass.has_value() || *declaredClass != "IMAGE") {
        return std::nullopt;
    }
    return subclassFrom(stringAttribute(dataset, "IMAGE_SUBCLASS"));
}

std::optional<ImageInfo> readImageInfo(hid_t dataset, const std::vector<hsize_t>& shape)
{
    // CLASS is the one required attribute, and its absence is the common case
    // by far: one existence check is all a plain dataset costs.
    const auto declaredClass = stringAttribute(dataset, "CLASS");
    if (!declaredClass.has_value() || *declaredClass != "IMAGE") {
        return std::nullopt;
    }

    ImageInfo info;
    info.subclass = subclassFrom(stringAttribute(dataset, "IMAGE_SUBCLASS"));
    info.version = stringAttribute(dataset, "IMAGE_VERSION").value_or(std::string{});
    info.displayOrigin =
        stringAttribute(dataset, "DISPLAY_ORIGIN").value_or(std::string{});
    // The spec's default origin is the upper left, which is where this viewer
    // already starts drawing. Any other corner would need the raster flipped,
    // and saying so beats drawing it the wrong way up in silence.
    info.originHonoured = info.displayOrigin.empty() || info.displayOrigin == "UL";

    if (info.subclass == ImageSubclass::Truecolor) {
        info.interlace =
            (stringAttribute(dataset, "INTERLACE_MODE").value_or("INTERLACE_PIXEL") ==
             "INTERLACE_PLANE")
                ? Interlace::Plane
                : Interlace::Pixel;

        // The spec fixes both orders: [height][width][components] when the
        // components are interleaved with the pixels, [components][height][width]
        // when they are stored as planes.
        if (shape.size() == 3) {
            if (info.interlace == Interlace::Pixel) {
                info.rowDim = 0;
                info.columnDim = 1;
                info.channelDim = 2;
            }
            else {
                info.channelDim = 0;
                info.rowDim = 1;
                info.columnDim = 2;
            }
            info.shapeMatches = true;
        }
    }
    else {
        // Every other subclass is one value per pixel: [height][width].
        info.rowDim = 0;
        info.columnDim = 1;
        info.shapeMatches = shape.size() == 2;
    }

    const auto range = numericAttribute(dataset, "IMAGE_MINMAXRANGE", 2);
    if (range.size() == 2 && range[0] < range[1]) {
        info.minimum = range[0];
        info.maximum = range[1];
    }

    const auto whiteIsZero = numericAttribute(dataset, "IMAGE_WHITE_IS_ZERO", 1);
    info.whiteIsZero = !whiteIsZero.empty() && whiteIsZero.front() != 0.0;

    return info;
}

} // namespace h5core

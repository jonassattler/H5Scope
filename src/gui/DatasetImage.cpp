// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetImage.hpp"

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace gui {
namespace {

/// Where a value falls on the ramp, 0 to 1. `span` is guaranteed non-zero by
/// the caller, because a flat image has no ramp at all.
inline double position(double value, double low, double span, bool reversed)
{
    const double t = std::clamp((value - low) / span, 0.0, 1.0);
    return reversed ? 1.0 - t : t;
}

/// One value on the black-to-white ramp.
inline int level(double value, double low, double span, bool invert)
{
    return static_cast<int>(std::lround(position(value, low, span, invert) * 255.0));
}

/// A colour `at` of the way along `stops`, interpolated in RGB.
///
/// The same arithmetic as Theme.rampColor, deliberately: a picture and a plot
/// of the same values must not disagree about what a colour means. Linear in
/// RGB rather than in HSV, because HSV walks the long way round the wheel
/// between two stops and invents hues that are in neither.
QRgb rampColor(const std::vector<QColor>& stops, double at)
{
    if (stops.empty()) {
        return qRgb(0, 0, 0);
    }
    if (stops.size() == 1) {
        return stops.front().rgb();
    }
    const double scaled = std::clamp(at, 0.0, 1.0)
                          * static_cast<double>(stops.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(scaled));
    const std::size_t upper = std::min(lower + 1, stops.size() - 1);
    const double t = scaled - static_cast<double>(lower);
    const QColor& a = stops[lower];
    const QColor& b = stops[upper];
    const auto blend = [t](int from, int to) {
        return static_cast<int>(std::lround(from + (to - from) * t));
    };
    return qRgb(blend(a.red(), b.red()), blend(a.green(), b.green()),
                blend(a.blue(), b.blue()));
}

/// A picture needs two dimensions of its own before a third can hold colour.
constexpr std::size_t kMinimumRankForChannels = 3;

/// The whole span an integer of this type can hold, for a dataset that says it
/// is a picture and does not say its range.
///
/// A raster's values are levels, not measurements: an 8-bit frame holding 30
/// to 40 is a nearly black picture, and stretching it to its own extremes
/// would be the viewer inventing contrast the file never claimed. The spec's
/// IMAGE_MINMAXRANGE says so when it is there; when it is not, the datatype
/// says it instead -- 0 to 255 for a byte, and so on up.
///
/// Only 8-, 16- and 32-bit integers. A 64-bit span does not survive a double
/// exactly and no raster is stored in one; those fall back to the data's own
/// extent, which is what every non-picture gets.
std::optional<std::pair<double, double>> fullTypeRange(const h5core::TypeInfo& type)
{
    if (type.cls != h5core::TypeClass::Integer || type.size == 0 || type.size > 4) {
        return std::nullopt;
    }
    const int bits = static_cast<int>(type.size) * 8;
    if (type.isSigned) {
        const double bound = std::pow(2.0, bits - 1);
        return std::pair{-bound, bound - 1.0};
    }
    return std::pair{0.0, std::pow(2.0, bits) - 1.0};
}

} // namespace

DatasetImage::DatasetImage(DatasetTableModel* table, QObject* parent)
    : QObject(parent), table_(table)
{
    connect(table_, &QAbstractItemModel::modelReset, this,
            [this] { invalidate(); });
    // datasetChanged is the new selection; modelReset above is also emitted
    // when the table is merely rearranged, and the reader's own black and
    // white points must survive that.
    connect(table_, &DatasetTableModel::datasetChanged, this, [this] {
        applyImageDefaults();
        invalidate();
    });
}

void DatasetImage::applyImageDefaults()
{
    const h5core::DataSource* dataset = table_->dataset();
    const auto& info =
        (dataset != nullptr) ? dataset->info().image : std::optional<h5core::ImageInfo>{};

    // Set directly rather than through the setters: setAutoRange seeds the two
    // boxes from whatever was last on screen, which is the right thing when a
    // reader leaves auto and the wrong thing when the file states a range.
    invert_ = info.has_value() && info->whiteIsZero;
    autoRange_ = true;
    if (info.has_value() && info->minimum.has_value()) {
        // The file states the range: nothing else has standing to argue.
        autoRange_ = false;
        rangeMinimum_ = *info->minimum;
        rangeMaximum_ = *info->maximum;
    } else if (info.has_value() && dataset != nullptr) {
        // It says it is a picture but not what its levels run between, so the
        // datatype answers: a byte raster is drawn against 0 to 255 whatever
        // this particular frame happens to reach.
        if (const auto span = fullTypeRange(dataset->info().type); span.has_value()) {
            autoRange_ = false;
            rangeMinimum_ = span->first;
            rangeMaximum_ = span->second;
        }
    }
    // Anything else -- a float field, an array nobody called a picture --
    // takes the range off its own values, which autoRange_ above is.

    // The colour axis comes from the file when the file says where it is, and
    // only when the shape agrees with the claim -- a dataset that declares an
    // interlace its dataspace cannot have is a file, not a licence to read
    // three planes out of a dimension that has one.
    channelDimension_ = -1;
    colorMode_ = ColorMode::Grayscale;
    resetChannels();

    if (info.has_value() && info->shapeMatches && info->channelDim.has_value()
        && channelSelectable()) {
        channelDimension_ = static_cast<int>(*info->channelDim);
        // Truecolour is the one subclass whose channels are colours rather
        // than bands, so it is the one that opens in colour -- and on all of
        // the components it has. The spec fixes truecolour at three and gives
        // a fourth no other meaning, so a raster that carries one carries a
        // coverage: reading it as RGB would draw an image the file said was
        // translucent as though it were solid. More than four is a claim the
        // spec does not describe, and only the three it does name are read.
        const int extent = channelExtent();
        if (info->subclass == h5core::ImageSubclass::Truecolor && extent >= 3) {
            colorMode_ = (extent == 4) ? ColorMode::Rgba : ColorMode::Rgb;
        }
    }
    clampChannels();
}

int DatasetImage::channelExtent() const
{
    const h5core::DataSource* dataset = table_->dataset();
    if (dataset == nullptr || channelDimension_ < 0) {
        return 0;
    }
    const auto& shape = dataset->info().shape;
    if (static_cast<std::size_t>(channelDimension_) >= shape.size()) {
        return 0;
    }
    return static_cast<int>(shape[static_cast<std::size_t>(channelDimension_)]);
}

int DatasetImage::channelCount() const { return channelExtent(); }

bool DatasetImage::channelSelectable() const
{
    const h5core::DataSource* dataset = table_->dataset();
    return dataset != nullptr && dataset->info().rank() >= kMinimumRankForChannels;
}

QVariantList DatasetImage::channelChoices() const
{
    QVariantList choices;
    choices.append(QVariantMap{{QStringLiteral("label"), tr("none")},
                               {QStringLiteral("dimension"), -1}});
    const h5core::DataSource* dataset = table_->dataset();
    if (dataset == nullptr || !channelSelectable()) {
        return choices;
    }
    const auto& shape = dataset->info().shape;
    for (std::size_t d = 0; d < shape.size(); ++d) {
        choices.append(QVariantMap{
            {QStringLiteral("label"),
             tr("dim %1 · %2").arg(d).arg(static_cast<qulonglong>(shape[d]))},
            {QStringLiteral("dimension"), static_cast<int>(d)}});
    }
    return choices;
}

void DatasetImage::resetChannels()
{
    grayIndex_ = 0;
    redIndex_ = 0;
    greenIndex_ = 1;
    blueIndex_ = 2;
    alphaIndex_ = 3;
}

void DatasetImage::clampChannels()
{
    const int extent = channelExtent();
    // With no colour axis there is no extent to hold these inside, and
    // clamping them all to zero is what used to strip the 0/1/2/3 defaults
    // off every dataset that did not open with one: by the time the reader
    // named an axis, red, green and blue were all channel 0 and a truecolour
    // picture came out grey.
    if (extent <= 0) {
        return;
    }
    const int last = extent - 1;
    grayIndex_ = std::clamp(grayIndex_, 0, last);
    redIndex_ = std::clamp(redIndex_, 0, last);
    greenIndex_ = std::clamp(greenIndex_, 0, last);
    blueIndex_ = std::clamp(blueIndex_, 0, last);
    alphaIndex_ = std::clamp(alphaIndex_, 0, last);
}

DatasetImage::ColorMode DatasetImage::colorMode() const
{
    // A mode needs somewhere to take its channels from. Reported rather than
    // merely enforced on write, so a mode chosen for one dataset cannot
    // survive into the next one as a claim the picture does not honour.
    if (channelDimension_ < 0) {
        return ColorMode::Grayscale;
    }
    const int extent = channelExtent();
    if (colorMode_ == ColorMode::Rgba) {
        return (extent >= 4) ? ColorMode::Rgba
             : (extent >= 3) ? ColorMode::Rgb
                             : ColorMode::Grayscale;
    }
    if (colorMode_ == ColorMode::Rgb) {
        return (extent >= 3) ? ColorMode::Rgb : ColorMode::Grayscale;
    }
    return ColorMode::Grayscale;
}

void DatasetImage::setColorMode(ColorMode mode)
{
    if (colorMode_ == mode) {
        return;
    }
    colorMode_ = mode;
    invalidate();
}

void DatasetImage::setChannelDimension(int dimension)
{
    const h5core::DataSource* dataset = table_->dataset();
    const auto rank = (dataset != nullptr) ? static_cast<int>(dataset->info().rank()) : 0;
    const int wanted = (dimension >= 0 && dimension < rank && channelSelectable())
                           ? dimension
                           : -1;
    if (channelDimension_ == wanted) {
        return;
    }
    channelDimension_ = wanted;
    // A different axis is a different set of channels, so the indices start
    // again at its first four rather than carrying over positions that meant
    // something along the dimension before it.
    resetChannels();
    clampChannels();
    invalidate();
    // Nothing is written back to the data settings panel. Naming a colour axis
    // is a statement about the picture, and it used to rearrange the table --
    // which changed the slice under the grid and the plot as well, for a
    // question neither of them had been asked. The picture arranges its own
    // axes instead; see TableAxes::asPicture and readPlane below.
}

void DatasetImage::setGrayIndex(int index)
{
    const int wanted = std::clamp(index, 0, std::max(channelExtent() - 1, 0));
    if (grayIndex_ == wanted) {
        return;
    }
    grayIndex_ = wanted;
    invalidate();
}

void DatasetImage::setRedIndex(int index)
{
    const int wanted = std::clamp(index, 0, std::max(channelExtent() - 1, 0));
    if (redIndex_ == wanted) {
        return;
    }
    redIndex_ = wanted;
    invalidate();
}

void DatasetImage::setGreenIndex(int index)
{
    const int wanted = std::clamp(index, 0, std::max(channelExtent() - 1, 0));
    if (greenIndex_ == wanted) {
        return;
    }
    greenIndex_ = wanted;
    invalidate();
}

void DatasetImage::setBlueIndex(int index)
{
    const int wanted = std::clamp(index, 0, std::max(channelExtent() - 1, 0));
    if (blueIndex_ == wanted) {
        return;
    }
    blueIndex_ = wanted;
    invalidate();
}

void DatasetImage::setAlphaIndex(int index)
{
    const int wanted = std::clamp(index, 0, std::max(channelExtent() - 1, 0));
    if (alphaIndex_ == wanted) {
        return;
    }
    alphaIndex_ = wanted;
    invalidate();
}

void DatasetImage::invalidate()
{
    sample_.reset();
    // The revision is what QML puts in the image URL, so it has to move even
    // when nothing else about this object is observable: an unchanged URL is
    // served from Qt's pixmap cache and the old raster stays on screen.
    ++revision_;
    emit changed();
}

void DatasetImage::recolour()
{
    ++revision_;
    emit changed();
}

void DatasetImage::setInvert(bool invert)
{
    if (invert_ == invert) {
        return;
    }
    invert_ = invert;
    invalidate();
}

void DatasetImage::setRampBegin(double at)
{
    const double clamped = std::clamp(at, 0.0, 1.0);
    if (qFuzzyCompare(rampBegin_, clamped)) {
        return;
    }
    rampBegin_ = clamped;
    // Nothing is re-read: only which colours the values already sampled land
    // on, as with the ramp itself.
    recolour();
}

void DatasetImage::setRampEnd(double at)
{
    const double clamped = std::clamp(at, 0.0, 1.0);
    if (qFuzzyCompare(rampEnd_, clamped)) {
        return;
    }
    rampEnd_ = clamped;
    recolour();
}

void DatasetImage::setAutoRange(bool automatic)
{
    if (autoRange_ == automatic) {
        return;
    }
    autoRange_ = automatic;
    // Leaving auto puts the reader in front of two boxes; seeding them with
    // what was just on screen is a better starting point than 0 and 1.
    if (!autoRange_) {
        ensure();
        if (sample_->hasFinite) {
            rangeMinimum_ = sample_->minimum;
            rangeMaximum_ = sample_->maximum;
        }
    }
    invalidate();
}

void DatasetImage::setRangeMinimum(double value)
{
    if (qFuzzyCompare(rangeMinimum_, value)) {
        return;
    }
    rangeMinimum_ = value;
    invalidate();
}

void DatasetImage::setRangeMaximum(double value)
{
    if (qFuzzyCompare(rangeMaximum_, value)) {
        return;
    }
    rangeMaximum_ = value;
    invalidate();
}

void DatasetImage::setRamp(const QVariantList& stops)
{
    if (ramp_ == stops) {
        return;
    }
    ramp_ = stops;
    // Resolved once here rather than per pixel in render(), which is called
    // for every raster of a picture that can be a thousand cells on a side.
    rampStops_.clear();
    rampStops_.reserve(static_cast<std::size_t>(stops.size()));
    for (const QVariant& stop : stops) {
        const QColor colour = stop.value<QColor>();
        if (colour.isValid()) {
            rampStops_.push_back(colour);
        }
    }
    // Nothing is re-read: only how the values already sampled are painted --
    // but the raster still has to be re-served, or the URL is unchanged and
    // Qt's pixmap cache hands back the picture in the previous ramp.
    recolour();
}

void DatasetImage::setRampName(const QString& name)
{
    if (rampName_ == name) {
        return;
    }
    rampName_ = name;
    // No invalidate(), and no rampStops_ to rebuild: the pixels are coloured
    // from `ramp_`, which the caller sets alongside this. What changes here is
    // only what the two settings panels and the table's fill call the ramp.
    emit changed();
}

void DatasetImage::setMissingColor(const QColor& color)
{
    if (missingColor_ == color) {
        return;
    }
    missingColor_ = color;
    invalidate();
}

DatasetTableModel::NumericGrid DatasetImage::readPlane(int index) const
{
    // Without a colour axis this is the table exactly as the grid has it. With
    // one it is that table read as a picture around that axis -- in a copy of
    // the axes, so the grid keeps whatever it was showing however the channels
    // are chosen.
    const TableAxes& axes = table_->axes();
    if (channelDimension_ < 0) {
        return table_->sampleValues(axes, 0, -1, kMaxExtent, 0, -1, kMaxExtent);
    }
    return table_->sampleValues(
        axes.asPicture(static_cast<std::size_t>(channelDimension_),
                       static_cast<hsize_t>(std::max(index, 0))),
        0, -1, kMaxExtent, 0, -1, kMaxExtent);
}

void DatasetImage::ensure() const
{
    if (sample_.has_value()) {
        return;
    }

    Raster raster;
    switch (colorMode()) {
    case ColorMode::Rgba:
        raster.planes.push_back(readPlane(redIndex_));
        raster.planes.push_back(readPlane(greenIndex_));
        raster.planes.push_back(readPlane(blueIndex_));
        raster.planes.push_back(readPlane(alphaIndex_));
        break;
    case ColorMode::Rgb:
        raster.planes.push_back(readPlane(redIndex_));
        raster.planes.push_back(readPlane(greenIndex_));
        raster.planes.push_back(readPlane(blueIndex_));
        break;
    case ColorMode::Grayscale:
        raster.planes.push_back(readPlane(grayIndex_));
        break;
    }

    // One ramp over all the channels: mapping each to its own extremes would
    // shift the hue of every pixel by however far the three happened to
    // differ, which is a colour the file does not contain.
    for (const auto& plane : raster.planes) {
        if (raster.error.isEmpty()) {
            raster.error = plane.error;
        }
        if (!plane.hasFinite) {
            continue;
        }
        if (!raster.hasFinite) {
            raster.minimum = plane.minimum;
            raster.maximum = plane.maximum;
            raster.hasFinite = true;
        } else {
            raster.minimum = std::min(raster.minimum, plane.minimum);
            raster.maximum = std::max(raster.maximum, plane.maximum);
        }
    }
    sample_ = std::move(raster);
}

int DatasetImage::width() const
{
    ensure();
    return sample_->columns();
}

int DatasetImage::height() const
{
    ensure();
    return sample_->rows();
}

int DatasetImage::sourceWidth() const { return table_->columnCount(); }
int DatasetImage::sourceHeight() const { return table_->rowCount(); }

bool DatasetImage::thinned() const
{
    ensure();
    return !sample_->planes.empty()
           && (sample_->planes.front().rowStride > 1
               || sample_->planes.front().columnStride > 1);
}

double DatasetImage::minimum() const
{
    ensure();
    return sample_->hasFinite ? sample_->minimum : 0.0;
}

double DatasetImage::maximum() const
{
    ensure();
    return sample_->hasFinite ? sample_->maximum : 0.0;
}

bool DatasetImage::numeric() const { return table_->numeric(); }

bool DatasetImage::hasData() const
{
    ensure();
    return sample_->rows() > 0 && sample_->columns() > 0 && sample_->hasFinite;
}

QString DatasetImage::error() const
{
    ensure();
    return sample_->error;
}

QImage DatasetImage::render() const
{
    ensure();
    const int rows = sample_->rows();
    const int columns = sample_->columns();
    if (rows <= 0 || columns <= 0) {
        return {};
    }

    double low = autoRange_ ? sample_->minimum : rangeMinimum_;
    double high = autoRange_ ? sample_->maximum : rangeMaximum_;
    if (!autoRange_ && high < low) {
        std::swap(low, high);
    }
    // A constant image has no ramp to put it on. Mid gray says "one value
    // everywhere", where black would be indistinguishable from a minimum.
    const double span = high - low;
    const bool flat = !(span > 0.0) || !sample_->hasFinite;
    // Three planes or four is one channel per primary, which is the picture's
    // own colour; a ramp is a reading imposed on a single channel, and the two
    // are different things. So RGB and RGBA ignore the ramp entirely.
    const bool colour = sample_->planes.size() >= 3;
    const bool coverage = sample_->planes.size() >= 4;

    // The stretch of the ramp the reader kept, applied after the value has
    // been placed on the whole of it. Turned round by dragging one handle
    // through the other is still a band, and reads the ramp backwards, which
    // is a thing to allow rather than to straighten out.
    const double rampFrom = rampBegin_;
    const double rampSpan = rampEnd_ - rampBegin_;
    const auto band = [rampFrom, rampSpan](double at) {
        return rampFrom + at * rampSpan;
    };

    QImage image(columns, rows, QImage::Format_ARGB32);
    const QRgb missing = missingColor_.rgba();

    for (int y = 0; y < rows; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < columns; ++x) {
            if (!colour) {
                const double value = sample_->planes.front().at(y, x);
                if (!std::isfinite(value)) {
                    line[x] = missing;
                    continue;
                }
                // Where on the whole ramp this value falls, then where that
                // lands inside the stretch of it the reader kept. Mid-ramp for
                // a flat picture: it says "one value everywhere", where an end
                // would read as a minimum or a maximum.
                const double at =
                    band(flat ? 0.5 : position(value, low, span, invert_));
                if (!rampStops_.empty()) {
                    line[x] = rampColor(rampStops_, at);
                    continue;
                }
                const int shade = static_cast<int>(std::lround(at * 255.0));
                line[x] = qRgb(shade, shade, shade);
                continue;
            }

            // A pixel is a colour only if all of its channels read. One that
            // does not is the same missing cell it would be in gray.
            const int channels = coverage ? 4 : 3;
            int shades[4] = {0, 0, 0, 255};
            bool complete = true;
            for (int c = 0; c < channels; ++c) {
                const double value = sample_->planes[static_cast<std::size_t>(c)].at(y, x);
                if (!std::isfinite(value)) {
                    complete = false;
                    break;
                }
                // Three planes are the picture's own colour, not a reading
                // imposed on one channel: no ramp, and so no stretch of one
                // to keep either.
                //
                // The fourth is a coverage rather than a colour, so the
                // inversion does not reach it: a negative of a picture is a
                // negative of what it shows, not of how much of it is there.
                shades[c] = flat ? (c == 3 ? 255 : 128)
                                 : level(value, low, span, invert_ && c < 3);
            }
            line[x] = complete
                          ? qRgba(shades[0], shades[1], shades[2], shades[3])
                          : missing;
        }
    }
    return image;
}

} // namespace gui

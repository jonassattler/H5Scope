// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "DatasetTableModel.hpp"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace gui {

/// The Data Viewer's image presentation: the table read as a raster.
///
/// Like DatasetPlot this decides nothing about *which* slice it shows; the data
/// settings panel does, and this is one more reading of that same table. What
/// it does decide is how the values in that slice become colour, and that is
/// the one place where a picture needs to know something a table does not: a
/// dataset of three planes is a colour image only if a reader says which
/// dimension holds the planes and which of them is red.
///
/// So this carries a colour axis of its own. `channelDimension` names the
/// dimension the channels live along, and the mode says how many of its indices
/// are read: one for grayscale, three for RGB, four for RGBA. Each is read
/// through TableAxes::asPicture, which holds that dimension at one index in a
/// *copy* of the table's axes -- so naming a colour axis, or moving along it,
/// changes the picture and leaves the grid beside it showing exactly what it
/// was showing. When the dataset carries the HDF5 Image spec's attributes all
/// of this is filled in from them, because the file has already said it.
///
/// The channel index is an index into the whole dimension, whatever the slice
/// says about it: asking for channel 2 reads channel 2 of the file, not the
/// second of whatever the table happens to have selected. A colour axis is not
/// a thing the reader subsets -- it is the thing they are choosing *from*.
///
/// QML reaches the pixels through DatasetImageProvider rather than through a
/// property, because an image is not a value to bind. `revision` is what QML
/// binds: it changes whenever the raster would, and the Image's source carries
/// it, which is what makes the provider re-run.
class DatasetImage : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from AppController.datasetImage")

    /// Map the darkest value to white instead of to black. In RGB that is a
    /// negative, which is the same statement about every channel at once.
    Q_PROPERTY(bool invert READ invert WRITE setInvert NOTIFY changed)
    /// The colour ramp a single-channel picture is drawn on, as a list of
    /// stops. Empty -- the default -- is the black-to-white ramp a grayscale
    /// image has always used.
    ///
    /// Pushed in from QML rather than named here, because the ramps are the
    /// plot's ramps and those live in Theme.qml: that file is the one place in
    /// this project allowed to hold a literal colour, and a picture and a
    /// bundle of lines showing the same values should not be able to disagree
    /// about what viridis is.
    Q_PROPERTY(QVariantList ramp READ ramp WRITE setRamp NOTIFY changed)
    /// Which ramp those stops came from, by the name Theme knows it under.
    ///
    /// Held here and not in the panel that sets it, because the ramp is not
    /// the picture's alone. A table cell filled by its own value is the same
    /// mapping put back over the numbers -- the same ramp, the same direction,
    /// the same band -- and two views answering one question about one dataset
    /// must not be able to disagree. The image reads the stops; the table and
    /// both settings panels read the name, which is the thing a dropdown and a
    /// ramp bar are drawn from.
    ///
    /// Not interpreted here: this class resolves colour from `ramp` above and
    /// nothing else. Theme.qml holds the stops, as it holds every literal
    /// colour in the project.
    Q_PROPERTY(QString rampName READ rampName WRITE setRampName NOTIFY changed)
    /// Which stretch of the ramp the values are spread over: 0 is its first
    /// colour, 1 its last. The two handles on the ramp bar set these, and only
    /// the colours between them are ever painted.
    ///
    /// A separate question from the value range below, and the reason the two
    /// used to be one control by mistake: this says which colours the reading
    /// is made of, that says which values reach them. A reader narrowing a
    /// ramp to its dark half and one setting a black point are doing different
    /// things, and a single pair of handles could only do one of them.
    Q_PROPERTY(double rampBegin READ rampBegin WRITE setRampBegin NOTIFY changed)
    Q_PROPERTY(double rampEnd READ rampEnd WRITE setRampEnd NOTIFY changed)
    /// Take the black and white points from the data itself. When false the
    /// two below are used, so two datasets can be compared on one scale.
    ///
    /// Not a mode the reader picks any more: it is what "the range nobody has
    /// pinned" means, and it survives only until a value range is typed. See
    /// applyImageDefaults for what a fresh selection starts on.
    Q_PROPERTY(bool autoRange READ autoRange WRITE setAutoRange NOTIFY changed)
    Q_PROPERTY(double rangeMinimum READ rangeMinimum WRITE setRangeMinimum
                   NOTIFY changed)
    Q_PROPERTY(double rangeMaximum READ rangeMaximum WRITE setRangeMaximum
                   NOTIFY changed)
    /// Colour for a cell that could not be read. Set from Theme by QML, so
    /// this file names no colour of its own.
    Q_PROPERTY(QColor missingColor READ missingColor WRITE setMissingColor
                   NOTIFY changed)

    // --- the colour axis -------------------------------------------------
    /// Grayscale, RGB or RGBA. Forced down to whatever the colour axis can
    /// actually supply: to grayscale when there is no colour axis at all, and
    /// to RGB when there is one but it is only three channels deep.
    Q_PROPERTY(gui::DatasetImage::ColorMode colorMode READ colorMode
                   WRITE setColorMode NOTIFY changed)
    /// Which dimension of the dataset holds the colour channels, or -1 when
    /// none does and every cell is one value.
    Q_PROPERTY(int channelDimension READ channelDimension WRITE setChannelDimension
                   NOTIFY changed)
    /// How many channels that dimension has. Zero when there is no colour axis.
    Q_PROPERTY(int channelCount READ channelCount NOTIFY changed)
    /// Whether a colour axis can be chosen at all. A rank-2 dataset has exactly
    /// the two dimensions the picture is made of, so there is no third one left
    /// to hold channels and the mode is fixed to grayscale.
    Q_PROPERTY(bool channelSelectable READ channelSelectable NOTIFY changed)
    /// What the colour-axis dropdown offers: [{ label, dimension }], "none"
    /// first. Built here so QML needs to know nothing about the dataspace.
    Q_PROPERTY(QVariantList channelChoices READ channelChoices NOTIFY changed)

    Q_PROPERTY(int grayIndex READ grayIndex WRITE setGrayIndex NOTIFY changed)
    Q_PROPERTY(int redIndex READ redIndex WRITE setRedIndex NOTIFY changed)
    Q_PROPERTY(int greenIndex READ greenIndex WRITE setGreenIndex NOTIFY changed)
    Q_PROPERTY(int blueIndex READ blueIndex WRITE setBlueIndex NOTIFY changed)
    /// Which index of the colour axis is the coverage, in RGBA. Read on the
    /// same value range as the three colours, because in every raster that
    /// carries one it is stored the same way they are -- a byte of 0 to 255
    /// beside three more.
    Q_PROPERTY(int alphaIndex READ alphaIndex WRITE setAlphaIndex NOTIFY changed)

    /// Bumped whenever the raster changes. QML puts it in the image URL.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    /// Size of the table behind the raster, which is larger when it is thinned.
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY changed)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY changed)
    Q_PROPERTY(bool thinned READ thinned NOTIFY changed)
    /// The values actually found, whatever range is being mapped. In RGB that
    /// is across all three channels, because they share one ramp.
    Q_PROPERTY(double minimum READ minimum NOTIFY changed)
    Q_PROPERTY(double maximum READ maximum NOTIFY changed)
    Q_PROPERTY(bool numeric READ numeric NOTIFY changed)
    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    enum class ColorMode {
        Grayscale, ///< one channel, on the black-to-white ramp
        Rgb,       ///< three channels, one per primary
        Rgba,      ///< four: the three primaries and a coverage
    };
    Q_ENUM(ColorMode)

    explicit DatasetImage(DatasetTableModel* table, QObject* parent = nullptr);

    [[nodiscard]] bool invert() const { return invert_; }
    void setInvert(bool invert);
    [[nodiscard]] QVariantList ramp() const { return ramp_; }
    void setRamp(const QVariantList& stops);
    [[nodiscard]] QString rampName() const { return rampName_; }
    void setRampName(const QString& name);
    [[nodiscard]] double rampBegin() const { return rampBegin_; }
    void setRampBegin(double at);
    [[nodiscard]] double rampEnd() const { return rampEnd_; }
    void setRampEnd(double at);
    [[nodiscard]] bool autoRange() const { return autoRange_; }
    void setAutoRange(bool automatic);
    [[nodiscard]] double rangeMinimum() const { return rangeMinimum_; }
    void setRangeMinimum(double value);
    [[nodiscard]] double rangeMaximum() const { return rangeMaximum_; }
    void setRangeMaximum(double value);

    [[nodiscard]] ColorMode colorMode() const;
    void setColorMode(ColorMode mode);
    [[nodiscard]] int channelDimension() const { return channelDimension_; }
    void setChannelDimension(int dimension);
    [[nodiscard]] int channelCount() const;
    [[nodiscard]] bool channelSelectable() const;
    [[nodiscard]] QVariantList channelChoices() const;

    [[nodiscard]] int grayIndex() const { return grayIndex_; }
    void setGrayIndex(int index);
    [[nodiscard]] int redIndex() const { return redIndex_; }
    void setRedIndex(int index);
    [[nodiscard]] int greenIndex() const { return greenIndex_; }
    void setGreenIndex(int index);
    [[nodiscard]] int blueIndex() const { return blueIndex_; }
    void setBlueIndex(int index);
    [[nodiscard]] int alphaIndex() const { return alphaIndex_; }
    void setAlphaIndex(int index);

    [[nodiscard]] int revision() const { return revision_; }
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int sourceWidth() const;
    [[nodiscard]] int sourceHeight() const;
    [[nodiscard]] bool thinned() const;
    [[nodiscard]] double minimum() const;
    [[nodiscard]] double maximum() const;
    [[nodiscard]] bool numeric() const;
    [[nodiscard]] bool hasData() const;
    [[nodiscard]] QString error() const;

    /// The raster, one pixel per sampled cell. Null when there is nothing
    /// numeric to show.
    ///
    /// ARGB whether or not the channels differ: a cell that would not read is
    /// painted in the warning colour rather than in a shade that would read as
    /// a value, and RGB needs the channels anyway.
    [[nodiscard]] QImage render() const;

    [[nodiscard]] QColor missingColor() const { return missingColor_; }
    void setMissingColor(const QColor& color);

    /// Drop the cached sample. The next reader re-reads the file.
    void invalidate();

signals:
    void changed();

private:
    /// One reading of the table: one plane in grayscale, three in RGB, four in
    /// RGBA, plus the extremes across all of them, because they share one ramp.
    struct Raster {
        std::vector<DatasetTableModel::NumericGrid> planes;
        double minimum = 0.0;
        double maximum = 0.0;
        bool hasFinite = false;
        QString error;

        [[nodiscard]] int rows() const
        {
            return planes.empty() ? 0 : planes.front().rows;
        }
        [[nodiscard]] int columns() const
        {
            return planes.empty() ? 0 : planes.front().columns;
        }
    };

    void ensure() const;
    /// One plane, read with the colour axis held at `index` -- or the table as
    /// it stands when there is no colour axis.
    [[nodiscard]] DatasetTableModel::NumericGrid readPlane(int index) const;
    /// Extent of the colour axis in the dataset, or 0 when there is none.
    [[nodiscard]] int channelExtent() const;
    /// Hold every channel index inside the colour axis. Called whenever that
    /// axis moves, so a stale index cannot outlive the dimension it named.
    ///
    /// Only when there *is* a colour axis. With none there is no extent to
    /// clamp against, and clamping to zero was how the four indices lost their
    /// 0/1/2/3 defaults on every dataset that did not open with a colour axis:
    /// by the time the reader named one, red, green and blue were all zero and
    /// the picture came out grey.
    void clampChannels();
    /// Put the four indices back to 0, 1, 2, 3 -- the first channels of the
    /// axis, in order, which is what every raster that has them stores.
    void resetChannels();

    /// Take the colour axis, the black and white points and the polarity from
    /// the HDF5 Image spec's attributes when the new selection carries them,
    /// and revert to reading them off the data when it does not. Once per
    /// selection, like DatasetPlot's orientation: a starting point the reader
    /// stays free to overrule, not a binding.
    void applyImageDefaults();

    /// Repaint without re-reading. The values already sampled land on
    /// different colours; the revision still has to move, because QML puts it
    /// in the image URL and an unchanged URL is served from Qt's pixmap cache.
    void recolour();

    DatasetTableModel* table_ = nullptr;
    bool invert_ = false;
    QVariantList ramp_;
    /// The same stops resolved once, so render() is not converting a QVariant
    /// per pixel.
    std::vector<QColor> rampStops_;
    /// "gray" is the black-to-white ramp `ramp_` empty already means, and is
    /// the default for the same reason it is the default there.
    QString rampName_{QStringLiteral("gray")};
    /// The whole ramp until the reader narrows it.
    double rampBegin_ = 0.0;
    double rampEnd_ = 1.0;
    bool autoRange_ = true;
    double rangeMinimum_ = 0.0;
    double rangeMaximum_ = 1.0;
    QColor missingColor_{Qt::magenta};
    int revision_ = 0;

    ColorMode colorMode_ = ColorMode::Grayscale;
    int channelDimension_ = -1;
    int grayIndex_ = 0;
    int redIndex_ = 0;
    int greenIndex_ = 1;
    int blueIndex_ = 2;
    int alphaIndex_ = 3;

    mutable std::optional<Raster> sample_;

public:
    /// Ceiling on the raster. A screen cannot resolve more, and every cell
    /// beyond it is a read of the file that nobody sees. Zooming into a
    /// dataset larger than this is the data settings panel's job: subset the
    /// dimensions and the raster is of that subset at full resolution.
    static constexpr int kMaxExtent = 1024;
};

} // namespace gui

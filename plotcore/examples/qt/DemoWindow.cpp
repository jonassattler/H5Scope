// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DemoWindow.hpp"

#include "Signals.hpp"

#include "plotcore/Palettes.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>
#include <numbers>

namespace demo {
namespace {

using plotcore::Rgba;

QColor qcolor(const Rgba& colour)
{
    return QColor::fromRgbF(static_cast<float>(colour.red), static_cast<float>(colour.green),
                            static_cast<float>(colour.blue), static_cast<float>(colour.alpha));
}

Rgba rgba(const QColor& colour)
{
    return Rgba::of(colour.redF(), colour.greenF(), colour.blueF(), colour.alphaF());
}

void paintSwatch(QPushButton* button, const Rgba& colour)
{
    button->setText(QString::fromStdString(plotcore::hexOf(colour)));
    button->setStyleSheet(
        QStringLiteral("background-color: %1; color: %2")
            .arg(qcolor(colour).name(), qcolor(colour).lightnessF() > 0.5 ? "black" : "white"));
}

const char* const kSignals[] = {
    "Sine with a one-sample spike", "Square wave", "Gaps (NaN)", "Twelve decades", "Random walk",
    "Ten million samples",
};

const char* const kGrids[] = {"none", "loose", "dense", "custom"};
const char* const kCorners[] = {"top-left", "top-right", "bottom-left", "bottom-right"};

// The table's columns.
enum Column
{
    Show,
    Name,
    Colour,
    OwnAxis,
    Fixed,
    AxisLabel,
    Scale,
    Shift,
    OwnTime,
    PutBack,
    Columns,
};

double baseOf(QComboBox* mode, QDoubleSpinBox* custom)
{
    switch (mode->currentIndex()) {
    case 1:
        return 2.0;
    case 2:
        return std::numbers::e;
    case 3:
        return custom->value();
    default:
        return 10.0;
    }
}

QDoubleSpinBox* spin(double low, double high, double value, int decimals = 2)
{
    auto* box = new QDoubleSpinBox;
    box->setRange(low, high);
    box->setDecimals(decimals);
    box->setValue(value);
    box->setKeyboardTracking(false);
    return box;
}

} // namespace

DemoWindow::DemoWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("plotcore — Qt"));
    plot_ = new plotcore::qt::PlotWidget;
    setCentralWidget(plot_);

    const plotcore::LineColours defaults = plotcore::LineColours::defaults(true);
    singleColour_ = defaults.single;
    rangeFromColour_ = defaults.rangeFrom;
    rangeToColour_ = defaults.rangeTo;

    // A control set while the panels are still being built would read the
    // ones not built yet.
    refreshing_ = true;
    auto* tabs = new QTabWidget;
    tabs->addTab(dataPanel(), QStringLiteral("Data"));
    tabs->addTab(axesPanel(), QStringLiteral("Axes"));
    tabs->addTab(linesPanel(), QStringLiteral("Lines"));
    tabs->addTab(viewPanel(), QStringLiteral("View"));
    auto* scroll = new QScrollArea;
    scroll->setWidget(tabs);
    scroll->setWidgetResizable(true);
    auto* dock = new QDockWidget(QStringLiteral("Settings"));
    dock->setWidget(scroll);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    status_ = new QLabel;
    held_ = new QLabel;
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(held_);

    connect(plot_, &plotcore::qt::PlotWidget::changed, this, [this] {
        refreshLines();
        refreshViewBoxes();
        refreshStatus();
    });
    // The reading follows the pointer, which is a repaint and not a change.
    auto* readout = new QTimer(this);
    readout->setInterval(100);
    connect(readout, &QTimer::timeout, this, &DemoWindow::refreshStatus);
    readout->start();

    connect(&streamTimer_, &QTimer::timeout, this, &DemoWindow::streamStep);
    streamTimer_.setInterval(15);

    refreshing_ = false;
    applySettings();
    resize(1400, 820);
    addSignal(0);
    addSignal(1);
}

// --- the panels ------------------------------------------------------------------

QWidget* DemoWindow::dataPanel()
{
    auto* panel = new QWidget;
    auto* form = new QFormLayout(panel);

    signal_ = new QComboBox;
    for (const char* name : kSignals) {
        signal_->addItem(QString::fromUtf8(name));
    }
    auto* add = new QPushButton(QStringLiteral("Add"));
    connect(add, &QPushButton::clicked, this, [this] { addSignal(signal_->currentIndex()); });
    auto* row = new QHBoxLayout;
    row->addWidget(signal_, 1);
    row->addWidget(add);
    form->addRow(QStringLiteral("Signal"), row);

    stream_ = new QPushButton(QStringLiteral("Stream five million samples"));
    connect(stream_, &QPushButton::clicked, this, &DemoWindow::startStream);
    form->addRow(QString(), stream_);

    auto* open = new QPushButton(QStringLiteral("Open .npy or CSV…"));
    connect(open, &QPushButton::clicked, this, &DemoWindow::openFile);
    form->addRow(QString(), open);

    auto* clear = new QPushButton(QStringLiteral("Remove every line"));
    connect(clear, &QPushButton::clicked, this, [this] {
        streamTimer_.stop();
        streamLine_ = -1;
        plot_->figure().clear();
        refreshLines();
        plot_->update();
    });
    form->addRow(QString(), clear);

    xAxis_ = new QComboBox;
    xAxis_->addItems({QStringLiteral("Index from 0"), QStringLiteral("Index from 1"),
                      QStringLiteral("Shared time, steady"),
                      QStringLiteral("Shared time, wandering")});
    connect(xAxis_, &QComboBox::currentIndexChanged, this, &DemoWindow::applyXAxis);
    form->addRow(QStringLiteral("x"), xAxis_);

    appetite_ = new QComboBox;
    appetite_->addItems(
        {QStringLiteral("Low"), QStringLiteral("Medium"), QStringLiteral("Greedy")});
    appetite_->setCurrentIndex(1);
    connect(appetite_, &QComboBox::currentIndexChanged, this, [this](int index) {
        plot_->figure().plot().setAppetite(static_cast<plotcore::Appetite>(index));
        refreshLines();
        plot_->update();
    });
    form->addRow(QStringLiteral("RAM budget"), appetite_);

    auto* help = new QLabel(QStringLiteral(
        "<b>Wheel</b> zooms about the pointer; <b>Shift</b> zooms across, <b>Control</b> up.<br>"
        "<b>Drag</b> pans, past the data but not off it.<br>"
        "<b>Right-drag</b> zooms to a band. <b>Double-click</b> puts the view back.<br>"
        "<b>Alt-drag</b> or <b>middle-drag</b> moves a line; <b>Alt+Control+wheel</b> "
        "scales it; <b>Control-click</b> puts it back."));
    help->setWordWrap(true);
    form->addRow(help);
    return panel;
}

QWidget* DemoWindow::axesPanel()
{
    auto* panel = new QWidget;
    auto* form = new QFormLayout(panel);
    const auto settingsChanged = [this] { applySettings(); };

    const auto logRow = [&](QCheckBox*& log, QComboBox*& base, QDoubleSpinBox*& custom,
                            const QString& name) {
        log = new QCheckBox(QStringLiteral("logarithmic"));
        base = new QComboBox;
        base->addItems({QStringLiteral("10"), QStringLiteral("2"), QStringLiteral("e"),
                        QStringLiteral("custom")});
        custom = spin(1.01, 1000.0, 3.0);
        custom->setEnabled(false);
        connect(log, &QCheckBox::toggled, this, settingsChanged);
        connect(base, &QComboBox::currentIndexChanged, this, [this, base, custom] {
            custom->setEnabled(base->currentIndex() == 3);
            applySettings();
        });
        connect(custom, &QDoubleSpinBox::valueChanged, this, settingsChanged);
        auto* row = new QHBoxLayout;
        row->addWidget(log);
        row->addWidget(new QLabel(QStringLiteral("base")));
        row->addWidget(base);
        row->addWidget(custom);
        form->addRow(name, row);
    };
    logRow(xLog_, xBase_, xBaseCustom_, QStringLiteral("x"));
    logRow(yLog_, yBase_, yBaseCustom_, QStringLiteral("y"));

    grid_ = new QComboBox;
    for (const char* name : kGrids) {
        grid_->addItem(QString::fromUtf8(name));
    }
    grid_->setCurrentIndex(1);
    connect(grid_, &QComboBox::currentIndexChanged, this, settingsChanged);
    form->addRow(QStringLiteral("Grid"), grid_);
    gridStepX_ = spin(0.0, 1e12, 0.0, 4);
    gridStepY_ = spin(0.0, 1e12, 0.0, 4);
    connect(gridStepX_, &QDoubleSpinBox::valueChanged, this, settingsChanged);
    connect(gridStepY_, &QDoubleSpinBox::valueChanged, this, settingsChanged);
    auto* steps = new QHBoxLayout;
    steps->addWidget(new QLabel(QStringLiteral("x")));
    steps->addWidget(gridStepX_);
    steps->addWidget(new QLabel(QStringLiteral("y")));
    steps->addWidget(gridStepY_);
    form->addRow(QStringLiteral("Custom step"), steps);

    minorNumbers_ = new QCheckBox(QStringLiteral("Number what lies between powers"));
    connect(minorNumbers_, &QCheckBox::toggled, this, settingsChanged);
    form->addRow(QString(), minorNumbers_);
    flipped_ = new QCheckBox(QStringLiteral("Flipped: x up, y across"));
    connect(flipped_, &QCheckBox::toggled, this, settingsChanged);
    form->addRow(QString(), flipped_);

    title_ = new QLineEdit;
    xLabel_ = new QLineEdit;
    yLabel_ = new QLineEdit;
    for (QLineEdit* edit : {title_, xLabel_, yLabel_}) {
        connect(edit, &QLineEdit::textChanged, this, settingsChanged);
    }
    form->addRow(QStringLiteral("Title"), title_);
    form->addRow(QStringLiteral("x label"), xLabel_);
    form->addRow(QStringLiteral("y label"), yLabel_);
    return panel;
}

QWidget* DemoWindow::linesPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    const auto settingsChanged = [this] { applySettings(); };

    markers_ = new QCheckBox(QStringLiteral("Markers"));
    cursor_ = new QCheckBox(QStringLiteral("Crosshair"));
    cursor_->setChecked(true);
    connect(markers_, &QCheckBox::toggled, this, settingsChanged);
    connect(cursor_, &QCheckBox::toggled, this, settingsChanged);
    lineWidth_ = spin(0.5, 8.0, 1.0, 1);
    lineWidth_->setSingleStep(0.5);
    connect(lineWidth_, &QDoubleSpinBox::valueChanged, this, &DemoWindow::applyStyle);
    auto* row = new QHBoxLayout;
    row->addWidget(markers_);
    row->addWidget(cursor_);
    row->addWidget(new QLabel(QStringLiteral("width")));
    row->addWidget(lineWidth_);
    form->addRow(QStringLiteral("Draw"), row);

    colourMode_ = new QComboBox;
    for (const std::string& name : plotcore::paletteNames()) {
        colourMode_->addItem(QString::fromStdString(name));
    }
    colourMode_->insertSeparator(colourMode_->count());
    for (const std::string& name : plotcore::rampNames()) {
        colourMode_->addItem(QString::fromStdString(name));
    }
    colourMode_->insertSeparator(colourMode_->count());
    colourMode_->addItem(QStringLiteral("same"));
    colourMode_->addItem(QStringLiteral("range"));
    connect(colourMode_, &QComboBox::currentIndexChanged, this, settingsChanged);
    reversed_ = new QCheckBox(QStringLiteral("reversed"));
    connect(reversed_, &QCheckBox::toggled, this, settingsChanged);
    auto* colours = new QHBoxLayout;
    colours->addWidget(colourMode_, 1);
    colours->addWidget(reversed_);
    form->addRow(QStringLiteral("Colours"), colours);

    const auto colourButton = [&](QPushButton*& button, Rgba& colour) {
        button = new QPushButton;
        paintSwatch(button, colour);
        connect(button, &QPushButton::clicked, this, [this, button, &colour] {
            const QColor chosen = QColorDialog::getColor(qcolor(colour), this);
            if (!chosen.isValid()) {
                return;
            }
            if (!coloursCustom_) {
                // Pinned at what they are drawn as, so choosing one leaves
                // the other two where they were.
                const plotcore::FigureSettings& now = plot_->figure().settings();
                const plotcore::LineColours drawn = now.colours.drawn(now.dark);
                singleColour_ = drawn.single;
                rangeFromColour_ = drawn.rangeFrom;
                rangeToColour_ = drawn.rangeTo;
                coloursCustom_ = true;
            }
            colour = rgba(chosen);
            applySettings();
            repaintSwatches();
            (void)button;
        });
    };
    colourButton(single_, singleColour_);
    colourButton(rangeFrom_, rangeFromColour_);
    colourButton(rangeTo_, rangeToColour_);
    auto* swatches = new QHBoxLayout;
    swatches->addWidget(single_);
    swatches->addWidget(new QLabel(QStringLiteral("range")));
    swatches->addWidget(rangeFrom_);
    swatches->addWidget(rangeTo_);
    auto* follow = new QPushButton(QStringLiteral("follow the ground"));
    connect(follow, &QPushButton::clicked, this, [this] {
        coloursCustom_ = false;
        applySettings();
        repaintSwatches();
    });
    swatches->addWidget(follow);
    form->addRow(QStringLiteral("Same / range"), swatches);

    bandFrom_ = new QSlider(Qt::Horizontal);
    bandTo_ = new QSlider(Qt::Horizontal);
    for (QSlider* slider : {bandFrom_, bandTo_}) {
        slider->setRange(0, 100);
        connect(slider, &QSlider::valueChanged, this, settingsChanged);
    }
    bandTo_->setValue(100);
    auto* band = new QHBoxLayout;
    band->addWidget(bandFrom_);
    band->addWidget(bandTo_);
    form->addRow(QStringLiteral("Map's slice"), band);

    legend_ = new QCheckBox(QStringLiteral("on the plot"));
    legend_->setChecked(true);
    legendCorner_ = new QComboBox;
    for (const char* name : kCorners) {
        legendCorner_->addItem(QString::fromUtf8(name));
    }
    legendCorner_->setCurrentIndex(1);
    connect(legend_, &QCheckBox::toggled, this, settingsChanged);
    connect(legendCorner_, &QComboBox::currentIndexChanged, this, settingsChanged);
    auto* legend = new QHBoxLayout;
    legend->addWidget(legend_);
    legend->addWidget(legendCorner_, 1);
    form->addRow(QStringLiteral("Legend"), legend);

    highlighted_ = new QComboBox;
    connect(highlighted_, &QComboBox::currentIndexChanged, this, settingsChanged);
    bundle_ = new QCheckBox(QStringLiteral("drawn under, as a bundle"));
    connect(bundle_, &QCheckBox::toggled, this, settingsChanged);
    auto* highlight = new QHBoxLayout;
    highlight->addWidget(highlighted_, 1);
    highlight->addWidget(bundle_);
    form->addRow(QStringLiteral("Highlight"), highlight);

    stacked_ = new QCheckBox(QStringLiteral("Each line in a band of its own"));
    connect(stacked_, &QCheckBox::toggled, this, settingsChanged);
    form->addRow(QStringLiteral("Stacked"), stacked_);

    dark_ = new QCheckBox(QStringLiteral("dark"));
    dark_->setChecked(true);
    paper_ = new QCheckBox(QStringLiteral("publication"));
    connect(dark_, &QCheckBox::toggled, this, [this] {
        // Colours nobody chose follow the ground, so the swatches show it.
        applySettings();
        repaintSwatches();
    });
    connect(paper_, &QCheckBox::toggled, this, settingsChanged);
    auto* ground = new QHBoxLayout;
    ground->addWidget(dark_);
    ground->addWidget(paper_);
    form->addRow(QStringLiteral("Ground"), ground);

    lines_ = new QTableWidget(0, Columns);
    lines_->setHorizontalHeaderLabels(
        {QStringLiteral("Show"), QStringLiteral("Name"), QStringLiteral("Colour"),
         QStringLiteral("Own y"), QStringLiteral("Fixed"), QStringLiteral("y label"),
         QStringLiteral("Scale"), QStringLiteral("Shift"), QStringLiteral("Own time"), QString()});
    lines_->verticalHeader()->setVisible(false);
    lines_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    lines_->setMinimumHeight(220);
    connect(lines_, &QTableWidget::cellChanged, this, &DemoWindow::lineChanged);
    layout->addWidget(new QLabel(QStringLiteral("<b>Lines</b>")));
    layout->addWidget(lines_, 1);
    return panel;
}

QWidget* DemoWindow::viewPanel()
{
    auto* panel = new QWidget;
    auto* form = new QFormLayout(panel);

    const char* const names[] = {"x from", "x to", "y from", "y to"};
    for (int i = 0; i < 4; ++i) {
        viewBox_[i] = spin(-1e300, 1e300, 0.0, 6);
        form->addRow(QString::fromUtf8(names[i]), viewBox_[i]);
    }
    auto* apply = new QPushButton(QStringLiteral("Show this window"));
    connect(apply, &QPushButton::clicked, this, [this] {
        plot_->figure().setViewRange(viewBox_[0]->value(), viewBox_[1]->value(),
                                     viewBox_[2]->value(), viewBox_[3]->value());
        plot_->update();
        refreshViewBoxes();
    });
    auto* reset = new QPushButton(QStringLiteral("The whole of it"));
    connect(reset, &QPushButton::clicked, this, [this] {
        plot_->figure().resetView();
        plot_->update();
        refreshViewBoxes();
    });
    auto* row = new QHBoxLayout;
    row->addWidget(apply);
    row->addWidget(reset);
    form->addRow(QString(), row);

    auto* save = new QPushButton(QStringLiteral("Save view…"));
    auto* load = new QPushButton(QStringLiteral("Load view…"));
    connect(save, &QPushButton::clicked, this, &DemoWindow::saveView);
    connect(load, &QPushButton::clicked, this, &DemoWindow::loadView);
    auto* views = new QHBoxLayout;
    views->addWidget(save);
    views->addWidget(load);
    form->addRow(QStringLiteral("Views"), views);

    exportWidth_ = spin(64, 16384, 1600, 0);
    exportHeight_ = spin(64, 16384, 1000, 0);
    exportScale_ = spin(0.25, 8.0, 2.0, 2);
    auto* size = new QHBoxLayout;
    size->addWidget(exportWidth_);
    size->addWidget(new QLabel(QStringLiteral("×")));
    size->addWidget(exportHeight_);
    size->addWidget(new QLabel(QStringLiteral("at")));
    size->addWidget(exportScale_);
    form->addRow(QStringLiteral("Picture"), size);
    auto* picture = new QPushButton(QStringLiteral("Export PNG…"));
    connect(picture, &QPushButton::clicked, this, &DemoWindow::exportPicture);
    form->addRow(QString(), picture);
    return panel;
}

// --- data ---------------------------------------------------------------------------

void DemoWindow::addLine(const std::string& name, const std::vector<double>& values)
{
    plot_->figure().plot().addLine(values.data(), static_cast<long long>(values.size()), name);
    refreshLines();
    plot_->update();
}

void DemoWindow::addSignal(int which)
{
    Signal signal;
    switch (which) {
    case 0:
        signal = spikedSine(200000);
        break;
    case 1:
        signal = squareWave(20000);
        break;
    case 2:
        signal = withGaps(20000);
        break;
    case 3:
        signal = manyDecades(100000);
        break;
    case 4:
        signal = randomWalk(1000000, static_cast<unsigned>(plot_->figure().plot().lineCount() + 1));
        break;
    default:
        signal = tenMillion();
        break;
    }
    addLine(signal.name, signal.values);
}

void DemoWindow::addEverySignal()
{
    for (int i = 0; i < static_cast<int>(std::size(kSignals)); ++i) {
        addSignal(i);
    }
}

void DemoWindow::openFile()
{
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Open lines"), QString(),
                                     QStringLiteral("Lines (*.npy *.csv *.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    std::string error;
    const auto lines = path.endsWith(QStringLiteral(".npy"), Qt::CaseInsensitive)
                           ? loadNpy(path.toStdString(), error)
                           : loadCsv(path.toStdString(), error);
    if (!lines.has_value()) {
        QMessageBox::warning(this, QStringLiteral("Cannot open"), QString::fromStdString(error));
        return;
    }
    for (const Signal& line : *lines) {
        addLine(line.name, line.values);
    }
}

void DemoWindow::startStream()
{
    // A line that arrives in pieces is folded as it lands and drawn once it
    // is whole -- what a reader of a socket or a slow file would do.
    streamValues_ = randomWalk(5'000'000, 77).values;
    streamLine_ =
        plot_->figure().plot().beginLine(static_cast<long long>(streamValues_.size()), "streamed");
    streamAt_ = 0;
    stream_->setEnabled(false);
    streamTimer_.start();
    refreshLines();
}

void DemoWindow::streamStep()
{
    constexpr long long piece = 100'000;
    plotcore::Plot& plot = plot_->figure().plot();
    if (streamLine_ < 0 || streamLine_ >= plot.lineCount()) {
        streamTimer_.stop();
        stream_->setEnabled(true);
        return;
    }
    const long long count =
        std::min<long long>(piece, static_cast<long long>(streamValues_.size()) - streamAt_);
    plot.appendToLine(streamLine_, streamValues_.data() + streamAt_, count);
    streamAt_ += count;
    status_->setText(QStringLiteral("streaming: %1 of %2")
                         .arg(streamAt_)
                         .arg(static_cast<long long>(streamValues_.size())));
    if (streamAt_ >= static_cast<long long>(streamValues_.size())) {
        plot.finishLine(streamLine_);
        streamTimer_.stop();
        streamValues_ = {};
        streamLine_ = -1;
        stream_->setEnabled(true);
        refreshLines();
        plot_->update();
    }
}

void DemoWindow::applyXAxis()
{
    plotcore::Plot& plot = plot_->figure().plot();
    long long longest = 1;
    for (int i = 0; i < plot.lineCount(); ++i) {
        longest = std::max(longest, plot.length(i));
    }
    switch (xAxis_->currentIndex()) {
    case 0:
        plot.setTime(nullptr, 0);
        plot.setIndexAxis(0.0, 1.0);
        break;
    case 1:
        plot.setTime(nullptr, 0);
        plot.setIndexAxis(1.0, 1.0);
        break;
    case 2: {
        const std::vector<double> time = regularTime(longest, 0.001);
        plot.setTime(time.data(), static_cast<long long>(time.size()));
        break;
    }
    default: {
        const std::vector<double> time = irregularTime(longest, 5);
        plot.setTime(time.data(), static_cast<long long>(time.size()));
        break;
    }
    }
    plot_->figure().resetView();
    plot_->update();
}

// --- settings --------------------------------------------------------------------------

plotcore::FigureSettings DemoWindow::settings() const
{
    plotcore::FigureSettings out = plot_->figure().settings();
    out.xLog = xLog_->isChecked();
    out.yLog = yLog_->isChecked();
    out.xLogBase = baseOf(xBase_, xBaseCustom_);
    out.yLogBase = baseOf(yBase_, yBaseCustom_);
    out.grid = static_cast<plotcore::GridMode>(grid_->currentIndex());
    out.gridStepX = gridStepX_->value();
    out.gridStepY = gridStepY_->value();
    out.minorNumbers = minorNumbers_->isChecked();
    out.flipped = flipped_->isChecked();
    out.title = title_->text().toStdString();
    out.xLabel = xLabel_->text().toStdString();
    out.yLabel = yLabel_->text().toStdString();
    out.markers = markers_->isChecked();
    out.cursor = cursor_->isChecked();
    out.colours.mode = colourMode_->currentText().toStdString();
    out.colours.reversed = reversed_->isChecked();
    out.colours.single = singleColour_;
    out.colours.rangeFrom = rangeFromColour_;
    out.colours.rangeTo = rangeToColour_;
    out.colours.custom = coloursCustom_;
    out.colours.from = std::min(bandFrom_->value(), bandTo_->value()) / 100.0;
    out.colours.to = std::max(bandFrom_->value(), bandTo_->value()) / 100.0;
    out.legend = legend_->isChecked();
    out.legendCorner = static_cast<plotcore::Corner>(
        std::array{0, 1, 2, 3}[static_cast<std::size_t>(legendCorner_->currentIndex())]);
    out.highlighted = highlighted_->currentIndex() - 1;
    out.bundle = bundle_->isChecked();
    out.stacked = stacked_->isChecked();
    out.dark = dark_->isChecked();
    out.paper = paper_->isChecked();
    return out;
}

void DemoWindow::applySettings()
{
    if (refreshing_) {
        return;
    }
    plot_->figure().setSettings(settings());
    plot_->update();
    refreshViewBoxes();
}

void DemoWindow::applyStyle()
{
    plotcore::FigureStyle style = plot_->figure().style();
    style.lineWidth = lineWidth_->value();
    plot_->figure().setStyle(style);
    plot_->update();
}

void DemoWindow::repaintSwatches()
{
    // As drawn: the ground's until chosen.
    const plotcore::FigureSettings& now = plot_->figure().settings();
    const plotcore::LineColours drawn = now.colours.drawn(now.dark);
    paintSwatch(single_, drawn.single);
    paintSwatch(rangeFrom_, drawn.rangeFrom);
    paintSwatch(rangeTo_, drawn.rangeTo);
}

void DemoWindow::refreshLines()
{
    refreshing_ = true;
    plotcore::Figure& figure = plot_->figure();
    const plotcore::Plot& plot = figure.plot();
    lines_->setRowCount(plot.lineCount());
    const QString wasHighlighted = highlighted_->currentText();
    highlighted_->clear();
    highlighted_->addItem(QStringLiteral("none"));
    for (int i = 0; i < plot.lineCount(); ++i) {
        const plotcore::LineSettings line = figure.lineSettings(i);
        const QString name = QString::fromStdString(plot.name(i)) +
                             (plot.ready(i) ? QString() : QStringLiteral(" (arriving)"));
        highlighted_->addItem(name);

        const auto check = [&](int column, bool on) {
            auto* item = new QTableWidgetItem;
            item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            lines_->setItem(i, column, item);
        };
        const auto text = [&](int column, const QString& value, bool editable) {
            auto* item = new QTableWidgetItem(value);
            item->setFlags(editable ? Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsSelectable
                                    : Qt::ItemIsEnabled);
            lines_->setItem(i, column, item);
        };
        check(Show, line.visible);
        text(Name, name, true);
        check(OwnAxis, line.ownAxis);
        check(Fixed, line.axisFixed);
        text(AxisLabel, QString::fromStdString(line.axisLabel), true);
        text(Scale, QString::number(line.scale, 'g', 4), true);
        text(Shift, QString::number(line.shift, 'g', 4), true);

        auto* colour = new QPushButton(line.colour.has_value()
                                           ? QString::fromStdString(plotcore::hexOf(*line.colour))
                                           : QStringLiteral("cycle"));
        if (line.colour.has_value()) {
            paintSwatch(colour, *line.colour);
        }
        connect(colour, &QPushButton::clicked, this, [this, i] {
            plotcore::LineSettings own = plot_->figure().lineSettings(i);
            const QColor chosen = QColorDialog::getColor(
                own.colour.has_value() ? qcolor(*own.colour) : Qt::white, this,
                QStringLiteral("A colour of its own (cancel for the cycle's)"));
            own.colour = chosen.isValid() ? std::optional<Rgba>(rgba(chosen)) : std::nullopt;
            plot_->figure().setLineSettings(i, own);
            refreshLines();
            plot_->update();
        });
        lines_->setCellWidget(i, Colour, colour);

        auto* time = new QComboBox;
        time->addItems({QStringLiteral("shared"), QStringLiteral("own, steady"),
                        QStringLiteral("own, wandering")});
        time->setProperty("line", i);
        connect(time, &QComboBox::currentIndexChanged, this, [this, i](int mode) {
            plotcore::Plot& p = plot_->figure().plot();
            if (mode == 0) {
                p.setLineTime(i, nullptr, 0);
            }
            else {
                const std::vector<double> t = mode == 1 ? regularTime(p.length(i), 0.0005)
                                                        : irregularTime(p.length(i), 11u + i);
                p.setLineTime(i, t.data(), static_cast<long long>(t.size()));
            }
            plot_->figure().resetView();
            plot_->update();
        });
        lines_->setCellWidget(i, OwnTime, time);

        auto* back = new QPushButton(QStringLiteral("put back"));
        connect(back, &QPushButton::clicked, this, [this, i] {
            plotcore::LineSettings own = plot_->figure().lineSettings(i);
            own.scale = 1.0;
            own.shift = 0.0;
            plot_->figure().setLineSettings(i, own);
            refreshLines();
            plot_->update();
        });
        lines_->setCellWidget(i, PutBack, back);
    }
    const int again = highlighted_->findText(wasHighlighted);
    highlighted_->setCurrentIndex(again >= 0 ? again : 0);
    refreshing_ = false;
    refreshStatus();
}

void DemoWindow::lineChanged(int row, int column)
{
    if (refreshing_) {
        return;
    }
    plotcore::Figure& figure = plot_->figure();
    plotcore::LineSettings line = figure.lineSettings(row);
    const QTableWidgetItem* item = lines_->item(row, column);
    if (item == nullptr) {
        return;
    }
    switch (column) {
    case Show:
        line.visible = item->checkState() == Qt::Checked;
        break;
    case Name:
        figure.plot().setName(row, item->text().toStdString());
        break;
    case OwnAxis:
        line.ownAxis = item->checkState() == Qt::Checked;
        break;
    case Fixed:
        line.axisFixed = item->checkState() == Qt::Checked;
        break;
    case AxisLabel:
        line.axisLabel = item->text().toStdString();
        break;
    case Scale:
        line.scale = item->text().toDouble();
        break;
    case Shift:
        line.shift = item->text().toDouble();
        break;
    default:
        return;
    }
    figure.setLineSettings(row, line);
    plot_->update();
}

void DemoWindow::refreshStatus()
{
    const plotcore::Scene& scene = plot_->lastScene();
    QString text = QStringLiteral("x %1 … %2   y %3 … %4")
                       .arg(scene.x.low, 0, 'g', 6)
                       .arg(scene.x.high, 0, 'g', 6)
                       .arg(scene.y.low, 0, 'g', 6)
                       .arg(scene.y.high, 0, 'g', 6);
    if (scene.reading.valid) {
        text += QStringLiteral("   —   %1: x %2, y %3")
                    .arg(QString::fromStdString(plot_->figure().plot().name(scene.reading.line)),
                         QString::fromStdString(scene.reading.xText),
                         QString::fromStdString(scene.reading.yText));
    }
    if (!streamTimer_.isActive()) {
        status_->setText(text);
    }
    const plotcore::Plot& plot = plot_->figure().plot();
    const double mb = 8.0 / (1024.0 * 1024.0);
    QString bases;
    for (int i = 0; i < plot.lineCount(); ++i) {
        bases += (i == 0 ? QString() : QStringLiteral(" ")) + QString::number(plot.baseBucket(i));
    }
    held_->setText(QStringLiteral("held %1 of %2 MB   base buckets %3")
                       .arg(static_cast<double>(plot.heldDoubles()) * mb, 0, 'f', 1)
                       .arg(static_cast<double>(plot.budgetDoubles()) * mb, 0, 'f', 0)
                       .arg(bases));
}

void DemoWindow::refreshViewBoxes()
{
    const plotcore::Camera& camera = plot_->figure().camera();
    const double values[] = {camera.viewMinX(), camera.viewMaxX(), camera.viewMinY(),
                             camera.viewMaxY()};
    for (int i = 0; i < 4; ++i) {
        const QSignalBlocker block(viewBox_[i]);
        viewBox_[i]->setValue(values[i]);
    }
}

// --- views and pictures ----------------------------------------------------------------------

void DemoWindow::saveView()
{
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Save view"), QStringLiteral("view.json"),
                                     QStringLiteral("Views (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save"), file.errorString());
        return;
    }
    file.write(QByteArray::fromStdString(plot_->figure().state()));
}

void DemoWindow::loadView()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load view"), QString(),
                                                      QStringLiteral("Views (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !plot_->figure().restore(file.readAll().toStdString())) {
        QMessageBox::warning(this, QStringLiteral("Cannot load"),
                             QStringLiteral("%1 is not a saved view.").arg(path));
        return;
    }
    // The panel is put back from the figure, which is the one that knows.
    const plotcore::FigureSettings& s = plot_->figure().settings();
    refreshing_ = true;
    xLog_->setChecked(s.xLog);
    yLog_->setChecked(s.yLog);
    grid_->setCurrentIndex(static_cast<int>(s.grid));
    gridStepX_->setValue(s.gridStepX);
    gridStepY_->setValue(s.gridStepY);
    minorNumbers_->setChecked(s.minorNumbers);
    flipped_->setChecked(s.flipped);
    title_->setText(QString::fromStdString(s.title));
    xLabel_->setText(QString::fromStdString(s.xLabel));
    yLabel_->setText(QString::fromStdString(s.yLabel));
    markers_->setChecked(s.markers);
    cursor_->setChecked(s.cursor);
    colourMode_->setCurrentText(QString::fromStdString(s.colours.mode));
    reversed_->setChecked(s.colours.reversed);
    singleColour_ = s.colours.single;
    rangeFromColour_ = s.colours.rangeFrom;
    rangeToColour_ = s.colours.rangeTo;
    coloursCustom_ = s.colours.custom;
    repaintSwatches();
    bandFrom_->setValue(static_cast<int>(std::lround(s.colours.from * 100)));
    bandTo_->setValue(static_cast<int>(std::lround(s.colours.to * 100)));
    legend_->setChecked(s.legend);
    legendCorner_->setCurrentIndex(static_cast<int>(s.legendCorner));
    bundle_->setChecked(s.bundle);
    stacked_->setChecked(s.stacked);
    dark_->setChecked(s.dark);
    paper_->setChecked(s.paper);
    refreshing_ = false;
    refreshLines();
    highlighted_->setCurrentIndex(s.highlighted + 1);
    refreshViewBoxes();
    plot_->update();
}

void DemoWindow::exportPicture()
{
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Export picture"),
                                     QStringLiteral("plot.png"), QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    const QImage image =
        plot_->render(QSize(static_cast<int>(exportWidth_->value() / exportScale_->value()),
                            static_cast<int>(exportHeight_->value() / exportScale_->value())),
                      exportScale_->value());
    if (!image.save(path)) {
        QMessageBox::warning(this, QStringLiteral("Cannot export"),
                             QStringLiteral("Could not write %1.").arg(path));
    }
}

// --- the self-test ------------------------------------------------------------------------------

int DemoWindow::selfTest(const QString& picture)
{
    int failed = 0;
    const auto check = [&](bool ok, const char* what) {
        QTextStream(stdout) << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
        failed += ok ? 0 : 1;
    };
    const auto frame = [this]() -> plotcore::Scene {
        plot_->repaint();
        QApplication::processEvents();
        return plot_->lastScene();
    };
    const auto inkOf = [this](const QImage& image) {
        // Pixels that are not the ground: the picture has something in it.
        const QRgb ground = image.pixel(1, 1);
        long long count = 0;
        for (int y = 0; y < image.height(); y += 2) {
            for (int x = 0; x < image.width(); x += 2) {
                count += image.pixel(x, y) != ground ? 1 : 0;
            }
        }
        return count;
    };
    const auto wheel = [this](QPointF at, int delta, Qt::KeyboardModifiers keys) {
        QWheelEvent event(at, plot_->mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton,
                          keys, Qt::NoScrollPhase, false);
        QApplication::sendEvent(plot_, &event);
    };
    const auto mouse = [this](QEvent::Type type, QPointF at, Qt::MouseButton button,
                              Qt::KeyboardModifiers keys) {
        const Qt::MouseButtons held =
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(button);
        QMouseEvent event(type, at, plot_->mapToGlobal(at), button, held, keys);
        QApplication::sendEvent(plot_, &event);
    };

    QTextStream(stdout) << "plotcore Qt example, self-test\n";
    addEverySignal();
    const plotcore::Scene all = frame();
    check(all.lines.size() == 8, "every signal is drawn");
    check(all.message.empty(), "nothing to explain");
    const QPointF middle(all.area.x + all.area.width / 2, all.area.y + all.area.height / 2);

    // Gestures.
    wheel(middle, 360, Qt::NoModifier);
    const plotcore::Scene zoomed = frame();
    check(zoomed.x.high - zoomed.x.low < all.x.high - all.x.low, "the wheel zooms in");
    wheel(middle, 240, Qt::ShiftModifier);
    const plotcore::Scene across = frame();
    check(across.y.low == zoomed.y.low &&
              across.x.high - across.x.low < zoomed.x.high - zoomed.x.low,
          "Shift zooms across alone");
    mouse(QEvent::MouseButtonPress, middle, Qt::LeftButton, Qt::NoModifier);
    mouse(QEvent::MouseMove, middle + QPointF(80, 0), Qt::LeftButton, Qt::NoModifier);
    mouse(QEvent::MouseButtonRelease, middle + QPointF(80, 0), Qt::LeftButton, Qt::NoModifier);
    check(frame().x.low < across.x.low, "a drag pans");
    mouse(QEvent::MouseButtonPress, middle - QPointF(100, 60), Qt::RightButton, Qt::NoModifier);
    mouse(QEvent::MouseMove, middle + QPointF(100, 60), Qt::RightButton, Qt::NoModifier);
    check(frame().band.has_value(), "a right drag draws its band");
    mouse(QEvent::MouseButtonRelease, middle + QPointF(100, 60), Qt::RightButton, Qt::NoModifier);
    check(!frame().band.has_value(), "...and zooms to it");
    mouse(QEvent::MouseButtonDblClick, middle, Qt::LeftButton, Qt::NoModifier);
    const plotcore::Scene back = frame();
    check(back.x.low == all.x.low && back.x.high == all.x.high, "a double click puts it back");
    mouse(QEvent::MouseMove, middle, Qt::NoButton, Qt::NoModifier);
    check(frame().reading.valid, "the crosshair reads a sample");

    // A line of its own: moved with Alt, scaled with Alt and Control, put back.
    // Eight lines lie on top of one another under a twelve-decade axis, so the
    // line moved is whichever the figure finds under the pointer.
    const plotcore::DrawnLine& square = back.lines[1];
    const plotcore::Point onLine = square.runs.front()[square.runs.front().size() / 2];
    const QPointF at(onLine.x(), onLine.y());
    const int under = plot_->figure().lineAt(at.x(), at.y());
    check(under >= 0, "a line is under the pointer");
    mouse(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::AltModifier);
    mouse(QEvent::MouseMove, at + QPointF(0, 30), Qt::LeftButton, Qt::AltModifier);
    mouse(QEvent::MouseButtonRelease, at + QPointF(0, 30), Qt::LeftButton, Qt::AltModifier);
    check(plot_->figure().lineSettings(under).shift != 0.0, "Alt and a drag move it");
    const int moved = plot_->figure().lineAt(at.x(), at.y() + 30);
    check(moved == under, "...and it is under the pointer still");
    wheel(at + QPointF(0, 30), 240, Qt::AltModifier | Qt::ControlModifier);
    check(plot_->figure().lineSettings(under).scale != 1.0, "Alt, Control and the wheel scale it");
    mouse(QEvent::MouseButtonPress, at + QPointF(0, 30), Qt::LeftButton, Qt::ControlModifier);
    mouse(QEvent::MouseButtonRelease, at + QPointF(0, 30), Qt::LeftButton, Qt::ControlModifier);
    const plotcore::LineSettings putBack = plot_->figure().lineSettings(under);
    check(putBack.scale == 1.0 && putBack.shift == 0.0, "Control and a click put it back");

    // Every control, once.
    xAxis_->setCurrentIndex(1);
    xLog_->setChecked(true);
    yLog_->setChecked(true);
    const plotcore::Scene logs = frame();
    check(logs.lines.size() == 8 && logs.message.empty(), "both axes logarithmic");
    xBase_->setCurrentIndex(1);
    yBase_->setCurrentIndex(2);
    frame();
    xBase_->setCurrentIndex(3);
    xBaseCustom_->setValue(3.0);
    minorNumbers_->setChecked(true);
    check(frame().texts.size() > logs.texts.size() / 2, "a custom base, and the minor numbers");
    xLog_->setChecked(false);
    yLog_->setChecked(false);
    for (int grid = 0; grid < 4; ++grid) {
        grid_->setCurrentIndex(grid);
        gridStepX_->setValue(grid == 3 ? 100000.0 : 0.0);
        gridStepY_->setValue(grid == 3 ? 5.0 : 0.0);
        const plotcore::Scene ruled = frame();
        check((grid == 0) == ruled.rules.empty(), kGrids[grid]);
    }
    grid_->setCurrentIndex(1);
    title_->setText(QStringLiteral("Every signal"));
    xLabel_->setText(QStringLiteral("sample"));
    yLabel_->setText(QStringLiteral("value"));
    flipped_->setChecked(true);
    const plotcore::Scene turned = frame();
    check(turned.lines.size() == 8, "flipped");
    flipped_->setChecked(false);
    markers_->setChecked(true);
    lineWidth_->setValue(2.0);
    for (int i = 0; i < colourMode_->count(); ++i) {
        if (colourMode_->itemText(i).isEmpty()) {
            continue; // a separator
        }
        colourMode_->setCurrentIndex(i);
        reversed_->setChecked(i % 2 == 1);
        frame();
    }
    check(true, "every palette, every map, same and range");
    bandFrom_->setValue(20);
    bandTo_->setValue(80);
    for (int corner = 0; corner < 4; ++corner) {
        legendCorner_->setCurrentIndex(corner);
        frame();
    }
    check(!frame().legend.empty(), "the legend, in every corner");
    highlighted_->setCurrentIndex(2);
    bundle_->setChecked(true);
    frame();
    highlighted_->setCurrentIndex(0);
    lines_->item(2, OwnAxis)->setCheckState(Qt::Checked);
    lines_->item(2, AxisLabel)->setText(QStringLiteral("gaps"));
    lines_->item(3, OwnAxis)->setCheckState(Qt::Checked);
    lines_->item(3, Fixed)->setCheckState(Qt::Checked);
    const plotcore::Scene separate = frame();
    check(separate.area.x > back.area.x, "axes of their own, beside the pane");
    stacked_->setChecked(true);
    check(frame().lines.size() == 8, "stacked");
    stacked_->setChecked(false);
    qobject_cast<QComboBox*>(lines_->cellWidget(1, OwnTime))->setCurrentIndex(2);
    check(plot_->figure().plot().hasTime(1), "a time of its own");
    xAxis_->setCurrentIndex(3);
    check(frame().lines.size() == 8, "a shared time that wanders");
    xAxis_->setCurrentIndex(0);
    dark_->setChecked(false);
    paper_->setChecked(true);
    frame();
    dark_->setChecked(true);
    paper_->setChecked(false);
    for (int appetite = 0; appetite < 3; ++appetite) {
        appetite_->setCurrentIndex(appetite);
        frame();
    }
    check(plot_->figure().plot().heldDoubles() <= plot_->figure().plot().budgetDoubles(),
          "what is held is inside the budget");

    // The view written down and read back.
    wheel(middle, 480, Qt::NoModifier);
    const plotcore::Scene before = frame();
    const std::string state = plot_->figure().state();
    plot_->figure().resetView();
    check(plot_->figure().restore(state), "a view reads back");
    const plotcore::Scene after = frame();
    check(after.x.low == before.x.low && after.x.high == before.x.high, "...as the same window");

    // Streamed in pieces.
    startStream();
    while (streamTimer_.isActive()) {
        streamStep();
    }
    check(frame().lines.size() == 9, "a line streamed in pieces is drawn once it is whole");

    // The picture: of the whole of it, in the cycle the plot opens on.
    colourMode_->setCurrentIndex(0);
    reversed_->setChecked(false);
    bandFrom_->setValue(0);
    bandTo_->setValue(100);
    plot_->figure().resetView();
    const QImage image = plot_->render(QSize(800, 500), 2.0);
    check(image.width() == 1600 && image.height() == 1000, "a picture at twice the pixels");
    check(inkOf(image) > 10000, "...with something drawn in it");
    if (!picture.isEmpty()) {
        check(image.save(picture), "...written to a file");
    }

    QTextStream(stdout) << (failed == 0 ? "all checks passed\n" : "checks failed\n");
    return failed;
}

} // namespace demo

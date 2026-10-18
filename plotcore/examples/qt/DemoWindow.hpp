// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The whole of plotcore on one window: the plot in the middle, and beside it a
// control for every setting a figure has, a row for every line, and the view
// written down, read back and exported. Every control writes the figure and
// reads it back after a gesture, so what is on the panel is what is drawn.

#include "plotcore/qt/PlotWidget.hpp"

#include <QMainWindow>
#include <QTimer>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTableWidget;

namespace demo {

class DemoWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DemoWindow(QWidget* parent = nullptr);

    [[nodiscard]] plotcore::qt::PlotWidget* plot() const { return plot_; }

    /// Add every signal the Data panel offers, as a reader clicking down the
    /// list would.
    void addEverySignal();
    /// Walk every control and every gesture once, offscreen, and check that
    /// each one moved the picture it says it moves. What CI runs; prints what
    /// it checked and returns the number of checks that failed.
    int selfTest(const QString& picture);

private:
    QWidget* dataPanel();
    QWidget* axesPanel();
    QWidget* linesPanel();
    QWidget* viewPanel();

    void addSignal(int which);
    void addLine(const std::string& name, const std::vector<double>& values);
    void openFile();
    void startStream();
    void streamStep();
    void applyXAxis();
    void applySettings();
    void applyStyle();
    void refreshLines();
    void refreshStatus();
    void refreshViewBoxes();
    void lineChanged(int row, int column);
    void saveView();
    void loadView();
    void exportPicture();
    /// Choose one of the three colours, pinning the other two at what they
    /// are drawn as -- the swatch's click without its dialog.
    void chooseColour(plotcore::Rgba& colour, plotcore::Rgba chosen);
    /// Give the three colours back to the ground.
    void followGround();
    void repaintSwatches();

    plotcore::FigureSettings settings() const;

    plotcore::qt::PlotWidget* plot_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* held_ = nullptr;
    bool refreshing_ = false;

    // Data.
    QComboBox* signal_ = nullptr;
    QComboBox* xAxis_ = nullptr;
    QComboBox* appetite_ = nullptr;
    QPushButton* stream_ = nullptr;
    QTimer streamTimer_;
    int streamLine_ = -1;
    long long streamAt_ = 0;
    std::vector<double> streamValues_;

    // Axes.
    QCheckBox* xLog_ = nullptr;
    QCheckBox* yLog_ = nullptr;
    QComboBox* xBase_ = nullptr;
    QComboBox* yBase_ = nullptr;
    QDoubleSpinBox* xBaseCustom_ = nullptr;
    QDoubleSpinBox* yBaseCustom_ = nullptr;
    QComboBox* grid_ = nullptr;
    QDoubleSpinBox* gridStepX_ = nullptr;
    QDoubleSpinBox* gridStepY_ = nullptr;
    QCheckBox* minorNumbers_ = nullptr;
    QCheckBox* flipped_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* xLabel_ = nullptr;
    QLineEdit* yLabel_ = nullptr;

    // Lines.
    QCheckBox* markers_ = nullptr;
    QCheckBox* cursor_ = nullptr;
    QDoubleSpinBox* lineWidth_ = nullptr;
    QComboBox* colourMode_ = nullptr;
    QPushButton* single_ = nullptr;
    QPushButton* rangeFrom_ = nullptr;
    QPushButton* rangeTo_ = nullptr;
    QCheckBox* reversed_ = nullptr;
    QSlider* bandFrom_ = nullptr;
    QSlider* bandTo_ = nullptr;
    QCheckBox* legend_ = nullptr;
    QComboBox* legendCorner_ = nullptr;
    QComboBox* highlighted_ = nullptr;
    QCheckBox* bundle_ = nullptr;
    QCheckBox* stacked_ = nullptr;
    QCheckBox* dark_ = nullptr;
    QCheckBox* paper_ = nullptr;
    QTableWidget* lines_ = nullptr;

    // View.
    QDoubleSpinBox* viewBox_[4] = {};
    QDoubleSpinBox* exportWidth_ = nullptr;
    QDoubleSpinBox* exportHeight_ = nullptr;
    QDoubleSpinBox* exportScale_ = nullptr;

    /// Whether the three colours below were chosen; until they are, the
    /// ground's are drawn (LineColours::custom).
    bool coloursCustom_ = false;
    plotcore::Rgba singleColour_;
    plotcore::Rgba rangeFromColour_;
    plotcore::Rgba rangeToColour_;
};

} // namespace demo

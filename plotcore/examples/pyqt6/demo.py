# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""The whole of plotcore on one PyQt6 window.

    python demo.py                     the window
    python demo.py --self-test [png]   every control and gesture, checked

The plot in the middle, and beside it a control for every setting a figure has,
a row for every line, the view written down and read back, a PNG, and the same
figure opened in matplotlib. Every control writes the figure and reads it back
after a gesture, so what is on the panel is what is drawn.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import List, Optional

import numpy as np
from PyQt6.QtCore import QPointF, Qt, QTimer
from PyQt6.QtGui import QColor, QMouseEvent, QWheelEvent
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QColorDialog,
    QComboBox,
    QDialog,
    QDockWidget,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSlider,
    QTableWidget,
    QTableWidgetItem,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

sys.path.insert(0, str(Path(__file__).resolve().parent))

import plotcore  # noqa: E402
from plotcore.qt import PlotWidget  # noqa: E402
from signals import SIGNALS, irregular_time, load, random_walk, regular_time  # noqa: E402

GRIDS = ["none", "loose", "dense", "custom"]
CORNERS = ["top-left", "top-right", "bottom-left", "bottom-right"]
BASES = ["10", "2", "e", "custom"]
COLUMNS = ["Show", "Name", "Colour", "Own y", "Fixed", "y label", "Scale", "Shift", "Own time", ""]
SHOW, NAME, COLOUR, OWN, FIXED, LABEL, SCALE, SHIFT, TIME, BACK = range(len(COLUMNS))


def qcolor(colour) -> QColor:
    return QColor.fromRgbF(*(float(c) for c in colour))


def spin(low: float, high: float, value: float, decimals: int = 2) -> QDoubleSpinBox:
    box = QDoubleSpinBox()
    box.setRange(low, high)
    box.setDecimals(decimals)
    box.setValue(value)
    box.setKeyboardTracking(False)
    return box


def paint_swatch(button: QPushButton, colour) -> None:
    c = qcolor(colour)
    button.setText(c.name())
    button.setStyleSheet(f"background-color: {c.name()}; color: "
                         f"{'black' if c.lightnessF() > 0.5 else 'white'}")


class DemoWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("plotcore — PyQt6")
        self.plot = PlotWidget()
        self.figure = self.plot.figure
        self.setCentralWidget(self.plot)
        self.refreshing = True
        tabs = QTabWidget()
        tabs.addTab(self._data_panel(), "Data")
        tabs.addTab(self._axes_panel(), "Axes")
        tabs.addTab(self._lines_panel(), "Lines")
        tabs.addTab(self._view_panel(), "View")
        scroll = QScrollArea()
        scroll.setWidget(tabs)
        scroll.setWidgetResizable(True)
        dock = QDockWidget("Settings")
        dock.setWidget(scroll)
        dock.setFeatures(QDockWidget.DockWidgetFeature.DockWidgetMovable
                         | QDockWidget.DockWidgetFeature.DockWidgetFloatable)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, dock)
        self.status = QLabel()
        self.held = QLabel()
        self.statusBar().addWidget(self.status, 1)
        self.statusBar().addPermanentWidget(self.held)
        self.plot.changed.connect(self._after_gesture)
        self.readout = QTimer(self, interval=100, timeout=self._refresh_status)
        self.readout.start()
        self.stream_timer = QTimer(self, interval=15, timeout=self._stream_step)
        self.stream_line = -1
        self.stream_at = 0
        self.stream_values: Optional[np.ndarray] = None
        self.mpl_dialog: Optional[QDialog] = None
        self.refreshing = False
        self._apply_settings()
        self.resize(1400, 820)
        self.add_signal(0)
        self.add_signal(1)

    # --- panels -------------------------------------------------------------

    def _data_panel(self) -> QWidget:
        panel = QWidget()
        form = QFormLayout(panel)
        self.signal = QComboBox()
        self.signal.addItems([name for name, _ in SIGNALS])
        add = QPushButton("Add")
        add.clicked.connect(lambda: self.add_signal(self.signal.currentIndex()))
        row = QHBoxLayout()
        row.addWidget(self.signal, 1)
        row.addWidget(add)
        form.addRow("Signal", row)
        self.stream = QPushButton("Stream five million samples")
        self.stream.clicked.connect(self.start_stream)
        form.addRow("", self.stream)
        open_button = QPushButton("Open .npy or CSV…")
        open_button.clicked.connect(self.open_file)
        form.addRow("", open_button)
        clear = QPushButton("Remove every line")
        clear.clicked.connect(self.clear_lines)
        form.addRow("", clear)
        self.x_axis = QComboBox()
        self.x_axis.addItems(["Index from 0", "Index from 1", "Shared time, steady",
                              "Shared time, wandering"])
        self.x_axis.currentIndexChanged.connect(self.apply_x_axis)
        form.addRow("x", self.x_axis)
        self.appetite = QComboBox()
        self.appetite.addItems(["low", "medium", "greedy"])
        self.appetite.setCurrentIndex(1)
        self.appetite.currentTextChanged.connect(self._set_appetite)
        form.addRow("RAM budget", self.appetite)
        help_text = QLabel(
            "<b>Wheel</b> zooms about the pointer; <b>Shift</b> across, <b>Control</b> up.<br>"
            "<b>Drag</b> pans, past the data but not off it.<br>"
            "<b>Right-drag</b> zooms to a band. <b>Double-click</b> puts the view back.<br>"
            "<b>Alt-drag</b> or <b>middle-drag</b> moves a line; <b>Alt+Control+wheel</b> "
            "scales it; <b>Control-click</b> puts it back.")
        help_text.setWordWrap(True)
        form.addRow(help_text)
        return panel

    def _axes_panel(self) -> QWidget:
        panel = QWidget()
        form = QFormLayout(panel)
        changed = self._apply_settings

        def log_row(name: str):
            log = QCheckBox("logarithmic")
            base = QComboBox()
            base.addItems(BASES)
            custom = spin(1.01, 1000.0, 3.0)
            custom.setEnabled(False)
            log.toggled.connect(changed)
            base.currentIndexChanged.connect(lambda i: (custom.setEnabled(i == 3), changed()))
            custom.valueChanged.connect(changed)
            row = QHBoxLayout()
            for widget in (log, QLabel("base"), base, custom):
                row.addWidget(widget)
            form.addRow(name, row)
            return log, base, custom

        self.x_log, self.x_base, self.x_custom = log_row("x")
        self.y_log, self.y_base, self.y_custom = log_row("y")
        self.grid = QComboBox()
        self.grid.addItems(GRIDS)
        self.grid.setCurrentIndex(1)
        self.grid.currentIndexChanged.connect(changed)
        form.addRow("Grid", self.grid)
        self.step_x = spin(0.0, 1e12, 0.0, 4)
        self.step_y = spin(0.0, 1e12, 0.0, 4)
        self.step_x.valueChanged.connect(changed)
        self.step_y.valueChanged.connect(changed)
        steps = QHBoxLayout()
        for widget in (QLabel("x"), self.step_x, QLabel("y"), self.step_y):
            steps.addWidget(widget)
        form.addRow("Custom step", steps)
        self.minor_numbers = QCheckBox("Number what lies between powers")
        self.minor_numbers.toggled.connect(changed)
        form.addRow("", self.minor_numbers)
        self.flipped = QCheckBox("Flipped: x up, y across")
        self.flipped.toggled.connect(changed)
        form.addRow("", self.flipped)
        self.title = QLineEdit()
        self.x_label = QLineEdit()
        self.y_label = QLineEdit()
        for edit, name in ((self.title, "Title"), (self.x_label, "x label"),
                           (self.y_label, "y label")):
            edit.textChanged.connect(changed)
            form.addRow(name, edit)
        return panel

    def _lines_panel(self) -> QWidget:
        panel = QWidget()
        layout = QVBoxLayout(panel)
        form = QFormLayout()
        layout.addLayout(form)
        changed = self._apply_settings
        self.markers = QCheckBox("Markers")
        self.cursor = QCheckBox("Crosshair")
        self.cursor.setChecked(True)
        self.markers.toggled.connect(changed)
        self.cursor.toggled.connect(changed)
        self.line_width = spin(0.5, 8.0, 1.0, 1)
        self.line_width.setSingleStep(0.5)
        self.line_width.valueChanged.connect(self._apply_style)
        row = QHBoxLayout()
        for widget in (self.markers, self.cursor, QLabel("width"), self.line_width):
            row.addWidget(widget)
        form.addRow("Draw", row)

        self.colour_mode = QComboBox()
        self.colour_mode.addItems(plotcore.palette_names())
        self.colour_mode.insertSeparator(self.colour_mode.count())
        self.colour_mode.addItems(plotcore.ramp_names())
        self.colour_mode.insertSeparator(self.colour_mode.count())
        self.colour_mode.addItems(["same", "range"])
        self.colour_mode.currentIndexChanged.connect(changed)
        self.reversed = QCheckBox("reversed")
        self.reversed.toggled.connect(changed)
        colours = QHBoxLayout()
        colours.addWidget(self.colour_mode, 1)
        colours.addWidget(self.reversed)
        form.addRow("Colours", colours)

        self.swatches = {}
        swatches = QHBoxLayout()
        for name in ("colours.single", "colours.rangeFrom", "colours.rangeTo"):
            button = QPushButton()
            paint_swatch(button, self.figure.settings[name])
            button.clicked.connect(lambda _=False, n=name: self._pick_colour(n))
            self.swatches[name] = button
            swatches.addWidget(button)
        follow = QPushButton("follow the ground")
        follow.clicked.connect(self._follow_ground)
        swatches.addWidget(follow)
        form.addRow("Same / range", swatches)

        self.band_from = QSlider(Qt.Orientation.Horizontal)
        self.band_to = QSlider(Qt.Orientation.Horizontal)
        for slider in (self.band_from, self.band_to):
            slider.setRange(0, 100)
            slider.valueChanged.connect(changed)
        self.band_to.setValue(100)
        band = QHBoxLayout()
        band.addWidget(self.band_from)
        band.addWidget(self.band_to)
        form.addRow("Map's slice", band)

        self.legend = QCheckBox("on the plot")
        self.legend.setChecked(True)
        self.legend_corner = QComboBox()
        self.legend_corner.addItems(CORNERS)
        self.legend_corner.setCurrentIndex(1)
        self.legend.toggled.connect(changed)
        self.legend_corner.currentIndexChanged.connect(changed)
        legend = QHBoxLayout()
        legend.addWidget(self.legend)
        legend.addWidget(self.legend_corner, 1)
        form.addRow("Legend", legend)

        self.highlighted = QComboBox()
        self.highlighted.currentIndexChanged.connect(changed)
        self.bundle = QCheckBox("drawn under, as a bundle")
        self.bundle.toggled.connect(changed)
        highlight = QHBoxLayout()
        highlight.addWidget(self.highlighted, 1)
        highlight.addWidget(self.bundle)
        form.addRow("Highlight", highlight)
        self.stacked = QCheckBox("Each line in a band of its own")
        self.stacked.toggled.connect(changed)
        form.addRow("Stacked", self.stacked)
        self.dark = QCheckBox("dark")
        self.dark.setChecked(True)
        self.paper = QCheckBox("publication")
        self.dark.toggled.connect(self._ground_changed)
        self.paper.toggled.connect(changed)
        ground = QHBoxLayout()
        ground.addWidget(self.dark)
        ground.addWidget(self.paper)
        form.addRow("Ground", ground)

        self.lines = QTableWidget(0, len(COLUMNS))
        self.lines.setHorizontalHeaderLabels(COLUMNS)
        self.lines.verticalHeader().setVisible(False)
        self.lines.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.ResizeToContents)
        self.lines.setMinimumHeight(220)
        self.lines.cellChanged.connect(self._line_changed)
        layout.addWidget(QLabel("<b>Lines</b>"))
        layout.addWidget(self.lines, 1)
        return panel

    def _view_panel(self) -> QWidget:
        panel = QWidget()
        form = QFormLayout(panel)
        self.view_boxes = []
        for name in ("x from", "x to", "y from", "y to"):
            box = spin(-1e300, 1e300, 0.0, 6)
            self.view_boxes.append(box)
            form.addRow(name, box)
        apply = QPushButton("Show this window")
        apply.clicked.connect(lambda: (self.figure.set_view(*(b.value() for b in self.view_boxes)),
                                       self.plot.update(), self._refresh_view_boxes()))
        reset = QPushButton("The whole of it")
        reset.clicked.connect(lambda: (self.figure.reset_view(), self.plot.update(),
                                       self._refresh_view_boxes()))
        row = QHBoxLayout()
        row.addWidget(apply)
        row.addWidget(reset)
        form.addRow("", row)
        save = QPushButton("Save view…")
        load_view = QPushButton("Load view…")
        save.clicked.connect(self.save_view)
        load_view.clicked.connect(self.load_view)
        views = QHBoxLayout()
        views.addWidget(save)
        views.addWidget(load_view)
        form.addRow("Views", views)
        self.export_width = spin(64, 16384, 1600, 0)
        self.export_height = spin(64, 16384, 1000, 0)
        self.export_scale = spin(0.25, 8.0, 2.0, 2)
        size = QHBoxLayout()
        for widget in (self.export_width, QLabel("×"), self.export_height, QLabel("at"),
                       self.export_scale):
            size.addWidget(widget)
        form.addRow("Picture", size)
        export = QPushButton("Export PNG…")
        export.clicked.connect(self.export_picture)
        form.addRow("", export)
        matplotlib_button = QPushButton("Open in matplotlib")
        matplotlib_button.clicked.connect(self.open_in_matplotlib)
        form.addRow("", matplotlib_button)
        return panel

    # --- data ----------------------------------------------------------------

    def add_line(self, name: str, values) -> None:
        self.figure.add_line(values, name)
        self.refresh_lines()
        self.plot.update()

    def add_signal(self, which: int) -> None:
        name, make = SIGNALS[which]
        if make is random_walk:
            label, values = random_walk(seed=len(self.figure) + 1)
        else:
            label, values = make()
        self.add_line(label, values)

    def add_every_signal(self) -> None:
        for which in range(len(SIGNALS)):
            self.add_signal(which)

    def clear_lines(self) -> None:
        self.stream_timer.stop()
        self.stream_line = -1
        self.figure.clear()
        self.refresh_lines()
        self.plot.update()

    def open_file(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Open lines", "",
                                              "Lines (*.npy *.csv *.txt);;All files (*)")
        if not path:
            return
        try:
            for name, values in load(path):
                self.add_line(name, values)
        except (OSError, ValueError) as error:
            QMessageBox.warning(self, "Cannot open", str(error))

    def start_stream(self) -> None:
        # A line that arrives in pieces is folded as it lands and drawn once
        # it is whole -- what a reader of a socket or a slow file would do.
        self.stream_values = random_walk(5_000_000, 77)[1]
        self.stream_line = self.figure.begin_line(len(self.stream_values), "streamed")
        self.stream_at = 0
        self.stream.setEnabled(False)
        self.stream_timer.start()
        self.refresh_lines()

    def _stream_step(self) -> None:
        if self.stream_values is None or self.stream_line < 0:
            self.stream_timer.stop()
            self.stream.setEnabled(True)
            return
        end = min(self.stream_at + 100_000, len(self.stream_values))
        self.figure.append_line(self.stream_line, self.stream_values[self.stream_at:end])
        self.stream_at = end
        self.status.setText(f"streaming: {end} of {len(self.stream_values)}")
        if end >= len(self.stream_values):
            self.figure.finish_line(self.stream_line)
            self.stream_timer.stop()
            self.stream_values = None
            self.stream_line = -1
            self.stream.setEnabled(True)
            self.refresh_lines()
            self.plot.update()

    def apply_x_axis(self) -> None:
        longest = max([line.length for line in self.figure.lines()] + [1])
        mode = self.x_axis.currentIndex()
        if mode < 2:
            self.figure.set_time(None)
            self.figure.set_index_axis(float(mode), 1.0)
        elif mode == 2:
            self.figure.set_time(regular_time(longest))
        else:
            self.figure.set_time(irregular_time(longest))
        self.figure.reset_view()
        self.plot.update()

    def _set_appetite(self, appetite: str) -> None:
        self.figure.set_appetite(appetite)
        self.refresh_lines()
        self.plot.update()

    # --- settings -------------------------------------------------------------

    @staticmethod
    def _base(mode: QComboBox, custom: QDoubleSpinBox) -> float:
        return [10.0, 2.0, float(np.e), custom.value()][mode.currentIndex()]

    def _apply_settings(self) -> None:
        if self.refreshing:
            return
        s = self.figure.settings
        s["xLog"] = self.x_log.isChecked()
        s["yLog"] = self.y_log.isChecked()
        s["xLogBase"] = self._base(self.x_base, self.x_custom)
        s["yLogBase"] = self._base(self.y_base, self.y_custom)
        s["grid"] = self.grid.currentText()
        s["gridStepX"] = self.step_x.value()
        s["gridStepY"] = self.step_y.value()
        s["minorNumbers"] = self.minor_numbers.isChecked()
        s["flipped"] = self.flipped.isChecked()
        s["title"] = self.title.text()
        s["xLabel"] = self.x_label.text()
        s["yLabel"] = self.y_label.text()
        s["markers"] = self.markers.isChecked()
        s["cursor"] = self.cursor.isChecked()
        if self.colour_mode.currentText():
            s["colours.mode"] = self.colour_mode.currentText()
        s["colours.reversed"] = self.reversed.isChecked()
        low, high = sorted((self.band_from.value(), self.band_to.value()))
        s["colours.from"] = low / 100.0
        s["colours.to"] = high / 100.0
        s["legend"] = self.legend.isChecked()
        s["legendCorner"] = self.legend_corner.currentText()
        s["highlighted"] = float(self.highlighted.currentIndex() - 1)
        s["bundle"] = self.bundle.isChecked()
        s["stacked"] = self.stacked.isChecked()
        s["dark"] = self.dark.isChecked()
        s["paper"] = self.paper.isChecked()
        self.plot.update()
        self._refresh_view_boxes()

    def _ground_changed(self) -> None:
        # Colours nobody chose follow the ground, so the swatches show it.
        self._apply_settings()
        self._repaint_swatches()

    def _pick_colour(self, name: str) -> None:
        chosen = QColorDialog.getColor(qcolor(self.figure.settings[name]), self)
        if chosen.isValid():
            self.choose_colour(name, (chosen.redF(), chosen.greenF(), chosen.blueF(),
                                      chosen.alphaF()))

    def choose_colour(self, name: str, colour) -> None:
        """One of the three colours, as the swatch's click sets it without its
        dialog. The figure pins the other two at what they are drawn as."""
        self.figure.settings[name] = colour
        self._repaint_swatches()
        self.plot.update()

    def _follow_ground(self) -> None:
        self.figure.settings["colours.custom"] = False
        self._repaint_swatches()
        self.plot.update()

    def _repaint_swatches(self) -> None:
        # As drawn: the ground's until one is chosen.
        for name, button in self.swatches.items():
            paint_swatch(button, self.figure.settings[name])

    def _apply_style(self) -> None:
        self.figure.style["lineWidth"] = self.line_width.value()
        self.plot.update()

    def refresh_lines(self) -> None:
        self.refreshing = True
        try:
            was = self.highlighted.currentText()
            self.highlighted.clear()
            self.highlighted.addItem("none")
            self.lines.setRowCount(len(self.figure))
            for i, line in enumerate(self.figure.lines()):
                settings = line.settings
                name = line.name + ("" if line.ready else " (arriving)")
                self.highlighted.addItem(name)

                def check(column: int, on: bool) -> None:
                    item = QTableWidgetItem()
                    item.setFlags(Qt.ItemFlag.ItemIsUserCheckable | Qt.ItemFlag.ItemIsEnabled)
                    item.setCheckState(Qt.CheckState.Checked if on else Qt.CheckState.Unchecked)
                    self.lines.setItem(i, column, item)

                def text(column: int, value: str) -> None:
                    item = QTableWidgetItem(value)
                    item.setFlags(Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsEditable
                                  | Qt.ItemFlag.ItemIsSelectable)
                    self.lines.setItem(i, column, item)

                check(SHOW, settings["visible"])
                text(NAME, name)
                check(OWN, settings["ownAxis"])
                check(FIXED, settings["axisFixed"])
                text(LABEL, settings["axisLabel"])
                text(SCALE, f"{settings['scale']:.4g}")
                text(SHIFT, f"{settings['shift']:.4g}")
                colour = QPushButton("cycle")
                if line.colour is not None:
                    paint_swatch(colour, line.colour)
                colour.clicked.connect(lambda _=False, index=i: self._pick_line_colour(index))
                self.lines.setCellWidget(i, COLOUR, colour)
                time = QComboBox()
                time.addItems(["shared", "own, steady", "own, wandering"])
                time.currentIndexChanged.connect(lambda mode, index=i: self._line_time(index, mode))
                self.lines.setCellWidget(i, TIME, time)
                back = QPushButton("put back")
                back.clicked.connect(lambda _=False, index=i: self._put_back(index))
                self.lines.setCellWidget(i, BACK, back)
            again = self.highlighted.findText(was)
            self.highlighted.setCurrentIndex(max(again, 0))
        finally:
            self.refreshing = False
        self._refresh_status()

    def _pick_line_colour(self, index: int) -> None:
        line = self.figure.line(index)
        chosen = QColorDialog.getColor(qcolor(line.colour or (1, 1, 1, 1)), self,
                                       "A colour of its own (cancel for the cycle's)")
        line.colour = ((chosen.redF(), chosen.greenF(), chosen.blueF(), chosen.alphaF())
                       if chosen.isValid() else None)
        self.refresh_lines()
        self.plot.update()

    def _line_time(self, index: int, mode: int) -> None:
        line = self.figure.line(index)
        if mode == 0:
            line.set_time(None)
        else:
            line.set_time(regular_time(line.length, 0.0005) if mode == 1
                          else irregular_time(line.length, 11 + index))
        self.figure.reset_view()
        self.plot.update()

    def _put_back(self, index: int) -> None:
        settings = self.figure.line(index).settings
        settings["scale"] = 1.0
        settings["shift"] = 0.0
        self.refresh_lines()
        self.plot.update()

    def _line_changed(self, row: int, column: int) -> None:
        if self.refreshing:
            return
        item = self.lines.item(row, column)
        if item is None:
            return
        line = self.figure.line(row)
        checked = item.checkState() == Qt.CheckState.Checked
        if column == SHOW:
            line.settings["visible"] = checked
        elif column == NAME:
            line.name = item.text()
        elif column == OWN:
            line.settings["ownAxis"] = checked
        elif column == FIXED:
            line.settings["axisFixed"] = checked
        elif column == LABEL:
            line.settings["axisLabel"] = item.text()
        elif column in (SCALE, SHIFT):
            try:
                line.settings["scale" if column == SCALE else "shift"] = float(item.text())
            except (ValueError, plotcore.Error):
                pass
        self.plot.update()

    def _after_gesture(self) -> None:
        self.refresh_lines()
        self._refresh_view_boxes()
        self._refresh_status()

    def _refresh_status(self) -> None:
        scene = self.plot.last_scene
        if scene is None:
            return
        x0, x1, y0, y1 = scene.view
        text = f"x {x0:.6g} … {x1:.6g}   y {y0:.6g} … {y1:.6g}"
        if scene.reading.valid:
            name = self.figure.line(scene.reading.line).name
            text += f"   —   {name}: x {scene.reading.x_text}, y {scene.reading.y_text}"
        if not self.stream_timer.isActive():
            self.status.setText(text)
        bases = " ".join(str(line.base_bucket) for line in self.figure.lines())
        self.held.setText(f"held {self.figure.held_bytes / 2**20:.1f} of "
                          f"{self.figure.budget_bytes / 2**20:.0f} MB   base buckets {bases}")

    def _refresh_view_boxes(self) -> None:
        for box, value in zip(self.view_boxes, self.figure.view):
            box.blockSignals(True)
            box.setValue(value)
            box.blockSignals(False)

    # --- views and pictures ---------------------------------------------------------

    def save_view(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Save view", "view.json", "Views (*.json)")
        if path:
            Path(path).write_text(self.figure.state(), encoding="utf-8")

    def load_view(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Load view", "", "Views (*.json)")
        if not path:
            return
        try:
            self.figure.restore(Path(path).read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            QMessageBox.warning(self, "Cannot load", str(error))
            return
        self.put_panel_back()

    def put_panel_back(self) -> None:
        """The panel from the figure, which is the one that knows."""
        s = self.figure.settings
        self.refreshing = True
        try:
            self.x_log.setChecked(s["xLog"])
            self.y_log.setChecked(s["yLog"])
            self.grid.setCurrentText(s["grid"])
            self.step_x.setValue(s["gridStepX"])
            self.step_y.setValue(s["gridStepY"])
            self.minor_numbers.setChecked(s["minorNumbers"])
            self.flipped.setChecked(s["flipped"])
            self.title.setText(s["title"])
            self.x_label.setText(s["xLabel"])
            self.y_label.setText(s["yLabel"])
            self.markers.setChecked(s["markers"])
            self.cursor.setChecked(s["cursor"])
            self.colour_mode.setCurrentText(s["colours.mode"])
            self.reversed.setChecked(s["colours.reversed"])
            self.band_from.setValue(round(s["colours.from"] * 100))
            self.band_to.setValue(round(s["colours.to"] * 100))
            self.legend.setChecked(s["legend"])
            self.legend_corner.setCurrentText(s["legendCorner"])
            self.bundle.setChecked(s["bundle"])
            self.stacked.setChecked(s["stacked"])
            self.dark.setChecked(s["dark"])
            self.paper.setChecked(s["paper"])
            self._repaint_swatches()
        finally:
            self.refreshing = False
        self.refresh_lines()
        self.highlighted.setCurrentIndex(int(s["highlighted"]) + 1)
        self._refresh_view_boxes()
        self.plot.update()

    def picture(self):
        scale = self.export_scale.value()
        return self.plot.render(round(self.export_width.value() / scale),
                                round(self.export_height.value() / scale), scale)

    def export_picture(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Export picture", "plot.png", "PNG (*.png)")
        if path and not self.picture().save(path):
            QMessageBox.warning(self, "Cannot export", f"Could not write {path}.")

    def open_in_matplotlib(self) -> None:
        """The same figure on matplotlib's axes, live: a gesture there moves
        this window's view too, because it is one figure."""
        from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
        from matplotlib.figure import Figure as MplFigure

        from plotcore.mpl import View

        dialog = QDialog(self)
        dialog.setWindowTitle("plotcore — matplotlib")
        canvas = FigureCanvasQTAgg(MplFigure(figsize=(9, 5)))
        layout = QVBoxLayout(dialog)
        layout.addWidget(canvas)
        view = View(self.figure, canvas.figure)
        canvas.mpl_connect("button_release_event", lambda _event: self.plot.update())
        canvas.mpl_connect("scroll_event", lambda _event: self.plot.update())
        dialog.view = view
        dialog.resize(1000, 600)
        dialog.show()
        view.redraw()
        self.mpl_dialog = dialog

    # --- the self-test ---------------------------------------------------------------

    def self_test(self, picture: str = "") -> int:
        failed = 0

        def check(ok: bool, what: str) -> None:
            nonlocal failed
            print(("  ok    " if ok else "  FAIL  ") + what)
            failed += 0 if ok else 1

        def frame() -> plotcore.Scene:
            self.plot.repaint()
            QApplication.processEvents()
            return self.plot.last_scene

        def wheel(at: QPointF, delta: int, keys=Qt.KeyboardModifier.NoModifier) -> None:
            event = QWheelEvent(at, self.plot.mapToGlobal(at), QPointF(0, 0).toPoint(),
                                QPointF(0, delta).toPoint(), Qt.MouseButton.NoButton, keys,
                                Qt.ScrollPhase.NoScrollPhase, False)
            QApplication.sendEvent(self.plot, event)

        def mouse(kind, at: QPointF, button, keys=Qt.KeyboardModifier.NoModifier) -> None:
            held = (Qt.MouseButton.NoButton if kind == QMouseEvent.Type.MouseButtonRelease
                    else button)
            event = QMouseEvent(kind, at, self.plot.mapToGlobal(at), button, held, keys)
            QApplication.sendEvent(self.plot, event)

        print("plotcore PyQt6 example, self-test")
        self.add_every_signal()
        whole = frame()
        check(len(whole.lines) == 8, "every signal is drawn")
        middle = QPointF(whole.area.x + whole.area.width / 2, whole.area.y + whole.area.height / 2)
        wheel(middle, 360)
        zoomed = frame()
        check(zoomed.view[1] - zoomed.view[0] < whole.view[1] - whole.view[0], "the wheel zooms in")
        Press, Move, Release = (QMouseEvent.Type.MouseButtonPress, QMouseEvent.Type.MouseMove,
                                QMouseEvent.Type.MouseButtonRelease)
        left, right = Qt.MouseButton.LeftButton, Qt.MouseButton.RightButton
        mouse(Press, middle, left)
        mouse(Move, middle + QPointF(80, 0), left)
        mouse(Release, middle + QPointF(80, 0), left)
        check(frame().view[0] < zoomed.view[0], "a drag pans")
        mouse(Press, middle - QPointF(100, 60), right)
        mouse(Move, middle + QPointF(100, 60), right)
        check(frame().band is not None, "a right drag draws its band")
        mouse(Release, middle + QPointF(100, 60), right)
        mouse(QMouseEvent.Type.MouseButtonDblClick, middle, left)
        check(frame().view == whole.view, "a double click puts it back")
        mouse(Move, middle, Qt.MouseButton.NoButton)
        check(frame().reading.valid, "the crosshair reads a sample")

        back = frame()
        run = back.lines[1].runs[0]
        at = QPointF(*run[len(run) // 2])
        under = self.figure.line_at(at.x(), at.y())
        check(under is not None, "a line is under the pointer")
        alt = Qt.KeyboardModifier.AltModifier
        mouse(Press, at, left, alt)
        mouse(Move, at + QPointF(0, 30), left, alt)
        mouse(Release, at + QPointF(0, 30), left, alt)
        check(under is not None and self.figure.line(under).settings["shift"] != 0.0,
              "Alt and a drag move it")
        wheel(at + QPointF(0, 30), 240, alt | Qt.KeyboardModifier.ControlModifier)
        check(under is not None and self.figure.line(under).settings["scale"] != 1.0,
              "Alt, Control and the wheel scale it")
        control = Qt.KeyboardModifier.ControlModifier
        mouse(Press, at + QPointF(0, 30), left, control)
        mouse(Release, at + QPointF(0, 30), left, control)
        check(under is not None and self.figure.line(under).settings["scale"] == 1.0
              and self.figure.line(under).settings["shift"] == 0.0,
              "Control and a click put it back")

        self.x_axis.setCurrentIndex(1)
        self.x_log.setChecked(True)
        self.y_log.setChecked(True)
        check(len(frame().lines) == 8 and not frame().message, "both axes logarithmic")
        self.x_base.setCurrentIndex(3)
        self.y_base.setCurrentIndex(2)
        self.minor_numbers.setChecked(True)
        frame()
        self.x_log.setChecked(False)
        self.y_log.setChecked(False)
        for i, grid in enumerate(GRIDS):
            self.grid.setCurrentIndex(i)
            self.step_x.setValue(100000.0 if grid == "custom" else 0.0)
            self.step_y.setValue(5.0 if grid == "custom" else 0.0)
            check((grid == "none") == (len(frame().rules) == 0), f"the {grid} grid")
        self.grid.setCurrentIndex(1)
        self.title.setText("Every signal")
        self.flipped.setChecked(True)
        check(len(frame().lines) == 8, "flipped")
        self.flipped.setChecked(False)
        self.markers.setChecked(True)
        self.line_width.setValue(2.0)
        for i in range(self.colour_mode.count()):
            if self.colour_mode.itemText(i):
                self.colour_mode.setCurrentIndex(i)
                self.reversed.setChecked(i % 2 == 1)
                frame()
        check(True, "every palette, every map, same and range")
        for i in range(len(CORNERS)):
            self.legend_corner.setCurrentIndex(i)
        check(bool(frame().legend), "the legend, in every corner")
        self.lines.item(2, OWN).setCheckState(Qt.CheckState.Checked)
        self.lines.item(2, LABEL).setText("gaps")
        self.lines.item(3, OWN).setCheckState(Qt.CheckState.Checked)
        self.lines.item(3, FIXED).setCheckState(Qt.CheckState.Checked)
        check(frame().area.x > back.area.x, "axes of their own, beside the pane")
        self.stacked.setChecked(True)
        check(len(frame().lines) == 8, "stacked")
        self.stacked.setChecked(False)
        self.lines.cellWidget(1, TIME).setCurrentIndex(2)
        check(self.figure.line(1).has_time, "a time of its own")
        self.x_axis.setCurrentIndex(3)
        check(len(frame().lines) == 8, "a shared time that wanders")
        self.x_axis.setCurrentIndex(0)

        # A colour nobody chose is the ground's and follows it; one chosen
        # stays where it was put until the ground is given it back.
        def drawn_in():
            return np.array(frame().lines[0].colour[:3])

        self.colour_mode.setCurrentText("same")
        on_dark = drawn_in()
        self.dark.setChecked(False)
        check(not np.allclose(drawn_in(), on_dark), "a colour nobody chose follows the ground")
        orange = (0.9, 0.3, 0.1, 1.0)
        self.choose_colour("colours.single", orange)
        self.dark.setChecked(True)
        check(np.allclose(drawn_in(), orange[:3]), "a colour chosen stays where it was put")
        self._follow_ground()
        check(np.allclose(drawn_in(), on_dark), "...until the ground is given it back")
        self.colour_mode.setCurrentIndex(0)

        self.dark.setChecked(False)
        self.paper.setChecked(True)
        frame()
        self.dark.setChecked(True)
        self.paper.setChecked(False)
        for appetite in ("low", "medium", "greedy"):
            self.appetite.setCurrentText(appetite)
            frame()
        check(self.figure.held_bytes <= self.figure.budget_bytes,
              "what is held is inside the budget")

        wheel(middle, 480)
        before = frame()
        state = self.figure.state()
        self.figure.reset_view()
        self.figure.restore(state)
        self.put_panel_back()
        check(frame().view == before.view, "a view reads back as the same window")

        self.start_stream()
        while self.stream_timer.isActive():
            self._stream_step()
        check(len(frame().lines) == 9, "a line streamed in pieces is drawn once it is whole")

        self.open_in_matplotlib()
        QApplication.processEvents()
        view = self.mpl_dialog.view
        check(len(view.ax.get_lines()) + sum(len(t.get_lines()) for t in view._extra) >= 9,
              "the same figure in matplotlib")
        self.mpl_dialog.close()

        self.colour_mode.setCurrentIndex(0)
        self.reversed.setChecked(False)
        self.figure.reset_view()
        image = self.plot.render(800, 500, 2.0)
        check(image.width() == 1600 and image.height() == 1000, "a picture at twice the pixels")
        if picture:
            check(image.save(picture), "...written to a file")
        print("all checks passed" if failed == 0 else "checks failed")
        return failed


def main(argv: List[str]) -> int:
    app = QApplication(argv)
    window = DemoWindow()
    if "--self-test" in argv:
        window.show()
        QApplication.processEvents()
        at = argv.index("--self-test")
        picture = argv[at + 1] if at + 1 < len(argv) else ""
        return 0 if window.self_test(picture) == 0 else 1
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main(sys.argv))

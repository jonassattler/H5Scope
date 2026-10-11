# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""RPC3 viewer: files in a tree, checked channels on one plot.

A file is listed from its header. The samples are read when the first of
its channels is checked, off the window's thread, because an RPC3 file is
multiplexed and that read is the whole file. Drop a file anywhere on the
window, or import several at once.

Build the DLL first:  python\\h5plot\\build.bat
Then:  C:\\Pythonuser\\venv\\Py311-EISB1\\Scripts\\python.exe python\\h5plot\\demo.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import math

from PySide6.QtCore import QEvent, QObject, QThread, Qt, QTimer, Signal
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMenu,
    QMessageBox,
    QPushButton,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from h5plot import PlotWidget
from h5plot.rpc3view import Catalog, Rpc3File


class _LoadThread(QThread):
    done = Signal(object, object)

    def __init__(self, catalog: Catalog, entry: Rpc3File):
        super().__init__()
        self.catalog = catalog
        self.entry = entry

    def run(self) -> None:
        error = None
        try:
            self.catalog.load(self.entry)
        except Exception as exc:
            error = exc
        self.done.emit(self.entry, error)


class _FileDrop(QObject):
    """Accept local files on the widget this is installed on.

    A drop lands on the child under the pointer. A parent that accepts
    drops does not see it once the child has ignored it.
    """

    def __init__(self, accept, parent=None):
        super().__init__(parent)
        self._accept = accept

    def eventFilter(self, watched, event):
        kind = event.type()
        if kind not in (QEvent.Type.DragEnter, QEvent.Type.DragMove, QEvent.Type.Drop):
            return False
        mime = event.mimeData()
        if mime is None or not mime.hasUrls():
            return False
        event.acceptProposedAction()
        if kind == QEvent.Type.Drop:
            self._accept(mime.urls())
        return True


class Viewer(QWidget):
    def __init__(self):
        super().__init__()
        self.catalog = Catalog()
        self._job: _LoadThread | None = None
        self._closed = False
        self._guard = False
        self.setWindowTitle("RPC3 Viewer")
        self.resize(1100, 640)
        self.setAcceptDrops(True)

        self.tree = QTreeWidget()
        self.tree.setHeaderLabel("Dateien")
        self.tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self._menu)
        self.tree.itemChanged.connect(self._changed)
        self.plot = PlotWidget()

        log_y = QCheckBox("Log y")
        log_y.toggled.connect(self.plot.set_y_log)
        log_x = QCheckBox("Log x")
        log_x.toggled.connect(self.plot.set_x_log)
        self._bases = (10.0, 2.0, math.e)
        self._base_names = ("base 10", "base 2", "base e")
        self._base_i = 0
        base = QPushButton(self._base_names[0])
        base.clicked.connect(lambda: self._cycle_base(base))
        stack = QPushButton("Stapeln")
        stack.clicked.connect(self.plot.stack_lines)
        scales = QWidget()
        scales_layout = QHBoxLayout(scales)
        scales_layout.setContentsMargins(8, 0, 8, 0)
        scales_layout.addWidget(log_y)
        scales_layout.addWidget(log_x)
        scales_layout.addWidget(base)
        scales_layout.addWidget(stack)
        scales_layout.addStretch(1)

        hint = QLabel(
            "Links verschiebt die Ansicht, rechts zieht ein Rechteck. "
            "Das Rad mit Shift zoomt X, mit Strg Y. "
            "Alt oder die mittlere Taste gibt der Kurve unter dem Zeiger eine eigene Achse."
        )
        hint.setWordWrap(True)

        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.setContentsMargins(8, 8, 8, 8)
        import_button = QPushButton("Importieren\u2026")
        import_button.clicked.connect(self._dialog)
        left_layout.addWidget(import_button)
        left_layout.addWidget(self.tree, 1)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.addWidget(self.plot, 1)
        right_layout.addWidget(scales)
        right_layout.addWidget(hint)

        splitter = QSplitter()
        splitter.addWidget(left)
        splitter.addWidget(right)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([280, 820])

        self._status = QLabel("Dateien importieren oder hier ablegen.")
        self._status.setContentsMargins(8, 4, 8, 4)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(splitter, 1)
        layout.addWidget(self._status)

        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._apply)
        drop = _FileDrop(self._dropped, self)
        for widget in (self, left, right, self.tree, self.plot, scales):
            widget.setAcceptDrops(True)
            widget.installEventFilter(drop)

    def _cycle_base(self, button: QPushButton) -> None:
        self._base_i = (self._base_i + 1) % len(self._bases)
        chosen = self._bases[self._base_i]
        self.plot.set_x_log_base(chosen)
        self.plot.set_y_log_base(chosen)
        button.setText(self._base_names[self._base_i])

    def _dialog(self) -> None:
        paths, _filter = QFileDialog.getOpenFileNames(
            self,
            "RPC3 importieren",
            "",
            "RPC3 (*.rpc *.rsp *.tim *.drv);;Alle Dateien (*.*)",
        )
        self.import_paths(paths)

    def _dropped(self, urls) -> None:
        self.import_paths([url.toLocalFile() for url in urls if url.isLocalFile()])

    def import_paths(self, paths) -> None:
        errors = []
        files = 0
        for raw in paths:
            if not raw:
                continue
            path = Path(raw)
            if not path.is_file():
                continue
            files += 1
            if self.catalog.contains(path):
                continue
            try:
                entry = self.catalog.add(path)
            except Exception as exc:
                errors.append(f"{path.name}: {exc}")
                continue
            self._insert(entry)
        if errors:
            QMessageBox.warning(self, "RPC3", "\n".join(errors))
        elif files == 0 and paths:
            self._status.setText("Keine Dateien.")

    def _insert(self, entry: Rpc3File) -> None:
        self._guard = True
        file_item = QTreeWidgetItem([entry.path.name])
        file_item.setFlags(
            Qt.ItemFlag.ItemIsEnabled
            | Qt.ItemFlag.ItemIsSelectable
            | Qt.ItemFlag.ItemIsUserCheckable
        )
        file_item.setData(0, Qt.ItemDataRole.UserRole, (entry, None))
        file_item.setToolTip(0, str(entry.path))
        file_item.setCheckState(0, Qt.CheckState.Unchecked)
        for slot in entry.channels:
            child = QTreeWidgetItem([_channel_text(slot)])
            child.setFlags(file_item.flags())
            child.setData(0, Qt.ItemDataRole.UserRole, (entry, slot))
            child.setToolTip(0, _channel_tip(entry, slot))
            child.setCheckState(0, Qt.CheckState.Unchecked)
            file_item.addChild(child)
        self.tree.addTopLevelItem(file_item)
        file_item.setExpanded(True)
        self._guard = False
        if self.catalog.files:
            self._status.setText("")

    def _changed(self, item: QTreeWidgetItem, _column: int) -> None:
        if self._guard:
            return
        entry, slot = item.data(0, Qt.ItemDataRole.UserRole)
        self._guard = True
        try:
            if slot is None:
                checked = item.checkState(0) == Qt.CheckState.Checked
                state = Qt.CheckState.Checked if checked else Qt.CheckState.Unchecked
                for index, child_slot in enumerate(entry.channels):
                    child_slot.checked = checked
                    item.child(index).setCheckState(0, state)
            else:
                slot.checked = item.checkState(0) == Qt.CheckState.Checked
                _sync_file_check(item.parent(), entry)
        finally:
            self._guard = False
        self._timer.start(0)

    def _menu(self, pos) -> None:
        item = self.tree.itemAt(pos)
        if item is None:
            return
        while item.parent() is not None:
            item = item.parent()
        entry, _slot = item.data(0, Qt.ItemDataRole.UserRole)
        menu = QMenu(self)
        remove = menu.addAction("Entfernen")
        if menu.exec(self.tree.viewport().mapToGlobal(pos)) is not remove:
            return
        self.catalog.remove(entry)
        index = self.tree.indexOfTopLevelItem(item)
        self.tree.takeTopLevelItem(index)
        self._apply()

    def _apply(self) -> None:
        if self._closed:
            return
        self._draw()
        if self._job is not None:
            self._status.setText(f"Lese {self._job.entry.path.name} \u2026")
            return
        entry = self.catalog.pending()
        if entry is None:
            if not self.catalog.files:
                self._status.setText("Dateien importieren oder hier ablegen.")
            elif not any(slot.checked for file in self.catalog.files for slot in file.channels):
                self._status.setText("")
            return
        self._job = _LoadThread(self.catalog, entry)
        self._job.done.connect(self._loaded)
        self._status.setText(f"Lese {entry.path.name} \u2026")
        self._job.start()

    def _loaded(self, entry: Rpc3File, error) -> None:
        if self._job is not None and self._job.entry is entry:
            self._job = None
        if self._closed or entry not in self.catalog.files:
            self._apply()
            return
        if error is not None:
            for slot in entry.channels:
                slot.checked = False
            self._push(entry)
            QMessageBox.warning(self, "RPC3", f"{entry.path.name}\n{error}")
        else:
            self._tips(entry)
        self._apply()

    def _push(self, entry: Rpc3File) -> None:
        item = self._file_item(entry)
        if item is None:
            return
        self._guard = True
        try:
            for index, slot in enumerate(entry.channels):
                item.child(index).setCheckState(
                    0, Qt.CheckState.Checked if slot.checked else Qt.CheckState.Unchecked
                )
            _sync_file_check(item, entry)
        finally:
            self._guard = False

    def _tips(self, entry: Rpc3File) -> None:
        item = self._file_item(entry)
        if item is None:
            return
        for index, slot in enumerate(entry.channels):
            item.child(index).setToolTip(0, _channel_tip(entry, slot))

    def _file_item(self, entry: Rpc3File) -> QTreeWidgetItem | None:
        for index in range(self.tree.topLevelItemCount()):
            item = self.tree.topLevelItem(index)
            found, _slot = item.data(0, Qt.ItemDataRole.UserRole)
            if found is entry:
                return item
        return None

    def _draw(self) -> None:
        def fill(plot):
            return "s", self.catalog.publish(plot)

        self.plot.show_lines(fill)

    def closeEvent(self, event) -> None:
        self._closed = True
        if self._job is not None:
            self._job.done.disconnect(self._loaded)
        event.accept()


def _channel_text(slot) -> str:
    unit = f"  [{slot.unit}]" if slot.unit else ""
    return f"{slot.name}{unit}"


def _channel_tip(entry: Rpc3File, slot) -> str:
    return f"{slot.samples} Abtastwerte, dt = {entry.dt:g} s"


def _sync_file_check(item: QTreeWidgetItem, entry: Rpc3File) -> None:
    marks = [slot.checked for slot in entry.channels]
    if marks and all(marks):
        state = Qt.CheckState.Checked
    elif any(marks):
        state = Qt.CheckState.PartiallyChecked
    else:
        state = Qt.CheckState.Unchecked
    item.setCheckState(0, state)


def main() -> int:
    app = QApplication(sys.argv)
    window = Viewer()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())

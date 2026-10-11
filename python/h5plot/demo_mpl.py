# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""The RPC3 viewer, drawn by matplotlib inside a Tk window.

The fold is the one the Qt window uses. Matplotlib draws that envelope and
numbers the axes itself. The toolbar stays off. The tree and the file dialog
are Tk. Dropping files needs tkinterdnd2; without it the dialog still opens
them.

    C:\\Pythonuser\\venv\\Py311-EISB1\\Scripts\\python.exe python\\h5plot\\demo_mpl.py

Scroll zooms, Shift scrolls x, Ctrl scrolls y. The left button pans,
the right button pulls a rectangle and names the window it will open.
"""

from __future__ import annotations

import queue
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from h5plot.rpc3view import Catalog, Rpc3File

_OFF = "\u2610"
_ON = "\u2611"
_MIX = "\u25a3"


class Viewer:
    def __init__(self, root: tk.Tk, drop: bool):
        self.root = root
        self.catalog = Catalog()
        self._loading: Rpc3File | None = None
        self._done: queue.Queue = queue.Queue()
        self._iids: dict[str, tuple] = {}
        self._children: dict[str, list[str]] = {}
        self._menu_iid = ""
        self._hint = (
            "Ablegen braucht tkinterdnd2 (pip install tkinterdnd2)."
            if not drop
            else "Dateien importieren oder hier ablegen."
        )
        root.title("RPC3 Viewer")
        root.geometry("1100x640")
        root.minsize(720, 420)

        self.status = ttk.Label(root, anchor="w", padding=(8, 4))
        self.status.pack(side=tk.BOTTOM, fill=tk.X)
        paned = ttk.Panedwindow(root, orient=tk.HORIZONTAL)
        paned.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        left = ttk.Frame(paned, width=280)
        right = ttk.Frame(paned)
        paned.add(left, weight=0)
        paned.add(right, weight=1)

        ttk.Button(left, text="Importieren\u2026", command=self._dialog).pack(
            fill=tk.X, padx=4, pady=4
        )
        box = ttk.Frame(left)
        box.pack(fill=tk.BOTH, expand=True)
        self.tree = ttk.Treeview(box, show="tree", selectmode="browse")
        scroll = ttk.Scrollbar(box, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.tree.bind("<Button-1>", self._click)
        self.tree.bind("<Button-3>", self._popup)
        self.menu = tk.Menu(root, tearoff=0)
        self.menu.add_command(label="Entfernen", command=self._remove)

        import matplotlib

        matplotlib.use("TkAgg")
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
        from matplotlib.figure import Figure

        from h5plot.mpl import MplView

        figure = Figure()
        canvas = FigureCanvasTkAgg(figure, master=right)
        self.view = MplView(figure=figure)
        self.canvas_widget = canvas.get_tk_widget()
        self.canvas_widget.pack(fill=tk.BOTH, expand=True)
        self.status.config(text=self._hint)
        if drop:
            self._arm_drop()
        root.after(50, lambda: paned.sashpos(0, 280))
        root.after(50, self._poll)

    def _poll(self) -> None:
        # The read runs off this thread. Tk is only touched here: a call into
        # it from the reader is refused unless the main loop is already running.
        try:
            while True:
                entry, error = self._done.get_nowait()
                self._loaded(entry, error)
        except queue.Empty:
            pass
        self.root.after(50, self._poll)

    def _arm_drop(self) -> None:
        from tkinterdnd2 import DND_FILES

        for widget in (self.root, self.tree, self.canvas_widget):
            widget.drop_target_register(DND_FILES)
            widget.dnd_bind("<<Drop>>", self._dropped)

    def _dropped(self, event) -> None:
        self.import_paths(self.root.tk.splitlist(event.data))

    def _dialog(self) -> None:
        paths = filedialog.askopenfilenames(
            title="RPC3 importieren",
            filetypes=[
                ("RPC3", "*.rpc *.rsp *.tim *.drv"),
                ("Alle Dateien", "*.*"),
            ],
        )
        self.import_paths(paths)

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
            messagebox.showwarning("RPC3", "\n".join(errors))
        elif files == 0 and paths:
            self.status.config(text="Keine Dateien.")

    def _insert(self, entry: Rpc3File) -> None:
        file_iid = self.tree.insert("", "end", text=_file_text(entry), open=True)
        self._iids[file_iid] = (entry, None)
        self._children[file_iid] = []
        for slot in entry.channels:
            child = self.tree.insert(file_iid, "end", text=_channel_text(slot))
            self._iids[child] = (entry, slot)
            self._children[file_iid].append(child)
        if self.catalog.files and self._loading is None:
            self.status.config(text="")

    def _click(self, event) -> None:
        element = self.tree.identify_element(event.x, event.y) or ""
        if "indicator" in element:
            return
        iid = self.tree.identify_row(event.y)
        if not iid or iid not in self._iids:
            return
        entry, slot = self._iids[iid]
        if slot is None:
            checked = not all(child.checked for child in entry.channels)
            for child in entry.channels:
                child.checked = checked
        else:
            slot.checked = not slot.checked
        self._refresh(entry)
        self._apply()

    def _popup(self, event) -> None:
        iid = self.tree.identify_row(event.y)
        if not iid or iid not in self._iids:
            return
        file_iid = iid if not self.tree.parent(iid) else self.tree.parent(iid)
        self._menu_iid = file_iid
        self.tree.selection_set(file_iid)
        self.menu.tk_popup(event.x_root, event.y_root)

    def _remove(self) -> None:
        iid = self._menu_iid
        if iid not in self._iids:
            return
        entry, _slot = self._iids[iid]
        self.catalog.remove(entry)
        for child in self._children.pop(iid, []):
            self._iids.pop(child, None)
        self._iids.pop(iid, None)
        self.tree.delete(iid)
        self._apply()

    def _refresh(self, entry: Rpc3File) -> None:
        for iid, (found, slot) in self._iids.items():
            if found is not entry:
                continue
            text = _file_text(entry) if slot is None else _channel_text(slot)
            self.tree.item(iid, text=text)

    def _apply(self) -> None:
        self._draw()
        if self._loading is not None:
            self.status.config(text=f"Lese {self._loading.path.name} \u2026")
            return
        entry = self.catalog.pending()
        if entry is None:
            if not self.catalog.files:
                self.status.config(text=self._hint)
            else:
                self.status.config(text="")
            return
        self._loading = entry
        self.status.config(text=f"Lese {entry.path.name} \u2026")
        threading.Thread(target=self._worker, args=(entry,), daemon=True).start()

    def _worker(self, entry: Rpc3File) -> None:
        error = None
        try:
            self.catalog.load(entry)
        except Exception as exc:
            error = exc
        self._done.put((entry, error))

    def _loaded(self, entry: Rpc3File, error) -> None:
        if self._loading is entry:
            self._loading = None
        if entry not in self.catalog.files:
            self._apply()
            return
        if error is not None:
            for slot in entry.channels:
                slot.checked = False
            self._refresh(entry)
            messagebox.showwarning("RPC3", f"{entry.path.name}\n{error}")
        else:
            self._refresh(entry)
        self._apply()

    def _draw(self) -> None:
        def fill(plot):
            return "s", self.catalog.publish(plot)

        self.view.show_lines(fill)


def _file_text(entry: Rpc3File) -> str:
    marks = [slot.checked for slot in entry.channels]
    if marks and all(marks):
        mark = _ON
    elif any(marks):
        mark = _MIX
    else:
        mark = _OFF
    return f"{mark}  {entry.path.name}"


def _channel_text(slot) -> str:
    mark = _ON if slot.checked else _OFF
    unit = f"  [{slot.unit}]" if slot.unit else ""
    return f"{mark}  {slot.name}{unit}"


def main() -> int:
    try:
        from tkinterdnd2 import TkinterDnD

        root = TkinterDnD.Tk()
        drop = True
    except ImportError:
        root = tk.Tk()
        drop = False
    Viewer(root, drop)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""RPC3 files for the two viewer windows.

A file is multiplexed, so one channel is not a read of its own. The header
is enough for the tree. The samples are read once, the first time a channel
of that file is shown, and kept. Both windows call this and draw the result
themselves: nothing here knows about Qt or Tk.

Each drawn channel carries its own time. Channels of one file share
``DELTA_T`` and start at zero, files do not, and one shared x would lay a
shorter or differently clocked file on the wrong samples.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

from .lib import _CYCLE


@dataclass
class ChannelSlot:
    """One channel as the tree shows it, before or after its samples exist."""

    index: int
    name: str
    unit: str
    samples: int
    colour: tuple[int, int, int]
    checked: bool = False


@dataclass
class Rpc3File:
    path: Path
    dt: float
    channels: list[ChannelSlot]
    loaded: list | None = None


@dataclass
class Catalog:
    """The files the viewer has imported, in the order the tree shows them."""

    files: list[Rpc3File] = field(default_factory=list)
    _keys: set[str] = field(default_factory=set)
    _colour: int = 0

    def contains(self, path: Path) -> bool:
        try:
            key = _identity(path)
        except OSError:
            return False
        return key in self._keys

    def add(self, path: Path) -> Rpc3File:
        """Read the header and append the file. The samples stay unread.

        A path already in the catalog is refused. ``FileNotFoundError`` and
        ``rpc3.FileFormatError`` propagate: the caller leaves that file out.
        """
        rpc3 = _rpc3()
        path = path.resolve()
        key = _identity(path)
        if key in self._keys:
            raise FileExistsError(path)
        channels, params = rpc3.read(str(path), header_only=True)
        if not channels:
            raise rpc3.FileFormatError(f"{path.name} has no channels")
        samples = params.get("SAMPLES")
        if samples is None:
            samples = int(params["FRAMES"]) * int(params["PTS_PER_FRAME"])
        slots = []
        for index, channel in enumerate(channels):
            colour = _CYCLE[self._colour % len(_CYCLE)]
            self._colour += 1
            slots.append(
                ChannelSlot(
                    index=index,
                    name=str(channel.name),
                    unit=str(channel.unit),
                    samples=int(samples),
                    colour=colour,
                )
            )
        entry = Rpc3File(path=path, dt=float(params["DELTA_T"]), channels=slots)
        self.files.append(entry)
        self._keys.add(key)
        return entry

    def load(self, entry: Rpc3File) -> None:
        """Read every channel of one file. A second call keeps the first read."""
        if entry.loaded is not None:
            return
        rpc3 = _rpc3()
        channels, _params = rpc3.read(str(entry.path))
        if not isinstance(channels, list) or len(channels) != len(entry.channels):
            raise rpc3.FileFormatError(f"{entry.path.name} changed while it was open")
        entry.loaded = channels
        for slot, channel in zip(entry.channels, channels):
            slot.samples = int(len(channel.data))

    def pending(self) -> Rpc3File | None:
        """The first file that is checked somewhere and not read yet."""
        for entry in self.files:
            if entry.loaded is None and any(slot.checked for slot in entry.channels):
                return entry
        return None

    def remove(self, entry: Rpc3File) -> None:
        if entry in self.files:
            self.files.remove(entry)
        self._keys.discard(_identity(entry.path))
        entry.loaded = None

    def publish(self, plot) -> str:
        """Replace the plot's lines with the checked channels.

        Returns the y unit when every drawn channel shares one, otherwise "".
        A channel with fewer than two samples is not a curve. The view is
        left where it was: the window resets once, after this returns.
        """
        drawn = []
        units: list[str] = []
        for entry in self.files:
            if entry.loaded is None:
                continue
            stem = entry.path.stem
            for slot in entry.channels:
                if not slot.checked:
                    continue
                channel = entry.loaded[slot.index]
                data = channel.data
                if data is None or len(data) < 2:
                    continue
                drawn.append((data, channel.time(), slot.colour, f"{stem}: {slot.name}"))
                units.append(slot.unit)
        plot.clear()
        for data, time, colour, name in drawn:
            index = plot.add_line(data, colour=colour, name=name)
            plot.set_line_x(index, time)
        if not units or any(unit != units[0] for unit in units):
            return ""
        return units[0]


def _identity(path: Path) -> str:
    return os.path.normcase(str(path.resolve()))


def _rpc3():
    try:
        import rpc3
        import rpc3.rpc3 as impl
    except ImportError as error:
        raise ImportError(
            "rpc3-file is not installed; pip install h5plot[rpc3]"
        ) from error
    # `read` looks this up in its own module. The package's import list does
    # not re-export it, so setting it on `rpc3` would leave the bar on, and
    # tqdm would write into a window that has no console.
    impl.progressbar = False
    return rpc3

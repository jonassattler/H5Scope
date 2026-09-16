# Changelog

One section per released version, newest first, and **every tag has one**. It
is what the release page says about itself: the GitHub release is published
with the contents of the matching section, so a version whose section is missing
— or empty, or duplicated, or word for word another version's — is a version
that cannot be released. `tools/release-notes.sh` refuses all four, and CI runs
it on a tag push in its first job rather than after half an hour of building.

The copied case is the one worth naming. Last release's notes with a new number
over them are worse than no notes at all: they are wrong rather than absent, and
they read as deliberate.

Short and concise. A reader lands here to find out whether the release is worth
taking and what will be different if they do, which is a handful of lines: what
was added, what changed under them, what broke. The argument for a change
belongs in the commit that made it, where there is room for it; the version
number belongs to `cmake/Version.cmake`, which counts it out of the tags.

Headings are `## <version>`, matched exactly against the version the build gave
itself — so `## 0.4.0`, not `## v0.4.0` and not `## Version 0.4.0`.

This starts at 0.4.0, which is when the release page started being written
rather than generated. The releases before it are on GitHub with the notes they
were published under, and backfilling them here would be inventing a record
rather than keeping one.

## 0.5.2

**A zoom costs nothing.** The plot holds each line whole — at the finest
resolution the RAM budget allows, with every coarser one derived above it — so
the one pass that reads a dataset is the only pass there is. Zooming, panning
and resizing after that are arithmetic over memory rather than reads of the
file: a zoom from the whole of a ten-million-sample trace down to a single
sample per pixel now draws twenty frames in about a millisecond, where each of
those frames used to be a round trip to disk and a tenth of a second of waiting.
It holds at a hundred million as well, and at a billion for every resolution the
budget can hold. Opening a large dataset costs about twice what it did, because
what it reads is now kept; everything after that is free.

**The plot holds what it reads.** How much it may hold is now a setting —
**Settings > RAM Budget**, low, medium or greedy — taken as a fraction of the
memory the machine actually has instead of a fixed sixteen megabytes shared by
everything. On a large dataset that is the difference between zooming back out
being instant and being a fresh read of twenty million elements. The Plot tab
and every custom tab draw on one budget between them rather than each holding
its own, and the choice is remembered between runs.

**A zoom is read towards the pointer, while you are still zooming.** The plot
now knows where a wheel gesture is aimed, so the resolutions it reads ahead
follow the reader in rather than being centred on the middle of the frame — and
they go out immediately instead of waiting for the gesture to finish. Four
octaves inward are read ahead where two outward were before, which they can
afford because each step in costs half of the one above it. Panning is
unchanged and still reads nothing until it stops.

**Custom tabs stop asking for one bucket at a time.** A custom plot read one
hyperslab per drawn point — two thousand round trips per entry on a normal
window, where the Plot tab read one per sixty-four thousand elements. Same
picture, a thousand times the work; a twenty-thousand-element line now reads
once. Datasets named by several entries are also opened once per tab rather
than once per edit.

Under all of it: the two plots shared no code above the values they drew and now
share the fold, the cache and the policy over it, so what is fixed in one is
fixed in both. `tools/bench-zoom` is the new measurement — per-frame times for a
zoom, beside a count of how many of those frames still held the one-sample spike
the pointer was on — and `make-example-file --adc N` writes the long traces to
run it against.

## 0.5.1

**Fixes reported from use.** A custom tab drawn against a time series drew
almost nothing: a time base long enough to be thinned was looked up by the
position of the sample that wanted it rather than by its own share of the
axis, so most of the line asked past the end of it and was dropped. Zooming a
custom tab with a touchpad could leave the line drawn across part of the frame
with empty axis either side, until another gesture put it right. And unticking
a line in a legend recoloured the ones left behind.

**Numbers are read the way you type them, not the way the locale does.** The
manual range and index boxes validated against the system locale, so on a
German desktop "0.2" was refused at the keystroke while "0,2" was accepted and
then committed as zero. Both now take either separator, and an exponent.

**Selecting a large dataset is roughly twice as fast**: `/plotting/adc_10M`
went from about 550 ms on the GUI thread to 230 ms. None of that was the file
— the shape of a slice was measured by writing out every index it names, which
is eighty megabytes built and freed to answer `:` on a dimension of ten
million. It is arithmetic now.

Also fixed: a use-after-free in the plot's closer-look cache that could crash
the Windows build when a second zoom level landed over the one being drawn.

## 0.5.0

**The plot draws itself now.** Qt Graphs is gone from the tree, and both the
Plot tab and custom plots draw through H5Scope's own renderer instead. That
paid for two things a library couldn't give: a line is decimated by the
min/max extremes of each column of pixels rather than by stride, so a spike a
single sample wide is never one of the samples a stride steps over, and
zooming in now re-reads the run on screen at a finer resolution instead of
stretching the same summary — magnification is no longer capped at 256×. The
stripped Linux binary is 16 MiB smaller (78.3 → 62.1 MiB) with the library
and its unused 3-D dependency gone, and Qt Graphs was also the only GPL-only
component in the tree: H5Scope stays GPL-3.0, but now because that is the
author's choice rather than an obligation carried in by a dependency.

**Reading the line.** Pointing at a line puts its value in the same status
strip every view already has instead of a floating box drawn over the
picture, and the crosshair is clipped to the pane so it never marks a point
that isn't on screen. The y axis gutter is now sized to the widest label it
actually prints, so a trace of large or negative values no longer has its
digits cut off against the frame.

## 0.4.0

**Custom plot tabs.** A fifth kind of tab, made with the `+` at the end of the
strip and belonging to no dataset: it draws 1-D slices from anywhere in the file
together on one pair of axes. Datasets go in with the green plus beside them in
the tree, from a right-click there, or from a right-click on a line in the Plot
tab's legend — and any line can be written out by hand as `/group/dataset[:, 0]`
and given a name of its own for the legend.

The x axis is the element's index, a stated start/step/stop, or another dataset
read as a time base; a line that is not the axis's length is laid along it point
for point or stretched across the whole of it. Tabs are named, reordered by
dragging, and can be torn off into a window of their own so two sit side by
side.

**Saved views.** An arrangement of lines, an axis and a colour cycle, kept under
a name. Views outlive the file and the session — they are written to the
settings and read back at start-up — and each says how much of whatever is open
it can still draw: green for every dataset, amber for some, red for none. They
are offered best fit first, from the rail and from a caret beside the `+`, and
one that no longer fits says how many of its lines will not draw before it
lands.

**Under them.** The plot surface, the legend and the plot settings panel now
draw whichever plot they are given rather than the selected dataset's, so the
custom tabs reuse them rather than forking them. Reading a whole tab is one
crossing of the HDF5 thread however many lines it holds.

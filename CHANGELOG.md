# Changelog

One section per released version, newest first, and **every tag has one**. It
is what the release page says about itself: the GitHub release is published
with the contents of the matching section and nothing else, so a version with
no section here is a version that cannot be released — `tools/release-notes.sh`
refuses, and CI runs it on a tag push in its first job rather than after half an
hour of building.

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

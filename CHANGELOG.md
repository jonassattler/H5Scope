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

## 0.7.1

**A custom tab can be drawn with x and y swapped**, as `plot(y, x)` draws a
depth or altitude profile: "flip x and y" in its plot settings. Nothing is read
again and the zoom stays where it was.

**Each y axis of its own can be named.** The plot settings offer "y label 2"
onwards, and the name is drawn beside that axis in its line's colour. It
follows the line through a reorder and is kept in a saved view.

**A compound's selection reads as one line in the slice bar.** It no longer
stands apart from its path, and what it prints for an array member is a line it
will accept: `[:].samples[2]` came back as `[:, 2].samples` and was refused.

**Completion takes the row you chose.** Tab took the first row whichever was
highlighted, Return ignored the list, and a click on a row did nothing. All
three now take the highlighted row.

**Fixed:**
- Ticking a custom line's postprocessing off and on again could blank the plot
  until the window was resized.
- Closing a custom tab while it was looking up a dataset could crash.
- `/run.3` and member 3 of `/run` in a custom tab could draw each other's
  values.
- A crafted image range reaching an infinity, or a dataset whose extents
  multiply past what a 64-bit count holds, could read past the end of a buffer.
  Both are now refused.
- A table of object references leaked a hold on the file with every cell read.
- A plot shown in a detached window, or closed while drawn, could draw from
  memory it had already freed.
- An attribute with no elements printed as `0`; it prints `[]`.
- A failure while answering a read no longer ends the program; it is reported.

## 0.7.0

**A pipeline can be written as text.** Untick **enable visual editing** in the
postprocessing panel and its rows become the same pipeline written out a step
to a line — `/group/dataset`, `.select(samples)`, `.slice(1, :)`, `.max(1)` —
and the two stay in step either way round. The text is checked as you type,
with the step that will not run named in the reason; Return applies it and puts
it back a step to a line, and Shift+Return starts a new line. Writing another
dataset's path on the first line opens that dataset and runs the pipeline there.

**Eleven more operations.** `sum`, `prod`, `cumsum`, `cumprod`, `diff`, `clip`,
`sqrt` and `pow` are numpy's and answer what numpy answers, down to the last
bit of a sum; their arguments are written as numpy's are, by position or by
name, as in `diff(1, axis=0)` or `sum(0, initial=5)`. `add` and `multiply` take
a number, and `normalize(min, max)` rescales the finite values onto a range —
0 to 1 unless told otherwise.

**A custom plot's line can be a pipeline.** Each line's card has **enable
postprocessing**; ticked, its SLICE box becomes a DATA box that takes a whole
pipeline in the same text form, completes the dataset's path, and says so when
what the pipeline leaves is not a single line. A line with only a slice in it
reads exactly what it did before. **Add to custom plot**, at the foot of the
postprocessing panel, puts what the panel is drawing into a new or an existing
custom plot, as soon as that is a line.

**A custom tab's lines are drawn at full strength.** Adding a second line
used to dim every line on the tab, which is how the Plot tab shows where a
bundle of rows piles up but only hid lines that were each put there on purpose.

**The dataset's path at the top of the postprocessing panel is no longer cut
short** while there is room beside it.

## 0.6.9

**Zooming on a logarithmic x axis works.** The line used to be summarised in
buckets of equal numbers of samples, which on a logarithmic axis left the left
of the pane an order of magnitude coarser for every decade on screen: blank at
first, then a few straight strokes, whatever the zoom. It is now summarised one
envelope per pixel column, so the whole pane is drawn at the resolution the data
allows, from the first sample the axis can place, on the Plot tab and on a
custom tab alike, against a time base too. Zooming and panning read nothing
from the file. The zoom now stops with the same handful of samples across the
pane wherever the pointer is, rather than inside the gap between the first two
samples at the left and a hundred and sixty samples short at the right. A
logarithmic axis starts at the smallest value above zero in the data itself,
where a time base counting from zero used to start its axis a bucket along.

**The plot can be dragged past the ends of its data**, on either axis, as far
as leaves a quarter of the pane on it. At the zoom a plot opens on it can now be
dragged at all. A zoom after a drag stays where it is.

**A publication picture pastes into Word on Windows as black ink on white.** It
came through as a black rectangle with the numbers lost in it, because Word
drops the transparency. The copy now also carries a PNG, which keeps it.

## 0.6.8

**A line of a custom tab can have a y axis of its own.** Each card under Data
Settings has a **separate y-axis** box. Ticked, the line is drawn against an
axis that spans that line alone, as if it were the only one on the plot, drawn
to the left of the common axis and numbered in the line's own colour. The
common axis then spans only the lines left on it, and Plot Settings go on
applying to it alone: a separate axis is always linear. Tick every line and
there is no common axis at all. A plot of one line has one axis, so the box is
disabled until there is a second line, and the choice is kept for when there
is. Zooming and panning in y move every separate axis by the same share of its
range, about the same place on the pane, unless its **exclude from zooming**
box is ticked, in which case it keeps showing the whole of its line. The
crosshair reads a line on its own axis where that axis drew it. The axes
appear in a copied picture, in its colours, and are kept in a saved view.

## 0.6.7

**Either axis can be logarithmic, and reads as matplotlib's does.** A new
*scale* row under Plot Settings puts x, y or both on a logarithmic scale, on the
Plot tab and on a custom tab alike, to **base 10, 2, e or one you type**. Which
ticks the axis carries and which of them are numbered are matplotlib's own
`LogLocator` and `LogFormatterSciNotation`, ported and checked against
matplotlib 3.11 over some fourteen hundred windows: powers written `10³` with
the exponent raised, strided as matplotlib strides them, with minor ticks at 2,
3, … 9 times each decade, and those multiples numbered (`2×10⁰`) once the view
is inside a single decade. `dense` rules at every tick and `loose` at the
numbered ones. Zoomed until one tick at most is left, the axis is numbered in
round linear steps, as matplotlib's is — on every base, where matplotlib leaves
base 2 there without numbers. Under **number the subdivisions**, each unlabelled
multiple carries its digit in small type, as on log paper. A *custom* step is a
**factor**: ten gives the decades, two the doublings. Zoom and pan are measured
in decades. A value at or below zero is drawn as a **gap**, exactly as missing
data is, and the axis runs from the smallest reading it can draw, with
matplotlib's margins; a flat line runs between the powers either side of it.
The scale travels into a picture taken with "copy plot" and into a saved view.

A linear axis zoomed far in, to a span under a ten-thousandth, now writes enough
figures to tell its ticks apart rather than "1.0e+0" at every one of them, and
no longer loses the tick at its own top to rounding.

## 0.6.6

**A figure's size can be typed in centimetres.** Settings > Plot Settings
offers the density and both sides of the figure as three boxes over one
number: the pixel count is settled a row above, so at a stated size a density
*is* a physical size. Write any one of the three and the other two follow —
8.5 cm wide at 1920 pixels is 574 dpi, and the height comes to 4.78. The pixel
count does not move, as ever. It replaces the readout of the inches it came
to, which said the same thing and could not be typed in.

**The plot's frame is a box.** The two rules the readings are read against are
now four, so a trace that runs out of the window at the top stops at a rule
rather than fading into the gutter, and the pane has an edge without a label
having to say where it is.

**Okabe-Ito opens on its orange.** Its achromatic entry — black on paper,
signal white on the dark theme — is drawn last rather than first. A plot opens
on this cycle and usually has one line in it, and that line was being drawn in
an ink the reader could not tell from the chrome. The colours and the cycle
are the published ones, offset by one.

**The tree's tags stand in a column of their own.** Room for all three on
every row, with whatever a row carries packed against its right-hand end, so
they line up instead of shifting with the number of tags the row happens to
have.

**Fixed:** dragging a selection band over a plot wrote two *Unable to assign
[undefined] to double* warnings to the console on every frame of the drag.

## 0.6.5

**Dots per inch now means what it says.** 0.6.4 read the setting as a
supersample: a figure asked for at 1920 × 1080 and 300 dpi came back at
6000 × 3375, with the type exactly as small against the picture as it had
been. That is not what a resolution is for. The size and the density are two
settings and both of them change the figure — the size is how many pixels
come back, and the density is how big those pixels are, which is what decides
how large the type, the rules and the ticks are *against* the plot. A
1920 × 1080 export at 300 dpi is now a 6.4 × 3.6 inch figure with its type at
its true point size, and it is 1920 × 1080 pixels at every density, as asked.
The dialog says the inches it comes to. Nothing changes at the default, and a
size asked for in pixels is still exactly those pixels.

## 0.6.4

**Publication mode keeps the colours.** It drew every stroke in one black ink,
which threw away the one thing a colour cycle is for: six traces pasted into a
document were six identical strokes, and the caption in the corner named them
in colours that were nowhere in the picture. A publication copy is now exactly
the picture the light theme would draw — its chrome and its line palette both,
whichever theme you are in — and the caption names what is actually drawn. One
line on the default cycle is still black, because that is okabe-ito's first
entry on paper; the accent is too, because signal white on a white page is not
a faint line but no line.

**A copy can be asked for at a resolution rather than at the display's.**
Settings > Plot Settings grows a *resolution* row: the picture is composed at
the size above it and rendered at as many dots per inch as you ask for, so the
same plot copied on a scaled laptop and on a plain monitor is the same picture.
The image carries the number as well, so it lands in a document at its true
physical size instead of at one dot per point. The default is unchanged — the
display's own scale, which is what has always been copied — and at 96 dpi a
point is a pixel, so a custom size still means exactly the pixels it says. The
dialog prints what will come out.

## 0.6.3

**A region drag says what it is selecting, in numbers.** The band said where
you were about to look and nothing about what you would see. It now writes the
corner the drag started at, the corner it has reached and how wide and how tall
it has become — in the axes' own units, out of the same arithmetic the zoom
resolves the band with, so the numbers you read while deciding are the window
you get, to as many digits as the view can resolve — the axis's own rule for
its ticks, so the numbers beside the band and the numbers under it agree and
grow digits together as you zoom. The lengths are written against all four
edges where there is room for one and left out where there is not, and the two
that measure the sides read along them. Nothing is drawn over the band, over
another number, off the pane, or under the pointer: the corner that follows the
cursor is written beside it, because an arrow is drawn down and to the right of
its own hotspot and that is exactly where the reading you are dragging used to
be. The crosshair goes off while a band is being drawn: it answers a different
question, in the same face, over the same picture.

**A wildcard search costs what a plain one does.** Any pattern that opened with
a star paid a back-tracking walk over every name in the file, whether it matched
anything or not — `*item*zz*` matches nothing at all in a file of 188,000 names
and still cost five times what a plain substring did, for every character typed.
A pattern of stars and letters is now the literal pieces between the stars,
found with the same scan a plain substring already got: 6 ms a keystroke on that
file against 17, and the same answers, held against
`QRegularExpression::fromWildcard` over a table of patterns and paths. Patterns
with `?` or `[…]` still take the walk, and now reject on a literal before
starting it.

**The filter box waits for you to stop typing.** A keystroke used to be a
search, so typing `temperature` bought eleven searches for prefixes you were
already abandoning — and on a large file each of them was a pause. A character
now arms the search and abandons the one before it; what runs is what stands in
the box when the typing stops. Clearing is not settled, because an empty box is
the file being asked for back. The tree also closes on the first character
rather than after the first search, so the search that used to be the expensive
one is cheap too.

**A point marker marks a measurement.** A dot is drawn at every sample, and it
was being drawn on summarised points as well — where a drawn point is the
largest or the smallest of a bucket, which is a reading nobody took. A line the
model folded on the way out of the file now carries no markers however the
setting is set and whatever it is drawn against; zooming in until a bucket is
one element brings them back.

**The tree's names stand clear of the caret.** The selection began four pixels
in front of the name, which is behind the arrow's own ink, so a lit slab ran up
to the arm of the chevron. There is a gap between them now and the mark starts
in the middle of it.

**Icons sit in the middle of their buttons.** Every glyph was drawn in the
top-left corner of a box two pixels taller than it was wide: the cross that
closes a custom tab, the cross on a data-settings card, all of them two pixels
high and four pixels narrower than they were meant to be. And a bare icon
button now answers the pointer — its hover ground and its rim were the same
colour as a hovered tree row, so the plus that puts a dataset into a custom plot
had nothing to say when you pointed at it.

**A copied plot is a figure rather than a screenshot.** Settings > Plot
Settings says what one looks like, and it is remembered between runs.
*Publication mode* draws it for print: every stroke in black, the light
theme's chrome whichever theme you are in, and no ground at all, so the picture
stands on the page it is pasted into rather than on a slab of this
application's own colour. *Include cursor* decides whether the crosshair and
the sample it has snapped to are part of it, which also settles an old
inconsistency — the button never caught one and Ctrl+C always did. And a
*custom size* in pixels draws the picture again at that size, with its own
ticks and its type set for it, over exactly the x and y range on screen. None
of it touches the pane you are looking at: the picture is a second frame drawn
where nobody can see it.

**A line in a custom plot can be given a colour of its own.** Right-click its
name in the legend, or use the swatch on its card in the data rail: either way
that one line takes the colour and every other line stays where it was.
*Clear colour* gives it back to the cycle rather than freezing whatever the
cycle last said. A colour travels with a saved view, and views saved before
this read back exactly as they did.

**Three published colour cycles, and plots open on Okabe-Ito.** Okabe-Ito,
Paul Tol bright and Paul Tol muted, at the values they are published at, so a
figure drawn here sits beside one a colleague drew in matplotlib or R and the
two agree about which line is which. Okabe-Ito's black is drawn at signal
white on the dark theme, where black is the ground. `spectrum` and `safe` are
still there and are still the two solved for this application's own two
grounds — these three were designed for ink on paper, and their palest entries
are faint on the light theme.

**A group is not a dataset, and the Table tab now says so.** Plot and Image
were already greyed when the selection had nothing for them; Table never was,
on the true half of the thought that it serves every datatype — which is about
*which* dataset and says nothing about whether there is one. Clicking a group
handed you a Data Viewer with its slice bar gone and a sentence where the grid
should be. All three are greyed together now, in the tab strip, in the View
menu and under Ctrl+2.

**The tree's tags stand against the readout.** The letters marking a link, an
image and a count of attributes sat directly after the name, which put them
somewhere different on every row — the name is the one thing on a row whose
length is arbitrary. They are pinned to the readout's left edge instead, so
they line up down the pane, and a row with no tags still spends no width on
them.

**The plot's numbers are drawn at full contrast.** The ground under a plot is
already pure black or pure white; the ticks, the axis names and the title were
set a step off the ink, as a label is everywhere else in the application. They
are the readings rather than labels for one, and they are now the exact
inverse of what they are drawn on.

## 0.6.2

**Drag a region with the right button and the plot goes there.** The wheel
zooms about a point, which meant a reader who could already see the part they
wanted had to arrive at it by turning the wheel and correcting with a drag, by
eye, several times. The band says where to look and the plot reads towards it
at once rather than after the gesture settles. Dragging a few pixels does
nothing: that is a slip, and a two-pixel-tall window is a magnification of
several hundred on an axis nobody meant to touch.

**...and the window is four numbers under Plot Settings > View.** X start and
stop, Y start and stop — a range you have in mind rather than one you can point
at, which is what a wheel cannot be asked for. They are the readout as well:
selecting a region, zooming, panning and resetting all report into the same
four boxes. They take `0.2`, `0,2` and `1.2e-3`, and the window is clamped to
the data, because there is nothing outside it to show.

**The grid has four densities instead of two.** None, loose, dense, and custom
with a step of your own for each axis. The numbered ticks do not move with it —
a grid is a reading aid and an axis printing sixteen labels down a narrow pane
has answered a question nobody asked — so dense rules *between* the labels and
draws those minor rules a step weaker.

**A plot can carry a title, two axis names and a legend.** All four are empty
or off to begin with and cost no room at all while they are: an untitled plot's
pane is exactly where it was. The legend is drawn in a corner of the plot
itself, naming each drawn line beside its own colour, with a custom tab's alias
where it has one.

**A plot goes to the clipboard as a picture.** "Copy plot" under Plot Settings,
or Ctrl+C with the pointer over the pane, on Windows and on Linux. What is
copied is the plot — the ground, the rules, the ticks, their labels, the title,
the axis names, the strokes and the legend in the corner — and not the panel of
controls beside it. That is what the legend on the plot is for: six unnamed
traces pasted into a document are six traces nobody can read.

Saved views made before this release keep their grid: "on" is what `loose`
now means.

## 0.6.1

**A compound's selection is one line.** Over a compound the slice bar makes
everything after the path a single editable box — `[0:1000].energy`, brackets
and all, checked as you type and applied whole. It was a second box after the
closing bracket, and that was the wrong shape: a chain and the subscript over
the axes it appends are one statement, so rearranging one is usually
rearranging both, and two boxes made that two edits with a shape nobody asked
for in between.

**A custom tab drawn against a time series zooms.** It was the one axis of the
three that could not: the line was refused a closer look because a time base is
a lookup table rather than a formula, and the time base itself was read once at
a pane's worth of points and never again — so zooming stretched the picture
instead of resolving it, and past a few octaves every sample in a column shared
one x and the curve drew as a staircase. A time base that only ever goes one
way is a map that can be run backwards, and that is every time base anyone
plots against: the range on screen is turned back into a range of elements, and
the axis is now held whole and folded finer with the lines it carries. Zooming
a time series costs what zooming an index costs, which is nothing.

**The filter box searches the whole file, and answers out of memory.** It used
to match what you had expanded, which made a search over a file nobody had
walked find nothing and made one over a file somebody *had* walked cost a
recursive pass over the tree with two regular-expression matches per row —
three hundred thousand objects measured at 160–330 ms a keystroke, and a
wildcard over a group of sixty-five thousand members took twenty-two seconds.
Every name is now read once, in the background, into one block of memory, and a
keystroke is a linear pass over it: 5 ms for a run of characters and 30 ms for
the worst wildcard, at three hundred thousand objects. What it finds is
complete — a name eight levels down in a branch you have never opened is found,
the branches holding it stay on screen, and the tree opens itself to the results
when there are few enough to be results. The box says how many there are. The
tree is still lazy: nothing is listed to answer a keystroke that nothing is
being shown for.

**The RAM budget now changes what is already held.** Turning it down used to
free nothing at all until you selected another dataset, and turning it up made
no zoom any cheaper — the setting trimmed a few cached runs and left the thing
actually holding the memory alone. Coarsening a held line is exact and costs
nothing, so turning the budget down is honoured in the moment you turn it; a
line is only re-read when you ask for *more* memory than it was built with.
Opening another plot tab shrinks the ones already open, so the total stays the
number the setting promises. And the first draw of a large line is about a
quarter quicker, in the innermost loop that folds it.

**Every card on the Information tab stays on screen.** The tab used to scroll as
a page, so an object with a lot to say about itself put half of what it said
below the fold, with nothing at the top of the window to say there was anything
under it. The cards now share the window out between them: each is drawn at the
height it wants for as long as there is room for it, and the ones there is no
room for scroll their own contents instead.

## 0.6.0

**Compound datasets are addressable.** A table of structs used to be a
terminus: the viewer could show you struct number four and nothing else, so a
ten-million-row event table was a grid of `{time, energy, …}` cells and no way
in. Naming a member with `.` opens it — `.energy` is a line the Plot tab draws,
`.position.x` is another, `.samples` adds an axis the table can lay out — and it
reaches the table, the plot, the image, the custom tabs and the postprocessing
pipeline alike, because all of them read through one interface and none of them
learned what a compound is.

A subscript written after a member binds to the axes that member contributes, so
`array[i, j].b[k]` and `(array[:, :].b)[i, j, k]` are the same selection, element
for element and shape for shape. It is spelled in three places: a box after the
slice bar's closing bracket, a whole line in a custom tab
(`/events[0:1000].energy`), and a **Select** row at the head of the
postprocessing panel. Saved views migrate untouched — every expression without a
`].` in it parses exactly as it always did.

A member is read as a member and not as a struct: the transfer asks HDF5 for
that field alone, so `.energy` over a hundred thousand records moves four bytes
a record rather than ninety-six, and a member line costs the same reads as a
dataset of its own — including the zoom, which still reads nothing at all.
Variable-length members are indexable but not sliceable: `.tags[3]` is a line
with a gap wherever a record's list was too short, and a range of one is refused
with a sentence saying what to write instead.

**The two text fields that are typed into now say what could come next.** A
custom tab's entry and the member box offer what matches as you type — groups
and datasets, then the datatype's members — and Tab writes as much as every
candidate shares, completing a dataset with the subscript that selects the whole
of it at the right rank. Nothing is read to answer a keystroke that you had not
already asked to see.

**The Information tab opens a compound out**, under the row that names its type:
a tree indented until nothing is left but base types, with an enum's symbols
where they were previously unprintable. And the JSON beside a compound cell is
written to be read — a struct over lines, a list of numbers on one.

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

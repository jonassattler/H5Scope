// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

pragma Singleton

import QtQuick

/// Design tokens for the whole application -- the TBD design system, ported.
///
/// Every colour, radius, spacing step and font in the UI resolves through this
/// object, so the look is defined in exactly one place.
///
/// The design system is this project's own and is licensed with it. It was
/// authored as CSS custom properties, for a web consumer; this file is that
/// token set ported to QML, and it is the only definition the application has.
/// Nothing here reads CSS, at run time or at build time.
///
/// "Upstream" in the comments below means that original CSS token set. Where
/// this port departs from it -- and it does, in the light scope especially --
/// the departure is argued at the property rather than silently absorbed.
///
/// Token name -> property name:
///
///     --n-0 .. --n-11      n0 .. n11          the neutral ramp, verbatim
///     --surface-base       background
///     --surface-raised     surface
///     --surface-card       surfaceRaised
///     --surface-inset      surfaceInset
///     --surface-hover/     surfaceHover / surfaceActive
///       -active
///     --line-1 / --line-2  border / borderStrong
///     --line-strong        borderGuide
///     --text-body          textPrimary        (see the note below)
///     --text-primary       textEmphasis       (see the note below)
///     --text-muted/faint   textSecondary / textDisabled
///     --accent-primary     accent             signal white
///     --accent-secondary   warning            hazard amber
///     --status-crit/info   danger / info
///     --s-1 .. --s-14      s1 .. s14          the 2px spacing grid
///     --r-0 .. --r-3       radiusNone, radiusS, radiusM, radiusL
///     --dur-1 .. --dur-5   dur1 .. dur5
///
/// NOTE on the two text names. Upstream sets `--text-body` and `--text-primary`
/// to the same pure white in the dark scope, so `textPrimary` and
/// `textEmphasis` are one colour there and only the type weight separates a
/// heading from its body. They diverge in the light scope -- `--text-body` is
/// n3, `--text-primary` is pure black -- which is the reason both names are
/// still carried rather than collapsed into one.
///
/// The Basic Controls style is used deliberately: it is pixel-identical on
/// every platform and carries no built-in visual opinions to fight, so what is
/// defined here is what ships everywhere.
QtObject {
    id: theme

    /// Flip to re-theme the whole UI. Wired to View -> Dark Theme in the menu.
    property bool dark: true

    // --- neutral ramp ----------------------------------------------------
    // Substrate black to signal white. Carries 95% of every surface; the
    // semantic colours below are all drawn from it.
    readonly property color n0:  "#000000"
    readonly property color n1:  "#08090A"
    readonly property color n2:  "#0E1012"
    readonly property color n3:  "#15181A"
    readonly property color n4:  "#1D2124"
    readonly property color n5:  "#282D31"
    readonly property color n6:  "#3A4045"
    readonly property color n7:  "#5A6268"
    readonly property color n8:  "#8B959B"
    readonly property color n9:  "#B9C2C7"
    readonly property color n10: "#E4E8EA"
    readonly property color n11: "#FFFFFF"

    // --- signal colours --------------------------------------------------
    // White IS the accent. Amber, cyan and red are reserved for state and are
    // never used decoratively.
    readonly property color sig500:   "#FFFFFF"
    readonly property color sig200:   "#E4E8EA"
    readonly property color amber500: "#FFB000"
    readonly property color amber600: "#D89400"
    readonly property color cyan500:  "#4DE1FF"
    readonly property color cyan900:  "#0B3A47"
    readonly property color red500:   "#FF3B1F"
    /// Two signal colours the upstream set has no light-scope entry for.
    ///
    /// A signal colour on this system is bright ink on a black ground, and
    /// bright ink on a *white* one carries almost no contrast at all: laser red
    /// reads at 3.6:1 against white where it reads at 5.9:1 against black, and
    /// hazard amber falls from 11.5:1 to 2.6:1 -- which is a warning nobody can
    /// see. These are the same two hues taken down until they read against
    /// white about as strongly as their bright forms read against black, which
    /// is what makes a warning in the light theme as loud as one in the dark.
    /// See the note above the light scope below.
    readonly property color amber700: "#7A5200"
    readonly property color red700:   "#C42000"

    // --- the light scope --------------------------------------------------
    //
    // Upstream's light scope is one line of CSS labelled "for print/marketing
    // blocks", and it shows the moment a UI is built on it: `--surface-card`
    // and `--surface-base` are both pure white, so a raised card is invisible
    // against the window behind it; `--surface-raised` is *darker* than the
    // card that is supposed to sit on top of it, so the elevation order runs
    // backwards; and every hover, line and muted text is a fraction of the
    // separation the same role carries in the dark.
    //
    // So the light scope here is derived rather than transcribed, by one rule:
    // **a role reads against white as strongly as it reads against black**.
    // Contrast is WCAG's relative luminance -- the same arithmetic `inkOn`
    // below uses -- so for a dark-scope colour `c` the light-scope colour is
    // whichever one satisfies
    //
    //     (L_light + 0.05) / 1.05  ==  0.05 / (L_c + 0.05)
    //
    // and every value in the two blocks under this one was solved for that,
    // then snapped to an entry of the neutral ramp wherever one landed close.
    // The result keeps the ramp's own blue-grey cast, keeps the elevation
    // order (further from the ground is further from white), and makes a
    // hover, a rule and a heading carry the same weight in both themes --
    // which is what "match the intensity of the dark mode" asks for.
    //
    // Two roles are deliberately *not* mirrored: `textPrimary` stays at
    // upstream's n3 rather than going to pure black, because a page of body
    // text set in pure black on pure white is the one place this rule would
    // make the light theme worse than the system that defined it; and the
    // signal colours cannot reach their dark-scope contrast in any hue that is
    // still recognisably amber or red, so they take amber700 and red700 above,
    // which get as close as the hue allows.

    // --- semantic surfaces -----------------------------------------------
    // Four stacked surfaces separated by hairlines, never by shadows. The base
    // and the inset are the same tone -- upstream's `Surfaces & lines` card is
    // explicit that only cards lift off it -- so what separates an inset field
    // from the window behind it is its border, not a step in tone. That is
    // true black in the dark theme and pure white in the light one, which is
    // the mirror of the same statement.
    readonly property color background:    dark ? n0 : n11
    readonly property color surface:       dark ? n1 : "#F8F9F9"
    readonly property color surfaceRaised: dark ? n2 : "#F2F4F5"
    readonly property color surfaceInset:  dark ? n0 : n11
    readonly property color surfaceHover:  dark ? n4 : "#DFE3E5"
    readonly property color surfaceActive: dark ? n6 : n9
    /// The ground under a box holding something typed and not yet applied.
    ///
    /// This project's own, with no upstream token: the system has no state for
    /// "entered but not committed" because it has no control that defers. This
    /// application's slice line and pipeline arguments all do -- every
    /// keystroke is checked and nothing is applied until the reader commits --
    /// and the reader had no way to tell an applied box from an edited one.
    ///
    /// A tenth of the way from the inset toward the signal, which is a step of
    /// ground rather than a colour: the box lifts a little, the way a hovered
    /// row does, instead of turning amber and claiming something is wrong.
    /// Derived rather than snapped to a ramp entry so that it is the same
    /// distance in both scopes -- the inset is true black in one and pure
    /// white in the other, and the accent is the opposite of it in each.
    readonly property color surfacePending: mix(surfaceInset, accent, 0.1)
    /// --surface-invert / --text-invert. The ground flipped: the design
    /// system's Tooltip is the only thing that stands on it, and it does so
    /// precisely because nothing else in the UI does.
    readonly property color surfaceInvert: dark ? n11 : n0
    readonly property color textInvert:    dark ? n0 : n11

    // --- lines -----------------------------------------------------------
    // `1px solid border` does the structural work everywhere.
    readonly property color border:       dark ? n4 : "#DFE3E5"
    readonly property color borderStrong: dark ? n6 : n9
    /// --line-strong. Used for the tree's connector guides, for the table's
    /// header rule, and for the menu drawer's edge. Upstream's light scope
    /// puts this at n8, which is weaker than its own --line-2 and so drew the
    /// *strongest* rule in the UI as the faintest one.
    readonly property color borderGuide:  dark ? n9 : n6
    /// The rule drawn down the postprocessing panel, and the only line in the
    /// application set at the ink of body text.
    ///
    /// Deviation, and a deliberate one: upstream has no token above
    /// --line-strong, because upstream has no line that is the subject of its
    /// own panel. This one is. The rest of the UI draws rules to separate
    /// things that are already legible without them, and borderGuide is the
    /// right weight for that; the chain in the pipeline panel *is* the
    /// pipeline -- it is the only thing saying that seven rows are one
    /// sequence rather than seven settings -- and at a separator's weight it
    /// read as a smudge down the gutter. Drawn at text ink it reads as
    /// drawing, which is what it is.
    readonly property color chainGuide:   dark ? n11 : n0
    /// The banding on alternate table rows.
    ///
    /// Not a surface step: it is texture, which is all a data grid should
    /// carry. The system puts its one permitted texture at 4-6%
    /// (`guidelines/grid-texture.card.html`), but that figure is for a pattern
    /// over a *raised* surface; this band is over the inset, which is true
    /// black, and 4% of white on black is two shades off nothing. It takes the
    /// top of the range and then some -- what matters is that the reader can
    /// follow a row across the table, which is the only thing striping is for.
    readonly property color rowStripe: dark ? Qt.rgba(1, 1, 1, 0.09)
                                            : Qt.rgba(0, 0, 0, 0.072)

    // --- semantic text ---------------------------------------------------
    readonly property color textPrimary:   dark ? n11 : n3
    readonly property color textEmphasis:  dark ? n11 : n0
    readonly property color textSecondary: dark ? n9 : n6
    // --text-faint is n8 in both scopes upstream, which is the one place that
    // reads as a transcription error rather than a decision: n8 carries 6.9:1
    // against black and 3.1:1 against white, so every count, shape and unit in
    // the light theme was drawn at less than half the weight of the same thing
    // in the dark. n7 is the entry that matches.
    readonly property color textDisabled:  dark ? n8 : n7

    // --- accents ---------------------------------------------------------
    // Upstream leaves --accent-primary at signal white in the light scope,
    // where it is invisible. Deviation: the light theme inverts the signal to
    // pure black, which is what that scope already does for --text-link.
    readonly property color accent:      dark ? sig500 : n0
    readonly property color accentHover: dark ? sig200 : n3
    readonly property color accentText:  dark ? n0 : n11

    /// What the image view stands its picture on, until a reader says
    /// otherwise for a particular dataset.
    ///
    /// The extremes rather than the surfaces: a raster is judged against a
    /// ground, and every image editor there is offers black or white for it
    /// because those are the two that add nothing of their own to what is
    /// drawn on them. Which of the two follows the theme, so the ground is the
    /// one the reader's eye is already adapted to.
    readonly property color imageGround: dark ? n0 : n11
    /// The two squares of the checkerboard an image with transparency is drawn
    /// over -- the same convention, and for the same reason: neither square is
    /// a colour a channel could have produced, so what shows through is
    /// unmistakably nothing rather than a dark or a pale pixel.
    readonly property color checkerLight: dark ? n4 : n11
    readonly property color checkerDark:  dark ? n2 : "#CDD3D6"
    /// The side of one square. Sixteen logical pixels is what the editors this
    /// borrows from use, and it is large enough to read as a pattern rather
    /// than as noise behind a picture drawn at one pixel per cell.
    readonly property int checkerSize: 16

    /// Black at 72%, behind modals. The only transparency in the system --
    /// there is no frosted glass over content anywhere.
    readonly property color scrim: Qt.rgba(0, 0, 0, 0.72)

    /// Laid over the ends of a colour ramp that the value range has cut off.
    ///
    /// The ramp bar itself never changes -- it is what the colours *are*, and
    /// a control that redraws its own scale as the reader moves a handle gives
    /// them nothing to move it against. What the band does is say which values
    /// reach the bar, so the part outside it is veiled rather than removed:
    /// still legibly the same ramp, visibly not in play. Lighter than `scrim`,
    /// which has a whole window to suppress; this has 18 pixels.
    readonly property color rampVeil: dark ? Qt.rgba(0, 0, 0, 0.62)
                                           : Qt.rgba(1, 1, 1, 0.68)

    // Amber, cyan and red are the only colours this system spends on state, so
    // they are the ones that most have to carry in both themes. Each takes its
    // bright form on black and its deep form on white; see amber700 and red700
    // above for why the light forms are darker than upstream's, and cyan900 --
    // which upstream does define -- for the one it already had.
    readonly property color warning: dark ? amber500 : amber700
    readonly property color info:    dark ? cyan500 : cyan900
    readonly property color danger:  dark ? red500 : red700

    // --- spacing ---------------------------------------------------------
    // The 2px grid, verbatim: 1 2 4 6 8 12 14 18 24 32 44 60 80 112.
    readonly property int s1:  1
    readonly property int s2:  2
    readonly property int s3:  4
    readonly property int s4:  6
    readonly property int s5:  8
    readonly property int s6:  12
    readonly property int s7:  14
    readonly property int s8:  18
    readonly property int s9:  24
    readonly property int s10: 32
    readonly property int s11: 44
    readonly property int s12: 60
    readonly property int s13: 80
    readonly property int s14: 112

    // Named steps the views actually reach for, all pinned to the grid above.
    // These five are this project's own names -- upstream has no token for
    // them -- so they keep their step on the ladder and tighten with it.
    readonly property int gapXS: s2
    readonly property int gapS:  s4
    readonly property int gapM:  s5
    readonly property int gapL:  s6
    readonly property int gapXL: s8

    // These two do have upstream tokens, and --page-pad and --gutter moved
    // independently of the ladder, so they are bound to the step that matches
    // the token rather than to the step they used to sit on.
    readonly property int pagePad: s8   // --page-pad, 18
    readonly property int gutter:  s6   // --gutter, 12

    // --- shape -----------------------------------------------------------
    // 2px is the default; 6px is the ceiling. Pills are for status dots only.
    readonly property int radiusNone: 0
    readonly property int radiusS:    2
    readonly property int radiusM:    3
    readonly property int radiusL:    6

    readonly property int borderWidth: 1
    readonly property int borderWidthAccent: 2

    // --- the device pixel grid ---------------------------------------------
    /// How many physical pixels the window draws to one of the logical ones
    /// every measurement in this file is written in. Written by Main.qml off
    /// the screen the window is on; 1 until something says otherwise, which is
    /// what a headless test and a display at 100% both are.
    ///
    /// It exists for `snap` and `hairline` below, and for nothing else: no
    /// layout in this application is written in physical pixels.
    property real pixelRatio: 1.0

    /// `value` moved to the nearest length that is a whole number of physical
    /// pixels.
    ///
    /// A fractional scale factor -- 125%, 150%, the settings every desktop now
    /// offers -- is what makes this necessary. At 150% a column 61 logical
    /// pixels wide is 91.5 physical ones, so the seams of a table of them fall
    /// alternately on and between physical pixels, and the rules drawn at those
    /// seams come out alternately one pixel wide and two. That is not a
    /// rounding error anyone can unsee: it makes half the lines of a grid look
    /// heavier than the other half. Snapping the *cell* size is what fixes it,
    /// because every seam after the first is a multiple of it.
    function snap(value) {
        const ratio = theme.pixelRatio > 0 ? theme.pixelRatio : 1.0
        return Math.max(1, Math.round(value * ratio)) / ratio
    }

    /// `borderWidth` as a whole number of physical pixels: the width to draw a
    /// rule at when it has to come out the same everywhere it appears.
    readonly property real hairline: theme.snap(theme.borderWidth)

    // --- metrics ---------------------------------------------------------
    // Fixed chrome, all separated by hairlines. Content scrolls; chrome never
    // does.
    readonly property int toolbarHeight:   40  // --topbar-h
    /// --menubar-h. The application's menu bar, and the top of the window.
    readonly property int menuBarHeight:   28
    readonly property int tabBarHeight:    28  // --header-h
    readonly property int statusBarHeight: 22  // --statusbar-h
    readonly property int sliceBarHeight:  38
    readonly property int treeWidth:       344
    readonly property int treeHeaderHeight: 28 // --header-h
    readonly property int treeRowHeight:   26
    readonly property int rowHeight:       28
    readonly property int controlHeight:   32  // --ctl-h-lg
    readonly property int smallControlHeight: 26 // --ctl-h-md
    /// --ctl-h-sm. A menu row: denser than any other control in the UI,
    /// because that is the desktop convention a menu is measured against.
    readonly property int tinyControlHeight: 22
    /// The menu drawer's floor width, from the design system's MenuBar.
    readonly property int menuMinWidth:    212
    /// ...and its ceiling. A drawer sizes itself to its longest row, and one
    /// row of Open Recent carries a folder path: without a ceiling a single
    /// deep directory sets the width of a drawer of ten short file names.
    readonly property int menuMaxWidth:    420
    /// The gutter a checked menu row puts its bullet in. The system has no
    /// checkmark glyph, so the mark is a dot and this is the space it takes.
    readonly property int menuMarkWidth:   12
    /// The SplitView's grab target. Painted as a hairline like every other
    /// seam, but a 2px gap step would be too narrow to catch with a pointer,
    /// so the handle keeps a width of its own.
    readonly property int splitHandleWidth: s4
    readonly property int indent:          16
    /// A status badge, the one size the design system defines, and the tighter
    /// variant the tree's tag column needs to fit three of them in a 26px row.
    readonly property int badgeHeight:        18
    readonly property int badgeHeightCompact: 16
    /// Narrowest the tree's right-hand readout may be drawn at. Below this it
    /// is an ellipsis and nothing else, so it is dropped instead -- the name
    /// is what the reader came for, and it takes the space back.
    readonly property int treeMetaMinWidth:  40
    /// Narrowest an Information panel may get before the layout reflows to one
    /// fewer column.
    readonly property int panelMinWidth:   340
    /// The label column inside one. Wide enough for the longest label the
    /// Information tab produces, which is an attribute name rather than one of
    /// its own words -- "IMAGE_SUBCLASS" is fourteen tracked mono characters.
    readonly property int infoLabelWidth:  s13 * 2
    readonly property int indexColumnWidth: 96
    readonly property int railWidth:       212  // --rail-w
    /// What the rail widens to for the postprocessing panel, and only for it.
    ///
    /// Every other panel is a column of settings and 212 is the width the
    /// design system gives that. This one is a *pipeline*: each row states an
    /// operation, its argument, the shape it leaves and a way to remove it, on
    /// one line, because the shape column read down the rows is the whole of
    /// what the panel is for. Stacked into 212 those become three lines a row
    /// and the shapes stop lining up with each other. So the rail takes the
    /// width this one panel needs and hands it straight back on close.
    readonly property int railWidthWide:   460
    /// The two columns down the left of a pipeline row, side by side rather
    /// than on top of each other: the handle a step is dragged by, and then
    /// the rule that joins the steps to one another.
    ///
    /// The handle is a whole icon wide, for the reason splitHandleWidth is
    /// wider than the split it moves: a pointer needs something to catch, and
    /// a gap step is not it. The rule column is only as wide as the corner it
    /// has to draw.
    readonly property int dragHandleWidth: s8
    readonly property int stepGutterWidth: s6
    /// The application's own file picker. Wide enough for a long path and
    /// tall enough for a dozen entries, which is where browsing stops feeling
    /// like peering through a slot.
    readonly property int pickerWidth:     760
    readonly property int pickerHeight:    520
    /// What the slice well is guaranteed at the narrowest the window goes.
    /// The rest of the bar is measured rather than assumed -- button labels
    /// are text, and text is the one thing whose width the host decides -- but
    /// the well holds a path and a subscript list, which are data, and no
    /// measurement of the current one can stand in for the next. So the window
    /// promises the well this much and the well spends it as SliceField says.
    /// Enough for a short path and a rank-four slice whole, which is the case
    /// the bar is read in.
    readonly property int sliceWellMinimum: 240
    /// The measure for running prose in a dialog -- the About box's licence
    /// notice is the only one. Narrower than the picker on purpose: a
    /// paragraph set to the width of a file list is a paragraph nobody reads.
    readonly property int dialogTextWidth: 420
    /// How much of the Data Viewer the overview grid takes before the stack of
    /// text panes begins, when a string dataset has both.
    readonly property int stringGridHeight: 176
    /// The table setup panel's sliders. The track is a hairline pair thick, so
    /// it reads as a rule rather than as a trough; the handle is the smallest
    /// square that still takes a pointer.
    readonly property int sliderTrackHeight: 2
    readonly property int sliderHandleSize:  12
    /// The colour ramp drawn as a slider's own track, which is what the value
    /// range is set on: 18px, the height of a Badge, so the bar reads as a
    /// thing in its own right rather than as a thickened rule.
    readonly property int rampBarHeight:     s8
    /// A handle on that bar. Not the round knob a plain track takes -- a band
    /// is set by *where its edge falls*, and an edge is a rule, which is what
    /// the system draws a boundary with everywhere else. Wide enough to catch
    /// with a pointer, and it overhangs the bar so the grip is reachable
    /// without covering the colour under it.
    readonly property int rampHandleWidth:   s4
    readonly property int rampHandleOverhang: s3
    /// A checkbox's or radio button's mark. Sized here rather than off a gap
    /// step because it is a pointer target, not a space between things: the
    /// 2px grid tightens the gaps around it and must not take the target with
    /// them. The same reasoning as splitHandleWidth above.
    readonly property int indicatorSize:     12
    /// Floor for a text pane, so a one-word element still reads as a pane and
    /// the stack keeps an even rhythm.
    readonly property int textPaneMinHeight: 72
    /// The Data Viewer's plot. A line is a hairline like every other stroke in
    /// the system; the marker is the smallest dot that still reads as one.
    readonly property real plotLineWidth: 1.0
    readonly property real plotMarkerSize: 4.0
    /// A bundle of lines is drawn in the one accent rather than in a colour per
    /// series -- the system rations colour to state, and a rainbow would spend
    /// it on decoration. Overlap is what separates them, so they are drawn
    /// under full strength; a single line keeps it.
    readonly property real plotSeriesOpacity: 0.55
    /// Inset between the chart's frame and its plot area, where the axis
    /// labels live. The left side takes more because a y label is a number of
    /// several digits written sideways-on to the axis, where an x label is a
    /// column index under it; the other two sides are air, and take a gap.
    readonly property int plotMargin: s7
    readonly property int plotLabelMargin: s10
    /// A settings panel's rows are taller than a table's: each carries a
    /// control, not a line of text.
    readonly property int settingRowHeight: 30
    /// A line icon inside a control, drawn on a 24-unit grid and scaled to
    /// this. Two-thirds of the control it sits in, which leaves the icon set
    /// into the button rather than crammed against its rim.
    readonly property int iconSize: 18

    // --- typography ------------------------------------------------------
    // IBM Plex Sans for everything readable, IBM Plex Mono for everything
    // measured. Both are compiled into the binary and registered before any
    // QML loads (see gui::loadEmbeddedFonts), so these names always resolve to
    // the shipped faces and never to whatever the host has installed. The
    // trailing entries are a safety net that should never come into play; the
    // QML suite fails if the bundled faces stop loading.
    readonly property var sansFamilies: ["IBM Plex Sans", "Helvetica Neue", "Arial"]
    readonly property var monoFamilies: ["IBM Plex Mono", "DejaVu Sans Mono", "Menlo", "Courier New"]

    readonly property string fontFamily: sansFamilies[0]
    readonly property string monoFamily: monoFamilies[0]

    readonly property font body: Qt.font({
        families: theme.sansFamilies,
        pixelSize: 14,
        weight: Font.Normal
    })
    readonly property font bodyStrong: Qt.font({
        families: theme.sansFamilies,
        pixelSize: 14,
        weight: Font.DemiBold
    })
    readonly property font bodySmall: Qt.font({
        families: theme.sansFamilies,
        pixelSize: 13,
        weight: Font.Normal
    })
    readonly property font caption: Qt.font({
        families: theme.sansFamilies,
        pixelSize: 12,
        weight: Font.Normal
    })
    /// Headings run tight and heavy: -0.02em at 18px is -0.36px.
    readonly property font title: Qt.font({
        families: theme.sansFamilies,
        pixelSize: 18,
        weight: Font.DemiBold,
        letterSpacing: -0.36
    })

    readonly property font bodySmallStrong: Qt.font({
        families: theme.sansFamilies,
        pixelSize: 13,
        weight: Font.DemiBold
    })

    /// A button's label, from the design system's Button: the core sans at
    /// semibold, 13px at the full size and 12px below it, tracked +0.01em.
    ///
    /// Not a machine label. This application had been setting its buttons in
    /// the mono uppercase `label` face, which is the voice it uses for the name
    /// of a thing -- a column head, a panel title, a unit. A button is not the
    /// name of a thing; it is an instruction, and the system sets instructions
    /// in the reading face.
    readonly property font button: Qt.font({
        families: theme.sansFamilies,
        pixelSize: 13,
        weight: Font.DemiBold,
        letterSpacing: 0.13
    })
    readonly property font buttonSmall: Qt.font({
        families: theme.sansFamilies,
        pixelSize: 12,
        weight: Font.DemiBold,
        letterSpacing: 0.12
    })

    /// Data cells: monospace so columns of numbers line up. The mockup sets
    /// these at 12.5px, which is between two steps of the system's type scale;
    /// this snaps up to --fs-body-s, the nearer one.
    readonly property font mono: Qt.font({
        families: theme.monoFamilies,
        pixelSize: 13,
        weight: Font.Normal
    })
    readonly property font monoSmall: Qt.font({
        families: theme.monoFamilies,
        pixelSize: 12,
        weight: Font.Normal
    })
    /// The tree's expander glyph, which sits below the type scale on purpose:
    /// it is punctuation, not text.
    readonly property font caret: Qt.font({
        families: theme.monoFamilies,
        pixelSize: 9,
        weight: Font.Normal
    })
    /// Machine labels: mono, uppercase, tracked +0.12em (1.32px at 11px).
    /// Every label, ID, unit, timestamp and column head in the UI takes this.
    readonly property font label: Qt.font({
        families: theme.monoFamilies,
        pixelSize: 11,
        weight: Font.Medium,
        letterSpacing: 1.32,
        capitalization: Font.AllUppercase
    })
    /// The smaller readout: tracked +0.18em (1.8px at 10px).
    readonly property font micro: Qt.font({
        families: theme.monoFamilies,
        pixelSize: 10,
        weight: Font.Medium,
        letterSpacing: 1.8,
        capitalization: Font.AllUppercase
    })
    /// Same size, the narrower +0.12em track: status strips and table footers,
    /// where the line is long enough that 0.18em would start to fall apart.
    readonly property font microLabel: Qt.font({
        families: theme.monoFamilies,
        pixelSize: 10,
        weight: Font.Medium,
        letterSpacing: 1.2,
        capitalization: Font.AllUppercase
    })
    /// A measured value small enough to sit inside a tree row, and the same
    /// face a menu row sets its shortcut in. Barely tracked (+0.06em at 10px)
    /// and never uppercased -- it carries shapes, counts and key names, none
    /// of which are labels.
    readonly property font readout: Qt.font({
        families: theme.monoFamilies,
        pixelSize: 10,
        weight: Font.Normal,
        letterSpacing: 0.6
    })

    // --- colour ramps for plotted lines ----------------------------------
    // The one place this system spends colour on something that is not state.
    //
    // It is not decoration: a bundle of lines separated only by overlap stops
    // being readable at a dozen of them, and a colour that says *which line*
    // is carrying data exactly as the position of the line does. That is the
    // same argument the system makes for amber and red -- colour means
    // something -- applied to a quantity rather than to a status. Which is why
    // it lives behind a setting whose default is still the single accent.
    //
    // The stops are named colour maps, kept as they are defined elsewhere so
    // that a plot exported from here and one drawn by anything else agree.
    // They are here rather than in PlotSurface because Theme is the one file
    // allowed to hold a literal colour; see tools/check-design-tokens.sh.
    //
    // A perceptual ramp runs from dark to light, so its first line sits close
    // to the plot's own ground. That is what the ramp is, and the reader who
    // wants every line at equal weight has "same" and "range" for it.
    readonly property var colorRamps: ({
        // Not black-to-white: the system's own neutral ramp, from the step
        // above the surfaces to signal white, so the darkest line is still a
        // line and not the background.
        "grayscale": [n5, n7, n9, n10, n11],
        "viridis":   ["#440154", "#3B528B", "#21918C", "#5EC962", "#FDE725"],
        "inferno":   ["#000004", "#420A68", "#932667", "#DD513A", "#FCA50A",
                      "#FCFFA4"],
        "hot":       ["#0B0000", "#E60000", "#FF7A00", "#FFF34D", "#FFFFFF"],
        "cool":      ["#00FFFF", "#FF00FF"]
    })

    /// The ramps in the order the plot settings offer them.
    readonly property var colorRampNames: ["grayscale", "viridis", "inferno",
                                           "hot", "cool"]

    /// The ramps a *value* is placed on: an image's pixel, a table cell's
    /// fill. The plot's list with its neutral grey swapped for a true
    /// black-to-white one, which is the key `"gray"` -- not in `colorRamps`,
    /// because a ramp with no stops is what both views already draw as black
    /// to white.
    ///
    /// The swap, and this second list, are the point. `colorRamps.grayscale`
    /// starts at n5 rather than at black *because a plot line at pure black
    /// would vanish into the plot's own ground* -- a reason that belongs to
    /// lines and to nothing else. A pixel of value zero is black; drawing it
    /// as n5 would be the viewer editing the data. So the two lists differ by
    /// exactly the ramp whose definition is about lines, and the image and
    /// table settings were offering both at once -- which is why "grayscale"
    /// appeared twice in one dropdown.
    readonly property var valueRampKeys: ["gray"].concat(
        colorRampNames.filter(name => name !== "grayscale"))
    /// The same list as the reader sees it. Only the first differs: "gray" is
    /// the key, "grayscale" is the word, and the word is the one the plot uses
    /// for its own neutral ramp.
    readonly property var valueRampLabels: ["grayscale"].concat(
        colorRampNames.filter(name => name !== "grayscale"))

    /// A colour `position` of the way along `stops`, interpolated in RGB.
    ///
    /// A function rather than a table because the number of lines is not known
    /// until the plot is drawn: five stops have to serve two lines and five
    /// hundred. Linear in RGB rather than in HSV -- HSV walks the long way
    /// round the wheel between two stops and invents hues that are in neither.
    function rampColor(stops, position) {
        if (!stops || stops.length === 0)
            return theme.accent
        if (stops.length === 1)
            return stops[0]
        const at = Math.max(0, Math.min(1, position)) * (stops.length - 1)
        const lower = Math.floor(at)
        const upper = Math.min(lower + 1, stops.length - 1)
        return theme.mix(stops[lower], stops[upper], at - lower)
    }

    /// The ink that reads on `fill`.
    ///
    /// A cell filled by its own value takes whatever colour the ramp gives it,
    /// and a fixed ink would be unreadable over half of every ramp there is --
    /// white on `hot`'s pale end, black on `viridis`'s dark one. The system has
    /// exactly two inks for a filled surface, substrate black and signal white,
    /// which is the same pair the accent's own fill uses; which of them is a
    /// question about the fill's luminance and nothing else.
    ///
    /// WCAG's relative luminance, and its crossover: contrast against white is
    /// 1.05 / (L + 0.05) and against black (L + 0.05) / 0.05, and the two are
    /// equal at L = sqrt(1.05 x 0.05) - 0.05, or 0.179. Below that the fill is
    /// dark enough that white reads better, above it black does -- so whichever
    /// side the fill falls on, the pair is never worse than 4.5:1.
    function inkOn(fill) {
        const c = Qt.color(fill)
        const linear = v => v <= 0.03928 ? v / 12.92
                                         : Math.pow((v + 0.055) / 1.055, 2.4)
        const luminance = 0.2126 * linear(c.r) + 0.7152 * linear(c.g)
                          + 0.0722 * linear(c.b)
        return luminance > 0.1791 ? theme.n0 : theme.n11
    }

    /// `colour` with its alpha taken to nothing.
    ///
    /// This exists because `"transparent"` is not a neutral value to animate
    /// through. It is `rgba(0, 0, 0, 0)` -- *black* with no alpha -- and Qt
    /// interpolates a colour animation component by component, so a ground
    /// crossfading from "transparent" to a hover colour passes through
    /// half-alpha black. Over the dark theme's true-black ground that is
    /// invisible; over the light theme's white one it is a grey flash on the
    /// way in and another on the way out, and a pointer crossing a row of
    /// buttons or a tree of names sets off one per item. That is the flicker.
    ///
    /// Fading to this instead keeps the hue fixed and moves only the alpha, so
    /// the ground appears and goes away rather than passing through a colour
    /// that is in neither end of the animation.
    function clear(colour) {
        const c = Qt.color(colour)
        return Qt.rgba(c.r, c.g, c.b, 0)
    }

    /// `from` and `to` mixed, `amount` of the way from the first to the second.
    function mix(from, to, amount) {
        const a = Qt.color(from)
        const b = Qt.color(to)
        const t = Math.max(0, Math.min(1, amount))
        return Qt.rgba(a.r + (b.r - a.r) * t,
                       a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t,
                       a.a + (b.a - a.a) * t)
    }

    // --- motion ----------------------------------------------------------
    // The system's ladder, 80-420ms, carried in full because it is part of the
    // token set this file is a port of.
    //
    // The application animates nothing. Every hover, press, selection, panel
    // and reveal in it changes state between one frame and the next: a viewer
    // of scientific data is read rather than watched, and a rail that takes a
    // fifth of a second to arrive is a fifth of a second in which the numbers
    // beside it are the wrong width. So the only step still spent is dur5, and
    // it is spent as a *dwell* -- how long the pointer must rest before a
    // tooltip appears -- which is a delay rather than a motion.
    readonly property int dur1: 80
    readonly property int dur2: 120
    readonly property int dur3: 180
    readonly property int dur4: 280
    readonly property int dur5: 420

    readonly property int durationFast: dur1
    readonly property int durationBase: dur3

    /// IBM's productive easing, cubic-bezier(.2,0,.38,.9), in the form QML
    /// wants: control points followed by the terminating (1,1).
    readonly property var easeStandard: [0.2, 0.0, 0.38, 0.9, 1.0, 1.0]
}

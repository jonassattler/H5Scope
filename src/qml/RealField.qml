// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// One number, typed or stepped, with a fractional part: the manual range boxes
/// of the plot and the image.
///
/// NumberField's sibling rather than a mode of it -- a table index and a data
/// value are different things, and a validator that accepts "1.5" for an index
/// would be wrong. Everything else about it, down to the focus rule and the
/// pair of arrows on its right-hand end, is that file's, so the two read as one
/// control in two flavours.
///
/// `integer` is the third flavour, and still not NumberField: that one is the
/// table's index box, clamped to a dimension's extent and six digits wide.
/// This one holds a *value* out of an integer dataset -- unbounded, and as
/// wide as 4294967295 -- and only refuses the fractional part the data cannot
/// take. It carries the number as a real throughout, which is exact for every
/// integer HDF5 stores in 32 bits and for every one in 64 that a reader is
/// going to type.
TextField {
    id: control

    property real value: 0.0
    /// Whole numbers only, for a dataset of integers.
    property bool integer: false

    /// How far one press of an arrow moves the number, or 0 to work it out.
    property real step: 0.0

    /// A number the user actually entered.
    signal committed(real amount)

    // Not an IntValidator: that one is bounded by C's `int`, and an unsigned
    // 32-bit dataset runs past it.
    RegularExpressionValidator {
        id: wholeOnly
        regularExpression: /^-?\d*$/
    }

    DoubleValidator {
        id: anyReal
        notation: DoubleValidator.ScientificNotation
    }

    text: control.formatted(control.value)
    validator: control.integer ? wholeOnly : anyReal
    font: Theme.monoSmall
    color: Theme.textPrimary
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    horizontalAlignment: Qt.AlignHCenter
    implicitWidth: Theme.s12 + stepper.width
    implicitHeight: Theme.smallControlHeight
    leftPadding: Theme.gapS
    rightPadding: stepper.width + Theme.gapXS
    opacity: control.enabled ? 1.0 : 0.4

    onEditingFinished: control.commit()

    Keys.onUpPressed: control.nudge(1)
    Keys.onDownPressed: control.nudge(-1)

    /// How far one press of an arrow moves the number.
    ///
    /// A whole-number field steps by one, because the next integer is the next
    /// value there is. Anything else steps by about five per cent of what it is
    /// already holding -- a proportion rather than a fixed amount, because
    /// these boxes hold black points of 0.002 and axis stops of 50000 and no
    /// one number is a sensible nudge for both. It is then rounded to 1, 2 or 5
    /// times a power of ten, so that pressing the arrow repeatedly walks round
    /// numbers instead of drifting through 0.10500000000000001. A box holding
    /// nothing has no magnitude to take a share of, and steps by one.
    readonly property real stepSize: {
        if (control.integer)
            return 1
        if (control.step > 0)
            return control.step
        const magnitude = Math.abs(control.value)
        if (!(magnitude > 0) || !isFinite(magnitude))
            return 1
        return control.rounded(magnitude * 0.05)
    }

    /// `amount` at 1, 2 or 5 times a power of ten -- whichever is nearest.
    function rounded(amount) {
        const power = Math.pow(10, Math.floor(Math.log(amount) / Math.LN10))
        const scaled = amount / power
        return power * (scaled < 1.5 ? 1 : scaled < 3.5 ? 2 : scaled < 7.5 ? 5 : 10)
    }

    /// Short enough to fit the box, exact enough to be worth reading.
    ///
    /// Six significant figures, and then the zeros that padding added taken
    /// back off -- but only ever after a decimal point. `toPrecision(6)` writes
    /// 500000 as "500000", six figures and no point at all, and a rule that
    /// stripped trailing zeros from *that* turned half a million into 5. Which
    /// is what the plot's `stop` box reported for a dataset of that length: a
    /// number nobody typed, sitting in a box the reader had just watched
    /// compute itself.
    ///
    /// A whole number is written out whole for the same reason. Six figures
    /// turn 16777215 into 1.67772e+7, which is not a number anyone typed and
    /// not one they can edit -- and an axis counting elements is a whole
    /// number nearly every time it is looked at.
    function formatted(amount) {
        if (!isFinite(amount))
            return ""
        if (control.integer)
            return String(Math.round(amount))
        if (Number.isInteger(amount) && Math.abs(amount) < 1e15)
            return String(amount)
        const written = Number(amount).toPrecision(6)
        const exponent = written.indexOf("e")
        if (exponent < 0)
            return control.trimmed(written)
        return control.trimmed(written.slice(0, exponent)) + written.slice(exponent)
    }

    /// A plain decimal with its padding zeros taken off. Untouched when it
    /// carries no point: every digit of an integer is significant, and the
    /// zeros at the end of one are the number.
    function trimmed(text) {
        return text.indexOf(".") < 0 ? text : text.replace(/\.?0+$/, "")
    }

    /// The number the box is showing, which is what a step moves from.
    function shown() {
        const entered = parseFloat(control.text)
        return isNaN(entered) ? control.value : entered
    }

    function nudge(direction) {
        const size = control.stepSize
        const from = control.shown()
        // The next multiple of the step in the direction of travel, rather
        // than `from + step`: stepping up from 103 with a step of 5 lands on
        // 105, not on 108. A number somebody typed is a starting point, and
        // the arrow's job is to walk the round ones either side of it rather
        // than to carry its remainder along for ever. The epsilon is there so
        // that a value already sitting on a multiple moves a whole step
        // instead of standing still on a rounding error.
        const grid = from / size
        const wanted = direction > 0
            ? (Math.floor(grid + 1e-9) + 1) * size
            : (Math.ceil(grid - 1e-9) - 1) * size
        control.committed(control.integer ? Math.round(wanted) : wanted)
        control.text = Qt.binding(() => control.formatted(control.value))
    }

    function commit() {
        const entered = parseFloat(control.text)
        if (!isNaN(entered))
            control.committed(control.integer ? Math.round(entered) : entered)
        // Typing broke the binding on `text`; restore it, because the value
        // may have been clamped or refused and the box has to show what is
        // actually in effect.
        control.text = Qt.binding(() => control.formatted(control.value))
    }

    background: Rectangle {
        radius: Theme.radiusS
        color: Theme.surfaceInset
        border.width: control.activeFocus ? Theme.borderWidthAccent
                                          : Theme.borderWidth
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }

    FieldStepper {
        id: stepper

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Theme.borderWidth
        enabled: control.enabled
        onStepped: direction => control.nudge(direction)
    }
}

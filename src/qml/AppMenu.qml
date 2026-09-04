// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// A menu drawer, styled from Theme.
///
/// `popupType: Popup.Item` keeps the drawer inside the scene graph rather than
/// handing it to the platform. This is the same choice FilePicker.qml makes and
/// for the same reason: a platform popup arrives in the host's palette and
/// typeface, which would make the menus the one part of this program that does
/// not look like the program.
///
/// The design system puts `--shadow-pop` under this drawer. QML has no
/// box-shadow, and this project has already settled that question for the one
/// other floating surface it has -- the toast in Main.qml -- in favour of a
/// border rather than a drop shadow. The drawer takes the `--line-strong`
/// hairline, which is the heaviest rule in the system and reads as the edge of
/// something that floats.
Menu {
    id: control

    popupType: Popup.Item

    padding: 0
    topPadding: Theme.s3
    bottomPadding: Theme.s3

    /// The widest row in the drawer.
    ///
    /// Asked of the rows rather than taken from `contentWidth` or
    /// `implicitContentWidth`: a Menu's content item is a ListView, which has
    /// no implicit width of its own, so both of those report zero and every
    /// drawer sat at its floor whatever was in it -- which is what squeezed
    /// the file name out of an Open Recent row and left the folder alone in it.
    ///
    /// Re-read when the row count changes, which is when a drawer's contents
    /// change: these are static rows and an Instantiator, not a list whose
    /// entries grow and shrink in place.
    readonly property int widestRow: {
        let widest = 0
        for (let i = 0; i < control.count; ++i) {
            const row = control.itemAt(i)
            if (row)
                widest = Math.max(widest, row.implicitWidth)
        }
        return widest
    }

    // Wide enough for the longest row, within reason. The ceiling keeps one
    // deep folder path from setting the width of a drawer of ten short file
    // names; past it the row elides and the pointer reads the rest.
    implicitWidth: Math.min(Theme.menuMaxWidth,
                            Math.max(Theme.menuMinWidth,
                                     control.widestRow + leftPadding
                                     + rightPadding))

    /// Every row Qt creates for itself rather than being handed: a nested
    /// Menu's title row, and any row added from an Action at run time.
    ///
    /// Without this they are drawn by the Basic style's own MenuItem, in Qt's
    /// palette and with Qt's arrow -- which on this drawer's near-black ground
    /// is dark ink on dark, indistinguishable from a row that has been
    /// disabled, on a gutter that lines up with nothing above it.
    delegate: AppMenuItem {
        id: created

        // A submenu's row is created here rather than declared, so nothing has
        // tied it to the drawer it opens: Qt sets `subMenu` on it and stops
        // there. `when` keeps this off the rows that came from an Action,
        // whose own enabled state AbstractButton is already maintaining.
        Binding {
            target: created
            property: "enabled"
            value: created.subMenu ? created.subMenu.enabled : true
            when: created.subMenu !== null
            restoreMode: Binding.RestoreBindingOrValue
        }
    }

    background: Rectangle {
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.borderGuide
    }
}

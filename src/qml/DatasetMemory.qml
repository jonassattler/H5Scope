// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import H5Scope.Backend

/// Remembers a set of a view's properties for the dataset they were set on, and
/// puts them back when the reader comes back to it.
///
/// The rule is that a setting belongs to the data it was made about. A black
/// point chosen for one frame says nothing about the next one; an x axis of
/// 0 : 0.001 : 5 describes one trace and is nonsense over another; a colour axis
/// on dimension 2 does not exist on a dataset of rank 2. Every one of those used
/// to carry over, so selecting a second dataset showed it through the first
/// one's settings -- and the reader had to undo them by hand to see what they
/// had actually opened.
///
/// Two signals do the whole of it. `selectionAboutToChange` fires while
/// AppController still names the dataset being left, which is the only moment
/// at which what is on screen can be filed under the right name;
/// `selectionChanged` fires once it names the new one, which is when what was
/// filed under *that* name can be put back.
///
/// A dataset with nothing remembered gets `defaults`, captured from the subject
/// as this was created -- so a fresh dataset opens the way the application
/// opens rather than the way the last dataset was left. `restoresDefaults` is
/// for the subjects that work their own defaults out from the file, like
/// DatasetImage: those are already right by the time this runs, and putting a
/// captured value over the top would be this component overruling the file.
QtObject {
    id: memory

    /// The object whose properties are remembered. A QML surface or one of the
    /// controller's own objects; this asks nothing of it but the names below.
    property QtObject subject
    /// What to file them under. Two groups never share a name-space, so a plot
    /// and an image can both remember a "colorMode" without meeting.
    property string group: ""
    /// The property names to remember, as strings.
    property var names: []
    /// Whether a dataset with nothing remembered puts the subject back to the
    /// values it started with. See the note above.
    property bool restoresDefaults: true

    /// The values the subject had when this was created.
    property var defaults: ({})
    /// Nothing is saved before the defaults have been taken: a save during
    /// construction would file whatever half-built state the subject was in.
    property bool ready: false

    function save() {
        if (!memory.ready || !memory.subject || memory.group === "")
            return
        const values = {}
        for (let i = 0; i < memory.names.length; ++i) {
            const name = memory.names[i]
            values[name] = memory.subject[name]
        }
        AppController.rememberSettings(memory.group, values)
    }

    function load() {
        if (!memory.subject || memory.group === "")
            return
        const stored = AppController.rememberedSettings(memory.group)
        for (let i = 0; i < memory.names.length; ++i) {
            const name = memory.names[i]
            if (stored.hasOwnProperty(name))
                memory.subject[name] = stored[name]
            else if (memory.restoresDefaults)
                memory.subject[name] = memory.defaults[name]
        }
    }

    Component.onCompleted: {
        if (!memory.subject)
            return
        const captured = {}
        for (let i = 0; i < memory.names.length; ++i) {
            const name = memory.names[i]
            captured[name] = memory.subject[name]
        }
        memory.defaults = captured
        memory.ready = true
        memory.load()
    }

    // Held in a property because QtObject has no default one to put children
    // in. It is a Connections either way.
    property Connections selection: Connections {
        target: AppController
        function onSelectionAboutToChange() { memory.save() }
        function onSelectionChanged() { memory.load() }
    }
}

// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStringList>

namespace gui {

/// Result of registering the typefaces compiled into the binary.
struct EmbeddedFontResult {
    /// Families the bundled files actually registered. Read back from the font
    /// database rather than hard-coded, so this reports what the binary really
    /// provides and not what it was hoped to provide.
    QStringList families;
    /// Resource paths that failed to load. Empty on success.
    QStringList missing;

    [[nodiscard]] bool ok() const { return missing.isEmpty(); }
};

/// Register the bundled IBM Plex faces with the application font database.
///
/// The design system specifies IBM Plex Sans and IBM Plex Mono, and the whole
/// point of this project's static linking is that the binary does not depend on
/// what the host happens to have installed -- a font is no different from a
/// library in that respect. Relying on a system copy would mean the UI silently
/// renders in Helvetica or DejaVu on any machine without Plex, which is a
/// visual regression no test would catch.
///
/// Idempotent: safe to call more than once, and calling it again does not
/// register the faces twice.
///
/// Requires a QGuiApplication to exist, as all font database access does.
EmbeddedFontResult loadEmbeddedFonts();

} // namespace gui

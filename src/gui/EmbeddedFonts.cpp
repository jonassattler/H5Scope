// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "EmbeddedFonts.hpp"

#include <QFontDatabase>

namespace gui {
namespace {

/// The faces the Theme singleton actually asks for: Sans at 400 and 600, Mono
/// at 400 and 500. Every other weight in the Plex family would be dead weight
/// in the binary, so none of them are bundled -- adding a weight to Theme.qml
/// means adding its file here too.
constexpr const char* kFontResources[] = {
    ":/fonts/IBMPlexSans-Regular.ttf",
    ":/fonts/IBMPlexSans-SemiBold.ttf",
    ":/fonts/IBMPlexMono-Regular.ttf",
    ":/fonts/IBMPlexMono-Medium.ttf",
};

} // namespace

EmbeddedFontResult loadEmbeddedFonts()
{
    static const EmbeddedFontResult result = [] {
        EmbeddedFontResult loaded;
        for (const char* resource : kFontResources) {
            const QString path = QString::fromLatin1(resource);
            const int id = QFontDatabase::addApplicationFont(path);
            if (id < 0) {
                loaded.missing << path;
                continue;
            }
            for (const QString& family : QFontDatabase::applicationFontFamilies(id)) {
                if (!loaded.families.contains(family)) {
                    loaded.families << family;
                }
            }
        }
        return loaded;
    }();
    return result;
}

} // namespace gui

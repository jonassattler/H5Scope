// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "EmbeddedIcon.hpp"

#include <QPixmap>

namespace gui {
namespace {

/// The sizes tools/make-icons.sh renders, which are also the sizes the Windows
/// resource carries. Adding one means adding it there too, or this asks for a
/// file that is not in the binary.
constexpr const char* kIconResources[] = {
    ":/icons/h5scope-16.png",  ":/icons/h5scope-24.png",
    ":/icons/h5scope-32.png",  ":/icons/h5scope-48.png",
    ":/icons/h5scope-64.png",  ":/icons/h5scope-128.png",
    ":/icons/h5scope-256.png",
};

} // namespace

QIcon applicationIcon()
{
    static const QIcon icon = [] {
        QIcon assembled;
        for (const char* resource : kIconResources) {
            const QPixmap pixmap(QString::fromLatin1(resource));
            if (!pixmap.isNull()) {
                assembled.addPixmap(pixmap);
            }
        }
        return assembled;
    }();
    return icon;
}

} // namespace gui

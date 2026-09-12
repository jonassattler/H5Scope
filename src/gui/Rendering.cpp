// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/Rendering.hpp"

#include <QtGui/QSurfaceFormat>

namespace gui {
namespace {

// Four, not more. The step from one sample to four is the whole of the visible
// difference on a hairline; eight costs another buffer's worth of bandwidth to
// move an edge by a fraction of a shade.
constexpr int kSamples = 4;

} // namespace

void askForMultisampling()
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    if (format.samples() >= kSamples) {
        return;
    }
    format.setSamples(kSamples);
    QSurfaceFormat::setDefaultFormat(format);
}

} // namespace gui

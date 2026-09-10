// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QIcon>

namespace gui {

/// The application icon, assembled from the renders compiled into the binary.
///
/// Every size is its own render of packaging/h5scope.svg -- see
/// tools/make-icons.sh -- rather than one large image left for Qt to scale
/// down. That distinction is the whole reason this is seven files: a 16x16
/// produced by resampling a 256x256 turns the icon's grid into grey haze,
/// where a 16x16 drawn from the vector draws only what is resolvable there.
///
/// Empty if the resource is missing, which is a build fault rather than a
/// run-time one; the caller reports it and carries on.
QIcon applicationIcon();

} // namespace gui

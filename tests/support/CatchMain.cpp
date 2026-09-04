// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The entry point for every Catch2 suite that touches HDF5 through the
// application's own machinery.
//
// Two things it does that Catch2's own main does not, and both are consequences
// of HDF5 having been given a thread of its own:
//
//   A QCoreApplication, because that thread runs a Qt event loop and Qt will
//   not start one without an application instance -- "QEventLoop: Cannot be
//   used without QCoreApplication", and then every job posted to it waits
//   forever.
//
//   H5Thread::shutdown() before the application goes, because closing the file
//   and handing back the claim are themselves jobs, and a job needs the event
//   loop that is about to be destroyed.

#include "gui/H5Thread.hpp"

#include <catch2/catch_session.hpp>

#include <QCoreApplication>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const int result = Catch::Session().run(argc, argv);
    gui::H5Thread::shutdown();
    return result;
}

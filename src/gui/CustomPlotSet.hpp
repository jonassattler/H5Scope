// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "CustomPlot.hpp"
#include "DatasetLookup.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gui {

/// The custom plot tabs, the views saved from them, and the one cache of what
/// the paths they name actually are.
///
/// A tab is not about the selection, so this is not about it either: nothing
/// in here moves when the reader clicks a different dataset. What it does
/// answer to is the *file* -- every entry is a path inside one, and two files
/// can hold a `/data` that have nothing to do with each other -- so opening or
/// closing one empties the whole of this. That is the same stance
/// `AppController::settings_` takes, and for the same reason: a session's
/// worth of looking at one file is not a preference.
///
/// Saved views are the odd ones out, and deliberately. They are the set's
/// rather than any one tab's -- a view saved from one tab is worth restoring
/// into a fresh one, which is most of what saving a comparison is for -- and
/// they outlive both the tabs and the *file*, because a view is a way of
/// looking at data rather than a piece of one file's contents. A reader who has
/// built a comparison of four runs wants it again next week, on next week's
/// file, which is exactly the case the recent-files list is kept for.
///
/// So they are written to QSettings and read back at start-up, and a view that
/// no longer fits the file in front of it says so rather than being thrown
/// away: `stateOf` reports whether all, some or none of the datasets it names
/// are there, and the panels draw a dot in that colour beside it.
class CustomPlotSet : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from AppController.customPlots")

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// The saved views, best fit first. See `viewNames`.
    Q_PROPERTY(QStringList viewNames READ viewNames NOTIFY viewsChanged)
    /// Which tab the window is showing, or -1 when it is showing something
    /// else. Written by the tab strip.
    ///
    /// Held here rather than only in QML because two things outside the tab
    /// need it: the tree's plus, which adds to whichever tab is up, and the
    /// menu entry that makes a dataset the current tab's time base. Both would
    /// otherwise have to be handed the answer from the window.
    Q_PROPERTY(int activeIndex READ activeIndex WRITE setActiveIndex
                   NOTIFY activeIndexChanged)
    /// The tab `activeIndex` names, or null. What the tree's plus adds to.
    Q_PROPERTY(gui::CustomPlot* active READ active NOTIFY activeIndexChanged)

public:
    /// How much of a saved view the file in front of it actually holds.
    enum MatchState {
        /// Nothing it names is here. Most likely the wrong file.
        NoMatch = 0,
        /// Some of it is. Usually a file of the same shape with a run missing,
        /// which is the case a reader most wants to be told about rather than
        /// refused over.
        PartialMatch = 1,
        /// Every dataset it names is here.
        FullMatch = 2,
    };
    Q_ENUM(MatchState)

    enum Roles {
        NameRole = Qt::UserRole + 1,
        /// Whether this tab is showing in a window of its own, in which case
        /// the strip does not list it. One plot lives in one place.
        DetachedRole,
    };
    Q_ENUM(Roles)

    explicit CustomPlotSet(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return static_cast<int>(plots_.size()); }
    /// The saved views in the order they are offered: every one that fits the
    /// file entirely, then the partial ones, then the ones that do not fit at
    /// all, and alphabetically within each. What a reader is looking for is
    /// nearly always something that will actually draw, so that is what the
    /// top of the list is for.
    [[nodiscard]] QStringList viewNames() const;
    /// How much of the view named fits the open file.
    Q_INVOKABLE [[nodiscard]] MatchState stateOf(const QString& name) const;
    [[nodiscard]] int activeIndex() const { return activeIndex_; }
    void setActiveIndex(int index);
    [[nodiscard]] CustomPlot* active() const;

    /// Add a tab, named for the lowest "Custom N" not taken. Returns its index.
    Q_INVOKABLE int addPlot();
    Q_INVOKABLE void removePlot(int index);
    Q_INVOKABLE void movePlot(int from, int to);
    Q_INVOKABLE [[nodiscard]] gui::CustomPlot* plotAt(int index) const;
    Q_INVOKABLE [[nodiscard]] int indexOfName(const QString& name) const;
    /// Rename a tab. Returns why it could not be, or empty once it is.
    Q_INVOKABLE QString setName(int index, const QString& name);
    /// `wanted`, or the first "`wanted` 2", "`wanted` 3" nobody else is using.
    ///
    /// For restoring a view, which carries the title of the tab it was saved
    /// from and will therefore collide with that tab every time it is put
    /// somewhere else. That is a different situation from a reader typing a
    /// name -- there they have said exactly what they mean and setName refuses
    /// rather than deciding for them; here they have asked for an arrangement
    /// and the title came along with it.
    Q_INVOKABLE [[nodiscard]] QString uniqueName(const QString& wanted,
                                                 int except) const;
    Q_INVOKABLE void setDetached(int index, bool detached);
    Q_INVOKABLE [[nodiscard]] bool detached(int index) const;

    /// Add a dataset to the tab at `index`, expanded into its 1-D lines.
    /// Offered here because every caller -- the tree's plus, the tree's menu,
    /// the legend's menu -- has an index rather than a plot.
    ///
    /// A dataset with more lines than are worth drawing unasked answers
    /// through `crowdingWarned` instead; call this again with `confirmed` to
    /// go ahead. See CustomPlot::addDataset.
    Q_INVOKABLE void addDatasetTo(int index, const QString& path,
                                  bool confirmed = false);
    /// Make `path` the time base of the tab at `index`. The whole of the
    /// dataset when it is a vector; its first line otherwise, which is the
    /// line the plot tab would have drawn first.
    Q_INVOKABLE void setTimeSeriesOf(int index, const QString& path);

    // --- saved views -------------------------------------------------------
    /// Save the tab at `index` under `name`, together with the drawing
    /// settings QML holds. Returns why it could not be, or empty once it is.
    Q_INVOKABLE QString saveView(const QString& name, int index,
                                 const QVariantMap& settings);
    Q_INVOKABLE void removeView(const QString& name);
    /// Resolve everything a view names and report how much of it will not
    /// draw, without changing anything. Answers through `viewChecked`, because
    /// the paths may never have been looked at.
    Q_INVOKABLE void checkView(const QString& name);
    /// Put a saved view into the tab at `index`. The entries and the x axis
    /// land here; the drawing settings go back to QML through `viewRestored`,
    /// because they were QML's to begin with.
    Q_INVOKABLE void restoreView(const QString& name, int index);

    /// Resolve every path every saved view names, so `stateOf` can answer.
    ///
    /// One crossing for the lot. Called when a file finishes opening, which is
    /// the only moment the answers can change.
    void refreshViewStates();

    [[nodiscard]] DatasetLookup* lookup() { return &lookup_; }

    /// Forget every tab, every view and everything known about the file. The
    /// controller calls this when a file is opened or closed.
    void clear();

signals:
    void countChanged();
    void viewsChanged();
    void activeIndexChanged();
    /// A tab's name changed, so the strip relabels itself.
    void namesChanged();
    /// The answer to checkView: how many of the view's lines will not draw,
    /// and the reasons, longest-lived first. `issues` is zero when it will all
    /// draw.
    void viewChecked(const QString& name, int issues, const QStringList& reasons);
    /// A view landed in the tab at `index`; `settings` is what QML saved with
    /// it and has to put back on the surface.
    void viewRestored(int index, const QVariantMap& settings);
    /// Something worth telling the reader, from a tab or from here.
    void notice(const QString& message);
    /// Adding `path` to the tab at `index` would put `lines` lines in it,
    /// which is more than are worth drawing without being asked. The window
    /// puts the question; calling addDatasetTo with `confirmed` is yes.
    void crowdingWarned(int index, const QString& path, int lines);

private:
    [[nodiscard]] QString freeName() const;
    [[nodiscard]] bool taken(const QString& name, int except) const;

    std::vector<CustomPlot*> plots_;
    std::vector<bool> detached_;
    int activeIndex_ = -1;

    DatasetLookup lookup_;

    struct View {
        QVariantMap plot;     ///< CustomPlot::state()
        QVariantMap settings; ///< what the surface was drawn with
    };
    QHash<QString, View> views_;

    /// Every path a saved view names, deduplicated.
    [[nodiscard]] QStringList viewPaths() const;
    /// Read the views back from QSettings, and write them out again.
    ///
    /// As JSON in one key rather than as a QVariantMap per view. QSettings
    /// would store the maps through QDataStream, which works and is a blob:
    /// unreadable in the file, tied to Qt's stream version, and impossible to
    /// migrate by hand. What is saved here is a handful of paths and settings
    /// and is worth being able to look at.
    void loadViews();
    void saveViews() const;
};

} // namespace gui

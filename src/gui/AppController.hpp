// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "H5Thread.hpp"
#include "DatasetImage.hpp"
#include "DatasetPlot.hpp"
#include "ObjectInfoModel.hpp"
#include "PostprocessModel.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/File.hpp"

#include <QAbstractItemModel>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace gui {

class AttributeTableModel;
class DatasetStringListModel;
class DatasetTableModel;
class H5TreeModel;
class ObjectInfoModel;
class TableSetupModel;
class TreeFilterProxyModel;

/// The single object QML talks to. Owns the open file and the four models, and
/// derives the tab-visibility flags that design.txt specifies.
///
/// Replaces the Widgets MainWindow's coordination role. Reporting stays
/// non-modal -- errors are exposed as properties for QML to render, never as
/// blocking dialogs, so the controller is fully testable headless.
class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QAbstractItemModel* treeModel READ treeModel CONSTANT)
    /// The tree as the view shows it: `treeModel` behind the tree's filter box.
    Q_PROPERTY(QAbstractItemModel* filteredTreeModel READ filteredTreeModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* datasetModel READ datasetModel CONSTANT)
    /// The same dataset flattened to one entry per element, for the Data
    /// Viewer's stack of text panes. Only meaningful for a string dataset.
    Q_PROPERTY(QAbstractItemModel* datasetStringModel READ datasetStringModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* attributeModel READ attributeModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* infoModel READ infoModel CONSTANT)
    /// One row per dimension of the selection: how it is subset and which axis
    /// it sits on. Drives the Data Viewer's "table setup" panel.
    Q_PROPERTY(QAbstractItemModel* tableSetupModel READ tableSetupModel CONSTANT)
    Q_PROPERTY(gui::PostprocessModel* postprocessModel READ postprocessModel CONSTANT)
    /// Whether the views are drawing a computed array rather than the file.
    /// The bar says so in orange when they are, because every number below it
    /// is then a number this application worked out rather than one the file
    /// holds.
    Q_PROPERTY(bool postprocessActive READ postprocessActive
                   NOTIFY postprocessChanged)
    /// The same table the grid shows, read as lines and as a raster. The Data
    /// Viewer's three presentations differ in how they draw one slice, not in
    /// which slice they draw, so both of these sit on `datasetModel`.
    Q_PROPERTY(gui::DatasetPlot* datasetPlot READ datasetPlot CONSTANT)
    Q_PROPERTY(gui::DatasetImage* datasetImage READ datasetImage CONSTANT)

    Q_PROPERTY(bool hasFile READ hasFile NOTIFY fileChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY fileChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// On-disk size of the open file, formatted. Empty when none is open.
    Q_PROPERTY(QString fileSize READ fileSize NOTIFY fileChanged)
    /// Version of the HDF5 library this binary statically links.
    Q_PROPERTY(QString hdf5Version READ hdf5Version CONSTANT)
    /// This application's own version, counted out of the history at build
    /// time. See cmake/Version.cmake.
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    /// The commit it was built from, short.
    Q_PROPERTY(QString appCommit READ appCommit CONSTANT)
    /// The file name of the running executable -- which carries the version,
    /// so a reader with several builds on disk can see which one answered.
    Q_PROPERTY(QString binaryName READ binaryName CONSTANT)
    /// Filter applied to the tree by the filter box at the foot of the tree.
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText
                   NOTIFY filterTextChanged)

    Q_PROPERTY(QString currentPath READ currentPath NOTIFY selectionChanged)
    /// design.txt: the Dataset tab exists only when a dataset is selected.
    Q_PROPERTY(bool datasetTabVisible READ datasetTabVisible NOTIFY selectionChanged)
    /// design.txt: the Metadata tab exists only when the object has attributes.
    Q_PROPERTY(bool metadataTabVisible READ metadataTabVisible NOTIFY selectionChanged)
    /// Rank of the selection, so QML knows whether to show slice controls.
    Q_PROPERTY(int datasetRank READ datasetRank NOTIFY selectionChanged)
    Q_PROPERTY(QString datasetMessage READ datasetMessage NOTIFY selectionChanged)
    /// True when the selected dataset holds text rather than numbers. The Data
    /// Viewer presents those two things differently.
    Q_PROPERTY(bool datasetIsString READ datasetIsString NOTIFY selectionChanged)
    /// Elements in the selected dataset, across every dimension. A scalar has
    /// one; that is the difference between one text pane and a stack of them.
    Q_PROPERTY(qint64 datasetElementCount READ datasetElementCount
                   NOTIFY selectionChanged)
    /// True when the values can be read as numbers. The plot and the image
    /// presentations exist only for those; the table serves everything.
    Q_PROPERTY(bool datasetIsNumeric READ datasetIsNumeric NOTIFY selectionChanged)
    /// True when the selected dataset holds compounds. A struct has no single
    /// value, so a grid cell can only show it elided onto one line -- the Data
    /// Viewer opens the picked one out underneath instead.
    Q_PROPERTY(bool datasetIsCompound READ datasetIsCompound NOTIFY selectionChanged)
    /// True when the values are floats, which are the only ones there is a
    /// choice of notation about. The table settings panel shows that row only
    /// when there is something for it to apply to.
    Q_PROPERTY(bool datasetIsFloat READ datasetIsFloat NOTIFY selectionChanged)
    /// The Information tab's panels, in order. Each entry is
    /// { title, meta, accent, rows: [{ label, value, isWarning }] }.
    Q_PROPERTY(QVariantList infoPanels READ infoPanels NOTIFY selectionChanged)
    /// The slice the table is showing, written the way one would type it:
    /// `/cube[:, 2, 0:4]`. Ranges print with an exclusive upper bound, so the
    /// line pastes straight back into a Custom expression box.
    Q_PROPERTY(QString sliceExpression READ sliceExpression
                   NOTIFY tableLayoutChanged)
    /// The same line with the path and the brackets taken off: `:, 2, 0:4`.
    /// The slice bar prints the path and the brackets as fixed chrome and
    /// makes exactly this editable, so that what can be typed is always a
    /// complete slice of the object already named beside it.
    Q_PROPERTY(QString sliceText READ sliceText NOTIFY tableLayoutChanged)
    /// Files opened before, newest first. Each entry is
    /// `{ path, name, folder, missing }` -- `missing` when the file is no
    /// longer where it was, which is worth showing rather than hiding, because
    /// a reader looking for a file they had last week wants to know it moved.
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    /// Segments of the status strip along the bottom of the window.
    Q_PROPERTY(QStringList statusLeft READ statusLeft NOTIFY selectionChanged)
    Q_PROPERTY(QStringList statusRight READ statusRight NOTIFY selectionChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] QAbstractItemModel* treeModel() const;
    [[nodiscard]] QAbstractItemModel* filteredTreeModel() const;
    [[nodiscard]] QAbstractItemModel* datasetModel() const;
    [[nodiscard]] QAbstractItemModel* datasetStringModel() const;
    [[nodiscard]] QAbstractItemModel* attributeModel() const;
    [[nodiscard]] QAbstractItemModel* infoModel() const;
    [[nodiscard]] QAbstractItemModel* tableSetupModel() const;
    [[nodiscard]] PostprocessModel* postprocessModel() const;
    [[nodiscard]] bool postprocessActive() const;
    [[nodiscard]] DatasetPlot* datasetPlot() const;
    [[nodiscard]] DatasetImage* datasetImage() const;

    [[nodiscard]] bool hasFile() const { return fileOpen_; }
    /// Whether the file is being read right now.
    ///
    /// Everything this application asks of HDF5 is asked of one other thread
    /// and answered a moment later, so there is always a moment in which the
    /// window is showing less than it is about to. This is that moment, and it
    /// is what the chrome puts an indicator on -- the alternative to a
    /// progress bar is a window that looks finished when it is not.
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString filePath() const { return filePath_; }
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] QString errorText() const { return errorText_; }
    [[nodiscard]] QString currentPath() const { return currentPath_; }
    [[nodiscard]] bool datasetTabVisible() const { return datasetTabVisible_; }
    [[nodiscard]] bool metadataTabVisible() const { return metadataTabVisible_; }
    [[nodiscard]] int datasetRank() const { return datasetRank_; }
    [[nodiscard]] QString datasetMessage() const { return datasetMessage_; }
    [[nodiscard]] bool datasetIsString() const { return datasetIsString_; }
    [[nodiscard]] qint64 datasetElementCount() const { return datasetElementCount_; }
    [[nodiscard]] bool datasetIsNumeric() const { return datasetIsNumeric_; }
    [[nodiscard]] bool datasetIsCompound() const { return datasetIsCompound_; }
    [[nodiscard]] bool datasetIsFloat() const { return datasetIsFloat_; }
    [[nodiscard]] QString fileSize() const;
    [[nodiscard]] QString hdf5Version() const;
    [[nodiscard]] static QString appVersion();
    [[nodiscard]] static QString appCommit();
    [[nodiscard]] static QString binaryName();
    [[nodiscard]] QString filterText() const;
    void setFilterText(const QString& text);
    [[nodiscard]] QString sliceExpression() const;
    [[nodiscard]] QString sliceText() const;
    /// Apply an edited slice body, as TableSetupModel::applySlice does.
    /// Returns the reason it could not be read, or an empty string once the
    /// table is showing it.
    Q_INVOKABLE QString applySlice(const QString& text);
    /// Why an edited slice body cannot be read, or an empty string when it
    /// can. Checks without applying, so the bar can report a line as it is
    /// typed.
    Q_INVOKABLE QString sliceError(const QString& text) const;
    // --- settings a view keeps for the dataset they were made on ---------
    /// What `group` last held for the dataset now selected, or an empty map.
    ///
    /// This is the whole of the per-dataset settings mechanism, and it is
    /// deliberately a bag of names and values rather than a schema: the
    /// settings that have to be kept apart are spread across three QML
    /// surfaces and two C++ objects, and a store that knew what any of them
    /// meant would have to be edited every time one of them grew a control.
    /// What it knows is which dataset was on screen when they were written.
    ///
    /// See DatasetMemory.qml, which is the only caller: it saves on
    /// `selectionAboutToChange`, while this still names the object being left,
    /// and restores on `selectionChanged`, once it names the new one.
    Q_INVOKABLE [[nodiscard]] QVariantMap rememberedSettings(const QString& group) const;
    Q_INVOKABLE void rememberSettings(const QString& group, const QVariantMap& values);

    [[nodiscard]] QVariantList infoPanels() const;
    [[nodiscard]] QVariantList recentFiles() const;
    /// Forget the list. Offered because a list of what someone has opened is
    /// a record of what they have been doing, and it is theirs to erase.
    Q_INVOKABLE void clearRecentFiles();
    [[nodiscard]] QStringList statusLeft() const;
    [[nodiscard]] QStringList statusRight() const;

    /// Open a plain filesystem path. Returns false and sets errorText on
    /// failure; never blocks.
    Q_INVOKABLE bool openFile(const QString& path);
    /// Convenience for QML's FileDialog, which yields a file:// URL.
    Q_INVOKABLE bool openUrl(const QUrl& url);
    Q_INVOKABLE void closeFile();

    /// Select the object at `path`, refreshing every tab. Returns false when
    /// the path does not exist.
    Q_INVOKABLE bool selectPath(const QString& path);

signals:
    void fileChanged();
    void busyChanged();
    /// The answer to openFile(), which only says that an open was started.
    /// `ok` is false when the path turned out not to be a readable HDF5 file,
    /// and `errorText` then says why.
    void fileOpened(bool ok, const QString& path);
    void recentFilesChanged();
    /// The selection is about to move to another object, and `currentPath`
    /// still names the one being left. This is when a view writes down what it
    /// was showing, because a moment later there is nothing left to say which
    /// dataset its settings belonged to.
    void selectionAboutToChange();
    void selectionChanged();
    void errorTextChanged();
    void filterTextChanged();
    /// The table's selection of indices or its axis assignment changed.
    void tableLayoutChanged();
    /// The postprocessing pipeline changed, was switched on, or was switched
    /// off -- which is to say the views are now drawing something else.
    void postprocessChanged();
    /// Non-fatal problems worth surfacing transiently in the UI.
    void statusMessage(const QString& message);

private:
    void refreshSelection();
    /// Announce that the selection is leaving `currentPath_`, and write down
    /// the one setting this object keeps itself -- the slice.
    void leaveSelection();
    void setErrorText(const QString& text);
    /// Put `path` at the head of the recent list and write it back out.
    void remember(const QString& path);

    /// Newest first, absolute, deduplicated. Held rather than read back from
    /// QSettings on every binding, because the menu asks for it on every open.
    QStringList recent_;

    /// Per dataset, per group, whatever that group wrote down. Keyed by the
    /// path inside the open file, and emptied when another file is opened --
    /// two files can hold a "/data" that have nothing to do with each other,
    /// and a range set on one of them is not a range for the other.
    ///
    /// In memory only. These are a session's worth of looking at one file, not
    /// a preference: a black point chosen for a frame is worth keeping while
    /// the reader flicks between it and the next frame, and is not worth
    /// carrying into next week.
    QHash<QString, QHash<QString, QVariantMap>> settings_;
    /// ...and the slice, which is the controller's own rather than any view's:
    /// all three views draw whatever it resolves to, so there is one of it.
    QHash<QString, QString> slices_;

    /// Whether the session has a file open. The file itself lives on the HDF5
    /// thread and is deliberately not reachable from here -- see H5Session.
    bool fileOpen_ = false;
    QString filePath_;
    QString currentPath_;
    QString errorText_;
    QString datasetMessage_;
    bool datasetTabVisible_ = false;
    bool metadataTabVisible_ = false;
    bool datasetIsString_ = false;
    bool datasetIsNumeric_ = false;
    bool datasetIsCompound_ = false;
    bool datasetIsFloat_ = false;
    int datasetRank_ = 0;
    qint64 datasetElementCount_ = 0;

    H5TreeModel* treeModel_ = nullptr;
    TreeFilterProxyModel* filteredTreeModel_ = nullptr;
    DatasetTableModel* datasetModel_ = nullptr;
    DatasetStringListModel* datasetStringModel_ = nullptr;
    AttributeTableModel* attributeModel_ = nullptr;
    ObjectInfoModel* infoModel_ = nullptr;
    TableSetupModel* tableSetupModel_ = nullptr;
    PostprocessModel* postprocessModel_ = nullptr;
    DatasetPlot* datasetPlot_ = nullptr;
    DatasetImage* datasetImage_ = nullptr;

    /// What the selected dataset is, as plain data. The dataset itself is held
    /// open by the session on the HDF5 thread -- so that re-running a pipeline
    /// does not re-open it -- and this is the description of it that everything
    /// on this side reasons about.
    bool hasDataset_ = false;
    h5core::DatasetInfo datasetInfo_;

    /// Everything asked of the HDF5 thread on behalf of a selection. Reset
    /// whenever the selection moves, so an answer about the last object is
    /// never applied to this one.
    H5Requests requests_;
    /// ...and for the file itself, which outlives any one selection.
    H5Requests fileRequests_;
    /// ...and for what the views draw, which changes more often than the
    /// selection does -- every rearrangement of the table and every edit of the
    /// pipeline. Its own ticket, so that disowning a superseded pipeline run
    /// does not also disown the selection that is still being described.
    H5Requests sourceRequests_;

    /// Everything one selection needs to know, read in a single round trip.
    ///
    /// The whole of what describing a selected object costs -- its kind, its
    /// attribute count, its full description if it is a dataset, its attributes,
    /// and the Information tab's panels -- gathered on the HDF5 thread and
    /// handed back as plain data. One job rather than the eight separate reads
    /// this used to make from the GUI thread, which is both why it no longer
    /// blocks and why it is no longer eight round trips.
    struct SelectionFacts {
        bool described = false;
        QString message;
        bool isDataset = false;
        bool hasAttributes = false;
        bool datasetOpened = false;
        h5core::DatasetInfo info;
        QString datasetMessage;
        ObjectInfoModel::Content panels;
        std::vector<h5core::AttributeInfo> attributes;
    };

    /// Apply what refreshSelection() asked for. Runs on this thread, with the
    /// selection it was asked about already checked against the current one.
    void applySelection(SelectionFacts facts);

    /// Hand the table what it should be drawing -- the file's own dataset, or
    /// the result of the pipeline over it -- along with the layout that goes
    /// with whichever it is. Called whenever the selection, the slice or the
    /// pipeline moves, which are the only three things that change the answer.
    void applyDataSource();

    /// True while a selection is being taken apart and put back together.
    ///
    /// Choosing an object rebuilds four things -- the dataset, the pipeline,
    /// the setup panel's shape and the remembered slice -- and each of them
    /// announces itself. Answering those announcements one at a time would
    /// hand the table a source and a layout that came from different datasets:
    /// the pipeline is told the new shape before the setup panel is, so the
    /// layout still on the panel at that moment is the *previous* object's.
    /// On a file holding both a 100000 x 10000 dataset and a small one that is
    /// not a cosmetic wrong answer -- it is a hundred thousand rows of reads
    /// that each throw, and `inspect-file` went from three seconds to not
    /// finishing.
    ///
    /// So the announcements are ignored while this is set and the source is
    /// applied once, at the end, when all four agree.
    bool selecting_ = false;

    /// Long enough to cover a session's worth of files, short enough that the
    /// menu stays a menu rather than becoming a file browser -- which is what
    /// the file picker is for.
    static constexpr int kMaxRecentFiles = 10;
};

} // namespace gui

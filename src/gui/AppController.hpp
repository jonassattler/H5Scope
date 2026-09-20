// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "H5Thread.hpp"
#include "CustomPlotSet.hpp"
#include "DatasetImage.hpp"
#include "DatasetPlot.hpp"
#include "ObjectInfoModel.hpp"
#include "PostprocessModel.hpp"
#include "postproc/MemberPath.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/FieldDataset.hpp"
#include "h5core/File.hpp"

#include <QAbstractItemModel>
#include <QBasicTimer>
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
class NameIndex;
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

public:
    /// The three the menu offers. Named rather than a number of megabytes,
    /// because the right number depends on the machine and the reader knows
    /// how much of theirs they want spent, not how many megabytes that is.
    enum RamBudget
    {
        LowRam,
        MediumRam,
        GreedyRam,
    };
    Q_ENUM(RamBudget)

private:
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
    /// The custom plot tabs: the one thing in this application that is not
    /// about the selection. They draw 1-D slices from anywhere in the file
    /// together, so nothing about them moves when the tree does -- but every
    /// entry is a path *inside* a file, and this is emptied when that changes.
    Q_PROPERTY(gui::CustomPlotSet* customPlots READ customPlots CONSTANT)

    /// How much memory the plots may spend holding what they have read.
    ///
    /// The one setting in this application that is about the machine rather
    /// than about the data, which is why it is under Settings and not under
    /// View. It decides nothing about what is drawn -- only how much of what
    /// has been read is kept, so that zooming and panning back over it does not
    /// read it again. See gui::PlotBudget.
    Q_PROPERTY(gui::AppController::RamBudget ramBudget READ ramBudget WRITE setRamBudget
                   NOTIFY ramBudgetChanged)

    // --- what a copied plot looks like -----------------------------------
    // Settings > Plot Settings, and the second group in this application that
    // is about the reader rather than about the file -- so, like the budget
    // above, they are remembered between runs.
    //
    // They live here rather than on either plot object deliberately. There is
    // one Plot tab and any number of custom ones, and "what a picture out of
    // this program looks like" is not a property of which tab you happened to
    // press the button on; a reader who sets up a publication export and then
    // opens a second tab has set it up for that one too.

    /// Draw the exported picture for print rather than for this screen.
    ///
    /// Black strokes, the light scope's chrome, and no ground at all -- so
    /// what the picture stands on is the page it is pasted into. It is a
    /// property of the *export* and not of the plot: the reader goes on
    /// looking at the plot they were looking at.
    Q_PROPERTY(bool plotExportPublication READ plotExportPublication WRITE setPlotExportPublication
                   NOTIFY plotExportPublicationChanged)

    /// Whether the crosshair is part of the picture.
    ///
    /// Off by default, which settles an inconsistency rather than introducing
    /// one: the copy *button* is pressed with the pointer over the rail, so it
    /// never caught a crosshair, and Ctrl+C is armed by the pointer being over
    /// the pane, so it always did. The picture now says the same thing
    /// whichever way it was asked for, and a reader who is pointing at a
    /// sample because that sample is the point can say so.
    Q_PROPERTY(bool plotExportCursor READ plotExportCursor WRITE setPlotExportCursor NOTIFY
                   plotExportCursorChanged)

    /// Whether the picture is the size of the pane or a size the reader chose.
    Q_PROPERTY(bool plotExportCustomSize READ plotExportCustomSize WRITE setPlotExportCustomSize
                   NOTIFY plotExportCustomSizeChanged)

    /// That size, in pixels.
    ///
    /// Clamped rather than trusted, on the way in from QML and again on the
    /// way in from QSettings; see kMaxExportPixels for what bounds it.
    Q_PROPERTY(int plotExportWidth READ plotExportWidth WRITE setPlotExportWidth NOTIFY
                   plotExportSizeChanged)
    Q_PROPERTY(int plotExportHeight READ plotExportHeight WRITE setPlotExportHeight NOTIFY
                   plotExportSizeChanged)
    /// ...and the bounds those two are clamped to, so the dialog's own fields
    /// carry them: a box that lets a number be typed and then silently changes
    /// it is a box arguing with the reader.
    Q_PROPERTY(int minExportPixels READ minExportPixels CONSTANT)
    Q_PROPERTY(int maxExportPixels READ maxExportPixels CONSTANT)

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
    /// The member chain the selection is read through: `.position.x`, or empty
    /// for the dataset itself.
    ///
    /// Held apart from `sliceText` rather than folded into it, because the two
    /// say different things and one of them is only a question for a compound.
    /// Keeping them apart is what leaves the pipeline's slice row alone: that
    /// row *is* the slice line, and it had better go on being exactly it. The
    /// slice bar puts them back together for the reader -- see `selectionText`
    /// -- which is a question about how a line is *written*, not about what
    /// the two of them mean.
    Q_PROPERTY(QString memberText READ memberText NOTIFY tableLayoutChanged)
    /// The two of them as one line, the way a reader would type it:
    /// `[:, 2].samples`.
    ///
    /// What the slice bar makes editable over a **compound**, where the split
    /// into two boxes was the wrong shape for the job. A chain and the
    /// subscript it appends axes to are one statement -- `.samples[2]` belongs
    /// on the slice and `[:, 2].samples` is the same selection written the
    /// other way round -- so a reader rearranging one of them is usually
    /// rearranging both, and two boxes made that two commits with a shape they
    /// did not ask for in between. One line is also the only form that can be
    /// *pasted*: it is what `sliceExpression` prints, less the path.
    Q_PROPERTY(QString selectionText READ selectionText NOTIFY tableLayoutChanged)
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
    [[nodiscard]] CustomPlotSet* customPlots() const;

    [[nodiscard]] RamBudget ramBudget() const { return ramBudget_; }
    void setRamBudget(RamBudget budget);

    [[nodiscard]] bool plotExportPublication() const { return plotExportPublication_; }
    void setPlotExportPublication(bool on);
    [[nodiscard]] bool plotExportCursor() const { return plotExportCursor_; }
    void setPlotExportCursor(bool on);
    [[nodiscard]] bool plotExportCustomSize() const { return plotExportCustomSize_; }
    void setPlotExportCustomSize(bool on);
    [[nodiscard]] int plotExportWidth() const { return plotExportWidth_; }
    void setPlotExportWidth(int pixels);
    [[nodiscard]] int plotExportHeight() const { return plotExportHeight_; }
    void setPlotExportHeight(int pixels);

    /// The narrowest and the widest a picture may be asked for, in pixels.
    ///
    /// The ceiling is a real limit rather than a taste: a grab is rendered
    /// into one texture and every graphics API has a maximum texture size --
    /// 16384 on anything recent, less on older hardware. Past it the grab does
    /// not come back small, it comes back not at all.
    ///
    /// Half of that, because a publication picture is drawn *twice* into one
    /// grab -- once on white and once on black, which is how it comes back
    /// with no ground; see ImageClipboard::copyItem. So the tallest picture
    /// this may ask for is the tallest texture there is, halved. Eight
    /// thousand pixels is a figure at 27 inches and 300 dpi, which is a poster
    /// rather than a plate.
    static constexpr int kMinExportPixels = 64;
    static constexpr int kMaxExportPixels = 8192;

    [[nodiscard]] int minExportPixels() const { return kMinExportPixels; }
    [[nodiscard]] int maxExportPixels() const { return kMaxExportPixels; }

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
    [[nodiscard]] QString memberText() const { return memberText_; }
    /// Apply an edited member chain. Returns the reason it could not be read,
    /// or an empty string once the views are showing that member.
    ///
    /// A subscript written on the chain is *folded onto the slice line*, and
    /// the box prints back the bare chain: `.samples[2]` becomes `.samples`
    /// with a `2` in the slice beside it. They are the same selection -- that
    /// is the identity the whole notation rests on -- and the canonical half is
    /// the one where the member's axes are ordinary dimensions the reader can
    /// slice, lay out and put on an axis like any others.
    Q_INVOKABLE QString applyMember(const QString& text);
    /// Why an edited member chain cannot be read, or empty when it can. Pure
    /// arithmetic over the datatype already described, so it answers on every
    /// keystroke without opening anything.
    Q_INVOKABLE QString memberError(const QString& text) const;

    /// The subscript and the chain as one line: `[:, 2].samples`.
    [[nodiscard]] QString selectionText() const;
    /// Apply one. The chain first, because it is what decides the shape the
    /// subscript is against -- and both are read before either is applied, so
    /// a line that will not do leaves the views exactly as they were.
    ///
    /// The subscript may be left off (`.energy` alone is the whole of the
    /// dataset through that member) and so may the chain (`[0:100]` alone is
    /// the struct itself). What comes back is always the canonical pair, which
    /// is where a chain's own subscripts have moved onto the slice: type
    /// `[:].samples[2]` and the box prints `[:, 2].samples`.
    Q_INVOKABLE QString applySelection(const QString& text);
    /// Why one cannot be read, or empty when it can. Checks without applying,
    /// as every other box in this window does.
    Q_INVOKABLE QString selectionError(const QString& text) const;
    /// What could be written next on such a line: the chains, each with
    /// whatever subscript is already in front of it.
    [[nodiscard]] Q_INVOKABLE QStringList selectionCompletions(const QString& text) const;

private:
    /// Read a selection line into the chain it names and the slice line it
    /// means. Returns the reason it will not do, or empty.
    ///
    /// One function because there are two callers and they must not disagree:
    /// what `selectionError` refuses is exactly what `applySelection` will not
    /// apply, and a second copy of the reading is a second grammar.
    [[nodiscard]] QString readSelection(const QString& text, QString& chainText,
                                        postproc::MemberChain& chain, QString& line) const;

public:

    // --- what could be written next --------------------------------------
    //
    // Two grammars are typed by hand in this application: a whole line naming
    // a dataset, a subscript and a member (the custom plots' entry boxes), and
    // a member chain on its own (the box after the slice bar's bracket). Each
    // gets its own list, and both answer in whole strings -- what the box
    // would hold if the candidate were taken -- because splicing a fragment
    // back into a line three grammars deep is work for the thing that knows
    // the grammar rather than for the box.
    //
    // **Nothing here reads more than the reader has already asked to see.**
    // The tree is lazy by design; what a completion of a path can offer is
    // what is listed, and a group that is not listed is *asked for* rather
    // than walked -- the list arrives a moment later and `completionsChanged`
    // says so. A completer that walked the file to answer a keystroke would
    // undo the one property that lets this program open a file of a million
    // objects.

    /// What could be written next on a line naming a dataset. Whole lines.
    [[nodiscard]] Q_INVOKABLE QStringList completions(const QString& text);
    /// The same for a member chain on its own, against the selection's type.
    [[nodiscard]] Q_INVOKABLE QStringList memberCompletions(const QString& text) const;
    /// What Tab writes: the longest head every candidate shares, which is as
    /// far as a reader can be taken without choosing for them.
    [[nodiscard]] Q_INVOKABLE QString commonCompletion(const QStringList& options) const;

private:
    /// What the data views draw: the dataset, or the dataset seen through the
    /// member chain. `originInfo_` is what the file said; this is the result.
    [[nodiscard]] h5core::DatasetInfo projectedInfo() const;
    /// What the postprocessing panel is running on, given what the views are
    /// drawing. Built in one place because the panel's rows state the dataset
    /// *and* the projection, and those two come from different facts.
    [[nodiscard]] PostprocessModel::Subject
    pipelineSubject(const h5core::DatasetInfo& info) const;
    /// The candidates for a path being typed, from what the tree has already
    /// listed. Asks for the listing it needs when it has not been made.
    [[nodiscard]] QStringList pathCompletions(const QString& head,
                                              const QString& fragment);
    /// The datatype a completion of a member chain resolves against: the
    /// selection's own, or whatever the custom plots' cache knows about that
    /// path. Null when neither knows it.
    [[nodiscard]] const h5core::TypeInfo* typeOf(const QString& path) const;

public:
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
    void ramBudgetChanged();
    void plotExportPublicationChanged();
    void plotExportCursorChanged();
    void plotExportCustomSizeChanged();
    /// One signal for both numbers: they are one setting, and nothing binds to
    /// either of them without binding to the size they make together.
    void plotExportSizeChanged();
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
    /// A listing or a datatype that a completion was waiting on has arrived.
    /// Whatever is showing a list of candidates asks again on this.
    void completionsChanged();
    void errorTextChanged();
    void filterTextChanged();
    /// The table's selection of indices or its axis assignment changed.
    void tableLayoutChanged();
    /// The postprocessing pipeline changed, was switched on, or was switched
    /// off -- which is to say the views are now drawing something else.
    void postprocessChanged();
    /// Non-fatal problems worth surfacing transiently in the UI.
    void statusMessage(const QString& message);

protected:
    /// Only the reveal settle; see kRevealMilliseconds.
    void timerEvent(QTimerEvent* event) override;

private:
    void refreshSelection();
    /// Open the tree to what the filter found, wherever in the file that is --
    /// including branches nobody has expanded, which is one listing per level
    /// and is why this is settled rather than run per keystroke.
    void revealMatches();
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
    RamBudget ramBudget_ = MediumRam;
    bool plotExportPublication_ = false;
    bool plotExportCursor_ = false;
    bool plotExportCustomSize_ = false;
    /// 1920 by 1080, which is a figure at a size somebody can use without
    /// having thought about it, and a shape most panes are already close to.
    int plotExportWidth_ = 1920;
    int plotExportHeight_ = 1080;
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
    /// What the file says the selection is, before any member chain. The
    /// chain resolves against this, and `datasetInfo_` below is the result:
    /// what the data views are actually drawing.
    h5core::DatasetInfo originInfo_;
    QString memberText_;
    h5core::MemberSelection memberSelection_;
    /// What each dataset's member chain was when it was last left, so that
    /// coming back to one comes back to the member. Beside `slices_`, and for
    /// the same reason.
    QHash<QString, QString> members_;
    bool datasetIsFloat_ = false;
    int datasetRank_ = 0;
    qint64 datasetElementCount_ = 0;

    H5TreeModel* treeModel_ = nullptr;
    TreeFilterProxyModel* filteredTreeModel_ = nullptr;
    /// Every name in the open file, so the filter box answers out of RAM. Walked
    /// in the background from the moment the file opens -- see NameIndex for why
    /// the one thing about a file that is read whole is its names.
    NameIndex* nameIndex_ = nullptr;
    /// How long the filter box is left alone before the tree is opened to what
    /// it found. A search is typed a character at a time and every prefix of it
    /// has its own results; opening to each of them in turn is a listing per
    /// level per keystroke, of branches the next keystroke throws away.
    static constexpr int kRevealMilliseconds = 200;
    QBasicTimer revealSettle_;
    DatasetTableModel* datasetModel_ = nullptr;
    DatasetStringListModel* datasetStringModel_ = nullptr;
    AttributeTableModel* attributeModel_ = nullptr;
    ObjectInfoModel* infoModel_ = nullptr;
    TableSetupModel* tableSetupModel_ = nullptr;
    PostprocessModel* postprocessModel_ = nullptr;
    DatasetPlot* datasetPlot_ = nullptr;
    DatasetImage* datasetImage_ = nullptr;
    CustomPlotSet* customPlots_ = nullptr;

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

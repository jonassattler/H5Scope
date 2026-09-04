// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "AppController.hpp"

#include "AttributeTableModel.hpp"
#include "DatasetImage.hpp"
#include "DatasetPlot.hpp"
#include "DatasetStringListModel.hpp"
#include "DatasetTableModel.hpp"
#include "H5TreeModel.hpp"
#include "ObjectInfoModel.hpp"
#include "PostprocessModel.hpp"
#include "TableSetupModel.hpp"
#include "TreeFilterProxyModel.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "H5Thread.hpp"
#include "h5core/Attribute.hpp"
#include "h5scope/Version.hpp"
#include "postproc/ComputedDataset.hpp"
#include "postproc/Pipeline.hpp"

#include <QCoreApplication>
#include <QScopeGuard>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QVariantMap>

#include <hdf5.h>

#include <algorithm>

namespace gui {

AppController::AppController(QObject* parent)
    : QObject(parent),
      treeModel_(new H5TreeModel(this)),
      datasetModel_(new DatasetTableModel(this)),
      attributeModel_(new AttributeTableModel(this)),
      infoModel_(new ObjectInfoModel(this)),
      filteredTreeModel_(new TreeFilterProxyModel(this)),
      tableSetupModel_(new TableSetupModel(this)),
      postprocessModel_(new PostprocessModel(this))
{
    filteredTreeModel_->setSourceModel(treeModel_);
    datasetStringModel_ = new DatasetStringListModel(datasetModel_, this);

    // What was opened last time. Read once, here, rather than on every binding
    // that asks: the File menu asks whenever it is opened. Nothing is read at
    // all until a host application has named itself, which keeps the tests off
    // the user's own settings.
    if (!QCoreApplication::organizationName().isEmpty()) {
        recent_ = QSettings().value(QStringLiteral("recentFiles")).toStringList();
        while (recent_.size() > kMaxRecentFiles) {
            recent_.removeLast();
        }
    }
    // Three readings of one table. Both of these follow datasetModel_'s resets
    // on their own, so nothing here has to tell them the selection moved.
    datasetPlot_ = new DatasetPlot(datasetModel_, this);
    datasetImage_ = new DatasetImage(datasetModel_, this);

    // The pipeline's second row is the slice above the table rather than a
    // copy of it, so it is given the model that owns that slice.
    postprocessModel_->setSliceSource(tableSetupModel_);

    // The panel is the authority on what the table shows; the table model
    // only ever hears about it through here.
    connect(tableSetupModel_, &TableSetupModel::tableLayoutChanged, this, [this] {
        // Through the pipeline rather than straight to the table, even when
        // there is no pipeline running: the second row of it *is* this slice,
        // so telling it is what re-reads the shapes, and its own `changed`
        // then hands the table whichever source it should be drawing. Calling
        // applyDataSource() here as well would run the whole thing twice for
        // one edit -- which on a selection near the cap is two reads of 128 MB.
        postprocessModel_->sliceChanged();
        emit tableLayoutChanged();
    });

    // A pipeline that changed is a different array under the same slice.
    connect(postprocessModel_, &PostprocessModel::changed, this, [this] {
        applyDataSource();
        emit postprocessChanged();
    });

    // Lazy population can fail mid-expand; surface it without the model
    // needing to know how the UI reports things.
    connect(treeModel_, &H5TreeModel::loadFailed, this, &AppController::statusMessage);

    // ...and so can anything else asked of the file, now that asking is a job
    // whose failure has nowhere else to go. The message is the one h5core
    // wrote; the status strip is where a reader looks for it.
    connect(&H5Thread::instance(), &H5Thread::jobFailed, this,
            &AppController::statusMessage);
    connect(&H5Thread::instance(), &H5Thread::busyChanged, this,
            &AppController::busyChanged);
}

AppController::~AppController() = default;

QAbstractItemModel* AppController::treeModel() const { return treeModel_; }
QAbstractItemModel* AppController::filteredTreeModel() const { return filteredTreeModel_; }
QAbstractItemModel* AppController::datasetModel() const { return datasetModel_; }
QAbstractItemModel* AppController::datasetStringModel() const
{
    return datasetStringModel_;
}
QAbstractItemModel* AppController::attributeModel() const { return attributeModel_; }
QAbstractItemModel* AppController::infoModel() const { return infoModel_; }
QAbstractItemModel* AppController::tableSetupModel() const { return tableSetupModel_; }
PostprocessModel* AppController::postprocessModel() const { return postprocessModel_; }

bool AppController::busy() const
{
    return H5Thread::instance().busy();
}

bool AppController::postprocessActive() const
{
    return postprocessModel_->active();
}

void AppController::applyDataSource()
{
    // Mid-selection the four things this reads from do not yet agree with each
    // other; see `selecting_`. The call at the end of applySelection() is the
    // one that counts.
    if (selecting_) {
        return;
    }
    if (!hasDataset_) {
        datasetModel_->setSource(false, {}, {});
        return;
    }

    // Whatever the last edit asked for is no longer what is wanted. Without
    // this, a pipeline run that was still going when it was switched off would
    // arrive afterwards and put its output back on the table.
    sourceRequests_.reset();

    const QString path = currentPath_;
    const bool pipeline = postprocessModel_->active();
    const TableLayout layout = tableSetupModel_->layout();

    if (!pipeline) {
        // The common case, and it reads nothing. What the table is being told
        // is the description this side already holds and the layout the panel
        // just resolved; the cells themselves are fetched per block, when the
        // grid asks. Doing it here rather than a round trip later is what keeps
        // rearranging a table immediate.
        datasetModel_->setSource(true, datasetInfo_, path);
        datasetModel_->setLayout(layout);
        const QString message = datasetModel_->errorText();
        if (datasetMessage_ != message) {
            datasetMessage_ = message;
            emit selectionChanged();
        }
        // The pipeline's last output, dropped on the thread that owns it.
        // Nothing waits for this: the table has already been put back on the
        // file, and a source it is no longer reading can go when it goes.
        H5Thread::instance().submitVoid(
            sourceRequests_,
            [](H5Session& session) { session.setComputed(nullptr); }, [] {});
        return;
    }

    const auto steps = postprocessModel_->pipeline();
    const auto upTo = postprocessModel_->upTo();
    const QString computedSuffix = tr("(postprocessed)");

    struct Source {
        bool present = false;
        bool computed = false;
        h5core::DatasetInfo info;
        QString path;
    };

    H5Thread::instance().submit(
        sourceRequests_,
        [path, steps, upTo, computedSuffix](H5Session& session) {
            Source source;
            h5core::Dataset* dataset = session.dataset(path.toStdString());
            if (dataset == nullptr) {
                session.setComputed(nullptr);
                return source;
            }

            // The pipeline reads the slice out of the file itself, so the
            // layout the panel resolved is not applied on top of it -- it has
            // already happened, and applying it twice would slice the slice.
            const postproc::RunResult result = postproc::run(*dataset, steps, upTo);
            if (!result.usable()) {
                // Not even the read worked. The views fall back to the file,
                // which is the last thing known to be drawable, and the reason
                // is on the row that gave it.
                session.setComputed(nullptr);
                source.present = true;
                source.info = dataset->info();
                source.path = path;
                return source;
            }

            auto computed = std::make_shared<postproc::ComputedDataset>(
                result.array, dataset->info(), dataset->path(),
                computedSuffix.toStdString());
            source.present = true;
            source.computed = true;
            source.info = computed->info();
            source.path = QString::fromStdString(computed->path());
            session.setComputed(std::move(computed));
            return source;
        },
        [this, layout, path](Source source) {
            if (path != currentPath_) {
                return; // the reader moved on while the pipeline ran
            }
            if (!source.present) {
                datasetModel_->setSource(false, {}, {});
                return;
            }
            datasetModel_->setSource(true, source.info, source.path);
            if (source.computed) {
                // The output array is a new array with its own rank, so the
                // axis assignment the panel made about the *dataset* cannot be
                // carried over to it. It gets the ordinary default instead: the
                // last dimension along the columns and the rest down the rows,
                // and no image arrangement, because a pipeline has just made
                // whatever the Image spec said about these dimensions untrue.
                datasetModel_->setLayout(defaultLayout(source.info.shape));
            } else {
                datasetModel_->setLayout(layout);
            }
            const QString message = datasetModel_->errorText();
            if (datasetMessage_ != message) {
                datasetMessage_ = message;
                emit selectionChanged();
            }
        });
}

DatasetPlot* AppController::datasetPlot() const { return datasetPlot_; }
DatasetImage* AppController::datasetImage() const { return datasetImage_; }

QString AppController::fileName() const
{
    return filePath_.isEmpty() ? QString{} : QFileInfo(filePath_).fileName();
}

QString AppController::fileSize() const
{
    if (filePath_.isEmpty()) {
        return {};
    }
    return QLocale::system().formattedDataSize(QFileInfo(filePath_).size());
}

QString AppController::appVersion()
{
    return QString::fromLatin1(h5scope::kVersion);
}

QString AppController::appCommit()
{
    return QString::fromLatin1(h5scope::kCommit);
}

QString AppController::binaryName()
{
    // The name, not the path: the path is long, changes with where it was
    // built, and says nothing the name does not. The name carries the version,
    // which is the question this answers -- "which of these am I running".
    return QFileInfo(QCoreApplication::applicationFilePath()).fileName();
}

QString AppController::hdf5Version() const
{
    // Across the thread like everything else. It reads three integers out of
    // the library rather than anything off a disk, so waiting for it costs
    // nothing -- but "everything else" is the point: a rule with an exception
    // in it for the calls that look harmless is not a rule.
    struct Version {
        unsigned major = 0;
        unsigned minor = 0;
        unsigned release = 0;
    };
    const Version version = H5Thread::instance().invoke([](H5Session&) {
        Version found;
        H5get_libversion(&found.major, &found.minor, &found.release);
        return found;
    });
    return QStringLiteral("hdf5 %1.%2.%3")
        .arg(version.major)
        .arg(version.minor)
        .arg(version.release);
}

QString AppController::filterText() const { return filteredTreeModel_->filterText(); }

void AppController::setFilterText(const QString& text)
{
    if (filteredTreeModel_->filterText() == text) {
        return;
    }
    filteredTreeModel_->setFilterText(text);
    emit filterTextChanged();
}

QVariantMap AppController::rememberedSettings(const QString& group) const
{
    const auto forPath = settings_.constFind(currentPath_);
    if (forPath == settings_.constEnd()) {
        return {};
    }
    return forPath->value(group);
}

void AppController::rememberSettings(const QString& group, const QVariantMap& values)
{
    // Nothing to hang them on. A view writing settings with no dataset
    // selected is a view that has just been built, not one the reader has
    // done anything to.
    if (currentPath_.isEmpty() || group.isEmpty()) {
        return;
    }
    settings_[currentPath_][group] = values;
}

void AppController::leaveSelection()
{
    if (currentPath_.isEmpty()) {
        return;
    }
    // The slice belongs to the controller rather than to any one view -- all
    // three of them draw whatever it resolves to -- so it is written down here
    // rather than through the same bag the views use.
    if (datasetTabVisible_) {
        slices_[currentPath_] = tableSetupModel_->sliceText();
    }
    emit selectionAboutToChange();
}

QVariantList AppController::infoPanels() const
{
    QVariantList panels = infoModel_->sections();

    // The attributes panel is the one place the two models meet: the info
    // model knows how many there are, the attribute model holds them. Splice
    // the real rows in so the Information tab shows values, not just a count.
    const int attributeCount = attributeModel_->rowCount();
    if (attributeCount == 0) {
        // The panel carries its own "no attributes" sentence in that case, and
        // splicing an empty row list in would leave it with neither.
        return panels;
    }

    QVariantList rows;
    rows.reserve(attributeCount);
    for (int row = 0; row < attributeCount; ++row) {
        const QModelIndex index = attributeModel_->index(row, 0);
        rows.append(QVariantMap{
            {QStringLiteral("label"),
             index.data(AttributeTableModel::NameRole).toString()},
            {QStringLiteral("value"),
             index.data(AttributeTableModel::ValueRole).toString()},
            {QStringLiteral("isWarning"), false},
        });
    }

    for (QVariant& panel : panels) {
        QVariantMap map = panel.toMap();
        if (map.value(QStringLiteral("title")).toString()
            == QLatin1String("attributes")) {
            map[QStringLiteral("rows")] = rows;
            panel = map;
            break;
        }
    }
    return panels;
}

QString AppController::sliceExpression() const
{
    if (!datasetTabVisible_) {
        return QStringLiteral("\u2014");
    }
    // A scalar has no axes to subscript, so it is written as itself.
    const QString body = tableSetupModel_->sliceText();
    if (body.isEmpty()) {
        return currentPath_;
    }
    return currentPath_ + QStringLiteral("[") + body + QStringLiteral("]");
}

QString AppController::sliceText() const
{
    return datasetTabVisible_ ? tableSetupModel_->sliceText() : QString{};
}

QString AppController::applySlice(const QString& text)
{
    if (!datasetTabVisible_) {
        return QStringLiteral("no dataset is selected");
    }
    return tableSetupModel_->applySlice(text);
}

QString AppController::sliceError(const QString& text) const
{
    if (!datasetTabVisible_) {
        return QStringLiteral("no dataset is selected");
    }
    return tableSetupModel_->sliceError(text);
}

QStringList AppController::statusLeft() const
{
    if (!hasFile()) {
        return {QStringLiteral("no file open")};
    }
    // The file name leads: with the breadcrumb bar gone this strip is the only
    // place in the window, apart from its title, that names the open file.
    if (currentPath_.isEmpty()) {
        return {fileName()};
    }
    return {fileName(), currentPath_,
            infoModel_->valueFor(QStringLiteral("Kind")).toLower()};
}

QStringList AppController::statusRight() const
{
    if (!hasFile()) {
        return {hdf5Version()};
    }

    QStringList segments;
    if (datasetTabVisible_) {
        segments << infoModel_->valueFor(QStringLiteral("Type"))
                 << infoModel_->valueFor(QStringLiteral("Shape"))
                 << QStringLiteral("%1 elements")
                        .arg(infoModel_->valueFor(QStringLiteral("Elements")));
    } else {
        // A link that resolves to nothing has no object behind it and so no
        // attribute count; "0 attrs" would claim one had been looked for.
        const QString attributes = infoModel_->valueFor(QStringLiteral("Attributes"));
        if (!attributes.isEmpty()) {
            segments << QStringLiteral("%1 attrs").arg(attributes);
        }
    }
    segments << hdf5Version();
    segments.removeAll(QString{});
    return segments;
}

void AppController::setErrorText(const QString& text)
{
    if (errorText_ != text) {
        errorText_ = text;
        emit errorTextChanged();
    }
}

QVariantList AppController::recentFiles() const
{
    QVariantList entries;
    entries.reserve(recent_.size());
    for (const QString& path : recent_) {
        const QFileInfo info(path);
        entries.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("folder"), info.absolutePath()},
            // Checked when the menu asks rather than when the file was opened:
            // a file can go missing between the two, and the row that offers to
            // open it is the place that has to know.
            {QStringLiteral("missing"), !info.exists()},
        });
    }
    return entries;
}

void AppController::remember(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute.isEmpty()) {
        return;
    }
    // Moved to the front rather than appended: the list is in the order they
    // were last opened, so re-opening one is not a second entry.
    recent_.removeAll(absolute);
    recent_.prepend(absolute);
    while (recent_.size() > kMaxRecentFiles) {
        recent_.removeLast();
    }

    // Nowhere to write to until a host application has named itself; the tests
    // construct controllers freely and must not leave anything on disk.
    if (!QCoreApplication::organizationName().isEmpty()) {
        QSettings settings;
        settings.setValue(QStringLiteral("recentFiles"), recent_);
    }
    emit recentFilesChanged();
}

void AppController::clearRecentFiles()
{
    if (recent_.isEmpty()) {
        return;
    }
    recent_.clear();
    if (!QCoreApplication::organizationName().isEmpty()) {
        QSettings settings;
        settings.remove(QStringLiteral("recentFiles"));
    }
    emit recentFilesChanged();
}

bool AppController::openFile(const QString& path)
{
    // Returns whether the open was *started*, not whether it succeeded: the
    // file is opened on the HDF5 thread and a large one over a network share
    // takes long enough that waiting here would freeze the window on the click
    // that asked for it. `fileChanged` and `errorText` are how it finishes.
    if (path.isEmpty()) {
        return false;
    }

    // Whatever was open is gone from this moment, whether or not the new file
    // turns out to open. Two files can hold a "/data" that have nothing to do
    // with each other, so what was remembered about the last one says nothing
    // about this one.
    requests_.reset();
    fileRequests_.reset();
    leaveSelection();
    settings_.clear();
    slices_.clear();
    postprocessModel_->reset();
    hasDataset_ = false;
    datasetInfo_ = {};
    fileOpen_ = false;
    filePath_.clear();
    currentPath_.clear();
    treeModel_->close();
    setErrorText(QString{});
    emit fileChanged();
    refreshSelection();

    struct Opened {
        bool ok = false;
        QString error;
        QString firstChild;
    };

    H5Thread::instance().submit(
        fileRequests_,
        [path](H5Session& session) {
            Opened opened;
            try {
                session.open(path.toStdString());
            } catch (const h5core::H5Error& error) {
                opened.error = QString::fromStdString(error.summary());
                return opened;
            }
            opened.ok = true;
            // The first top-level name, so the tabs are never blank on open.
            // Taken here rather than by asking the tree afterwards, because the
            // tree's own listing is a separate round trip and this is one line
            // of the same one.
            try {
                const auto children =
                    session.file()->children("/", h5core::File::Resolve::Links);
                if (!children.empty()) {
                    opened.firstChild =
                        QString::fromStdString(children.front().path);
                }
            } catch (const h5core::H5Error&) {
                // A file whose root will not list is still open, and the tree
                // will say so in its own words.
            }
            return opened;
        },
        [this, path](Opened opened) {
            if (!opened.ok) {
                // Recorded, not thrown at the user through a modal dialog: a
                // blocking dialog here would hang any non-interactive caller,
                // tests included.
                setErrorText(opened.error);
                emit fileOpened(false, path);
                return;
            }
            setErrorText(QString{});
            fileOpen_ = true;
            filePath_ = path;
            // Only a file that actually opened. A path that failed is not
            // something to offer the reader again from a menu.
            remember(path);
            treeModel_->open();
            emit fileChanged();

            currentPath_ = opened.firstChild.isEmpty() ? QStringLiteral("/")
                                                       : opened.firstChild;
            refreshSelection();
            emit fileOpened(true, path);
        });
    return true;
}

bool AppController::openUrl(const QUrl& url)
{
    return openFile(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void AppController::closeFile()
{
    requests_.reset();
    fileRequests_.reset();
    leaveSelection();
    settings_.clear();
    slices_.clear();
    postprocessModel_->reset();
    hasDataset_ = false;
    datasetInfo_ = {};
    fileOpen_ = false;
    filePath_.clear();
    currentPath_.clear();
    setErrorText(QString{});
    treeModel_->close();
    // The close itself is a job like any other: H5Fclose is an HDF5 call and
    // belongs on the thread that owns the library, and the session is where the
    // file has been all along.
    H5Thread::instance().submitVoid(
        fileRequests_, [](H5Session& session) { session.close(); }, [] {});
    emit fileChanged();
    refreshSelection();
}

bool AppController::selectPath(const QString& path)
{
    if (!fileOpen_ || path.isEmpty()) {
        return false;
    }
    // Whether the path is really there is settled by the gather below, which
    // has to read the object anyway. Saying yes here means "this selection has
    // been taken up", which it has: the tree highlights it immediately and the
    // tabs follow when the file answers.
    //
    // A link is selectable whether or not it resolves. A dangling soft link and
    // an external link into a missing file are both things the tree shows, and
    // clicking one has to say what it is rather than do nothing at all.
    leaveSelection();
    currentPath_ = path;
    refreshSelection();
    return true;
}

void AppController::refreshSelection()
{
    // Anything still coming describes the object that was selected before this
    // one. Disowning it here is what stops a slow read of the last dataset
    // arriving after this one and overwriting it.
    requests_.reset();

    if (!fileOpen_ || currentPath_.isEmpty()) {
        datasetTabVisible_ = false;
        metadataTabVisible_ = false;
        datasetIsString_ = false;
        datasetIsNumeric_ = false;
        datasetIsCompound_ = false;
        datasetIsFloat_ = false;
        datasetRank_ = 0;
        datasetElementCount_ = 0;
        datasetMessage_.clear();
        hasDataset_ = false;
        datasetInfo_ = {};
        infoModel_->clear();
        attributeModel_->clear();
        datasetModel_->setSource(false, {}, {});
        tableSetupModel_->setShape({});
        emit selectionChanged();
        return;
    }

    // Nothing is cleared here, and nothing is announced. For the one turn it
    // takes the file to answer, the tabs go on showing the object that was
    // selected a moment ago -- which is a truthful thing for them to be doing
    // and a great deal calmer than blanking them and filling them in again.
    // applySelection() is where the change becomes visible, once there is
    // something to show.
    const QString path = currentPath_;
    H5Thread::instance().submit(
        requests_,
        [path](H5Session& session) {
            SelectionFacts facts;
            h5core::File* file = session.file();
            if (file == nullptr) {
                return facts;
            }
            try {
                const auto node = file->nodeInfo(path.toStdString());
                facts.described = true;
                facts.isDataset = node.kind == h5core::NodeKind::Dataset;
                // An unresolved link has no object behind it, so it has no
                // attributes either; asking would fail rather than answer
                // nothing.
                facts.hasAttributes =
                    node.resolves() && file->attributeCount(path.toStdString()) > 0;
            } catch (const h5core::H5Error& error) {
                facts.message = QString::fromStdString(error.summary());
                return facts;
            }

            facts.panels = ObjectInfoModel::gather(*file, path);

            if (facts.isDataset) {
                try {
                    // Opened into the session, so that re-running a pipeline
                    // over it later does not re-open it.
                    const h5core::Dataset* dataset = session.dataset(path.toStdString());
                    if (dataset != nullptr) {
                        facts.datasetOpened = true;
                        facts.info = dataset->info();
                    } else {
                        // Re-open it plainly, purely to get the reason.
                        const h5core::Dataset probe(*file, path.toStdString());
                        facts.datasetOpened = true;
                        facts.info = probe.info();
                    }
                } catch (const h5core::H5Error& error) {
                    facts.datasetMessage = QString::fromStdString(error.summary());
                }
            }

            if (facts.hasAttributes) {
                try {
                    facts.attributes = h5core::readAttributes(*file, path.toStdString());
                } catch (const h5core::H5Error&) {
                    facts.attributes.clear();
                }
            }
            return facts;
        },
        [this, path](SelectionFacts facts) {
            if (path != currentPath_) {
                return; // the reader moved on while the file was answering
            }
            applySelection(std::move(facts));
        });
}

void AppController::applySelection(SelectionFacts facts)
{
    // Everything below rebuilds one part of the selection and announces it.
    // Nothing acts on those announcements until they have all been made; see
    // `selecting_`.
    selecting_ = true;
    const QScopeGuard settled([this] {
        selecting_ = false;
        applyDataSource();
    });

    datasetTabVisible_ = false;
    metadataTabVisible_ = false;
    datasetIsString_ = false;
    datasetIsNumeric_ = false;
    datasetIsCompound_ = false;
    datasetIsFloat_ = false;
    datasetRank_ = 0;
    datasetElementCount_ = 0;
    datasetMessage_.clear();
    hasDataset_ = false;
    datasetInfo_ = {};

    if (!facts.described) {
        if (!facts.message.isEmpty()) {
            emit statusMessage(facts.message);
        }
        infoModel_->clear();
        attributeModel_->clear();
        emit selectionChanged();
        return;
    }

    infoModel_->showContent(std::move(facts.panels));
    datasetTabVisible_ = facts.isDataset;
    metadataTabVisible_ = facts.hasAttributes;

    if (facts.isDataset && facts.datasetOpened) {
        const h5core::DatasetInfo& info = facts.info;
        datasetRank_ = static_cast<int>(info.rank());
        datasetIsString_ = info.type.cls == h5core::TypeClass::String;
        datasetIsNumeric_ = info.isNumeric() && info.readable();
        datasetIsCompound_ =
            info.type.cls == h5core::TypeClass::Compound && info.readable();
        datasetIsFloat_ = info.type.cls == h5core::TypeClass::Float && info.readable();
        datasetElementCount_ = static_cast<qint64>(info.elementCount());
        const std::vector<hsize_t> shape = info.shape;
        // A dataset that says it is a picture stops being one while a pipeline
        // is running on it: the Image spec fixes which dimension is height and
        // which is colour, and a transpose or a reduction is about to make that
        // statement untrue. Suppressed here, at the one place the arrangement
        // is decided, rather than at each of the places that would otherwise
        // have to know.
        const auto image = postprocessModel_->active()
                               ? std::optional<h5core::ImageInfo>{}
                               : info.image;
        hasDataset_ = true;
        datasetInfo_ = info;
        // Before the layout, so the panel's reset lands on a pipeline that
        // already knows the shape it is starting from.
        postprocessModel_->setDataset(currentPath_, shape,
                                      info.isNumeric() && info.readable());
        tableSetupModel_->setShape(shape, image);
        // ...and then whatever slice was last written for this dataset. A line
        // that no longer reads -- which nothing in one session should produce,
        // the shape being the same -- leaves the defaults alone.
        if (const auto slice = slices_.constFind(currentPath_);
            slice != slices_.constEnd()) {
            static_cast<void>(tableSetupModel_->applySlice(*slice));
        }
    } else if (facts.isDataset) {
        datasetModel_->setSource(false, {}, {});
        postprocessModel_->setDataset({}, {}, false);
        tableSetupModel_->setShape({});
        datasetMessage_ = facts.datasetMessage;
    } else {
        datasetModel_->setSource(false, {}, {});
        postprocessModel_->setDataset({}, {}, false);
        tableSetupModel_->setShape({});
    }

    if (metadataTabVisible_) {
        attributeModel_->setAttributes(std::move(facts.attributes));
    } else {
        attributeModel_->clear();
    }

    emit selectionChanged();
}

} // namespace gui

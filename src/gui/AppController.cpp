// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "AppController.hpp"

#include "AttributeTableModel.hpp"
#include "Completion.hpp"
#include "DatasetImage.hpp"
#include "DatasetPlot.hpp"
#include "DatasetStringListModel.hpp"
#include "DatasetTableModel.hpp"
#include "H5TreeModel.hpp"
#include "NameIndex.hpp"
#include "ObjectInfoModel.hpp"
#include "PostprocessModel.hpp"
#include "PlotBudget.hpp"
#include "TableSetupModel.hpp"
#include "TreeFilterProxyModel.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "H5Thread.hpp"
#include "h5core/Attribute.hpp"
#include "h5scope/Version.hpp"
#include "postproc/ComputedDataset.hpp"
#include "postproc/MemberPath.hpp"
#include "postproc/Subscripts.hpp"
#include "postproc/Pipeline.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>
#include <QScopeGuard>
#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <QVariantMap>

#include <hdf5.h>

#include <algorithm>
#include <cmath>

namespace gui {

namespace {

/// The reader's three choices, as the budget's own three.
///
/// Two enumerations rather than one, because they answer to different things:
/// this one is a setting with a name in a menu and a number in QSettings, and
/// PlotBudget's is what the arithmetic switches on. Folding them would put a
/// QML-facing enumeration in a file that has no Qt in its argument.
Appetite appetiteOf(AppController::RamBudget budget)
{
    switch (budget) {
    case AppController::LowRam:
        return Appetite::Low;
    case AppController::GreedyRam:
        return Appetite::Greedy;
    case AppController::MediumRam:
        break;
    }
    return Appetite::Medium;
}

/// Split `[subscript].chain` into its two halves.
///
/// The path is not in this box, so there is nothing here to disambiguate: what
/// stands between the outermost brackets is the subscript, and everything after
/// the one that closes them is the chain. (A custom plot's entry line has to
/// see a `]` before it will read a `.` as a member, because a link name holds a
/// dot as freely as it holds a bracket -- `/data/run.3` is a dataset. There is
/// no link name in this box.)
///
/// Matched rather than found, because both halves may carry brackets of their
/// own: `[:, [0,3]].samples[2]` has three of them and only the second closes
/// the subscript.
///
/// A line with no leading bracket is all chain, and the subscript it leaves off
/// is the whole of the dataset -- which is what an empty subscript already
/// means to the grammar below, so nothing has to spell it out.
[[nodiscard]] bool splitSelection(const QString& text, QString& subscript, QString& chain,
                                  QString& error)
{
    const QString line = text.trimmed();
    subscript.clear();
    chain.clear();
    if (line.isEmpty()) {
        return true;
    }
    if (!line.startsWith(QLatin1Char('['))) {
        chain = line;
        return true;
    }
    int depth = 0;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar at = line.at(i);
        if (at == QLatin1Char('[')) {
            ++depth;
            continue;
        }
        if (at != QLatin1Char(']')) {
            continue;
        }
        --depth;
        if (depth > 0) {
            continue;
        }
        subscript = line.mid(1, i - 1).trimmed();
        chain = line.mid(i + 1).trimmed();
        return true;
    }
    error = AppController::tr("the subscript is missing its closing bracket");
    return false;
}

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent),
      // In declaration order, which is the order they are actually built in
      // whatever order they are written here.
      treeModel_(new H5TreeModel(this)),
      filteredTreeModel_(new TreeFilterProxyModel(this)),
      nameIndex_(new NameIndex(this)),
      datasetModel_(new DatasetTableModel(this)),
      attributeModel_(new AttributeTableModel(this)),
      infoModel_(new ObjectInfoModel(this)),
      tableSetupModel_(new TableSetupModel(this)),
      postprocessModel_(new PostprocessModel(this))
{
    filteredTreeModel_->setSourceModel(treeModel_);
    filteredTreeModel_->setNameIndex(nameIndex_);
    // A result in a branch the walk had not reached when the filter was typed
    // is still a result. The index says so as soon as it gets there, and this
    // is what opens the tree to it.
    connect(nameIndex_, &NameIndex::grew, this, [this] {
        if (nameIndex_->complete() && !filterText().isEmpty()) {
            revealMatches();
        }
    });
    datasetStringModel_ = new DatasetStringListModel(datasetModel_, this);

    // What was opened last time. Read once, here, rather than on every binding
    // that asks: the File menu asks whenever it is opened. Nothing is read at
    // all until a host application has named itself, which keeps the tests off
    // the user's own settings.
    if (!QCoreApplication::organizationName().isEmpty()) {
        QSettings settings;
        recent_ = settings.value(QStringLiteral("recentFiles")).toStringList();
        while (recent_.size() > kMaxRecentFiles) {
            recent_.removeLast();
        }
        // ...and how much the reader said the plots may hold. Read before the
        // plots are made below, so the first line drawn is already read under
        // the budget they chose rather than under the default and then again.
        const int stored =
            settings.value(QStringLiteral("ramBudget"), static_cast<int>(MediumRam)).toInt();
        ramBudget_ = static_cast<RamBudget>(std::clamp(stored, 0, 2));
        // ...and what a copied plot should look like. Nothing here is read
        // until a picture is asked for, so these are read for the same reason
        // the two above are: once, where the reading is, rather than on every
        // binding that asks.
        plotExportPublication_ =
            settings.value(QStringLiteral("plotExportPublication"), false).toBool();
        plotExportCursor_ = settings.value(QStringLiteral("plotExportCursor"), false).toBool();
        plotExportCustomSize_ =
            settings.value(QStringLiteral("plotExportCustomSize"), false).toBool();
        plotExportWidth_ =
            std::clamp(settings.value(QStringLiteral("plotExportWidth"), plotExportWidth_).toInt(),
                       kMinExportPixels, kMaxExportPixels);
        plotExportHeight_ = std::clamp(
            settings.value(QStringLiteral("plotExportHeight"), plotExportHeight_).toInt(),
            kMinExportPixels, kMaxExportPixels);
        plotExportCustomDpi_ =
            settings.value(QStringLiteral("plotExportCustomDpi"), false).toBool();
        plotExportDpi_ =
            std::clamp(settings.value(QStringLiteral("plotExportDpi"), plotExportDpi_).toInt(),
                       kMinExportDpi, kMaxExportDpi);
    }
    PlotBudget::instance().setAppetite(appetiteOf(ramBudget_));
    // Three readings of one table. Both of these follow datasetModel_'s resets
    // on their own, so nothing here has to tell them the selection moved.
    datasetPlot_ = new DatasetPlot(datasetModel_, this);
    datasetImage_ = new DatasetImage(datasetModel_, this);

    // ...and one reading of everything else. The custom plots are not built
    // over the table, because they are not about the selected dataset at all;
    // they are here so that QML still talks to exactly one object.
    customPlots_ = new CustomPlotSet(this);
    connect(customPlots_, &CustomPlotSet::notice, this,
            &AppController::statusMessage);
    // The saved views outlive the file, so how much of each one fits has to be
    // worked out again whenever another is opened. One crossing for the lot,
    // and only once there is something to ask.
    connect(this, &AppController::fileOpened, customPlots_,
            [this](bool ok, const QString&) {
                if (ok) {
                    customPlots_->refreshViewStates();
                }
            });

    // The pipeline's second row is the slice above the table rather than a
    // copy of it, so it is given the model that owns that slice.
    postprocessModel_->setSliceSource(tableSetupModel_);
    // And where its Select row's chain lives, which is here. The row is not a
    // copy of the box in the slice bar any more than the slice row is a copy of
    // the slice: it is it.
    postprocessModel_->setMemberSource(this);

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

    // A group that has just been listed is a set of completions that did not
    // exist a moment ago, and a row that has just learned whether it is a group
    // or a dataset is a completion that has just learned what to write after
    // it. The tree is lazy in both, so this is how a box asking "what could go
    // here" hears that the answer has changed under it.
    connect(treeModel_, &H5TreeModel::rowsInserted, this,
            &AppController::completionsChanged);
    connect(treeModel_, &H5TreeModel::dataChanged, this,
            &AppController::completionsChanged);

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
    TableLayout layout = tableSetupModel_->layout();
    // What the views are drawing is named for what it is: "/events.samples"
    // rather than "/events", because a reader looking at a column of floats
    // should be told which column.
    const QString shown = path + memberText_;
    const h5core::MemberSelection member = memberSelection_;

    if (!pipeline) {
        // The common case, and it reads nothing. What the table is being told
        // is the description this side already holds and the layout the panel
        // just resolved; the cells themselves are fetched per block, when the
        // grid asks. Doing it here rather than a round trip later is what keeps
        // rearranging a table immediate.
        datasetModel_->setSource(true, datasetInfo_, shown, memberText_,
                                 static_cast<int>(originInfo_.shape.size()));
        // Moved rather than copied. A layout names every index it selects, so
        // on a ten-million-element vector it is eighty megabytes, and this
        // branch is the last reader of it.
        datasetModel_->setLayout(std::move(layout));
        const QString message = datasetModel_->errorText();
        if (datasetMessage_ != message) {
            datasetMessage_ = message;
            emit selectionChanged();
        }
        // The pipeline's last output, dropped on the thread that owns it, and
        // the member the selection is now read through, set on the same
        // crossing. Nothing waits for either: the table has already been put
        // back on the file, and a source it is no longer reading can go when it
        // goes. The member has to be set before the next read rather than
        // before this call returns, and every read is a later job.
        H5Thread::instance().submitVoid(
            sourceRequests_,
            [member](H5Session& session) {
                session.setComputed(nullptr);
                session.setMember(member);
            },
            [] {});
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
        [path, steps, upTo, computedSuffix, member](H5Session& session) {
            Source source;
            // Before the dataset is asked for: the pipeline runs on what the
            // views draw, which with a chain set is the member.
            session.setMember(member);
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
                computedSuffix.toStdString(),
                postproc::preservesIntegers(steps, result.ran));
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

CustomPlotSet* AppController::customPlots() const { return customPlots_; }

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
    // The tree opens itself to the results, and opening a branch nobody has
    // expanded is a listing per level. Settled rather than done per keystroke:
    // a reader typing `temperature` would otherwise have the file listed its
    // way down to the results of `t`, `te`, `tem` and nine more prefixes, every
    // one of them abandoned by the next character.
    revealSettle_.start(kRevealMilliseconds, this);
}

void AppController::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != revealSettle_.timerId()) {
        QObject::timerEvent(event);
        return;
    }
    revealSettle_.stop();
    revealMatches();
}

void AppController::revealMatches()
{
    // Bounded by TreeFilterProxyModel::kRevealLimit, which answers with nothing
    // at all rather than with a prefix of the results -- see the note there.
    // So this is at most a couple of hundred walks of a handful of levels, and
    // every level already listed costs nothing at all.
    for (const QString& path : filteredTreeModel_->revealPaths()) {
        treeModel_->revealPath(path);
    }
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

h5core::DatasetInfo AppController::projectedInfo() const
{
    // What the data views draw. With no chain it is the dataset; with one it is
    // the dataset's shape followed by the axes the chain appends, holding what
    // the chain lands on -- which is the same derivation h5core::FieldDataset
    // makes when it opens, stated here so the panels know the shape before
    // anything has been read.
    if (memberSelection_.empty()) {
        return originInfo_;
    }
    h5core::DatasetInfo info = originInfo_;
    info.type = memberSelection_.type;
    info.shape.insert(info.shape.end(), memberSelection_.dims.begin(),
                      memberSelection_.dims.end());
    info.maxShape = info.shape;
    info.chunk.clear();
    // A member projection has just made whatever the Image spec said about
    // these dimensions untrue, for ComputedDataset's reason.
    info.image.reset();
    if (info.space == h5core::Dataspace::Scalar && !memberSelection_.dims.empty()) {
        info.space = h5core::Dataspace::Simple;
    }
    return info;
}

PostprocessModel::Subject AppController::pipelineSubject(
    const h5core::DatasetInfo& info) const
{
    PostprocessModel::Subject subject;
    subject.path = currentPath_;
    subject.shape = info.shape;
    subject.numeric = info.isNumeric() && info.readable();
    subject.originShape = originInfo_.shape;
    // The same condition that puts the member box in the bar on screen, so the
    // two surfaces for naming a member appear and disappear together.
    if (originInfo_.type.cls == h5core::TypeClass::Compound && info.readable()) {
        subject.memberChoices = postproc::memberChains(originInfo_.type);
    }
    subject.originType = originInfo_.type;
    return subject;
}

const h5core::TypeInfo* AppController::typeOf(const QString& path) const
{
    // The selection first, because that is the one datatype this controller
    // described itself and the one a member box is nearly always about.
    if (hasDataset_ && path == currentPath_) {
        return &originInfo_.type;
    }
    if (const PathFacts* facts = customPlots_->lookup()->facts(path);
        facts != nullptr && facts->isDataset) {
        return &facts->type;
    }
    return nullptr;
}

QStringList AppController::pathCompletions(const QString& head, const QString& fragment)
{
    // The group whose children could go here. `head` ends in the separator, so
    // taking it off leaves the group -- and taking everything off leaves the
    // root, which is its own parent.
    QString group = head;
    if (group.endsWith(QLatin1Char('/')) && group.size() > 1) {
        group.chop(1);
    }
    if (group.isEmpty()) {
        group = QStringLiteral("/");
    }

    // Asked for rather than walked, whichever way it is missing: the tree is
    // lazy because a file can hold a million objects, and a completer that
    // listed its way down to answer a keystroke would spend exactly what that
    // laziness saves. The listing arrives a moment later and
    // `completionsChanged` is what tells the box to ask again.
    const QModelIndex at = treeModel_->indexForPath(group);
    if (!at.isValid() && group != QStringLiteral("/")) {
        // Not reached at all: a group several levels into a file nobody has
        // expanded. revealPath walks down one listing per round trip.
        treeModel_->revealPath(group);
        return {};
    }
    // Asking how many children it has is what asks for them. Population is
    // driven from rowCount() in this model and deliberately so -- see its
    // header -- so there is no other way to say "list this" for a group
    // already in hand.
    const int children = treeModel_->rowCount(at);
    if (!treeModel_->isPopulated(at)) {
        return {};
    }

    QStringList out;
    QStringList unknown;
    for (int row = 0; row < children; ++row) {
        const QModelIndex child = treeModel_->index(row, 0, at);
        const QString name = child.data(H5TreeModel::NameRole).toString();
        if (!name.startsWith(fragment)) {
            continue;
        }
        const QString path = head + name;
        if (!child.data(H5TreeModel::IsResolvedRole).toBool()) {
            // The name is in the link table; what it names comes out of the
            // object header, which is a second read. Asking for the role above
            // is what asks for it. Offered bare until it lands, because the
            // name is a true answer and "/group" turning into "/group/" a
            // moment later is a better list than no list.
            static_cast<void>(child.data(H5TreeModel::IsGroupRole));
            out.append(path);
            continue;
        }
        if (child.data(H5TreeModel::IsGroupRole).toBool()) {
            // A group is a step on the way rather than a destination, so the
            // separator comes with it: one Tab, then keep typing.
            out.append(path + QLatin1Char('/'));
            continue;
        }
        if (!child.data(H5TreeModel::IsDatasetRole).toBool()) {
            continue;
        }
        // ...and a dataset comes with the subscript that selects the whole of
        // it, which is the half of this a reader would otherwise have to count
        // dimensions for. The rank is the custom plots' own cache, which is
        // where every fact about a path this controller did not select lives;
        // a path it has never resolved is completed bare and asked about, and
        // the subscript appears on the next keystroke.
        const PathFacts* facts = customPlots_->lookup()->facts(path);
        if (facts == nullptr) {
            unknown.append(path);
            out.append(path);
            continue;
        }
        out.append(path + wholeSubscript(facts->shape.size()));
    }

    // Only ever the paths nothing knows anything about, for the reason spelled
    // out over the member branch below: a resolve with nothing to ask runs its
    // continuation there and then, and this one's continuation is what makes
    // every box ask again.
    if (!unknown.isEmpty()) {
        customPlots_->lookup()->resolve(unknown,
                                        [this] { emit completionsChanged(); });
    }
    return out;
}

QStringList AppController::completions(const QString& text)
{
    if (!fileOpen_) {
        return {};
    }
    const CompletionRequest request = completionRequest(text);
    switch (request.part) {
    case CompletionRequest::Part::Subscript:
        return {};
    case CompletionRequest::Part::Path:
        return pathCompletions(request.head, request.fragment);
    case CompletionRequest::Part::Member:
        break;
    }

    const h5core::TypeInfo* type = typeOf(request.path);
    if (type == nullptr) {
        // The path is not one this controller or the plots' cache knows. Ask,
        // and answer on the next keystroke rather than guessing at a datatype.
        //
        // Only when it is genuinely unknown. `resolve` runs its continuation
        // *synchronously* when there is nothing to ask -- callers use it as
        // "make sure, then go" -- and the continuation here is the signal that
        // makes every box re-ask, so calling it for a path already known and
        // still not a dataset (a group, a broken link) is an unbounded loop
        // with the window locked inside it. `knows` is the difference between
        // asking once and asking forever.
        if (!customPlots_->lookup()->knows(request.path)) {
            customPlots_->lookup()->resolve({request.path},
                                            [this] { emit completionsChanged(); });
        }
        return {};
    }

    QStringList out;
    for (const QString& chain : postproc::memberChains(*type)) {
        if (chain.startsWith(request.fragment)) {
            out.append(request.head + chain);
        }
    }
    return out;
}

QStringList AppController::memberCompletions(const QString& text) const
{
    if (!datasetTabVisible_ || !hasDataset_) {
        return {};
    }
    const QString fragment = text.trimmed();
    QStringList out;
    for (const QString& chain : postproc::memberChains(originInfo_.type)) {
        if (chain.startsWith(fragment)) {
            out.append(chain);
        }
    }
    return out;
}

QString AppController::commonCompletion(const QStringList& options) const
{
    return commonHead(options);
}

QString AppController::memberError(const QString& text) const
{
    if (!datasetTabVisible_ || !hasDataset_) {
        return tr("no dataset is selected");
    }
    const postproc::MemberChain chain =
        postproc::resolveMemberChain(text, originInfo_.type);
    return chain.error;
}

QString AppController::applyMember(const QString& text)
{
    if (!datasetTabVisible_ || !hasDataset_) {
        return tr("no dataset is selected");
    }
    const postproc::MemberChain chain =
        postproc::resolveMemberChain(text, originInfo_.type);
    if (!chain.valid()) {
        return chain.error;
    }

    // Everything below announces itself with selectionChanged, because that is
    // the signal the properties it moves are notified by -- and every
    // DatasetMemory in the UI reads that signal as "put back what is filed for
    // the dataset now current". Without the half of the pair that files it
    // first, naming a member reverted the plot's range, the image's colour axis
    // and the pipeline's own switch to whatever they were when the reader last
    // *left* this dataset. The dataset is not changing here, so filing and
    // restoring under the same name is the identity it should be.
    emit selectionAboutToChange();

    // What the leading dimensions -- the dataset's own -- are already showing.
    // The chain only ever changes the axes after them, so a reader who has set
    // up a slice and then picks a member keeps the slice they set up.
    const QStringList kept = tableSetupModel_->summaries();
    const auto originRank = static_cast<qsizetype>(originInfo_.shape.size());

    memberSelection_ = chain.selection;
    // The canonical chain, which the resolver already wrote: the members with
    // their subscripts taken off, because those are about to go on the slice.
    memberText_ = QString::fromStdString(memberSelection_.text);
    if (memberText_.isEmpty()) {
        members_.remove(currentPath_);
    } else {
        members_.insert(currentPath_, memberText_);
    }

    const h5core::DatasetInfo info = projectedInfo();
    datasetInfo_ = info;
    datasetRank_ = static_cast<int>(info.rank());
    datasetIsString_ = info.type.cls == h5core::TypeClass::String;
    datasetIsNumeric_ = info.isNumeric() && info.readable();
    datasetIsFloat_ = info.type.cls == h5core::TypeClass::Float && info.readable();
    datasetElementCount_ = static_cast<qint64>(info.elementCount());

    postprocessModel_->setDataset(pipelineSubject(info));
    tableSetupModel_->setShape(info.shape, info.image);

    // The subscripts the chain carried belong on the slice line: `.samples[2]`
    // and a `2` on the axis `.samples` appended are the same selection, and the
    // line is where every other subscript in this program lives. This is the
    // one place a box rewrites what was typed, and what it rewrites it into is
    // sitting next to it.
    QStringList line;
    for (qsizetype d = 0; d < originRank; ++d) {
        line.append(d < kept.size() ? kept[d] : QStringLiteral(":"));
    }
    for (const QString& folded : chain.folded) {
        line.append(folded.isEmpty() ? QStringLiteral(":") : folded);
    }
    if (!line.isEmpty()) {
        static_cast<void>(tableSetupModel_->applySlice(line.join(QStringLiteral(", "))));
    }

    emit selectionChanged();
    emit tableLayoutChanged();
    applyDataSource();
    return {};
}

QString AppController::sliceError(const QString& text) const
{
    if (!datasetTabVisible_) {
        return QStringLiteral("no dataset is selected");
    }
    return tableSetupModel_->sliceError(text);
}

QString AppController::selectionText() const
{
    if (!datasetTabVisible_) {
        return {};
    }
    const QString body = tableSetupModel_->sliceText();
    const QString brackets =
        body.isEmpty() ? QString() : QStringLiteral("[") + body + QStringLiteral("]");
    return brackets + memberText_;
}

QString AppController::readSelection(const QString& text, QString& chainText,
                                    postproc::MemberChain& chain, QString& line) const
{
    // Both halves, in the order they depend on one another: the chain decides
    // the shape, and the subscript is a statement about that shape. Whichever
    // of them is wrong is the answer, and the chain is asked first because a
    // chain that does not resolve is the reason the shape is not the one the
    // reader thinks it is.
    if (!datasetTabVisible_ || !hasDataset_) {
        return tr("no dataset is selected");
    }
    QString subscript;
    QString problem;
    if (!splitSelection(text, subscript, chainText, problem)) {
        return problem;
    }
    chain = postproc::resolveMemberChain(chainText, originInfo_.type);
    if (!chain.valid()) {
        return chain.error;
    }
    // The shape the chain produces, which is the dataset's own followed by the
    // axes the chain appends -- the same derivation projectedInfo() makes, done
    // here without moving the selection to make it.
    std::vector<hsize_t> shape = originInfo_.shape;
    shape.insert(shape.end(), chain.selection.dims.begin(), chain.selection.dims.end());

    if (subscript.isEmpty() && !shape.empty()) {
        // A subscript left off is the whole of the dataset -- `.energy` on its
        // own says nothing about which records and so means all of them. The
        // ellipsis is what the grammar already spells that with, so this is
        // writing down what was left off rather than inventing a reading of it.
        subscript = QStringLiteral("...");
    }
    line = postproc::sliceLineFor(subscript, chain.folded, originInfo_.shape.size());
    if (shape.empty()) {
        // A scalar through a scalar member: one cell, and nothing to subscript.
        return line.trimmed().isEmpty()
                   ? QString()
                   : tr("%1 has no dimensions to subscript").arg(currentPath_);
    }

    std::vector<postproc::IndexExpression> chosen;
    QStringList written;
    QString bad;
    if (!postproc::readSubscripts(line, shape, chosen, written, bad)) {
        return bad;
    }
    return {};
}

QString AppController::selectionError(const QString& text) const
{
    QString chainText;
    postproc::MemberChain chain;
    QString line;
    return readSelection(text, chainText, chain, line);
}

QString AppController::applySelection(const QString& text)
{
    // Read whole before anything moves. A selection is one statement about the
    // dataset, so a line the second half of which will not do must leave the
    // views showing what they were showing -- not the member applied and the
    // subscript refused, which is a selection nobody asked for.
    QString chainText;
    postproc::MemberChain chain;
    QString line;
    if (const QString problem = readSelection(text, chainText, chain, line);
        !problem.isEmpty()) {
        return problem;
    }

    // The chain first: it is what decides the shape, and applyMember() rewrites
    // the slice line against it. The subscript the reader wrote then goes on
    // over the top of that, which is the one thing applyMember() cannot know.
    const auto canonical = QString::fromStdString(chain.selection.text);
    if (canonical != memberText_) {
        if (const QString problem = applyMember(chainText); !problem.isEmpty()) {
            return problem;
        }
    }
    if (line.trimmed().isEmpty()) {
        return {}; // a scalar: there was never anything to subscript
    }
    return applySlice(line);
}

QStringList AppController::selectionCompletions(const QString& text) const
{
    // The chains, each written back onto whatever subscript is already in front
    // of it -- whole lines, as every completer here answers in, because what
    // the box would hold is the only thing a box can be handed.
    QString subscript;
    QString chain;
    QString problem;
    if (!splitSelection(text, subscript, chain, problem)) {
        return {};
    }
    const QString head = text.trimmed().startsWith(QLatin1Char('['))
                             ? QStringLiteral("[") + subscript + QStringLiteral("]")
                             : QString();
    QStringList out;
    for (const QString& offered : memberCompletions(chain)) {
        out.append(head + offered);
    }
    return out;
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

void AppController::setRamBudget(RamBudget budget)
{
    if (ramBudget_ == budget) {
        return;
    }
    ramBudget_ = budget;
    // The plots hear it through PlotBudget rather than from here: there is one
    // of this object and any number of them, and a tab made after this was set
    // must get the same answer as one made before it.
    PlotBudget::instance().setAppetite(appetiteOf(ramBudget_));
    if (!QCoreApplication::organizationName().isEmpty()) {
        QSettings settings;
        settings.setValue(QStringLiteral("ramBudget"), static_cast<int>(ramBudget_));
    }
    emit ramBudgetChanged();
}

namespace {

/// Write one setting, or write nowhere at all.
///
/// The guard is the same one the recent-files list and the budget carry and is
/// there for the same reason -- the tests construct controllers freely and must
/// not leave anything on disk -- and by the fifth setting it was worth having
/// once rather than five times.
void store(const QString& key, const QVariant& value)
{
    if (QCoreApplication::organizationName().isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue(key, value);
}

} // namespace

void AppController::setPlotExportPublication(bool on)
{
    if (plotExportPublication_ == on) {
        return;
    }
    plotExportPublication_ = on;
    store(QStringLiteral("plotExportPublication"), on);
    emit plotExportPublicationChanged();
}

void AppController::setPlotExportCursor(bool on)
{
    if (plotExportCursor_ == on) {
        return;
    }
    plotExportCursor_ = on;
    store(QStringLiteral("plotExportCursor"), on);
    emit plotExportCursorChanged();
}

void AppController::setPlotExportCustomSize(bool on)
{
    if (plotExportCustomSize_ == on) {
        return;
    }
    plotExportCustomSize_ = on;
    store(QStringLiteral("plotExportCustomSize"), on);
    emit plotExportCustomSizeChanged();
}

void AppController::setPlotExportWidth(int pixels)
{
    const int wanted = std::clamp(pixels, kMinExportPixels, kMaxExportPixels);
    if (plotExportWidth_ == wanted) {
        return;
    }
    plotExportWidth_ = wanted;
    store(QStringLiteral("plotExportWidth"), wanted);
    emit plotExportSizeChanged();
}

void AppController::setPlotExportHeight(int pixels)
{
    const int wanted = std::clamp(pixels, kMinExportPixels, kMaxExportPixels);
    if (plotExportHeight_ == wanted) {
        return;
    }
    plotExportHeight_ = wanted;
    store(QStringLiteral("plotExportHeight"), wanted);
    emit plotExportSizeChanged();
}

void AppController::setPlotExportCustomDpi(bool on)
{
    if (plotExportCustomDpi_ == on) {
        return;
    }
    plotExportCustomDpi_ = on;
    store(QStringLiteral("plotExportCustomDpi"), on);
    emit plotExportCustomDpiChanged();
}

void AppController::setPlotExportDpi(int dpi)
{
    const int wanted = std::clamp(dpi, kMinExportDpi, kMaxExportDpi);
    if (plotExportDpi_ == wanted) {
        return;
    }
    plotExportDpi_ = wanted;
    store(QStringLiteral("plotExportDpi"), wanted);
    emit plotExportDpiChanged();
}

double AppController::plotExportScale(double displayRatio) const
{
    if (plotExportCustomDpi_) {
        return plotExportDpi_ / kExportBaseDpi;
    }
    if (plotExportCustomSize_) {
        // A size stated in pixels is a composition as well, because nothing
        // has said otherwise: one point, one pixel.
        return 1.0;
    }
    // Never below one. A display reporting a fractional ratio is reporting
    // how it lays out text, not how few pixels it has, and a picture smaller
    // than the pane it was taken of is nobody's copy.
    return std::max(1.0, displayRatio);
}

QSize AppController::plotExportPixels(double paneWidth, double paneHeight,
                                      double displayRatio) const
{
    if (plotExportCustomSize_) {
        return {plotExportWidth_, plotExportHeight_};
    }
    if (paneWidth <= 0.0 || paneHeight <= 0.0) {
        return {};
    }
    // The pane as it stands, at this display's own resolution -- which is the
    // picture this application has always copied, down to the pixel.
    const double ratio = std::max(1.0, displayRatio);
    const double longest = std::max(paneWidth, paneHeight) * ratio;
    const double held = longest > kMaxExportPixels ? kMaxExportPixels / longest : 1.0;
    return {static_cast<int>(std::lround(paneWidth * ratio * held)),
            static_cast<int>(std::lround(paneHeight * ratio * held))};
}

QSize AppController::plotExportLayout(double paneWidth, double paneHeight,
                                      double displayRatio) const
{
    const QSize out = plotExportPixels(paneWidth, paneHeight, displayRatio);
    if (out.isEmpty()) {
        return {};
    }
    double scale = plotExportScale(displayRatio);

    // A composition too small to draw is not a composition. PlotFrame spends
    // a fixed number of points on its gutters before it has drawn anything,
    // so past some point the picture is all margin and no plot. The floor is
    // the same one a chosen size is held to, and it is worth knowing how far
    // out of the way it is: at 300 dpi it starts to bite below a 200-pixel
    // side, which is a figure two thirds of an inch across.
    const double shortest = std::min(out.width(), out.height());
    if (scale > 0.0 && shortest / scale < kMinExportPixels) {
        scale = shortest / kMinExportPixels;
    }
    if (scale <= 0.0) {
        return out;
    }
    return {static_cast<int>(std::lround(out.width() / scale)),
            static_cast<int>(std::lround(out.height() / scale))};
}

double AppController::plotExportTaggedDpi() const
{
    return plotExportCustomDpi_ ? static_cast<double>(plotExportDpi_) : 0.0;
}

double AppController::plotExportCentimetres(int pixels, int dpi) const
{
    if (pixels <= 0 || dpi <= 0) {
        return 0.0;
    }
    return pixels * kCentimetresPerInch / dpi;
}

int AppController::plotExportDpiFor(int pixels, double centimetres) const
{
    // A side of nothing is not a size, and it is what a half-typed box holds
    // for a keystroke or two. The density stands until there is a number.
    if (pixels <= 0 || !(centimetres > 0.0)) {
        return plotExportDpi_;
    }
    const double wanted = pixels * kCentimetresPerInch / centimetres;
    return std::clamp(static_cast<int>(std::lround(wanted)), kMinExportDpi, kMaxExportDpi);
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
    // With the slices, and for their reason: two files can hold a `/data` that
    // have nothing to do with each other, and a member chain says even more
    // about which one than a slice does -- it names a datatype.
    members_.clear();
    customPlots_->clear();
    postprocessModel_->reset();
    hasDataset_ = false;
    datasetInfo_ = {};
    fileOpen_ = false;
    filePath_.clear();
    currentPath_.clear();
    treeModel_->close();
    nameIndex_->close();
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
            // Behind the tree rather than in front of it. The queue is ordered
            // and there is one HDF5 thread, so the index walks in bounded
            // passes that re-arm at the back of it: every listing the reader
            // asks for is served before the next of them.
            nameIndex_->open();
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
    // With the slices, and for their reason: two files can hold a `/data` that
    // have nothing to do with each other, and a member chain says even more
    // about which one than a slice does -- it names a datatype.
    members_.clear();
    customPlots_->clear();
    postprocessModel_->reset();
    hasDataset_ = false;
    datasetInfo_ = {};
    fileOpen_ = false;
    filePath_.clear();
    currentPath_.clear();
    setErrorText(QString{});
    treeModel_->close();
    nameIndex_->close();
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
    originInfo_ = {};
    memberText_.clear();
    memberSelection_ = {};

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
        originInfo_ = facts.info;
        // Whatever member this dataset was last read through, before anything
        // is described: every fact below is a fact about what is being drawn,
        // and with a chain set that is the member and not the struct.
        memberText_.clear();
        memberSelection_ = {};
        if (const auto held = members_.constFind(currentPath_);
            held != members_.constEnd()) {
            const postproc::MemberChain chain =
                postproc::resolveMemberChain(*held, originInfo_.type);
            if (chain.valid() && !chain.empty()) {
                memberText_ = *held;
                memberSelection_ = chain.selection;
            }
        }
        const h5core::DatasetInfo info = projectedInfo();

        datasetRank_ = static_cast<int>(info.rank());
        datasetIsString_ = info.type.cls == h5core::TypeClass::String;
        datasetIsNumeric_ = info.isNumeric() && info.readable();
        // The *dataset's* class, not the projection's: this is what puts the
        // member box on screen, and a reader who has chained down to a float
        // still needs the box they typed it into.
        datasetIsCompound_ =
            originInfo_.type.cls == h5core::TypeClass::Compound && info.readable();
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
        postprocessModel_->setDataset(pipelineSubject(info));
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
        postprocessModel_->setDataset({});
        tableSetupModel_->setShape({});
        datasetMessage_ = facts.datasetMessage;
    } else {
        datasetModel_->setSource(false, {}, {});
        postprocessModel_->setDataset({});
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

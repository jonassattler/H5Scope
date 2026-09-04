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

bool AppController::postprocessActive() const
{
    return postprocessModel_->active();
}

void AppController::applyDataSource()
{
    // Mid-selection the four things this reads from do not yet agree with each
    // other; see `selecting_`. The call at the end of selectPath() is the one
    // that counts.
    if (selecting_) {
        return;
    }
    if (!dataset_) {
        datasetModel_->setDataset(nullptr);
        return;
    }

    if (!postprocessModel_->active()) {
        datasetModel_->setDataset(dataset_);
        datasetModel_->setLayout(tableSetupModel_->layout());
        return;
    }

    // The pipeline reads the slice out of the file itself, so the layout the
    // panel resolved is not applied on top of it -- it has already happened,
    // and applying it twice would slice the slice.
    const postproc::RunResult result =
        postproc::run(*dataset_, postprocessModel_->pipeline(),
                      postprocessModel_->upTo());
    if (!result.usable()) {
        // Not even the read worked. The views fall back to the file, which is
        // the last thing that was known to be drawable, and the reason is on
        // the row that gave it.
        datasetModel_->setDataset(dataset_);
        datasetModel_->setLayout(tableSetupModel_->layout());
        return;
    }

    auto computed = std::make_shared<postproc::ComputedDataset>(
        result.array, dataset_->info(), dataset_->path(),
        tr("(postprocessed)").toStdString());
    const std::vector<hsize_t> shape = computed->info().shape;
    datasetModel_->setDataset(std::move(computed));
    // The output array is a new array with its own rank, so the axis
    // assignment the panel made about the *dataset* cannot be carried over to
    // it. It gets the ordinary default instead: the last dimension along the
    // columns and the rest down the rows, and no image arrangement, because a
    // pipeline has just made whatever the Image spec said about these
    // dimensions untrue.
    datasetModel_->setLayout(defaultLayout(shape));
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
    unsigned major = 0;
    unsigned minor = 0;
    unsigned release = 0;
    H5get_libversion(&major, &minor, &release);
    return QStringLiteral("hdf5 %1.%2.%3").arg(major).arg(minor).arg(release);
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
    std::shared_ptr<h5core::File> file;
    try {
        file = std::make_shared<h5core::File>(path.toStdString());
    } catch (const h5core::H5Error& error) {
        // Recorded, not thrown at the user through a modal dialog: a blocking
        // dialog here would hang any non-interactive caller, tests included.
        setErrorText(QString::fromStdString(error.summary()));
        return false;
    }

    setErrorText(QString{});
    // Two files can hold a "/data" that have nothing to do with each other, so
    // what was remembered about the last one says nothing about this one.
    leaveSelection();
    settings_.clear();
    slices_.clear();
    postprocessModel_->reset();
    file_ = std::move(file);
    filePath_ = path;
    // Only a file that actually opened. A path that failed is not something to
    // offer the reader again from a menu.
    remember(path);
    currentPath_.clear();
    treeModel_->setFile(file_);
    emit fileChanged();

    // Select the first top-level object so the tabs are never blank on open.
    if (treeModel_->rowCount({}) > 0) {
        const QModelIndex first = treeModel_->index(0, 0, {});
        currentPath_ = treeModel_->pathAt(first);
    } else {
        currentPath_ = QStringLiteral("/");
    }
    refreshSelection();
    return true;
}

bool AppController::openUrl(const QUrl& url)
{
    return openFile(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void AppController::closeFile()
{
    leaveSelection();
    settings_.clear();
    slices_.clear();
    postprocessModel_->reset();
    file_.reset();
    filePath_.clear();
    currentPath_.clear();
    setErrorText(QString{});
    treeModel_->setFile(nullptr);
    emit fileChanged();
    refreshSelection();
}

bool AppController::selectPath(const QString& path)
{
    if (!file_ || path.isEmpty()) {
        return false;
    }
    // A link is selectable whether or not it resolves. A dangling soft link and
    // an external link into a missing file are both things the tree shows, and
    // clicking one has to say what it is rather than do nothing at all.
    if (!file_->hasLink(path.toStdString())) {
        return false;
    }
    leaveSelection();
    currentPath_ = path;
    refreshSelection();
    return true;
}

void AppController::refreshSelection()
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

    if (!file_ || currentPath_.isEmpty()) {
        infoModel_->clear();
        attributeModel_->clear();
        datasetModel_->setDataset(nullptr);
        tableSetupModel_->setShape({});
        emit selectionChanged();
        return;
    }

    infoModel_->showObject(file_, currentPath_);

    try {
        const auto node = file_->nodeInfo(currentPath_.toStdString());
        datasetTabVisible_ = node.kind == h5core::NodeKind::Dataset;
        // An unresolved link has no object behind it, so it has no attributes
        // either; asking would fail rather than answer nothing.
        metadataTabVisible_ =
            node.resolves()
            && file_->attributeCount(currentPath_.toStdString()) > 0;
    } catch (const h5core::H5Error& error) {
        emit statusMessage(QString::fromStdString(error.summary()));
        emit selectionChanged();
        return;
    }

    if (datasetTabVisible_) {
        try {
            auto dataset =
                std::make_shared<h5core::Dataset>(*file_, currentPath_.toStdString());
            datasetRank_ = static_cast<int>(dataset->info().rank());
            datasetIsString_ =
                dataset->info().type.cls == h5core::TypeClass::String;
            datasetIsNumeric_ =
                dataset->info().isNumeric() && dataset->info().readable();
            datasetIsCompound_ =
                dataset->info().type.cls == h5core::TypeClass::Compound
                && dataset->info().readable();
            datasetIsFloat_ =
                dataset->info().type.cls == h5core::TypeClass::Float
                && dataset->info().readable();
            datasetElementCount_ =
                static_cast<qint64>(dataset->info().elementCount());
            const std::vector<hsize_t> shape = dataset->info().shape;
            // A dataset that says it is a picture stops being one while a
            // pipeline is running on it: the Image spec fixes which dimension
            // is height and which is colour, and a transpose or a reduction is
            // about to make that statement untrue. Suppressed here, at the one
            // place the arrangement is decided, rather than at each of the
            // places that would otherwise have to know.
            const auto image = postprocessModel_->active()
                                   ? std::optional<h5core::ImageInfo>{}
                                   : dataset->info().image;
            dataset_ = std::move(dataset);
            // Before the layout, so the panel's reset lands on a pipeline that
            // already knows the shape it is starting from.
            postprocessModel_->setDataset(currentPath_, shape,
                                          dataset_->info().isNumeric()
                                              && dataset_->info().readable());
            tableSetupModel_->setShape(shape, image);
            // ...and then whatever slice was last written for this dataset. A
            // line that no longer reads -- which nothing in one session should
            // produce, the shape being the same -- leaves the defaults alone.
            if (const auto slice = slices_.constFind(currentPath_);
                slice != slices_.constEnd()) {
                static_cast<void>(tableSetupModel_->applySlice(*slice));
            }
            // The one place the table is handed its source, once the dataset,
            // the pipeline, the panel's shape and the remembered slice all
            // agree about which object this is.
            // The one place the table is handed its source, once the dataset,
            // the pipeline, the panel's shape and the remembered slice all
            // agree about which object this is.
            selecting_ = false;
            applyDataSource();
            selecting_ = true;
            datasetMessage_ = datasetModel_->errorText();
        } catch (const h5core::H5Error& error) {
            dataset_.reset();
            datasetModel_->setDataset(nullptr);
            postprocessModel_->setDataset({}, {}, false);
            tableSetupModel_->setShape({});
            datasetMessage_ = QString::fromStdString(error.summary());
        }
    } else {
        dataset_.reset();
        datasetModel_->setDataset(nullptr);
        postprocessModel_->setDataset({}, {}, false);
        tableSetupModel_->setShape({});
    }

    if (metadataTabVisible_) {
        attributeModel_->showObject(file_, currentPath_);
    } else {
        attributeModel_->clear();
    }

    emit selectionChanged();
}

} // namespace gui

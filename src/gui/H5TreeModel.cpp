// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "H5TreeModel.hpp"

#include "h5core/Error.hpp"

#include <QLocale>
#include <QStringList>
#include <QVariantList>

namespace gui {
namespace {

/// What a link tag says when the pointer rests on it.
///
/// A sentence rather than the readout's arrow-and-path, because a tag is a
/// mark whose whole meaning is the word it stands for: `L` on a row is worth
/// nothing to a reader who is not told that it means the name here is a
/// pointer and this is what it points at. Broken links say so and say which
/// half is missing -- an external link has two things that can be absent and
/// they are not the same problem.
///
/// Takes no read: everything here came out of the link traversal that listed
/// the row in the first place.
QString linkDescription(const h5core::NodeInfo& info)
{
    if (info.link == h5core::LinkType::Hard) {
        return {};
    }

    const QString target = QString::fromStdString(info.linkTarget);
    if (info.link == h5core::LinkType::External) {
        const QString file = QString::fromStdString(info.linkFile);
        return info.resolves()
                   ? QObject::tr("External link to %1 in %2.").arg(target, file)
                   : QObject::tr("External link to %1 in %2, which leads "
                                 "nowhere: the file or the object is missing.")
                         .arg(target, file);
    }
    return info.resolves()
               ? QObject::tr("Soft link to %1.").arg(target)
               : QObject::tr("Soft link to %1, which leads nowhere: there is "
                             "no object at that path.")
                     .arg(target);
}

} // namespace

H5TreeModel::H5TreeModel(QObject* parent) : QAbstractItemModel(parent) {}

H5TreeModel::~H5TreeModel() = default;

void H5TreeModel::setFile(std::shared_ptr<h5core::File> file)
{
    beginResetModel();
    file_ = std::move(file);
    root_.reset();

    if (file_) {
        root_ = std::make_unique<Node>();
        try {
            root_->info = file_->nodeInfo("/");
        } catch (const h5core::H5Error&) {
            root_->info.path = "/";
            root_->info.kind = h5core::NodeKind::Group;
        }
        root_->info.name = "/";
        root_->identified = true;
    }
    endResetModel();
}

H5TreeModel::Node* H5TreeModel::nodeFor(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return root_.get();
    }
    return static_cast<Node*>(index.internalPointer());
}

bool H5TreeModel::isExpandable(const Node* node) const
{
    if (node == nullptr) {
        return false;
    }
    ensureIdentity(node);
    return !node->cyclic && node->info.kind == h5core::NodeKind::Group;
}

void H5TreeModel::ensureIdentity(const Node* node) const
{
    if (node->identified) {
        return;
    }
    node->identified = true;
    if (!file_) {
        return;
    }
    try {
        file_->resolve(node->info);
    } catch (const h5core::H5Error&) {
        // A name that will not resolve is a state of the file, and the row
        // says so rather than disappearing. Nothing here can throw out to the
        // view: data() is called from the render loop.
        node->info.kind = h5core::NodeKind::Unresolved;
    }

    // A name repeating an ancestor's object identity closes a loop; show it,
    // but never descend into it, or a hard-linked cycle recurses forever.
    //
    // Here rather than in populate(), which is where it used to be, because
    // populate() no longer opens anything: a *soft* link back to an ancestor
    // has no identity until it is followed, and following it is what this
    // function does. The test needs no kind of its own -- every ancestor is a
    // group, and only a group can carry a group's identity.
    if (!node->info.address.has_value()) {
        return;
    }
    for (const Node* ancestor = node->parent; ancestor != nullptr;
         ancestor = ancestor->parent) {
        if (ancestor->info.address == node->info.address
            && ancestor->info.fileNumber == node->info.fileNumber) {
            node->cyclic = true;
            return;
        }
    }
}

void H5TreeModel::populate(Node* node) const
{
    if (node == nullptr || node->populated) {
        return;
    }
    node->populated = true;

    if (!file_ || !isExpandable(node)) {
        return;
    }

    std::vector<h5core::NodeInfo> children;
    try {
        // The link table and nothing else. What each name points at is settled
        // per row, by ensureIdentity(), because the view will ask about forty
        // of these and expanding must not wait on all of them.
        children = file_->children(node->info.path, h5core::File::Resolve::Links);
    } catch (const h5core::H5Error& error) {
        emit const_cast<H5TreeModel*>(this)->loadFailed(
            QStringLiteral("Could not list '%1': %2")
                .arg(QString::fromStdString(node->info.path),
                     QString::fromUtf8(error.what())));
        return;
    }

    node->children.reserve(children.size());
    int row = 0;
    for (auto& info : children) {
        auto child = std::make_unique<Node>();
        child->parent = node;
        child->rowInParent = row++;
        child->info = std::move(info);
        node->children.push_back(std::move(child));
    }
}

QModelIndex H5TreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return {};
    }

    Node* parentNode = nodeFor(parent);
    if (parentNode == nullptr) {
        return {};
    }
    populate(parentNode);

    if (row < 0 || static_cast<std::size_t>(row) >= parentNode->children.size()) {
        return {};
    }
    return createIndex(row, column,
                       parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex H5TreeModel::parent(const QModelIndex& child) const
{
    Node* node = nodeFor(child);
    if (node == nullptr || node == root_.get() || node->parent == nullptr) {
        return {};
    }
    Node* parentNode = node->parent;
    if (parentNode == root_.get()) {
        return {};
    }
    return createIndex(parentNode->rowInParent, 0, parentNode);
}

int H5TreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0) {
        return 0;
    }
    Node* node = nodeFor(parent);
    if (node == nullptr) {
        return 0;
    }
    populate(node);
    return static_cast<int>(node->children.size());
}

int H5TreeModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 1;
}

bool H5TreeModel::hasChildren(const QModelIndex& parent) const
{
    Node* node = nodeFor(parent);
    if (node == nullptr) {
        return false;
    }
    // Claim children for any unread group so the expander appears without us
    // having to read the group first -- that read is the whole point of being
    // lazy.
    if (!node->populated) {
        return isExpandable(node);
    }
    return !node->children.empty();
}

QHash<int, QByteArray> H5TreeModel::roleNames() const
{
    return {
        {NameRole, "name"},         {PathRole, "path"},
        {KindRole, "kind"},         {KindTextRole, "kindText"},
        {IsGroupRole, "isGroup"},   {IsDatasetRole, "isDataset"},
        {IsCyclicRole, "isCyclic"}, {IsImageRole, "isImage"},
        {IsLinkRole, "isLink"},
        {LinkResolvesRole, "linkResolves"},
        {HasAttributesRole, "hasAttributes"},
        {AttributeCountRole, "attributeCount"},
        {ImageSubclassRole, "imageSubclass"},
        {LinkDescriptionRole, "linkDescription"},
        {MetaRole, "meta"},
        {IsLastChildRole, "isLastChild"},
        {AncestorLinesRole, "ancestorLines"},
        {Qt::DisplayRole, "display"},
    };
}

QVariant H5TreeModel::data(const QModelIndex& index, int role) const
{
    Node* node = nodeFor(index);
    if (node == nullptr || node == root_.get()) {
        return {};
    }

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(node->info.name);
    case PathRole:
        return QString::fromStdString(node->info.path);
    case KindRole:
        ensureIdentity(node);
        return static_cast<int>(node->info.kind);
    case KindTextRole:
        ensureIdentity(node);
        return (node->info.link == h5core::LinkType::Hard)
                   ? QString::fromStdString(h5core::toString(node->info.kind))
                   : QStringLiteral("%1 \u2192 %2")
                         .arg(QString::fromStdString(
                                  h5core::toString(node->info.link)),
                              QString::fromStdString(h5core::toString(node->info.kind)));
    case IsGroupRole:
        ensureIdentity(node);
        return node->info.kind == h5core::NodeKind::Group;
    case IsDatasetRole:
        ensureIdentity(node);
        return node->info.kind == h5core::NodeKind::Dataset;
    case IsCyclicRole:
        ensureIdentity(node);
        return node->cyclic;
    case IsLinkRole:
        // Out of the link table, so no object need be opened to answer it.
        return node->info.link != h5core::LinkType::Hard;
    case LinkResolvesRole:
        // A hard link always resolves and the link table says it is one, so
        // only the other two are worth an object-header read.
        if (node->info.link != h5core::LinkType::Hard) {
            ensureIdentity(node);
        }
        return node->info.resolves();
    case HasAttributesRole:
        ensureIdentity(node);
        return node->info.attributeCount > 0;
    case AttributeCountRole:
        ensureIdentity(node);
        return static_cast<int>(node->info.attributeCount);
    case IsImageRole:
        ensureReadout(node);
        return node->image;
    case ImageSubclassRole:
        ensureReadout(node);
        return node->imageSubclass;
    case LinkDescriptionRole:
        // Only a link has one, and whether it *leads* anywhere is part of the
        // sentence -- so this is the one text role that has to resolve.
        if (node->info.link == h5core::LinkType::Hard) {
            return QString{};
        }
        ensureIdentity(node);
        return linkDescription(node->info);
    case MetaRole:
        return metaFor(node);
    case IsLastChildRole: {
        const Node* parent = node->parent;
        return parent == nullptr
               || node->rowInParent
                      == static_cast<int>(parent->children.size()) - 1;
    }
    case AncestorLinesRole: {
        // Walk to the root collecting each ancestor's "has a later sibling",
        // then reverse so the list reads outermost level first.
        QVariantList lines;
        for (const Node* a = node->parent; a != nullptr && a != root_.get();
             a = a->parent) {
            const Node* grandparent = a->parent;
            const bool continues =
                grandparent != nullptr
                && a->rowInParent
                       < static_cast<int>(grandparent->children.size()) - 1;
            lines.prepend(continues);
        }
        return lines;
    }
    default:
        return {};
    }
}

QString H5TreeModel::metaFor(const Node* node) const
{
    ensureReadout(node);
    return *node->meta;
}

void H5TreeModel::ensureReadout(const Node* node) const
{
    if (node->meta.has_value()) {
        return;
    }
    ensureIdentity(node);

    QString meta;
    if (file_ != nullptr) {
        try {
            // Where a link points is what distinguishes it from the object it
            // names, so that is what the readout says -- for a broken one it
            // is the only thing there is to say. Costs no read at all: the
            // target came out of the link table with the name.
            if (node->info.link != h5core::LinkType::Hard) {
                meta = QStringLiteral("\u2192 ");
                if (!node->info.linkFile.empty()) {
                    meta += QStringLiteral("%1:")
                                .arg(QString::fromStdString(node->info.linkFile));
                }
                meta += QString::fromStdString(node->info.linkTarget);
                if (!node->info.resolves()) {
                    meta += QStringLiteral("  (missing)");
                }
            } else if (node->info.kind == h5core::NodeKind::Dataset) {
                // The dataspace and the image tag, and nothing else. The full
                // DatasetInfo -- datatype, layout, filter pipeline, external
                // sources -- is four more reads per row and the tree shows
                // none of it; the Info panel reads it for the one object that
                // is selected. And the image probe is skipped outright unless
                // the object header said there are attributes to probe.
                const auto outline = file_->datasetOutline(
                    node->info.path, node->info.attributeCount > 0);
                node->image = outline.image;
                if (node->image) {
                    node->imageSubclass =
                        QString::fromStdString(h5core::toString(outline.subclass));
                }
                if (outline.space == h5core::Dataspace::Null) {
                    meta = QStringLiteral("null");
                } else if (outline.shape.empty()) {
                    meta = QStringLiteral("scalar");
                } else {
                    QStringList parts;
                    parts.reserve(static_cast<int>(outline.shape.size()));
                    for (const hsize_t dim : outline.shape) {
                        parts << QLocale::system().toString(
                            static_cast<qulonglong>(dim));
                    }
                    meta = QStringLiteral("(%1)")
                               .arg(parts.join(QStringLiteral(" \u00d7 ")));
                }
            } else if (node->cyclic) {
                // A hard link back to an ancestor. It is the same object under
                // a second name, so there is no target path to print and no
                // count to take -- what the row has to say is that it closes a
                // loop.
                meta = QStringLiteral("cycle");
            } else if (node->info.kind == h5core::NodeKind::Group) {
                // H5Gget_info, which reads the count out of the group's own
                // header. Listing the group and taking the size of the list --
                // which is what this used to do -- resolves every link in it,
                // so drawing a member count beside 256 group rows cost a walk
                // of the whole level below them. That is quadratic in the
                // shape real acquisition files have, and it is the single
                // reason this pane was slow.
                //
                // Singular when there is one of them: the readout is a phrase
                // the reader reads, not a count with a fixed noun stapled to
                // it, and "1 items" is not a phrase.
                const auto members = file_->memberCount(node->info.path);
                meta = members == 1
                           ? QStringLiteral("1 item")
                           : QStringLiteral("%1 items").arg(members);
            } else if (node->info.kind == h5core::NodeKind::NamedDataType) {
                // A committed type has no shape and no children, so what it is
                // *of* is the whole of what the row has to say about it.
                // Opening it reads the datatype and nothing else.
                meta = QString::fromStdString(
                    file_->namedType(node->info.path).description);
            }
        } catch (const h5core::H5Error&) {
            // A node whose metadata will not read still lists, and says so:
            // blank would read as "nothing to report" about an object that has
            // something to report and could not be asked.
            meta = tr("unreadable");
        }
    }

    // The readout column carries a fact about every row or it is not a column.
    // Everything above states the most specific thing there is to say; this is
    // the floor under it, for the kinds that have nothing more -- an
    // unresolved link, or an object of a kind this build does not know.
    if (meta.isEmpty()) {
        meta = QString::fromStdString(h5core::toString(node->info.kind)).toLower();
    }

    node->meta = meta;
}

bool H5TreeModel::isPopulated(const QModelIndex& index) const
{
    const Node* node = nodeFor(index);
    return node != nullptr && node->populated;
}

QString H5TreeModel::pathAt(const QModelIndex& index) const
{
    Node* node = nodeFor(index);
    return (node == nullptr) ? QString{} : QString::fromStdString(node->info.path);
}

h5core::NodeKind H5TreeModel::kindAt(const QModelIndex& index) const
{
    Node* node = nodeFor(index);
    if (node == nullptr) {
        return h5core::NodeKind::Unknown;
    }
    ensureIdentity(node);
    return node->info.kind;
}

QModelIndex H5TreeModel::indexForPath(const QString& path)
{
    if (!file_ || path.isEmpty()) {
        return {};
    }

    QModelIndex current;
    for (const QString& part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        bool found = false;
        const int rows = rowCount(current); // populates as it descends
        const std::string name = part.toStdString();
        for (int row = 0; row < rows; ++row) {
            const QModelIndex child = index(row, 0, current);
            // Against the node's own name rather than through data(), which
            // would build a QString and a QVariant for every member of every
            // group on the way down. A path into a file with flat groups of
            // several thousand walks all of them.
            if (nodeFor(child)->info.name == name) {
                current = child;
                found = true;
                break;
            }
        }
        if (!found) {
            return {};
        }
    }
    return current;
}

} // namespace gui

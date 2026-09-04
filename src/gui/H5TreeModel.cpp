// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "H5TreeModel.hpp"

#include "h5core/Error.hpp"
#include "h5core/File.hpp"

#include <QLocale>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <algorithm>

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

H5TreeModel::RowFacts H5TreeModel::readFacts(h5core::File& file,
                                             const std::string& path,
                                             h5core::LinkType link, bool readout)
{
    H5TreeModel::RowFacts facts;
    facts.path = path;

    h5core::NodeInfo node;
    node.path = path;
    node.link = link;
    file.resolve(node);

    facts.read = true;
    facts.kind = node.kind;
    facts.fileNumber = node.fileNumber;
    facts.address = node.address;
    facts.attributeCount = node.attributeCount;
    facts.readout = readout;

    if (!readout) {
        // The kind and the attribute count, out of one object header, and
        // nothing further. This is what expanding a level costs when the level
        // is not on screen.
        return facts;
    }

    if (link != h5core::LinkType::Hard || !node.resolves()) {
        // A link's readout is where it points, which the link table already
        // gave us, and a name that resolves to nothing has no object to ask.
        return facts;
    }

    try {
        switch (node.kind) {
        case h5core::NodeKind::Group:
            facts.members = file.memberCount(path);
            break;
        case h5core::NodeKind::Dataset: {
            const auto outline = file.datasetOutline(path, node.attributeCount > 0);
            facts.space = outline.space;
            facts.shape = outline.shape;
            facts.image = outline.image;
            facts.subclass = outline.subclass;
            break;
        }
        case h5core::NodeKind::NamedDataType:
            facts.typeDescription = file.namedType(path).description;
            break;
        default:
            break;
        }
    } catch (const h5core::H5Error&) {
        // The kind is known and the rest is not. `read` stays true -- the row
        // is describable, just not fully -- and readoutFor() says so.
        facts.read = false;
    }
    return facts;
}

H5TreeModel::H5TreeModel(QObject* parent) : QAbstractItemModel(parent) {}

H5TreeModel::~H5TreeModel() = default;

void H5TreeModel::open()
{
    beginResetModel();
    requests_.reset(); // disown anything still coming for the last file
    pending_.clear();
    inFlight_ = 0;
    root_ = std::make_unique<Node>();
    root_->info.path = "/";
    root_->info.name = "/";
    root_->info.kind = h5core::NodeKind::Group;
    root_->known = true;
    endResetModel();

    // The root's own record -- its attribute count, mostly -- and then its
    // children, which is what the pane shows the moment a file is opened.
    requestListing(root_.get());
    emit loadingChanged();
}

void H5TreeModel::close()
{
    beginResetModel();
    requests_.reset();
    pending_.clear();
    inFlight_ = 0;
    root_.reset();
    endResetModel();
    emit loadingChanged();
}

bool H5TreeModel::loading() const
{
    return inFlight_ > 0 || !pending_.empty();
}

void H5TreeModel::noteActivity()
{
    emit loadingChanged();
}

H5TreeModel::Node* H5TreeModel::nodeFor(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return root_.get();
    }
    return static_cast<Node*>(index.internalPointer());
}

QModelIndex H5TreeModel::indexFor(Node* node) const
{
    if (node == nullptr || node == root_.get() || node->parent == nullptr) {
        return {};
    }
    return createIndex(node->rowInParent, 0, node);
}

// --- listing ---------------------------------------------------------------

void H5TreeModel::runWaiters(Node* node) const
{
    std::vector<std::function<void()>> waiters;
    waiters.swap(node->waiters);
    for (auto& waiter : waiters) {
        waiter();
    }
}

void H5TreeModel::whenListed(Node* node, std::function<void()> then) const
{
    if (node == nullptr) {
        then();
        return;
    }
    if (node->listed) {
        then();
        return;
    }
    node->waiters.push_back(std::move(then));
    requestListing(node);
}

void H5TreeModel::requestListing(Node* node) const
{
    if (node == nullptr || node->listed || node->listing) {
        return;
    }
    // A node whose kind is not known cannot be listed yet -- it may not be a
    // group at all. Find out first, and let the facts start the listing when
    // they land; that is what `listAfterFacts` is.
    if (node != root_.get() && !node->known) {
        node->listAfterFacts = true;
        requestFacts(node, false);
        return;
    }
    // A node that is not a group, or one that closes a loop, has no children
    // and never will. Settling that here rather than leaving it unlisted is
    // what lets anything waiting on the listing stop waiting.
    if (node != root_.get()
        && (node->cyclic || node->info.kind != h5core::NodeKind::Group)) {
        node->listed = true;
        runWaiters(node);
        return;
    }

    node->listing = true;
    ++inFlight_;
    auto* self = const_cast<H5TreeModel*>(this);
    const std::string path = node->info.path;

    struct Listing {
        std::vector<h5core::NodeInfo> children;
        std::string error;
    };

    H5Thread::instance().submit(
        requests_,
        [path](H5Session& session) {
            Listing listing;
            if (session.file() == nullptr) {
                return listing;
            }
            try {
                listing.children =
                    session.file()->children(path, h5core::File::Resolve::Links);
            } catch (const h5core::H5Error& error) {
                listing.error = error.summary();
            }
            return listing;
        },
        [self, node, path](Listing listing) {
            node->listing = false;
            node->listed = true;
            --self->inFlight_;

            if (!listing.error.empty()) {
                emit self->loadFailed(
                    QStringLiteral("Could not list '%1': %2")
                        .arg(QString::fromStdString(path),
                             QString::fromStdString(listing.error)));
                self->runWaiters(node);
                self->noteActivity();
                return;
            }
            if (listing.children.empty()) {
                self->runWaiters(node);
                self->noteActivity();
                return;
            }

            const QModelIndex parent = self->indexFor(node);
            self->beginInsertRows(parent, 0,
                                  static_cast<int>(listing.children.size()) - 1);
            node->children.reserve(listing.children.size());
            int row = 0;
            for (auto& info : listing.children) {
                auto child = std::make_unique<Node>();
                child->parent = node;
                child->rowInParent = row++;
                child->info = std::move(info);
                node->children.push_back(std::move(child));
            }
            self->endInsertRows();
            self->runWaiters(node);
            self->noteActivity();
        });
}

// --- the per-row facts, batched --------------------------------------------

void H5TreeModel::requestFacts(Node* node, bool readout) const
{
    if (node == nullptr || node == root_.get()) {
        return;
    }
    if (node->known && (!readout || node->readoutKnown)) {
        return;
    }
    if (node->asking) {
        // Already on its way. If this asks for more than that one does, say so
        // -- the batch has not been sent yet, and one job that answers both is
        // better than a second job for the half that was left out.
        node->askingReadout = node->askingReadout || readout;
        return;
    }
    node->asking = true;
    node->askingReadout = readout;
    pending_.push_back(node);

    if (flushScheduled_) {
        return;
    }
    flushScheduled_ = true;
    // Zero-delay, so everything the view asks for in one layout pass becomes
    // one job. Forty rows scrolling into view are forty questions and one
    // round trip.
    QTimer::singleShot(0, const_cast<H5TreeModel*>(this), [this] {
        const_cast<H5TreeModel*>(this)->flushFacts();
    });
}

void H5TreeModel::flushFacts()
{
    flushScheduled_ = false;
    if (pending_.empty()) {
        return;
    }

    std::vector<Node*> batch;
    batch.swap(pending_);

    struct Request {
        std::string path;
        h5core::LinkType link;
        bool readout;
    };
    std::vector<Request> asked;
    asked.reserve(batch.size());
    for (const Node* node : batch) {
        asked.push_back({node->info.path, node->info.link, node->askingReadout});
    }

    ++inFlight_;
    H5Thread::instance().submit(
        requests_,
        [asked = std::move(asked)](H5Session& session) {
            std::vector<RowFacts> facts;
            facts.reserve(asked.size());
            for (const Request& request : asked) {
                if (session.file() == nullptr) {
                    facts.emplace_back();
                    continue;
                }
                try {
                    facts.push_back(readFacts(*session.file(), request.path,
                                              request.link, request.readout));
                } catch (const h5core::H5Error&) {
                    RowFacts failed;
                    failed.path = request.path;
                    facts.push_back(std::move(failed));
                }
            }
            return facts;
        },
        [this, batch = std::move(batch)](std::vector<RowFacts> facts) {
            --inFlight_;
            const std::size_t count = std::min(batch.size(), facts.size());
            for (std::size_t i = 0; i < count; ++i) {
                applyFacts(batch[i], facts[i]);
            }
            // A listing that was asked for before the node's kind was known
            // starts here, now that it is.
            for (std::size_t i = 0; i < count; ++i) {
                if (batch[i]->listAfterFacts) {
                    batch[i]->listAfterFacts = false;
                    requestListing(batch[i]);
                }
            }
            // One dataChanged per row rather than one for the range: the rows
            // in a batch are not contiguous and rarely share a parent.
            for (std::size_t i = 0; i < count; ++i) {
                const QModelIndex index = indexFor(batch[i]);
                if (index.isValid()) {
                    emit dataChanged(index, index);
                }
            }
            noteActivity();
        });
}

void H5TreeModel::applyFacts(Node* node, const RowFacts& facts)
{
    node->asking = false;
    node->askingReadout = false;
    node->known = true;
    node->info.kind = facts.kind;
    node->info.fileNumber = facts.fileNumber;
    node->info.address = facts.address;
    node->info.attributeCount = facts.attributeCount;
    node->image = facts.image;
    if (facts.image) {
        node->imageSubclass = QString::fromStdString(h5core::toString(facts.subclass));
    }

    // A name repeating an ancestor's object identity closes a loop. Settled
    // here rather than when the parent was listed, because a soft link back to
    // an ancestor has no identity until it has been followed -- and the test
    // needs no kind of its own, since every ancestor is a group and only a
    // group can carry a group's identity.
    if (node->info.address.has_value()) {
        for (const Node* ancestor = node->parent; ancestor != nullptr;
             ancestor = ancestor->parent) {
            if (ancestor->info.address == node->info.address
                && ancestor->info.fileNumber == node->info.fileNumber) {
                node->cyclic = true;
                break;
            }
        }
    }

    if (facts.readout) {
        node->readoutKnown = true;
        node->meta = readoutFor(node, facts);
    } else if (node->info.link != h5core::LinkType::Hard || node->cyclic
               || !node->info.resolves()) {
        // Three readouts that need no second read: a link says where it points,
        // a loop says it is one, and a name that resolves to nothing has
        // nothing further to say. They come free with the kind.
        node->readoutKnown = true;
        node->meta = readoutFor(node, facts);
    }
}

QString H5TreeModel::readoutFor(const Node* node, const RowFacts& facts) const
{
    QString meta;

    // Where a link points is what distinguishes it from the object it names,
    // so that is what the readout says -- for a broken one it is the only
    // thing there is to say. Costs no read: the target came out of the link
    // table with the name.
    if (node->info.link != h5core::LinkType::Hard) {
        meta = QStringLiteral("→ ");
        if (!node->info.linkFile.empty()) {
            meta += QStringLiteral("%1:").arg(
                QString::fromStdString(node->info.linkFile));
        }
        meta += QString::fromStdString(node->info.linkTarget);
        if (!node->info.resolves()) {
            meta += QStringLiteral("  (missing)");
        }
        return meta;
    }

    if (node->cyclic) {
        // The same object under a second name, so there is no target path to
        // print and no count to take -- what the row has to say is that it
        // closes a loop.
        return QStringLiteral("cycle");
    }

    if (!facts.read) {
        // A node whose metadata will not read still lists, and says so: blank
        // would read as "nothing to report" about an object that has something
        // to report and could not be asked.
        return tr("unreadable");
    }

    switch (facts.kind) {
    case h5core::NodeKind::Group:
        // Singular when there is one of them: the readout is a phrase the
        // reader reads, not a count with a fixed noun stapled to it, and
        // "1 items" is not a phrase.
        return facts.members == 1
                   ? QStringLiteral("1 item")
                   : QStringLiteral("%1 items").arg(facts.members);
    case h5core::NodeKind::Dataset:
        if (facts.space == h5core::Dataspace::Null) {
            return QStringLiteral("null");
        }
        if (facts.shape.empty()) {
            return QStringLiteral("scalar");
        }
        {
            QStringList parts;
            parts.reserve(static_cast<int>(facts.shape.size()));
            for (const hsize_t dim : facts.shape) {
                parts << QLocale::system().toString(static_cast<qulonglong>(dim));
            }
            return QStringLiteral("(%1)").arg(parts.join(QStringLiteral(" × ")));
        }
    case h5core::NodeKind::NamedDataType:
        // A committed type has no shape and no children, so what it is *of* is
        // the whole of what the row has to say about it.
        return QString::fromStdString(facts.typeDescription);
    default:
        break;
    }

    // The readout column carries a fact about every row or it is not a column.
    // This is the floor under everything above, for the kinds that have nothing
    // more -- an unresolved link, or an object of a kind this build does not
    // know.
    return QString::fromStdString(h5core::toString(facts.kind)).toLower();
}

// --- QAbstractItemModel ----------------------------------------------------

QModelIndex H5TreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return {};
    }
    Node* parentNode = nodeFor(parent);
    if (parentNode == nullptr
        || row < 0
        || static_cast<std::size_t>(row) >= parentNode->children.size()) {
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
    return indexFor(node->parent);
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
    // What is known now, plus a request for the rest. The view is told about
    // the rest through rowsInserted rather than by waiting here, which is the
    // whole difference between this and the version that blocked.
    requestListing(node);
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
    if (!node->children.empty()) {
        return true;
    }
    if (node->listed) {
        return false;
    }
    if (!node->known) {
        // Not known yet to be a group. Claim nothing for now and ask; the
        // expander appears a frame later, which is honest -- the file has not
        // said yet.
        requestFacts(node, false);
        return node == root_.get();
    }
    return !node->cyclic && node->info.kind == h5core::NodeKind::Group;
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
        {IsResolvedRole, "isResolved"},
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
    // --- out of the link table: known the moment the row exists ------------
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(node->info.name);
    case PathRole:
        return QString::fromStdString(node->info.path);
    case IsLinkRole:
        return node->info.link != h5core::LinkType::Hard;
    case IsLastChildRole: {
        const Node* parent = node->parent;
        return parent == nullptr
               || node->rowInParent == static_cast<int>(parent->children.size()) - 1;
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
    case IsResolvedRole:
        return node->known;

    // --- out of the object header: asked for, and filled in when it lands --
    case KindRole:
        requestFacts(node, false);
        return static_cast<int>(node->info.kind);
    case KindTextRole:
        requestFacts(node, false);
        return (node->info.link == h5core::LinkType::Hard)
                   ? QString::fromStdString(h5core::toString(node->info.kind))
                   : QStringLiteral("%1 → %2")
                         .arg(QString::fromStdString(h5core::toString(node->info.link)),
                              QString::fromStdString(h5core::toString(node->info.kind)));
    case IsGroupRole:
        requestFacts(node, false);
        return node->info.kind == h5core::NodeKind::Group;
    case IsDatasetRole:
        requestFacts(node, false);
        return node->info.kind == h5core::NodeKind::Dataset;
    case IsCyclicRole:
        requestFacts(node, false);
        return node->cyclic;
    case LinkResolvesRole:
        // A hard link always resolves and the link table says it is one, so
        // only the other two are worth an object-header read.
        if (node->info.link == h5core::LinkType::Hard) {
            return true;
        }
        requestFacts(node, false);
        return node->known && node->info.resolves();
    case HasAttributesRole:
        requestFacts(node, false);
        return node->info.attributeCount > 0;
    case AttributeCountRole:
        requestFacts(node, false);
        return static_cast<int>(node->info.attributeCount);
    case IsImageRole:
        requestFacts(node, true);
        return node->image;
    case ImageSubclassRole:
        requestFacts(node, true);
        return node->imageSubclass;
    case LinkDescriptionRole:
        if (node->info.link == h5core::LinkType::Hard) {
            return QString{};
        }
        requestFacts(node, false);
        return node->known ? linkDescription(node->info) : QString{};
    case MetaRole:
        requestFacts(node, true);
        return node->meta;
    default:
        return {};
    }
}

// --- paths -----------------------------------------------------------------

bool H5TreeModel::isPopulated(const QModelIndex& index) const
{
    const Node* node = nodeFor(index);
    return node != nullptr && node->listed;
}

QString H5TreeModel::pathAt(const QModelIndex& index) const
{
    Node* node = nodeFor(index);
    return (node == nullptr) ? QString{} : QString::fromStdString(node->info.path);
}

h5core::NodeKind H5TreeModel::kindAt(const QModelIndex& index) const
{
    Node* node = nodeFor(index);
    return (node == nullptr) ? h5core::NodeKind::Unknown : node->info.kind;
}

QModelIndex H5TreeModel::indexForPath(const QString& path) const
{
    if (root_ == nullptr || path.isEmpty()) {
        return {};
    }
    QModelIndex current;
    for (const QString& part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        Node* parent = nodeFor(current);
        if (parent == nullptr || !parent->listed) {
            return {};
        }
        const std::string name = part.toStdString();
        bool found = false;
        for (const auto& child : parent->children) {
            // Against the node's own name rather than through data(), which
            // would build a QString and a QVariant for every member of every
            // group on the way down.
            if (child->info.name == name) {
                current = createIndex(child->rowInParent, 0, child.get());
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

void H5TreeModel::revealPath(const QString& path)
{
    if (root_ == nullptr) {
        emit pathRevealed(QModelIndex{}, path);
        return;
    }
    continueReveal(root_.get(), path.split(QLatin1Char('/'), Qt::SkipEmptyParts), path);
}

void H5TreeModel::continueReveal(Node* parent, QStringList remaining,
                                 const QString& whole)
{
    if (parent == nullptr) {
        emit pathRevealed(QModelIndex{}, whole);
        return;
    }
    if (remaining.isEmpty()) {
        emit pathRevealed(indexFor(parent), whole);
        return;
    }

    // One level per round trip, each one waiting on the last. A path twelve
    // groups deep into a file nobody has expanded is twelve listings, which is
    // exactly why this is a signal and not a return value.
    whenListed(parent, [this, parent, remaining, whole]() mutable {
        if (!parent->listed) {
            emit pathRevealed(QModelIndex{}, whole);
            return;
        }
        const std::string name = remaining.front().toStdString();
        for (const auto& child : parent->children) {
            if (child->info.name != name) {
                continue;
            }
            Node* next = child.get();
            remaining.pop_front();
            if (remaining.isEmpty()) {
                // Describe the row before announcing it: every caller asks
                // something about the object it just revealed.
                if (next->known) {
                    emit pathRevealed(indexFor(next), whole);
                } else {
                    requestFacts(next, true);
                    QMetaObject::invokeMethod(
                        this,
                        [this, next, whole] {
                            emit pathRevealed(indexFor(next), whole);
                        },
                        Qt::QueuedConnection);
                }
                return;
            }
            continueReveal(next, remaining, whole);
            return;
        }
        emit pathRevealed(QModelIndex{}, whole);
    });
}

} // namespace gui

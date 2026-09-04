// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "H5Thread.hpp"
#include "h5core/Types.hpp"

#include <functional>
#include <string>

#include <QAbstractItemModel>
#include <QHash>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace gui {

/// Lazy, asynchronous tree over an HDF5 file hierarchy, consumed by QML's
/// TreeView.
///
/// Two properties, and they are separate concerns that happen to meet here.
///
/// **It reads only what is on screen.** Laziness has three stages, because
/// expanding a group of eight thousand members must not cost eight thousand
/// reads before the first forty rows can be drawn:
///
///   1. *listed*  -- the parent's link table, read whole on expand. Names, link
///                   types, link targets and (for hard links) object identity,
///                   all out of one structure. No object is opened.
///   2. *known*   -- what one name actually points at: group, dataset or
///                   nothing, how many attributes it has, and the shape or
///                   member count beside it. Two object-header reads, made the
///                   first time that row is asked about.
///
/// Stage 2 is per *visible* row and is kept forever after; stage 1 is per
/// expanded group. Nothing here is proportional to the size of the file.
///
/// **It never blocks the window.** Every one of those reads happens on
/// `H5Thread`, which is the only thread in this process allowed to touch HDF5
/// at all. `rowCount()` and `data()` answer immediately with what is already
/// known -- an empty group, a blank readout -- and post a request for the rest;
/// when it arrives the model emits `rowsInserted` or `dataChanged` and the view
/// fills in. A row therefore appears before it is fully described, which is
/// what it means for a tree over a slow filesystem to be usable at all.
///
/// Requests are batched. The view asks about forty rows in one layout pass, and
/// those become one job rather than forty: `pending_` accumulates within an
/// event-loop turn and is flushed by a zero-delay timer, which is the last
/// thing that runs before the frame is drawn.
///
/// Note that population is still driven from `rowCount()` rather than through
/// canFetchMore()/fetchMore(). QQuickTreeView, unlike QTreeView, never calls the
/// fetchMore pair -- it simply asks for rowCount() on expand -- so a
/// fetch-based model renders every group as empty in QML.
class H5TreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        KindRole,
        KindTextRole,
        IsGroupRole,
        IsDatasetRole,
        IsCyclicRole,
        /// True when the name is a soft or external link rather than a hard
        /// one -- i.e. when the row carries link information at all.
        IsLinkRole,
        /// False when that link leads nowhere. Meaningless for a hard link,
        /// which always resolves.
        LinkResolvesRole,
        /// True when the object carries at least one HDF5 attribute. Comes out
        /// of the same object-header read that settles the node's kind.
        HasAttributesRole,
        /// How many of them. The tag is a claim that there are some; what a
        /// reader wants next is how many, and that is one number already in
        /// hand from the same call.
        AttributeCountRole,
        /// True when the dataset declares itself an image, per the HDF5 Image
        /// and Palette Specification. The tree tags those rows, because it is
        /// the one thing about a dataset that changes what the Data Viewer
        /// opens on.
        IsImageRole,
        /// Which kind of image it says it is -- "Truecolour", "Indexed" and so
        /// on. Empty for everything that is not one.
        ImageSubclassRole,
        /// Where a soft or external link leads, in a sentence: the target, the
        /// file when there is one, and what is missing when it resolves to
        /// nothing. Empty for a hard link, which leads nowhere but to itself.
        LinkDescriptionRole,
        /// Right-hand readout in the tree row: a dataset's shape, a group's
        /// member count. Requested on demand and kept per node.
        MetaRole,
        /// True when this node is the last of its parent's children, so the
        /// view can terminate its connector guide rather than running it on.
        IsLastChildRole,
        /// One bool per ancestor level, root first: whether that ancestor has
        /// a following sibling and therefore needs its trunk line drawn
        /// through this row. Lets the delegate render tree guides without
        /// knowing anything about nodes other than itself.
        AncestorLinesRole,
        /// False while this row is still waiting to hear what it is. The
        /// delegate draws the readout column dimmed rather than empty, so a
        /// row that has not been described yet does not read as a row with
        /// nothing to say about it.
        IsResolvedRole,
    };
    Q_ENUM(Roles)

    explicit H5TreeModel(QObject* parent = nullptr);
    ~H5TreeModel() override;

    /// Show whatever `H5Thread`'s session currently has open. Reads the root
    /// group's own record, asynchronously; the model is empty until it lands.
    ///
    /// There is deliberately no setFile(h5core::File*): the file belongs to the
    /// HDF5 thread and a model that held a pointer to it could be tempted to
    /// use it. See `H5Session`.
    void open();
    /// Show nothing. Outstanding replies are disowned rather than waited for.
    void close();
    [[nodiscard]] bool hasFile() const { return root_ != nullptr; }

    /// Whether anything is still on its way from the file. Drives the pane's
    /// activity indicator.
    [[nodiscard]] bool loading() const;

    [[nodiscard]] QString pathAt(const QModelIndex& index) const;
    /// Whether this node's children have already been read. Lets a filter
    /// recurse without forcing the reads the lazy tree exists to defer.
    [[nodiscard]] bool isPopulated(const QModelIndex& index) const;
    /// What the node is, or NodeKind::Unknown while that is still being asked.
    [[nodiscard]] h5core::NodeKind kindAt(const QModelIndex& index) const;

    /// Resolve an absolute HDF5 path against what has *already* been read.
    /// Returns an invalid index for a path whose groups are not listed yet;
    /// revealPath() is how to ask for one that may not be.
    [[nodiscard]] QModelIndex indexForPath(const QString& path) const;

    /// Walk down to `path`, listing each group on the way, and announce the
    /// index when it is there. Announces an invalid index if it is not.
    ///
    /// Asynchronous, and therefore a signal rather than a return value: a path
    /// twelve levels deep into a file nobody has expanded is twelve listings,
    /// and doing them between two frames is what this whole class exists to
    /// avoid.
    void revealPath(const QString& path);

    // QAbstractItemModel
    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;

signals:
    /// Emitted when a listing fails, so the UI can show the reason without the
    /// model needing to know about dialogs.
    void loadFailed(const QString& message);
    /// The answer to revealPath(). `index` is invalid when the path is not in
    /// the file; `path` is the one that was asked for, so a caller that asked
    /// twice can tell the answers apart.
    void pathRevealed(const QModelIndex& index, const QString& path);
    void loadingChanged();

private:
    /// What one object-header round trip finds out about a row.
    ///
    /// Plain data, deliberately: this is what crosses back from the HDF5
    /// thread, so there is nothing in it that has to be read there. The
    /// formatting into a readout happens on this side, where the locale and
    /// the translations live.
    struct RowFacts {
        std::string path;
        bool read = false; ///< false when the metadata would not read at all
        /// Whether the readout half was asked for and is therefore filled in.
        /// A row that is only being measured -- does it have children, is it a
        /// group -- needs its kind and nothing else, and the shape or the
        /// member count beside it is a second read that nobody is going to
        /// look at.
        bool readout = false;
        h5core::NodeKind kind = h5core::NodeKind::Unknown;
        std::optional<unsigned long> fileNumber;
        std::optional<haddr_t> address;
        std::size_t attributeCount = 0;
        hsize_t members = 0;                     ///< groups
        h5core::Dataspace space = h5core::Dataspace::Simple; ///< datasets
        std::vector<hsize_t> shape;
        bool image = false;
        h5core::ImageSubclass subclass = h5core::ImageSubclass::Indexed;
        std::string typeDescription; ///< named datatypes
    };

    struct Node {
        h5core::NodeInfo info;
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        int rowInParent = 0;

        /// The link table has been read. Children exist, or the node has none.
        bool listed = false;
        /// ...and a listing is on its way, so nothing asks for a second one.
        bool listing = false;

        /// The object header has been read: `info.kind` and
        /// `info.attributeCount` are settled.
        bool known = false;
        /// ...and so is the readout beside the name, which is a further read
        /// and is made only for a row something has actually asked to draw.
        bool readoutKnown = false;
        /// A read is on its way, and whether it was asked to fetch the readout
        /// too. A row that is measured and then drawn in the same pass wants
        /// both, and gets them in one job.
        bool asking = false;
        bool askingReadout = false;

        /// True when this node repeats an ancestor's object identity. Shown,
        /// never expanded, or a loop would recurse forever. Settled when the
        /// facts arrive: a soft link back to an ancestor has no identity until
        /// it has been followed.
        bool cyclic = false;

        QString meta;
        bool image = false;
        QString imageSubclass;

        /// What to do once this node has been listed -- or once it is settled
        /// that it never will be, because it is not a group. revealPath() walks
        /// down a level at a time and each level is a round trip, so it leaves
        /// the rest of the walk here rather than polling for the answer.
        std::vector<std::function<void()>> waiters;
        /// Set when a listing was asked for before the node's kind was known.
        /// The facts settle it, and their arrival is what starts the listing.
        bool listAfterFacts = false;
    };

    /// Read everything one row needs, on the HDF5 thread, in one go.
    ///
    /// Identity and readout together rather than as two round trips: they are
    /// two reads either way, and a row that appeared and then changed twice
    /// would flicker twice. Static because it runs on the other thread and
    /// must touch no model state at all.
    [[nodiscard]] static RowFacts readFacts(h5core::File& file,
                                            const std::string& path,
                                            h5core::LinkType link, bool readout);

    [[nodiscard]] Node* nodeFor(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexFor(Node* node) const;

    /// Ask for `node`'s children if nothing has yet. Cheap and idempotent; the
    /// reply arrives as a row insertion.
    void requestListing(Node* node) const;
    /// Run `then` once `node` is listed, now if it already is. The only way to
    /// sequence anything after a listing, because a listing is a round trip.
    void whenListed(Node* node, std::function<void()> then) const;
    /// Everything waiting on `node`'s listing, run and forgotten.
    void runWaiters(Node* node) const;
    /// Put `node` on the queue of rows waiting to be described, unless it is
    /// there already or has been. Flushed as one job at the end of the turn.
    ///
    /// `readout` says whether the shape or member count beside the name is
    /// wanted as well as the kind. A row being measured asks for the kind
    /// alone; a row being drawn asks for both, and gets them in one job.
    void requestFacts(Node* node, bool readout) const;
    void flushFacts();
    /// Apply what came back, work out whether the row closes a loop, and build
    /// its readout.
    void applyFacts(Node* node, const RowFacts& facts);
    [[nodiscard]] QString readoutFor(const Node* node, const RowFacts& facts) const;

    /// One step of revealPath(): match `remaining` against `parent`'s children,
    /// listing it first if need be.
    void continueReveal(Node* parent, QStringList remaining, const QString& whole);

    void noteActivity();

    std::unique_ptr<Node> root_;
    /// Rows waiting to be described, and the flush that batches them. Mutable
    /// because data() is const and asking is what it does.
    mutable std::vector<Node*> pending_;
    mutable bool flushScheduled_ = false;
    mutable H5Requests requests_;
    /// Listings and fact reads in flight, for loading().
    mutable int inFlight_ = 0;
};

} // namespace gui

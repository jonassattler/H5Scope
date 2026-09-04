// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/File.hpp"

#include <QAbstractItemModel>
#include <QHash>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace gui {

/// Lazy tree over an HDF5 file hierarchy, consumed by QML's TreeView.
///
/// Population is deferred until a node's row count is actually requested,
/// which happens when the view expands it. HDF5 files routinely hold tens of
/// thousands of objects, so walking the whole hierarchy up front would stall
/// the UI on open.
///
/// Note the population happens synchronously inside rowCount() rather than via
/// canFetchMore()/fetchMore(). QQuickTreeView, unlike QTreeView, never calls
/// the fetchMore pair -- it simply asks for rowCount() on expand -- so a
/// fetch-based model renders every group as empty in QML. Populating in
/// rowCount() is safe here because a given parent's count is computed once and
/// is stable forever after; the view never observes it change.
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
        /// True when the object carries at least one HDF5 attribute. One
        /// metadata call per node, folded into the same read as the rest.
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
        /// member count. Computed on demand and cached per node.
        MetaRole,
        /// True when this node is the last of its parent's children, so the
        /// view can terminate its connector guide rather than running it on.
        IsLastChildRole,
        /// One bool per ancestor level, root first: whether that ancestor has
        /// a following sibling and therefore needs its trunk line drawn
        /// through this row. Lets the delegate render tree guides without
        /// knowing anything about nodes other than itself.
        AncestorLinesRole,
    };
    Q_ENUM(Roles)

    explicit H5TreeModel(QObject* parent = nullptr);
    ~H5TreeModel() override;

    /// Show `file`, replacing anything previously shown. Pass nullptr to clear.
    void setFile(std::shared_ptr<h5core::File> file);
    [[nodiscard]] const std::shared_ptr<h5core::File>& file() const { return file_; }

    [[nodiscard]] QString pathAt(const QModelIndex& index) const;
    /// Whether this node's children have already been read. Lets a filter
    /// recurse without forcing the reads the lazy tree exists to defer.
    [[nodiscard]] bool isPopulated(const QModelIndex& index) const;
    [[nodiscard]] h5core::NodeKind kindAt(const QModelIndex& index) const;

    /// Resolve an absolute HDF5 path to an index, populating along the way.
    /// Returns an invalid index when the path does not exist.
    [[nodiscard]] QModelIndex indexForPath(const QString& path);

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
    /// Emitted when lazy population fails, so the UI can show the reason
    /// without the model needing to know about dialogs.
    void loadFailed(const QString& message);

private:
    struct Node {
        h5core::NodeInfo info;
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        int rowInParent = 0;
        bool populated = false;
        /// True when this node repeats an ancestor's object identity. Such a
        /// node is shown but never expanded, otherwise a hard-linked loop
        /// would recurse forever.
        bool cyclic = false;
        /// MetaRole's value, computed the first time the view asks. Mutable so
        /// data(), which Qt makes const, can fill it in.
        mutable std::optional<QString> meta;
        /// IsImageRole's value, filled at the same time: both answers come out
        /// of the one dataset open, so neither pays for the other.
        mutable bool image = false;
        /// AttributeCountRole's value, filled at the same time -- and
        /// HasAttributesRole's, which is this being above zero. One
        /// H5Oget_info_by_name3 asking for the attribute count alone, which is
        /// the same order of cost as the shape read beside it.
        mutable int attributes = 0;
        /// ImageSubclassRole's value, out of the same dataset open as `image`.
        mutable QString imageSubclass;
    };

    /// Fill in the row's readout, its image flag and its attribute flag, once
    /// per node. All three come out of the same metadata reads -- never
    /// element data -- and none of them populates the node, so scrolling the
    /// tree stays cheap and no answer pays for another.
    void ensureReadout(const Node* node) const;
    /// A dataset's shape or a group's member count, as the tree row shows it.
    [[nodiscard]] QString metaFor(const Node* node) const;

    [[nodiscard]] Node* nodeFor(const QModelIndex& index) const;
    [[nodiscard]] bool isExpandable(const Node* node) const;
    /// Read `node`'s children if not already done. Idempotent and signal-free.
    void populate(Node* node) const;

    std::shared_ptr<h5core::File> file_;
    std::unique_ptr<Node> root_;
};

} // namespace gui

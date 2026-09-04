// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "ObjectInfoModel.hpp"

#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"

#include <QLocale>
#include <QStringList>
#include <QVariantMap>

namespace gui {
namespace {

/// How many of a group's members this panel names before it stops and says how
/// many are left. A flat group of thousands is a shape real files have, and
/// naming every one of them is both a freeze on the click that selects the
/// group and a table no reader gets to the bottom of.
constexpr std::size_t kMaxListedMembers = 200;

QString formatShape(const std::vector<hsize_t>& shape)
{
    if (shape.empty()) {
        return QStringLiteral("scalar");
    }
    QStringList parts;
    parts.reserve(static_cast<int>(shape.size()));
    for (const hsize_t dim : shape) {
        parts << QString::number(dim);
    }
    return parts.join(QStringLiteral(" x "));
}

/// Like formatShape, but an unlimited extent reads as a word rather than as
/// the 2^64-1 sentinel HDF5 actually stores.
QString formatMaxShape(const std::vector<hsize_t>& shape)
{
    if (shape.empty()) {
        return QStringLiteral("scalar");
    }
    QStringList parts;
    parts.reserve(static_cast<int>(shape.size()));
    for (const hsize_t dim : shape) {
        parts << (dim == H5S_UNLIMITED ? QStringLiteral("unlimited")
                                       : QString::number(dim));
    }
    return parts.join(QStringLiteral(" x "));
}

/// A count of bytes, with the noun agreeing with it. Qt's formattedDataSize
/// does this for a *size* -- it is where "256 bytes" and "1 byte" above come
/// from -- but an element's width is a plain count and was being written
/// through a literal that always said "bytes", so a one-byte type read "1
/// bytes". A number and its unit are one phrase, and this is the whole of it.
QString formatBytes(std::size_t bytes)
{
    return bytes == 1 ? QStringLiteral("1 byte")
                      : QStringLiteral("%1 bytes").arg(bytes);
}

QString joinStrings(const std::vector<std::string>& values)
{
    QStringList parts;
    parts.reserve(static_cast<int>(values.size()));
    for (const auto& value : values) {
        parts << QString::fromStdString(value);
    }
    return parts.join(QStringLiteral(", "));
}

/// How a dataset's extent reads in one phrase. A null dataspace has no shape
/// and no elements, which is a different statement from a scalar's "no shape,
/// one element", and the two must not print the same.
QString formatExtent(const h5core::DatasetInfo& info)
{
    if (info.isNull()) {
        return QStringLiteral("null");
    }
    return formatShape(info.shape);
}

/// The parent path of an absolute HDF5 path; the root is its own parent.
QString parentPath(const QString& path)
{
    const qsizetype cut = path.lastIndexOf(QLatin1Char('/'));
    if (cut <= 0) {
        return QStringLiteral("/");
    }
    return path.left(cut);
}

} // namespace

ObjectInfoModel::ObjectInfoModel(QObject* parent) : QAbstractListModel(parent) {}

int ObjectInfoModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QHash<int, QByteArray> ObjectInfoModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {ValueRole, "value"},
        {IsWarningRole, "isWarning"},
        {SectionRole, "section"},
    };
}

QVariant ObjectInfoModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || static_cast<std::size_t>(index.row()) >= rows_.size()) {
        return {};
    }
    const Row& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case LabelRole:
        return row.label;
    case ValueRole:
    case Qt::DisplayRole:
        return row.value;
    case IsWarningRole:
        return row.warning;
    case SectionRole:
        return row.section;
    default:
        return {};
    }
}

QString ObjectInfoModel::valueFor(const QString& label) const
{
    for (const Row& row : rows_) {
        if (row.label == label) {
            return row.value;
        }
    }
    return {};
}

QVariantList ObjectInfoModel::sections() const
{
    QVariantList panels;
    panels.reserve(static_cast<int>(sections_.size()));

    for (const Section& section : sections_) {
        QVariantList rows;
        for (const Row& row : rows_) {
            if (row.section != section.name) {
                continue;
            }
            rows.append(QVariantMap{
                {QStringLiteral("label"), row.label},
                {QStringLiteral("value"), row.value},
                {QStringLiteral("isWarning"), row.warning},
            });
        }
        if (rows.isEmpty()) {
            continue;
        }
        panels.append(QVariantMap{
            {QStringLiteral("title"), section.name},
            {QStringLiteral("meta"), section.meta},
            {QStringLiteral("accent"), section.accent},
            // Non-empty when the panel has nothing to list and should say so
            // in a sentence rather than in an empty table.
            {QStringLiteral("emptyText"), section.emptyText},
            {QStringLiteral("rows"), rows},
        });
    }
    return panels;
}

void ObjectInfoModel::Content::beginSection(QString name, QString meta, bool accent,
                                            QString emptyText)
{
    currentSection = name;
    sections.push_back(
        Section{std::move(name), std::move(meta), accent, std::move(emptyText)});
}

void ObjectInfoModel::Content::add(QString label, QString value, bool warning)
{
    rows.push_back(Row{std::move(label), std::move(value), warning, currentSection});
}

void ObjectInfoModel::showContent(Content content)
{
    beginResetModel();
    rows_ = std::move(content.rows);
    sections_ = std::move(content.sections);
    endResetModel();
}

void ObjectInfoModel::clear()
{
    beginResetModel();
    rows_.clear();
    sections_.clear();
    endResetModel();
}

ObjectInfoModel::Content ObjectInfoModel::gather(h5core::File& file, const QString& path)
{
    Content content;
    if (path.isEmpty()) {
        return content;
    }

    try {
        const auto node = file.nodeInfo(path.toStdString());
        const QString kindText = QString::fromStdString(h5core::toString(node.kind));

        // The panels below, and their order, are the Information tab's layout.
        // Exactly one panel carries the accent rule, per the design system.
        content.beginSection(QStringLiteral("object"), kindText.toLower(), true);
        content.add(QStringLiteral("Path"), QString::fromStdString(node.path));
        content.add(QStringLiteral("Name"), QString::fromStdString(node.name));
        content.add(QStringLiteral("Kind"), kindText);
        content.add(QStringLiteral("Parent"), parentPath(QString::fromStdString(node.path)));

        // A soft or external link stores a path, and that path is the whole
        // reason the name is there. It is worth stating whether or not it
        // leads anywhere -- and especially when it does not.
        if (node.link != h5core::LinkType::Hard) {
            const QString linkText =
                QString::fromStdString(h5core::toString(node.link));
            content.beginSection(QStringLiteral("link"), linkText.toLower());
            content.add(QStringLiteral("Link"), linkText);
            if (!node.linkFile.empty()) {
                content.add(QStringLiteral("File"), QString::fromStdString(node.linkFile));
            }
            content.add(QStringLiteral("Target"), QString::fromStdString(node.linkTarget));
            if (node.resolves()) {
                content.add(QStringLiteral("Resolves to"), kindText);
            } else {
                content.add(QStringLiteral("Resolves to"),
                    node.link == h5core::LinkType::External
                        ? QStringLiteral("nothing: the file or the object is missing")
                        : QStringLiteral("nothing: no object at that path"),
                    true);
            }
        }

        if (!node.resolves()) {
            // Nothing further can be read: there is no object to read it from.
            return content;
        }

        if (node.kind == h5core::NodeKind::NamedDataType) {
            const auto type = file.namedType(path.toStdString());
            content.beginSection(QStringLiteral("datatype"),
                         QString::fromStdString(type.description));
            content.add(QStringLiteral("Type"), QString::fromStdString(type.description));
            content.add(QStringLiteral("Class"),
                QString::fromStdString(h5core::toString(type.cls)));
            content.add(QStringLiteral("Element size"),
                type.isVariableLength ? QStringLiteral("variable")
                                      : formatBytes(type.size));
            if (!type.memberNames.empty()) {
                content.add(QStringLiteral("Members"), joinStrings(type.memberNames));
            }
        }

        const int attributeCount =
            static_cast<int>(file.attributeCount(path.toStdString()));

        if (node.kind == h5core::NodeKind::Dataset) {
            const h5core::Dataset dataset(file, path.toStdString());
            const auto& info = dataset.info();

            content.beginSection(QStringLiteral("dataspace"), formatExtent(info));
            content.add(QStringLiteral("Dataspace"),
                QString::fromStdString(h5core::toString(info.space)));
            content.add(QStringLiteral("Rank"), QString::number(info.rank()));
            content.add(QStringLiteral("Shape"), formatExtent(info));
            if (!info.isNull()) {
                content.add(QStringLiteral("Max shape"), formatMaxShape(info.maxShape));
            }
            content.add(QStringLiteral("Elements"),
                QLocale::system().toString(
                    static_cast<qulonglong>(info.elementCount())));

            content.beginSection(QStringLiteral("datatype"),
                         QString::fromStdString(info.type.description));
            content.add(QStringLiteral("Type"), QString::fromStdString(info.type.description));
            content.add(QStringLiteral("Class"),
                QString::fromStdString(h5core::toString(info.type.cls)));
            content.add(QStringLiteral("Element size"),
                info.type.isVariableLength ? QStringLiteral("variable")
                                            : formatBytes(info.type.size));
            if (!info.type.memberNames.empty()) {
                content.add(QStringLiteral("Members"), joinStrings(info.type.memberNames));
            }

            // What the file says it is a picture of, and what that made the
            // Data Viewer open on. Only a dataset carrying CLASS="IMAGE" has
            // this panel; nothing is inferred from a shape.
            if (info.image.has_value()) {
                const auto& image = *info.image;
                const QString subclass =
                    QString::fromStdString(h5core::toString(image.subclass));
                content.beginSection(QStringLiteral("image"), subclass.toLower());
                content.add(QStringLiteral("Subclass"), subclass);
                if (image.subclass == h5core::ImageSubclass::Truecolor) {
                    content.add(QStringLiteral("Interlace"),
                        QString::fromStdString(h5core::toString(image.interlace)));
                }
                if (!image.version.empty()) {
                    content.add(QStringLiteral("Spec version"),
                        QString::fromStdString(image.version));
                }
                if (image.shapeMatches) {
                    content.add(QStringLiteral("Rows"),
                        QStringLiteral("dimension %1").arg(image.rowDim));
                    content.add(QStringLiteral("Columns"),
                        QStringLiteral("dimension %1").arg(image.columnDim));
                    if (image.channelDim.has_value()) {
                        content.add(QStringLiteral("Channels"),
                            QStringLiteral("dimension %1, one at a time")
                                .arg(*image.channelDim));
                    }
                } else {
                    content.add(QStringLiteral("Warning"),
                        QStringLiteral("The attributes say %1, which this shape "
                                       "cannot be; the slice is left as it would "
                                       "be for any dataset")
                            .arg(subclass.toLower()),
                        true);
                }
                if (image.minimum.has_value()) {
                    content.add(QStringLiteral("Display range"),
                        QStringLiteral("%1 to %2")
                            .arg(*image.minimum)
                            .arg(*image.maximum));
                }
                if (image.whiteIsZero) {
                    content.add(QStringLiteral("Polarity"), QStringLiteral("white is zero"));
                }
                if (!image.originHonoured) {
                    content.add(QStringLiteral("Warning"),
                        QStringLiteral("Origin is %1; this viewer draws from the "
                                       "upper left, so the raster is not flipped "
                                       "to match")
                            .arg(QString::fromStdString(image.displayOrigin)),
                        true);
                }
            }

            content.beginSection(QStringLiteral("storage"),
                         QLocale::system().formattedDataSize(
                             static_cast<qint64>(info.storageSize)));
            content.add(QStringLiteral("Layout"),
                QString::fromStdString(h5core::toString(info.layout)));
            if (!info.chunk.empty()) {
                content.add(QStringLiteral("Chunk"), formatShape(info.chunk));
            }
            if (!info.filters.empty()) {
                content.add(QStringLiteral("Filters"), joinStrings(info.filters));
            }
            // Where the bytes actually are, when they are not in this file.
            for (const auto& external : info.externalFiles) {
                content.add(QStringLiteral("External file"), QString::fromStdString(external));
            }
            for (const auto& source : info.virtualSources) {
                content.add(QStringLiteral("Source"), QString::fromStdString(source));
            }
            content.add(QStringLiteral("Storage size"),
                QLocale::system().formattedDataSize(
                    static_cast<qint64>(info.storageSize)));
            if (!info.readable()) {
                content.add(QStringLiteral("Warning"),
                    QStringLiteral("Data cannot be read: %1")
                        .arg(QString::fromStdString(info.unreadableReason())),
                    true);
            } else if (!info.unavailableFilters.empty()) {
                // Optional and absent: HDF5 skipped it on writing and skips it
                // again on reading, so the values are exactly right. Saying so
                // is better than either a warning or silence.
                content.add(QStringLiteral("Note"),
                    QStringLiteral("Optional filter not in this build, and not "
                                   "needed to read the data: %1")
                        .arg(joinStrings(info.unavailableFilters)));
            }
        } else if (node.kind == h5core::NodeKind::Group) {
            // The count comes out of the group's own header rather than out of
            // the length of a listing, so a group of eight thousand members
            // costs the same to describe as one of eight.
            const auto members = file.memberCount(path.toStdString());
            content.beginSection(QStringLiteral("members"),
                         QStringLiteral("%1 direct").arg(members));
            content.add(QStringLiteral("Children"), QString::number(members));

            // Then the names, up to a limit. Every one of them is a link to
            // follow and an object header to read, and this panel is built
            // synchronously on the click that selects the group -- so listing
            // all of a flat group of thousands would freeze the window to
            // print a table nobody reads to the end of. The tree beside it is
            // the surface for going through them, and it is lazy.
            auto children = file.children(path.toStdString(),
                                           h5core::File::Resolve::Links);
            const std::size_t shown = std::min(children.size(), kMaxListedMembers);
            for (std::size_t i = 0; i < shown; ++i) {
                file.resolve(children[i]);
                content.add(QString::fromStdString(h5core::toString(children[i].kind)).toLower(),
                    QString::fromStdString(children[i].name));
            }
            if (children.size() > shown) {
                content.add(QStringLiteral("More"),
                    QStringLiteral("%1 further members, listed in the tree")
                        .arg(children.size() - shown));
            }
        }

        // The attribute rows themselves are appended by AppController, which
        // owns the attribute model; this panel carries the count either way so
        // an object with no attributes still says so.
        //
        // And says it in words. A panel headed "attributes" holding one row
        // reading "Attributes  0" is a table of one meaningless entry, which
        // reads as though something failed to load rather than as though there
        // is nothing there. `empty` is the flag the panel draws a sentence for
        // instead; a row still has to be added, or sections() would drop the
        // panel for having none.
        const bool none = attributeCount == 0;
        content.beginSection(QStringLiteral("attributes"),
                     none ? QString{} : QStringLiteral("%1 total").arg(attributeCount),
                     false, none ? tr("No attributes on this object.") : QString{});
        content.add(QStringLiteral("Attributes"), QString::number(attributeCount));
    } catch (const h5core::H5Error& error) {
        content.beginSection(QStringLiteral("error"), {}, true);
        content.add(QStringLiteral("Error"), QString::fromStdString(error.summary()),
                    true);
    }

    return content;
}

} // namespace gui

// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/File.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>

#include <memory>
#include <vector>

namespace gui {

/// Label/value rows describing the selected object, for the Information tab.
///
/// The Widgets version rendered an HTML table into a QTextBrowser; QML has no
/// equivalent, and a structured model is better anyway -- the view decides how
/// to present it and the rows stay testable.
class ObjectInfoModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        LabelRole = Qt::UserRole + 1,
        ValueRole,
        IsWarningRole,
        SectionRole,
    };
    Q_ENUM(Roles)

    explicit ObjectInfoModel(QObject* parent = nullptr);

    void showObject(const std::shared_ptr<h5core::File>& file, const QString& path);
    void clear();

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Value for `label`, or an empty string. For tests.
    [[nodiscard]] QString valueFor(const QString& label) const;

    /// The rows grouped into the panels the Information tab draws, in order.
    /// Each entry is { title, meta, accent, rows: [{ label, value, isWarning }] }.
    /// The flat model above stays the source of truth; this is a view of it.
    [[nodiscard]] QVariantList sections() const;

private:
    struct Row {
        QString label;
        QString value;
        bool warning = false;
        QString section;
    };

    /// One panel: its rows are every Row carrying the matching section name.
    /// Kept ordered and separate from rows_ so a panel can hold a summary
    /// (`meta`) that is not itself one of the label/value rows.
    struct Section {
        QString name;
        QString meta;
        bool accent = false;
        /// What the panel says instead of its rows when it has nothing to
        /// list. Empty for every panel that always has something.
        QString emptyText;
    };

    void beginSection(QString name, QString meta = {}, bool accent = false,
                      QString emptyText = {});
    void add(QString label, QString value, bool warning = false);

    std::vector<Row> rows_;
    std::vector<Section> sections_;
    QString currentSection_;
};

} // namespace gui

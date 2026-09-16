// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "MemberPath.hpp"

#include "Subscripts.hpp"

#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>

namespace postproc {

namespace {

/// The members a type offers, listed for an error message.
///
/// A reader who mistypes a member name wants the list, not the refusal: the
/// names are in the file and nowhere else, and this is the moment they are
/// being asked for.
QString namesOf(const h5core::TypeInfo& type)
{
    QStringList names;
    names.reserve(static_cast<qsizetype>(type.members.size()));
    for (const h5core::TypeMember& member : type.members) {
        names.append(QString::fromStdString(member.name));
    }
    return names.join(QStringLiteral(", "));
}

/// The one index a ragged member may be given.
///
/// A vlen has a different length in every record, so a *range* of one has no
/// single shape and every view here is a rectangle. One index does have a
/// shape -- the dataset's own -- with the records whose list is too short
/// simply having no value there, which is a gap a plot already knows how to
/// draw. Returns nothing for ':' and for an empty subscript, both of which
/// mean the whole list.
std::optional<hsize_t> readVlenIndex(const QString& member, const QString& text,
                                     QString& error)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral(":")) {
        return std::nullopt;
    }
    bool ok = false;
    const long long index = trimmed.toLongLong(&ok);
    if (!ok || index < 0) {
        error = QStringLiteral(
                    "'.%1' is a vlen: its length differs in every element, so a "
                    "range of it has no single shape -- index one of it, as "
                    "'.%1[0]', or select it on its own to see the lists")
                    .arg(member);
        return std::nullopt;
    }
    return static_cast<hsize_t>(index);
}

} // namespace

bool parseMemberChain(const QString& text, std::vector<MemberStep>& chain,
                      QString& error)
{
    chain.clear();
    error.clear();

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return true; // no chain is a perfectly good answer
    }
    if (!trimmed.startsWith(QLatin1Char('.'))) {
        error = QStringLiteral("a member is named after a '.', as in '.%1'")
                    .arg(trimmed);
        return false;
    }

    qsizetype at = 0;
    while (at < trimmed.size()) {
        // At a '.', by construction: the loop below stops on one or ends.
        ++at;
        const qsizetype nameStart = at;
        while (at < trimmed.size() && trimmed[at] != QLatin1Char('.')
               && trimmed[at] != QLatin1Char('[')) {
            ++at;
        }
        MemberStep step;
        step.name = trimmed.mid(nameStart, at - nameStart).trimmed();
        if (step.name.isEmpty()) {
            error = QStringLiteral("a '.' with no member name after it");
            return false;
        }

        if (at < trimmed.size() && trimmed[at] == QLatin1Char('[')) {
            // Depth-counted, the way splitSubscripts counts: a subscript holds
            // its own brackets for a scattered selection, [0,2,4].
            int depth = 0;
            const qsizetype open = at;
            for (; at < trimmed.size(); ++at) {
                if (trimmed[at] == QLatin1Char('[')) {
                    ++depth;
                } else if (trimmed[at] == QLatin1Char(']')) {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
            }
            if (depth != 0 || at >= trimmed.size()) {
                error = QStringLiteral("this has a '[' with no matching ']'");
                return false;
            }
            step.subscript = trimmed.mid(open + 1, at - open - 1).trimmed();
            step.bracketed = true;
            ++at; // past the ']'
        }
        chain.push_back(step);

        if (at < trimmed.size() && trimmed[at] != QLatin1Char('.')) {
            error = QStringLiteral("'%1' is not part of a member chain -- a member "
                                   "follows a '.'")
                        .arg(trimmed.mid(at));
            return false;
        }
    }
    return true;
}

QString writeMemberChain(const std::vector<MemberStep>& chain)
{
    QString out;
    for (const MemberStep& step : chain) {
        out += QLatin1Char('.') + step.name;
        if (step.bracketed) {
            out += QLatin1Char('[') + step.subscript + QLatin1Char(']');
        }
    }
    return out;
}

MemberChain resolveMemberChain(const QString& text, const h5core::TypeInfo& type)
{
    std::vector<MemberStep> chain;
    MemberChain result;
    if (!parseMemberChain(text, chain, result.error)) {
        return result;
    }
    return resolveMemberChain(chain, type);
}

MemberChain resolveMemberChain(const std::vector<MemberStep>& chain,
                               const h5core::TypeInfo& type)
{
    MemberChain result;
    result.selection.type = type;
    if (chain.empty()) {
        return result;
    }

    // Every array met on the way down contributes its dimensions to the result
    // and is then stepped through, so `level` is always the thing a member is
    // looked up in: a compound, or something that is about to be refused.
    const h5core::TypeInfo* level = &type;
    const auto unwrap = [&result](const h5core::TypeInfo* from, const QString& name) {
        std::vector<hsize_t> appended;
        while (from->cls == h5core::TypeClass::Array && from->base != nullptr) {
            appended.insert(appended.end(), from->arrayDims.begin(),
                            from->arrayDims.end());
            from = from->base.get();
        }
        for (const hsize_t dim : appended) {
            result.selection.dims.push_back(dim);
            result.dimNames.append(name);
        }
        return std::pair{from, appended};
    };

    // A dataset whose own type is an array of compounds: those dimensions are
    // the result's too, and nobody named a member to call them after.
    std::tie(level, std::ignore) = unwrap(level, QString{});

    for (std::size_t i = 0; i < chain.size(); ++i) {
        const MemberStep& step = chain[i];
        const QString holder = i == 0 ? QStringLiteral("this dataset")
                                      : QStringLiteral(".") + chain[i - 1].name;

        if (level->cls != h5core::TypeClass::Compound) {
            result.error = QStringLiteral("%1 is %2, which has no members to select")
                               .arg(holder, QString::fromStdString(level->description));
            return result;
        }

        const auto found =
            std::find_if(level->members.begin(), level->members.end(),
                         [&step](const h5core::TypeMember& m) {
                             return QString::fromStdString(m.name) == step.name;
                         });
        if (found == level->members.end()) {
            const QString has = namesOf(*level);
            result.error = has.isEmpty()
                               ? QStringLiteral("%1 has no member '%2'")
                                     .arg(holder, step.name)
                               : QStringLiteral("%1 has no member '%2' -- it has %3")
                                     .arg(holder, step.name, has);
            return result;
        }

        result.selection.links.push_back(
            h5core::MemberLink{static_cast<unsigned>(found - level->members.begin()),
                               found->name, std::nullopt});

        const auto [landed, appended] = unwrap(&found->type, step.name);

        if (landed->cls == h5core::TypeClass::VarLen) {
            if (!appended.empty()) {
                result.error = QStringLiteral("'.%1' is %2, and an array of ragged "
                                              "lists has no single shape")
                                   .arg(step.name,
                                        QString::fromStdString(found->type.description));
                return result;
            }
            if (i + 1 < chain.size()) {
                result.error =
                    QStringLiteral("'.%1' is a vlen, and a chain cannot go on through "
                                   "one -- select it on its own to see the lists")
                        .arg(step.name);
                return result;
            }
            level = landed;
            if (step.bracketed) {
                const std::optional<hsize_t> index =
                    readVlenIndex(step.name, step.subscript, result.error);
                if (!result.error.isEmpty()) {
                    return result;
                }
                if (index.has_value()) {
                    result.selection.links.back().vlenIndex = index;
                    if (landed->base != nullptr) {
                        level = landed->base.get();
                    }
                }
            }
            continue;
        }

        if (step.bracketed) {
            if (appended.empty()) {
                result.error = QStringLiteral("'.%1' is %2, which has no dimensions "
                                              "to subscript")
                                   .arg(step.name,
                                        QString::fromStdString(landed->description));
                return result;
            }
            // The member's own axes, read with the slice line's own grammar --
            // the ellipsis, the trailing dimensions nobody wrote, the clamping.
            // A member subscript is not a second kind of subscript, and `written`
            // is what the identity folds onto the slice line.
            std::vector<IndexExpression> chosen;
            QStringList written;
            QString problem;
            if (!readSubscripts(step.subscript, appended, chosen, written, problem)) {
                result.error = QStringLiteral(".%1: %2").arg(step.name, problem);
                return result;
            }
            for (qsizetype d = 0; d < static_cast<qsizetype>(appended.size()); ++d) {
                result.folded.append(d < written.size() ? written[d] : QString{});
            }
        } else {
            for (std::size_t d = 0; d < appended.size(); ++d) {
                result.folded.append(QString{});
            }
        }
        level = landed;
    }

    result.selection.type = *level;

    // The canonical chain: the members, and nothing else. Every subscript the
    // reader wrote has gone into `folded` to be put on the slice line, so what
    // is left here is what the box prints back and what names the result. The
    // one exception is a vlen index, which has no slice line to go to.
    QString canonical;
    for (const h5core::MemberLink& link : result.selection.links) {
        canonical += QLatin1Char('.') + QString::fromStdString(link.name);
        if (link.vlenIndex.has_value()) {
            canonical += QLatin1Char('[') + QString::number(*link.vlenIndex)
                         + QLatin1Char(']');
        }
    }
    result.selection.text = canonical.toStdString();
    return result;
}

} // namespace postproc

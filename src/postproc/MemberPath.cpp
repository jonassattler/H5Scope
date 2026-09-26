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

/// A subscript with its outer brackets taken off, if it was written with them.
/// `bracketedIfListed` is the other direction, and this undoes it.
QString bracketedBody(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
        return trimmed.mid(1, trimmed.size() - 2);
    }
    return trimmed;
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

QString sliceLineFor(const QString& subscript, const QStringList& folded,
                     std::size_t originRank)
{
    if (folded.isEmpty()) {
        return subscript;
    }

    // What the reader wrote for the dataset's own axes, one term per axis. The
    // two shorthands the grammar allows -- the trailing dimensions nobody wrote
    // and the one '...' standing for the rest -- have to be spelled out here,
    // because the member's terms go *after* them and a term that stands for
    // "however many are left" cannot have anything written after it.
    QStringList leading;
    QString ignored;
    if (!splitSubscripts(bracketedBody(subscript), leading, ignored)) {
        return subscript; // it does not read; the grammar below says so better
    }
    if (leading.size() == 1 && leading.front().trimmed().isEmpty()) {
        leading.clear();
    }

    QStringList line;
    const auto rank = static_cast<qsizetype>(originRank);
    const qsizetype ellipsis = leading.indexOf(QStringLiteral("..."));
    if (ellipsis >= 0) {
        const qsizetype named = leading.size() - 1;
        for (qsizetype i = 0; i < leading.size(); ++i) {
            if (i != ellipsis) {
                line.append(leading.at(i));
            } else {
                for (qsizetype f = 0; f < rank - named; ++f) {
                    line.append(QStringLiteral(":"));
                }
            }
        }
    } else {
        line = leading;
        while (line.size() < rank) {
            line.append(QStringLiteral(":"));
        }
    }

    for (const QString& term : folded) {
        line.append(term.isEmpty() ? QStringLiteral(":") : term);
    }
    return line.join(QStringLiteral(", "));
}

namespace {

/// The chains under one node, appended to `out` under `prefix`.
///
/// Stops at the cap rather than at a depth: a type is finite and describeType
/// already bounded how deep it was looked at, so what has to be bounded here is
/// the width -- a compound of a thousand members, which is a real shape.
void appendChains(const h5core::TypeInfo& type, const QString& prefix,
                  QStringList& out, int limit)
{
    // An array of compounds is entered: its dimensions become axes and the
    // members of what it holds are still members. This is the same unwrapping
    // resolveMemberChain does, and it has to be, or the list would offer a
    // chain the resolver then refused.
    const h5core::TypeInfo* level = &type;
    while (level->cls == h5core::TypeClass::Array && level->base != nullptr) {
        level = level->base.get();
    }
    if (level->cls != h5core::TypeClass::Compound) {
        return;
    }
    for (const h5core::TypeMember& member : level->members) {
        if (out.size() >= limit) {
            return;
        }
        const QString chain = prefix + QLatin1Char('.')
                              + QString::fromStdString(member.name);
        out.append(chain);
        appendChains(member.type, chain, out, limit);
    }
}

} // namespace

QStringList memberChains(const h5core::TypeInfo& type, int limit)
{
    QStringList out;
    appendChains(type, QString{}, out, std::max(limit, 0));
    return out;
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

QString writeSelection(const QStringList& terms, std::size_t originRank,
                       const MemberChain& chain)
{
    const auto bracketed = [](const QStringList& body) {
        return QLatin1Char('[') + body.join(QStringLiteral(", ")) + QLatin1Char(']');
    };
    const auto whole = [](const QStringList& body) {
        return std::all_of(body.begin(), body.end(), [](const QString& term) {
            return term.trimmed() == QStringLiteral(":");
        });
    };
    const auto rank = static_cast<qsizetype>(originRank);
    const QString chainText = QString::fromStdString(chain.selection.text);

    std::size_t appended = 0;
    for (const std::size_t axes : chain.linkAxes) {
        appended += axes;
    }
    // Every term accounted for, or the line is not one this can split: a chain
    // with axes no member carries, or a slice of some other shape. Both are
    // written the old way, which is at worst the line the bar always printed.
    if (chain.leadingAxes > 0 || chain.linkAxes.size() != chain.selection.links.size()
        || terms.size() != rank + static_cast<qsizetype>(appended)) {
        return (terms.isEmpty() ? QString() : bracketed(terms)) + chainText;
    }

    QString out = rank > 0 ? bracketed(terms.mid(0, rank)) : QString();
    qsizetype at = rank;
    for (std::size_t i = 0; i < chain.selection.links.size(); ++i) {
        const h5core::MemberLink& link = chain.selection.links[i];
        out += QLatin1Char('.') + QString::fromStdString(link.name);
        if (link.vlenIndex.has_value()) {
            out += QLatin1Char('[') + QString::number(*link.vlenIndex) + QLatin1Char(']');
        }
        const auto axes = static_cast<qsizetype>(chain.linkAxes[i]);
        const QStringList own = terms.mid(at, axes);
        if (!own.isEmpty() && !whole(own)) {
            out += bracketed(own);
        }
        at += axes;
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
    result.leadingAxes = result.selection.dims.size();

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
        result.linkAxes.push_back(appended.size());

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

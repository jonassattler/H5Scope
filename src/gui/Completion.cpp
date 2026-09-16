// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Completion.hpp"

#include "DatasetLookup.hpp"

namespace gui {

CompletionRequest completionRequest(const QString& text)
{
    CompletionRequest request;

    // The same split the entry box is checked through, so the completer and
    // the validator can never disagree about where the path stops. It is also
    // where the rule lives that a chain is recognised only after a ']' -- a
    // link name holds a '.' as freely as it holds a '[', so `/data/run.3` is a
    // path being typed and not a member being named.
    const Expression parsed = splitExpression(text);

    if (!parsed.member.isEmpty() && text.endsWith(parsed.member)) {
        request.part = CompletionRequest::Part::Member;
        request.head = text.left(text.size() - parsed.member.size());
        request.fragment = parsed.member;
        request.path = parsed.path;
        request.subscript = parsed.subscript;
        return request;
    }

    if (text.contains(QLatin1Char('['))) {
        if (!parsed.valid() || !text.endsWith(QLatin1Char(']'))) {
            // Still inside the brackets, or they do not balance. Either way
            // there is no list of what could come next.
            request.part = CompletionRequest::Part::Subscript;
            request.head = text;
            return request;
        }
        // A subscript closed and nothing after it: the chain has not been
        // started, and offering the members is what says there are any.
        request.part = CompletionRequest::Part::Member;
        request.head = text;
        request.path = parsed.path;
        request.subscript = parsed.subscript;
        return request;
    }

    request.part = CompletionRequest::Part::Path;
    const qsizetype cut = text.lastIndexOf(QLatin1Char('/'));
    request.head = cut < 0 ? QString{} : text.left(cut + 1);
    request.fragment = cut < 0 ? text : text.mid(cut + 1);
    return request;
}

QString wholeSubscript(std::size_t rank)
{
    if (rank == 0) {
        return {};
    }
    QStringList terms;
    terms.reserve(static_cast<qsizetype>(rank));
    for (std::size_t d = 0; d < rank; ++d) {
        terms.append(QStringLiteral(":"));
    }
    return QLatin1Char('[') + terms.join(QStringLiteral(", ")) + QLatin1Char(']');
}

QString commonHead(const QStringList& candidates)
{
    if (candidates.isEmpty()) {
        return {};
    }
    QString head = candidates.front();
    for (const QString& candidate : candidates) {
        qsizetype shared = 0;
        while (shared < head.size() && shared < candidate.size()
               && head.at(shared) == candidate.at(shared)) {
            ++shared;
        }
        head.truncate(shared);
    }
    return head;
}

} // namespace gui

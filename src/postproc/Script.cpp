// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "postproc/Script.hpp"

#include <QStringList>

#include <algorithm>

namespace postproc {
namespace {

const QString kSelect = QStringLiteral("select");

bool identifierStart(QChar c)
{
    return c.isLetter() || c == u'_';
}

bool identifierPart(QChar c)
{
    return c.isLetterOrNumber() || c == u'_';
}

/// The identifier starting at `at`, or empty when there is none there.
QString identifierAt(const QString& text, qsizetype at)
{
    if (at >= text.size() || !identifierStart(text.at(at))) {
        return {};
    }
    qsizetype end = at + 1;
    while (end < text.size() && identifierPart(text.at(end))) {
        ++end;
    }
    return text.mid(at, end - at);
}

/// Whether a word after a `.` is one a script knows: an operation, or the
/// select that is not one.
bool knownWord(const QString& word)
{
    return word.compare(kSelect, Qt::CaseInsensitive) == 0
           || operationNamed(word).has_value();
}

/// Where the path stops: the end of the first line, or the first `.name(`
/// on it whose name a script knows.
qsizetype pathEnd(const QString& text)
{
    qsizetype end = text.indexOf(u'\n');
    if (end < 0) {
        end = text.size();
    }
    for (qsizetype at = text.indexOf(u'.'); at >= 0 && at < end;
         at = text.indexOf(u'.', at + 1)) {
        const QString word = identifierAt(text, at + 1);
        if (word.isEmpty()) {
            continue;
        }
        qsizetype after = at + 1 + word.size();
        while (after < end && (text.at(after) == u' ' || text.at(after) == u'\t')) {
            ++after;
        }
        if (after < end && text.at(after) == u'(' && knownWord(word)) {
            return at;
        }
    }
    return end;
}

QString operationList()
{
    QStringList names{kSelect};
    for (const OperationInfo& info : operations()) {
        names << info.name;
    }
    return names.join(QStringLiteral(", "));
}

} // namespace

QString writeStep(const Step& step)
{
    const QString name = operationInfo(step.kind).name;
    return step.argument.isEmpty()
               ? QStringLiteral(".%1").arg(name)
               : QStringLiteral(".%1(%2)").arg(name, step.argument);
}

Script parseScript(const QString& text)
{
    Script script;
    const QString body = text.trimmed();

    const qsizetype end = pathEnd(body);
    script.path = body.left(end).trimmed();
    if (script.path.isEmpty()) {
        script.error = QStringLiteral("start with the path of a dataset, e.g. "
                                      "/group/dataset");
        return script;
    }

    qsizetype at = end;
    while (true) {
        while (at < body.size() && body.at(at).isSpace()) {
            ++at;
        }
        if (at >= body.size()) {
            break;
        }
        if (body.at(at) != u'.') {
            // The line starts with something that is not a step. Quoted up to
            // the end of its line, which is as much as the reader needs to find
            // it.
            qsizetype stop = body.indexOf(u'\n', at);
            if (stop < 0) {
                stop = body.size();
            }
            script.error = QStringLiteral("'%1' is not a step — each step is a '.' "
                                          "and an operation, e.g. .max(0)")
                               .arg(body.mid(at, stop - at).trimmed());
            return script;
        }
        const QString word = identifierAt(body, at + 1);
        if (word.isEmpty()) {
            script.error = QStringLiteral("a '.' has to be followed by an operation "
                                          "— one of %1")
                               .arg(operationList());
            return script;
        }
        if (!knownWord(word)) {
            script.error = QStringLiteral("'%1' is not an operation — one of %2")
                               .arg(word, operationList());
            return script;
        }
        at += 1 + word.size();
        while (at < body.size() && (body.at(at) == u' ' || body.at(at) == u'\t')) {
            ++at;
        }

        QString argument;
        if (at < body.size() && body.at(at) == u'(') {
            // Balanced over both kinds of bracket, because an argument holds
            // either: an axis tuple, `sum((0, 1))`, or a list of indices,
            // `slice([0, 2, 5])`.
            int depth = 0;
            qsizetype close = at;
            for (; close < body.size(); ++close) {
                const QChar c = body.at(close);
                if (c == u'(' || c == u'[') {
                    ++depth;
                } else if (c == u')' || c == u']') {
                    if (--depth == 0) {
                        break;
                    }
                }
            }
            if (close >= body.size() || body.at(close) != u')') {
                script.error = QStringLiteral("'.%1(' is never closed — it needs a ')'")
                                   .arg(word);
                return script;
            }
            argument = body.mid(at + 1, close - at - 1).trimmed();
            at = close + 1;
        }

        if (word.compare(kSelect, Qt::CaseInsensitive) == 0) {
            if (!script.member.isEmpty() || script.sliced || !script.steps.empty()) {
                script.error = QStringLiteral(".select comes first, straight after the "
                                              "path — it names the member everything "
                                              "after it reads");
                return script;
            }
            if (argument.isEmpty()) {
                script.error = QStringLiteral(".select needs a member, e.g. "
                                              ".select(samples)");
                return script;
            }
            script.member = argument.startsWith(u'.') ? argument
                                                      : QStringLiteral(".") + argument;
            continue;
        }

        const OperationKind kind = *operationNamed(word);
        if (kind == OperationKind::Slice && !script.sliced && script.steps.empty()) {
            script.sliced = true;
            script.slice = argument;
            continue;
        }
        script.steps.push_back({kind, argument});
    }
    return script;
}

QString writeScript(const Script& script, ScriptLayout layout)
{
    const bool lines = layout == ScriptLayout::Lines;
    const auto step = [lines](const QString& name, const QString& argument) {
        return argument.isEmpty() && lines ? QStringLiteral(".%1").arg(name)
                                           : QStringLiteral(".%1(%2)").arg(name, argument);
    };

    QStringList parts{script.path};
    if (!script.member.isEmpty()) {
        // Written without the chain's own leading dot: `.select(samples)`
        // rather than `.select(.samples)`, which is two dots for one idea.
        parts << step(kSelect, script.member.startsWith(u'.') ? script.member.mid(1)
                                                              : script.member);
    }
    if (script.sliced) {
        // Always with its parentheses, even empty: `.slice` alone would read
        // as a slice of nothing rather than of everything.
        parts << QStringLiteral(".slice(%1)").arg(script.slice);
    }
    for (const Step& written : script.steps) {
        parts << step(operationInfo(written.kind).name, written.argument);
    }
    return parts.join(lines ? QStringLiteral("\n") : QString{});
}

std::vector<Step> pipelineOf(const Script& script, const QStringList& folded,
                             std::size_t originRank)
{
    // Only a chain that carries subscripts of its own is the short spelling.
    // One that carries none leaves the slice over the whole derived shape --
    // which is what the panel's slice row holds, and why `.select(samples)`
    // with `.slice(0:5, 2)` is two rows of it written down. Handing that to
    // sliceLineFor would append the chain's axes a second time.
    const bool carried = std::any_of(folded.begin(), folded.end(),
                                     [](const QString& term) { return !term.trimmed().isEmpty(); });
    std::vector<Step> steps;
    steps.reserve(script.steps.size() + 1);
    steps.push_back({OperationKind::Slice, carried
                                               ? sliceLineFor(script.slice, folded, originRank)
                                               : script.slice});
    steps.insert(steps.end(), script.steps.begin(), script.steps.end());
    return steps;
}

ScriptCheck checkScript(const Script& script, const std::vector<hsize_t>& originShape,
                        const h5core::TypeInfo& type)
{
    ScriptCheck check;
    if (!script.ok()) {
        check.error = script.error;
        return check;
    }

    check.chain = resolveMemberChain(script.member, type);
    if (!check.chain.valid()) {
        check.error = QStringLiteral(".select(%1): %2")
                          .arg(script.member.mid(1), check.chain.error);
        return check;
    }
    // The shapes before the question of numbers, so that a caller applying a
    // script it has been told cannot run -- the panel does, and says why beside
    // it -- still has the slice line to apply.
    check.input = originShape;
    check.input.insert(check.input.end(), check.chain.selection.dims.begin(),
                       check.chain.selection.dims.end());
    check.pipeline = pipelineOf(script, check.chain.folded, originShape.size());

    const h5core::TypeInfo& landed = check.chain.selection.type;
    if (!h5core::isNumeric(landed.cls)) {
        check.error = landed.cls == h5core::TypeClass::Compound
                          ? QStringLiteral("this holds structs, and there is no "
                                           "arithmetic for a struct — name one of "
                                           "its members with .select(...)")
                          : QStringLiteral("postprocessing works on numbers, and "
                                           "this holds %1")
                                .arg(QString::fromStdString(landed.description));
        return check;
    }

    const Trace walked = trace(check.input, check.pipeline, check.pipeline.size());
    check.output = walked.output;
    if (!walked.ok()) {
        // Said against the step that gave it, because in a box of seven lines
        // "axis 3 is out of bounds" does not say which of the three reductions
        // it is about.
        const std::size_t stage = walked.ran;
        const QString where = stage == 0 ? QStringLiteral(".slice(%1)").arg(script.slice)
                                         : writeStep(script.steps[stage - 1]);
        check.error = QStringLiteral("%1: %2").arg(where, walked.error);
    }
    return check;
}

} // namespace postproc

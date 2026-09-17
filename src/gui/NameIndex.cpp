// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "NameIndex.hpp"

#include "H5Session.hpp"
#include "h5core/File.hpp"

#include <QHashFunctions>

#include <algorithm>
#include <string>
#include <utility>

namespace gui {
namespace {

/// One character, case-folded.
///
/// Inline, with ASCII done in two comparisons and everything else handed to
/// Qt. That split is not premature: this is the innermost operation of the
/// whole search -- nine million characters per keystroke on a large file -- and
/// `QChar::toCaseFolded` is an out-of-line call into QtCore that no inliner
/// here can see through. Leaving it to Qt for every character measured at
/// 23 ns each, which is 230 ms a keystroke; the fast path is about fifteen
/// times that. Names outside ASCII fold exactly as they did before, because
/// they still go to the same function.
[[nodiscard]] inline char16_t fold(char16_t c)
{
    if (c < 0x80) {
        return (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c + 0x20) : c;
    }
    return QChar(c).toCaseFolded().unicode();
}

[[nodiscard]] inline bool sameLetter(char16_t a, char16_t b)
{
    return a == b || fold(a) == fold(b);
}

/// Whether `text` holds `needle` anywhere, with `needle` already folded.
///
/// Hand-written rather than `QStringView::indexOf(..., Qt::CaseInsensitive)`
/// for the reason above it: Qt's folds both sides through QtCore on every
/// comparison, and this is called once per name in the file per keystroke.
[[nodiscard]] bool foldedContains(QStringView text, QStringView needle)
{
    const qsizetype wanted = needle.size();
    if (wanted == 0) {
        return true;
    }
    if (text.size() < wanted) {
        return false;
    }
    const char16_t* hay = reinterpret_cast<const char16_t*>(text.utf16());
    const char16_t* want = reinterpret_cast<const char16_t*>(needle.utf16());
    const qsizetype last = text.size() - wanted;
    for (qsizetype i = 0; i <= last; ++i) {
        if (fold(hay[i]) != want[0]) {
            continue;
        }
        qsizetype j = 1;
        while (j < wanted && fold(hay[i + j]) == want[j]) {
            ++j;
        }
        if (j == wanted) {
            return true;
        }
    }
    return false;
}

/// The same, reporting where it bit. -1 when it did not.
[[nodiscard]] qsizetype foldedIndexOf(QStringView text, QStringView needle)
{
    const qsizetype wanted = needle.size();
    if (wanted == 0 || text.size() < wanted) {
        return wanted == 0 ? 0 : -1;
    }
    const char16_t* hay = reinterpret_cast<const char16_t*>(text.utf16());
    const char16_t* want = reinterpret_cast<const char16_t*>(needle.utf16());
    const qsizetype last = text.size() - wanted;
    for (qsizetype i = 0; i <= last; ++i) {
        qsizetype j = 0;
        while (j < wanted && fold(hay[i + j]) == want[j]) {
            ++j;
        }
        if (j == wanted) {
            return i;
        }
    }
    return -1;
}

/// Match one `[...]` class against `letter`, starting at the `[`.
///
/// `after` is left just past the `]`. Returns false with `after` untouched when
/// the class does not close, which is a pattern halfway to being typed rather
/// than a pattern that matches nothing -- see NameQuery, which reads the whole
/// text as plain text in that case.
[[nodiscard]] bool classMatches(QStringView pattern, qsizetype at, char16_t letter,
                                qsizetype& after, bool& closed)
{
    closed = false;
    qsizetype i = at + 1;
    bool negate = false;
    if (i < pattern.size() && (pattern[i] == u'!' || pattern[i] == u'^')) {
        negate = true;
        ++i;
    }
    bool found = false;
    // A `]` immediately after the bracket is a literal `]`, which is the one
    // rule that keeps `[]]` from being an unclosed class. Both glob and PCRE
    // read it that way.
    bool first = true;
    for (; i < pattern.size(); ++i) {
        if (pattern[i] == u']' && !first) {
            closed = true;
            after = i + 1;
            return found != negate;
        }
        first = false;
        const char16_t low = pattern[i].unicode();
        if (i + 2 < pattern.size() && pattern[i + 1] == u'-' && pattern[i + 2] != u']') {
            const char16_t high = pattern[i + 2].unicode();
            const char16_t folded = QChar::toCaseFolded(letter);
            found = found
                    || (letter >= low && letter <= high)
                    || (folded >= QChar::toCaseFolded(low)
                        && folded <= QChar::toCaseFolded(high));
            i += 2;
            continue;
        }
        found = found || sameLetter(low, letter);
    }
    return false;
}

/// Whether `text` holds a `[` that opens a class and closes it again. Decides
/// whether the text is a pattern at all; see NameQuery.
[[nodiscard]] bool classesClose(QStringView text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] != u'[') {
            continue;
        }
        qsizetype after = 0;
        bool closed = false;
        (void)classMatches(text, i, u'x', after, closed);
        if (!closed) {
            return false;
        }
        i = after - 1;
    }
    return true;
}

/// The last segment of an absolute path -- what the tree draws as the row's
/// name.
[[nodiscard]] QStringView lastSegment(QStringView path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    if (slash < 0 || path.size() == 1) {
        return path;
    }
    return path.sliced(slash + 1);
}

/// An object's identity, as one number, purely to notice a group the walk has
/// already been through. See h5core::NodeInfo: the pair is what identifies an
/// object across the files an external link can reach.
[[nodiscard]] std::uint64_t identityOf(const h5core::NodeInfo& node)
{
    const auto file = static_cast<std::uint64_t>(node.fileNumber.value_or(0));
    const auto address = static_cast<std::uint64_t>(node.address.value_or(0));
    return address * 0x9E3779B97F4A7C15ULL ^ (file + 0x165667B19E3779F9ULL);
}

} // namespace

// --- the grammar -----------------------------------------------------------

bool matchesWildcard(QStringView pattern, QStringView text)
{
    // The classic back-tracking glob: walk both, and on a mismatch fall back to
    // the last `*` having eaten one more character. Linear on every pattern
    // anyone types, because the common case is a mismatch before the first `*`
    // is ever reached.
    qsizetype p = 0;
    qsizetype t = 0;
    qsizetype star = -1;
    qsizetype eaten = 0;

    while (t < text.size()) {
        if (p < pattern.size()) {
            const char16_t token = pattern[p].unicode();
            if (token == u'*') {
                star = ++p;
                eaten = t;
                continue;
            }
            if (token == u'[') {
                qsizetype after = 0;
                bool closed = false;
                const bool took = classMatches(pattern, p, text[t].unicode(), after, closed);
                if (closed) {
                    if (took) {
                        p = after;
                        ++t;
                        continue;
                    }
                    // A class that closed and did not take is an ordinary
                    // mismatch: fall through to the backtrack below.
                    if (star < 0) {
                        return false;
                    }
                    p = star;
                    t = ++eaten;
                    continue;
                }
                // Unclosed: literal, which is what NameQuery has already
                // decided by refusing to call this at all. Kept so the
                // function is total.
            }
            if (token == u'?' || sameLetter(token, text[t].unicode())) {
                ++p;
                ++t;
                continue;
            }
        }
        if (star < 0) {
            return false;
        }
        p = star;
        t = ++eaten;
    }

    while (p < pattern.size() && pattern[p] == u'*') {
        ++p;
    }
    return p == pattern.size();
}

NameQuery::NameQuery(QString text) : text_(std::move(text))
{
    const bool looksLikeOne = text_.contains(u'*') || text_.contains(u'?')
                              || text_.contains(u'[');
    wildcard_ = looksLikeOne && classesClose(text_);
    // Folded once here rather than per character of every name: the text side
    // still has to be folded as it is read, but the needle never changes
    // between keystrokes and there is no reason to fold it again per name.
    folded_.resize(text_.size());
    for (qsizetype i = 0; i < text_.size(); ++i) {
        folded_[i] = QChar(fold(text_.at(i).unicode()));
    }
}

bool NameQuery::acceptsPath(QStringView path) const
{
    if (text_.isEmpty()) {
        return true;
    }
    if (!wildcard_) {
        // Plain text is unanchored, so a substring of the path subsumes a
        // substring of the name -- the name *is* the tail of the path. One
        // search rather than two, which halves the work per name on the
        // commonest thing anyone types.
        return foldedContains(path, folded_);
    }
    // The pattern is anchored, so the name is tried on its own: `temp*` is
    // about names, and `/run/?/temp*` is about the path it is written as.
    return matchesWildcard(text_, lastSegment(path)) || matchesWildcard(text_, path);
}

bool NameQuery::accepts(QStringView name, QStringView path) const
{
    if (text_.isEmpty()) {
        return true;
    }
    if (!wildcard_) {
        return foldedContains(path, folded_);
    }
    return matchesWildcard(text_, name) || matchesWildcard(text_, path);
}

std::pair<int, int> NameQuery::markIn(QStringView name) const
{
    if (text_.isEmpty()) {
        return {-1, 0};
    }
    if (wildcard_) {
        return matchesWildcard(text_, name) ? std::pair{0, static_cast<int>(name.size())}
                                            : std::pair{-1, 0};
    }
    const qsizetype at = foldedIndexOf(name, folded_);
    if (at < 0) {
        return {-1, 0};
    }
    return {static_cast<int>(at), static_cast<int>(text_.size())};
}

// --- the index -------------------------------------------------------------

NameIndex::NameIndex(QObject* parent) : QObject(parent) {}

NameIndex::~NameIndex() = default;

void NameIndex::close()
{
    requests_.reset();
    names_.clear();
    names_.squeeze();
    entries_.clear();
    entries_.shrink_to_fit();
    marks_.clear();
    marks_.shrink_to_fit();
    buckets_.clear();
    buckets_.shrink_to_fit();
    frontier_.clear();
    frontier_.shrink_to_fit();
    frontierAt_ = 0;
    hits_ = 0;
    complete_ = false;
    truncated_ = false;
    walking_ = false;
}

void NameIndex::open()
{
    close();
    append(-1, "/", true, true, 0);
    frontier_.push_back(0);
    requestPass();
}

void NameIndex::append(int parent, const std::string& path, bool group, bool opaque,
                       std::uint64_t identity)
{
    const QString text = QString::fromStdString(path);
    Entry entry;
    entry.at = static_cast<qint32>(names_.size());
    entry.length = static_cast<qint32>(text.size());
    entry.parent = static_cast<qint32>(parent);
    entry.group = group;
    entry.identity = identity;
    // A group speaks for nothing below it until its own listing has landed.
    entry.opaque = group && opaque;
    names_ += text;
    entries_.push_back(entry);
    marks_.push_back(NoMark);
    insert(static_cast<int>(entries_.size()) - 1);
}

QStringView NameIndex::pathOf(int entry) const
{
    const Entry& e = entries_[static_cast<std::size_t>(entry)];
    return QStringView{names_}.sliced(e.at, e.length);
}

QStringView NameIndex::nameOf(int entry) const { return lastSegment(pathOf(entry)); }

bool NameIndex::encloses(int entry, std::uint64_t identity) const
{
    if (identity == 0) {
        return false;
    }
    for (int above = entry; above >= 0;
         above = entries_[static_cast<std::size_t>(above)].parent) {
        if (entries_[static_cast<std::size_t>(above)].identity == identity) {
            return true;
        }
    }
    return false;
}

// --- the hash --------------------------------------------------------------

void NameIndex::rehash(std::size_t wanted)
{
    std::size_t size = 1024;
    while (size * 7 < wanted * 10) {
        size *= 2;
    }
    buckets_.assign(size, -1);
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const std::size_t mask = size - 1;
        std::size_t at = qHash(pathOf(i), 0) & mask;
        while (buckets_[at] >= 0) {
            at = (at + 1) & mask;
        }
        buckets_[at] = i;
    }
}

void NameIndex::insert(int entry)
{
    if (buckets_.empty() || (entries_.size() + 1) * 10 > buckets_.size() * 7) {
        rehash(entries_.size() * 2 + 1024);
        return; // rehash() has just placed everything, this one included
    }
    const std::size_t mask = buckets_.size() - 1;
    std::size_t at = qHash(pathOf(entry), 0) & mask;
    while (buckets_[at] >= 0) {
        at = (at + 1) & mask;
    }
    buckets_[at] = entry;
}

int NameIndex::find(QStringView path) const
{
    if (buckets_.empty()) {
        return -1;
    }
    const std::size_t mask = buckets_.size() - 1;
    std::size_t at = qHash(path, 0) & mask;
    while (buckets_[at] >= 0) {
        const int entry = buckets_[at];
        if (pathOf(entry) == path) {
            return entry;
        }
        at = (at + 1) & mask;
    }
    return -1;
}

// --- the walk --------------------------------------------------------------

void NameIndex::requestPass()
{
    if (walking_ || complete_) {
        return;
    }
    if (frontierAt_ >= frontier_.size()) {
        complete_ = true;
        emit grew();
        return;
    }
    if (static_cast<int>(entries_.size()) >= kMaxNames) {
        // Give up rather than go on: every group still in the queue keeps the
        // Opaque it was appended with, so the filter falls back to walking what
        // the model has read for those branches instead of believing an index
        // that stops halfway.
        truncated_ = true;
        complete_ = true;
        emit grew();
        return;
    }

    // What this pass will list, as paths: the job runs on the other thread and
    // may not read anything of ours, entries_ included.
    std::vector<std::pair<qint32, std::string>> asking;
    const std::size_t until =
        std::min(frontier_.size(), frontierAt_ + static_cast<std::size_t>(kGroupsPerPass));
    asking.reserve(until - frontierAt_);
    for (std::size_t i = frontierAt_; i < until; ++i) {
        const qint32 entry = frontier_[i];
        asking.emplace_back(entry, pathOf(entry).toString().toStdString());
    }
    frontierAt_ = until;
    walking_ = true;

    struct Listed {
        qint32 parent = -1;
        std::vector<h5core::NodeInfo> children;
    };

    H5Thread::instance().submit(
        requests_,
        [asking = std::move(asking)](H5Session& session) {
            std::vector<Listed> listed;
            listed.reserve(asking.size());
            h5core::File* file = session.file();
            if (file == nullptr) {
                return listed;
            }
            for (const auto& [entry, path] : asking) {
                Listed one;
                one.parent = entry;
                try {
                    // Resolve::Objects rather than Links: the kind is what says
                    // whether there is anything below a name, and asking for it
                    // here is one object-header read per name against the two a
                    // listing plus a member count would be. Measured over three
                    // hundred thousand objects it is the difference between one
                    // second and two and a quarter.
                    one.children = file->children(path, h5core::File::Resolve::Objects);
                } catch (const std::exception&) {
                    // A group that will not list is a state of the file. The
                    // entry keeps its Opaque and the filter looks for itself.
                    continue;
                }
                listed.push_back(std::move(one));
            }
            return listed;
        },
        [this](std::vector<Listed> listed) {
            walking_ = false;
            const int before = static_cast<int>(entries_.size());
            for (const Listed& one : listed) {
                entries_[static_cast<std::size_t>(one.parent)].opaque = false;
                for (const h5core::NodeInfo& child : one.children) {
                    const bool group = child.kind == h5core::NodeKind::Group;
                    const std::uint64_t identity = identityOf(child);
                    const bool loop = group && encloses(one.parent, identity);
                    append(one.parent, child.path, group, true, identity);
                    if (group && !loop) {
                        frontier_.push_back(static_cast<qint32>(entries_.size()) - 1);
                    }
                }
            }
            if (!query_.isEmpty()) {
                markFrom(before);
            }
            emit grew();
            requestPass();
        });
}

// --- what is on screen -----------------------------------------------------

void NameIndex::select(const NameQuery& query)
{
    query_ = query;
    marks_.assign(entries_.size(), NoMark);
    hits_ = 0;
    if (query_.isEmpty()) {
        return;
    }
    markFrom(0);
}

void NameIndex::markUp(int entry)
{
    // Stops at the first branch already spoken for, which is what keeps the
    // whole pass linear however many names match: every entry is written at
    // most once as Above.
    int above = entries_[static_cast<std::size_t>(entry)].parent;
    while (above >= 0 && marks_[static_cast<std::size_t>(above)] == NoMark) {
        marks_[static_cast<std::size_t>(above)] = Above;
        above = entries_[static_cast<std::size_t>(above)].parent;
    }
}

void NameIndex::markFrom(int from)
{
    marks_.resize(entries_.size(), NoMark);
    const int count = static_cast<int>(entries_.size());
    for (int i = from; i < count; ++i) {
        if (!query_.acceptsPath(pathOf(i))) {
            continue;
        }
        marks_[static_cast<std::size_t>(i)] = Hit;
        ++hits_;
        markUp(i);
    }
}

NameIndex::Answer NameIndex::answer(QStringView path) const
{
    if (query_.isEmpty() || entries_.empty()) {
        return Answer::Unknown;
    }
    const int at = find(path);
    if (at < 0) {
        return Answer::Unknown;
    }
    if (marks_[static_cast<std::size_t>(at)] != NoMark) {
        return Answer::Yes;
    }
    const Entry& entry = entries_[static_cast<std::size_t>(at)];
    if (entry.opaque) {
        return Answer::Unknown;
    }
    // A group whose own listing has landed still has groups under it that the
    // walk has not reached, and each of those is Opaque without this one being
    // -- so "nothing here matches" is only true of a group once the whole walk
    // is in. Until then the filter looks for itself, which is what it did
    // before this class existed. Datasets are exempt: nothing is below one.
    if (entry.group && !complete_) {
        return Answer::Unknown;
    }
    return Answer::No;
}

QStringList NameIndex::topHits(int limit) const
{
    QStringList found;
    if (query_.isEmpty()) {
        return found;
    }
    const int count = static_cast<int>(entries_.size());
    for (int i = 0; i < count; ++i) {
        if (marks_[static_cast<std::size_t>(i)] != Hit) {
            continue;
        }
        bool topmost = true;
        for (int above = entries_[static_cast<std::size_t>(i)].parent; above >= 0;
             above = entries_[static_cast<std::size_t>(above)].parent) {
            if (marks_[static_cast<std::size_t>(above)] == Hit) {
                topmost = false;
                break;
            }
        }
        if (!topmost) {
            continue;
        }
        found.append(pathOf(i).toString());
        if (found.size() > limit) {
            // One past the bound, so the caller can tell "more than this" from
            // "exactly this" without being handed a quarter of a million rows.
            return found;
        }
    }
    return found;
}

} // namespace gui

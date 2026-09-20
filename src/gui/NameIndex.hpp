// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "H5Thread.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstdint>
#include <utility>
#include <vector>

namespace gui {

/// Whether the whole of `text` matches the wildcard `pattern`, ignoring case.
///
/// `*` stands for any run of characters *including* `/`, `?` for exactly one,
/// and `[...]` for a class -- `[abc]`, `[a-z]`, `[!abc]` to negate. Everything
/// else is itself. That is the grammar `QRegularExpression::fromWildcard` gives
/// with NonPathWildcardConversion, and `tests/test_models.cpp` holds the two
/// against each other over a table of patterns so the parity is asserted rather
/// than claimed.
///
/// Written out by hand because this is called once per name in the file on
/// every keystroke, and PCRE2 is about a microsecond a call -- three hundred
/// thousand names is then a third of a second per character typed, which is
/// exactly the thing the search index exists to remove. A back-tracking glob is
/// two orders of magnitude cheaper on the patterns people write, because almost
/// every one of them fails on the first character and never reaches a `*`.
[[nodiscard]] bool matchesWildcard(QStringView pattern, QStringView text);

/// What was typed in the filter box, in the form that answers fastest.
///
/// Two grammars, and which one is in use is decided by what was typed rather
/// than by a mode the reader has to set:
///
///   plain text   matched anywhere in the name or the path, case-insensitively.
///                `temp` finds `/run/3/temperature`.
///   a wildcard   `*`, `?` or a `[...]` class anywhere in the text makes it a
///                pattern, matched against the whole of the name and the whole
///                of the path. `temp*` is every name that begins with temp;
///                `*.raw` every one that ends in it; `/run/?/temp*` reaches
///                into the path itself.
///
/// A pattern is anchored and plain text is not, which sounds inconsistent and
/// is the only reading that makes both useful: the whole point of writing
/// `temp*` rather than `temp` is to say *begins with*, and a `*` that only ever
/// added to an unanchored substring search would mean nothing at all.
///
/// One class rather than a function per caller, because the filter proxy and
/// the search index both have to answer the same question and a row shown by
/// one and not collected by the other is a row the reader cannot reach.
class NameQuery
{
public:
    NameQuery() = default;
    explicit NameQuery(QString text);

    [[nodiscard]] bool isEmpty() const { return text_.isEmpty(); }
    [[nodiscard]] const QString& text() const { return text_; }
    /// Whether this was read as a pattern. False for a pattern that does not
    /// close its `[` -- half a typed class is not an empty tree, it is a
    /// substring search that goes on answering while the rest is written.
    [[nodiscard]] bool isWildcard() const { return wildcard_; }

    /// Whether the filter takes a row. `name` must be `path`'s last segment;
    /// they are passed separately because the caller already has both and
    /// finding one in the other is the only work this would otherwise do.
    [[nodiscard]] bool accepts(QStringView name, QStringView path) const;
    /// The same, when only the path is in hand. The name is its last segment
    /// and is taken only if the grammar in force needs one, which plain text
    /// does not.
    [[nodiscard]] bool acceptsPath(QStringView path) const;

    /// Where the filter bit into `name`: `{ start, length }`, start -1 when it
    /// did not. A pattern takes the whole name or none of it, so there is no
    /// shorter run to report and no arithmetic to do.
    [[nodiscard]] std::pair<int, int> markIn(QStringView name) const;

    [[nodiscard]] bool operator==(const NameQuery& other) const
    {
        return text_ == other.text_;
    }

private:
    /// One string against the pattern, whichever way it is matched.
    [[nodiscard]] bool matches(QStringView text) const;

    QString text_;
    /// `text_`, case-folded, so that only the side that changes has to be.
    QString folded_;
    bool wildcard_ = false;

    // --- the fast reading of a pattern ------------------------------------
    // A glob of nothing but stars and letters is not a walk over characters at
    // all: it is "these pieces, in this order", with the ends pinned wherever
    // the pattern has no star. Every piece is then one scan of the folded text
    // -- the same scan plain text already gets -- instead of a back-tracking
    // walk that starts again at every offset of every name.
    //
    // It is worth the members because of what it measured. On a file of
    // 188,000 names a plain substring took 19 ms a keystroke and *any* pattern
    // opening with a star took 210-260 ms, whether it matched everything or
    // nothing: `*item*zz*` matches not one name in that file and cost 256 ms,
    // because the cost is the back-tracking and not the hits. A reader typing
    // an eight-character wildcard paid that eight times.
    //
    // `?` and `[...]` still need the general matcher, and still have it.
    /// The longest run of plain letters anywhere in the pattern, folded.
    ///
    /// A necessary condition, for the patterns the fast reading below cannot
    /// take: whatever `?` and `[...]` do, the letters around them have to be in
    /// the text somewhere. One scan of the folded text answers it, and a name
    /// that fails never reaches the back-tracking walk at all.
    QString required_;
    /// The literal runs between the stars, folded, in order. Empty unless
    /// `simple_`.
    std::vector<QString> pieces_;
    /// Whether the pattern is stars and literals alone, so `pieces_` is the
    /// whole of it.
    bool simple_ = false;
    /// Whether the pattern pins that end: `temp*` pins the start, `*.raw` the
    /// end, `temp` -- were it a pattern -- both.
    bool anchoredStart_ = false;
    bool anchoredEnd_ = false;
};

/// Every name in the open file, held in one block of memory so that the filter
/// box answers out of RAM.
///
/// The tree is lazy on purpose -- a file can hold a million objects, and a
/// group that is not on screen is not read. That laziness used to reach the
/// filter too: it matched what the reader had already expanded, which made a
/// search over a file nobody had walked find nothing, and made a search over
/// one somebody *had* walked cost a recursive pass over the model with two
/// QString conversions and two PCRE2 matches per node. Three hundred thousand
/// objects measured at 160-330 ms per keystroke, which is not a filter box, it
/// is a progress bar with an edit cursor in it.
///
/// So: **the tree stays lazy and the search does not.** The names are read once
/// per file, in the background, and after that a keystroke is a linear pass
/// over contiguous memory. A name is small -- three hundred thousand paths is
/// eighteen megabytes -- and it is the one thing about a file that a reader may
/// want to search the whole of without having looked at any of it.
///
/// Three properties are worth knowing before changing anything here:
///
///  1. **The walk is cut into jobs.** `H5Thread`'s queue is strictly ordered
///     and there is exactly one of it, so a single walk of a large file would
///     put every listing the reader asks for behind a second of indexing. Each
///     pass lists a bounded number of groups and re-arms itself at the *back*
///     of the queue, which is what lets the tree go on answering clicks while
///     this fills in behind it.
///  2. **It answers before it is finished.** Marks are computed for whatever
///     has arrived, so a search made a moment after the file opened is
///     answered from what is indexed and widens as the rest lands. Nothing
///     waits.
///  3. **It says when it cannot say.** A group it declined to descend into --
///     a hard link back to somewhere it has been, or anything past the cap --
///     is `Opaque`, and `answer()` reports `Unknown` for it. The filter then
///     falls back to walking what the model has read, which is exactly what it
///     did before this class existed.
class NameIndex : public QObject
{
    Q_OBJECT

public:
    /// What the index has to say about a path and everything under it.
    enum class Answer {
        /// This path matches, or something below it does.
        Yes,
        /// Neither it nor anything below it does, and the index has seen all
        /// of it.
        No,
        /// The index cannot say: the walk has not reached here, or it stopped
        /// here. The caller has to look for itself.
        Unknown,
    };

    explicit NameIndex(QObject* parent = nullptr);
    ~NameIndex() override;

    /// Start walking whatever `H5Thread`'s session has open, from scratch.
    void open();
    /// Forget everything and stop walking. Replies in flight are disowned.
    void close();

    /// How many names are in it so far.
    [[nodiscard]] int count() const { return static_cast<int>(entries_.size()); }
    /// Whether the walk has finished -- including by giving up at the cap.
    [[nodiscard]] bool complete() const { return complete_; }
    /// Whether it gave up: the file holds more names than `kMaxNames`.
    [[nodiscard]] bool truncated() const { return truncated_; }

    /// Mark every name against `query`. Cheap to call with the query already
    /// in force; that is what a growing index does on every pass.
    void select(const NameQuery& query);
    [[nodiscard]] const NameQuery& query() const { return query_; }

    /// What the index has to say about `path`. `Unknown` unless a query is in
    /// force.
    [[nodiscard]] Answer answer(QStringView path) const;

    /// How many names the query itself took, as opposed to the branches above
    /// them. Zero when nothing is typed.
    [[nodiscard]] int hits() const { return hits_; }

    /// The topmost hits, as absolute paths: the ones with no matching ancestor,
    /// which is what a reader means by "the results". Bounded by `limit`, and
    /// empty past it -- see `kRevealLimit` in TreeFilterProxyModel for what
    /// that bound is for.
    [[nodiscard]] QStringList topHits(int limit) const;

signals:
    /// More names have arrived, or the walk has finished. The filter re-runs
    /// itself on this, because a branch that had nothing in it a moment ago
    /// may have something in it now.
    void grew();

private:
    /// One name. Sixteen bytes, and there are as many of these as the file has
    /// objects, which is why none of them is a QString: the paths live end to
    /// end in `names_` and this is where each one starts.
    struct Entry {
        qint32 at = 0;      ///< index into names_
        qint32 length = 0;  ///< characters, the whole absolute path
        qint32 parent = -1; ///< the entry above, or -1 at the root
        /// Nothing below this is indexed, and the filter must look for itself.
        bool opaque = false;
        /// It is a group, so the walk has somewhere to go from here.
        bool group = false;
        /// What the object is, so that a group repeating an *ancestor* can be
        /// told from a group merely named twice. Zero for anything that does
        /// not resolve.
        std::uint64_t identity = 0;
    };

    enum Mark : quint8 { NoMark = 0, Hit = 1, Above = 2 };

    /// Groups per pass. Enough that a file of ordinary size is indexed in one
    /// or two of them, few enough that the listing a click asks for is never
    /// behind more than this much work. A group's own width is not bounded
    /// here -- `/flat` with eight thousand members is one listing whatever
    /// this says -- which is fine: one wide listing is one seek, and it is the
    /// same one the tree would pay to expand it.
    static constexpr int kGroupsPerPass = 512;

    /// Where the index gives up. Eighteen megabytes of names at three hundred
    /// thousand objects, so this is about a hundred and twenty megabytes --
    /// past what any reader is searching by eye, and far past where a filter
    /// box is the right tool. The walk stops, `truncated()` says so, and every
    /// group it never reached is Opaque, which puts the filter back on the
    /// lazy behaviour it had before rather than leaving it wrong.
    static constexpr int kMaxNames = 2'000'000;

    void requestPass();
    /// Add one child of `parent`.
    void append(int parent, const std::string& path, bool group, bool opaque,
                std::uint64_t identity);
    /// Whether `identity` is one of `entry`'s own ancestors -- which is a loop,
    /// and the one thing the walk must not follow.
    ///
    /// An ancestor rather than anything seen before, because two hard links to
    /// one group are not a loop: `/aliases/alias_0000` and `/runs/run_0000` are
    /// the same object under two names and the tree shows the contents of both,
    /// so an index that walked only the first would have nothing to say about
    /// the second and would hand the filter an Opaque where an answer was
    /// available. Loops are rare and shallow; this walk is a handful of
    /// comparisons up the path.
    [[nodiscard]] bool encloses(int entry, std::uint64_t identity) const;
    /// Mark entries `from`..end against the query, and the branches above them.
    void markFrom(int from);
    void markUp(int entry);
    [[nodiscard]] QStringView pathOf(int entry) const;
    [[nodiscard]] QStringView nameOf(int entry) const;
    [[nodiscard]] int find(QStringView path) const;
    void insert(int entry);
    void rehash(std::size_t wanted);

    /// Every path, end to end. One allocation that doubles a handful of times
    /// rather than one per name: the per-QString overhead alone would be more
    /// than the characters at these counts.
    QString names_;
    std::vector<Entry> entries_;
    std::vector<quint8> marks_;
    /// Open addressing, power of two, -1 for empty. A QHash of QString keys
    /// would cost more in nodes than the names cost in characters.
    std::vector<qint32> buckets_;

    /// Groups listed but not yet walked into, and how far along it the walk
    /// has got. A queue rather than a stack, so the file is indexed a level at
    /// a time: a reader who types into the box a moment after opening is
    /// searching the top of the file, which is where the names they can name
    /// from memory are.
    std::vector<qint32> frontier_;
    std::size_t frontierAt_ = 0;
    NameQuery query_;
    int hits_ = 0;
    bool complete_ = false;
    bool truncated_ = false;
    bool walking_ = false;
    H5Requests requests_;
};

} // namespace gui

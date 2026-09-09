// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/DataSource.hpp"
#include "h5core/Types.hpp"

#include <hdf5.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <format>
#include <functional>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

namespace h5test {

/// A DataSource that answers like a dataset and writes down what it was asked.
///
/// The views read through `h5core::DataSource`, and `gui::H5Session` will hold
/// any implementation of it -- that is what postprocessing already uses. So one
/// of these installed as the session's source puts the real table, plot and
/// image over a file whose every read is counted, with no timing in the
/// assertion and no large file on disk.
///
/// What it is for is the half of the cost the file does not decide. How many
/// *elements* a view reads is bounded by how much of the dataset was asked for
/// and is easy to reason about; how many *reads* it takes to fetch them, and
/// how many crossings of the HDF5 thread those reads arrive in, are bounded by
/// nothing but the code. Those are the numbers that regress silently -- a loop
/// that asks per row rather than per screenful moves exactly the same bytes --
/// and they are what tests/test_cost.cpp asserts on.
///
/// Values are a cheap function of the index rather than stored, so a source of
/// any shape costs nothing to make: `value(index) == the row-major position`,
/// which makes an assertion about *which* elements arrived as easy to write as
/// one about how many.
class CountingSource : public h5core::DataSource
{
public:
    /// One read, as it was asked for.
    struct Call {
        enum class Kind { Window, Numeric, Element };
        Kind kind = Kind::Numeric;
        std::vector<hsize_t> offset;
        std::vector<hsize_t> count;

        [[nodiscard]] hsize_t elements() const
        {
            return std::accumulate(count.begin(), count.end(),
                                   static_cast<hsize_t>(1), std::multiplies<>{});
        }
    };

    explicit CountingSource(std::vector<hsize_t> shape,
                            h5core::TypeClass cls = h5core::TypeClass::Float)
    {
        info_.shape = std::move(shape);
        info_.maxShape = info_.shape;
        info_.space = info_.shape.empty() ? h5core::Dataspace::Scalar
                                          : h5core::Dataspace::Simple;
        info_.layout = h5core::Layout::Contiguous;
        info_.type.cls = cls;
        info_.type.size = sizeof(double);
        info_.type.isSigned = true;
        info_.type.convertible = true;
        info_.type.description =
            cls == h5core::TypeClass::Float ? "float64" : "counted";
    }

    // --- what the source is ------------------------------------------------

    [[nodiscard]] const h5core::DatasetInfo& info() const noexcept override
    {
        return info_;
    }
    [[nodiscard]] const std::string& path() const noexcept override { return path_; }

    /// Editable before it is installed, for the cases that need a datatype the
    /// views treat differently -- a string dataset has no numbers to plot.
    [[nodiscard]] h5core::DatasetInfo& mutableInfo() noexcept { return info_; }

    // --- what it was asked --------------------------------------------------

    /// Every read since the last reset(), in order.
    [[nodiscard]] std::vector<Call> calls() const
    {
        const std::lock_guard<std::mutex> held(mutex_);
        return calls_;
    }

    /// Reads of any kind. The blunt number, and usually the one that matters.
    [[nodiscard]] int reads() const { return reads_.load(); }
    [[nodiscard]] int windows() const { return windows_.load(); }
    [[nodiscard]] int numerics() const { return numerics_.load(); }
    [[nodiscard]] int elementReads() const { return elementReads_.load(); }

    /// Elements handed back across every read. Separate from the read count on
    /// purpose: the two move independently, and a change that halves one while
    /// doubling the other is a change worth seeing rather than a wash.
    [[nodiscard]] hsize_t elementsRead() const { return elementsRead_.load(); }

    /// The largest single read, in elements. What a block-at-a-time reader
    /// promises never to exceed.
    [[nodiscard]] hsize_t largestRead() const { return largestRead_.load(); }

    void reset()
    {
        const std::lock_guard<std::mutex> held(mutex_);
        calls_.clear();
        reads_ = 0;
        windows_ = 0;
        numerics_ = 0;
        elementReads_ = 0;
        elementsRead_ = 0;
        largestRead_ = 0;
    }

    /// The value this source answers for a full index tuple: its row-major
    /// position. Lets a test say which element it expected as arithmetic
    /// rather than as a table of numbers.
    [[nodiscard]] double value(const std::vector<hsize_t>& index) const
    {
        hsize_t position = 0;
        for (std::size_t d = 0; d < info_.shape.size(); ++d) {
            position *= info_.shape[d];
            position += (d < index.size()) ? index[d] : 0;
        }
        return static_cast<double>(position);
    }

    // --- the reads themselves ----------------------------------------------

    [[nodiscard]] h5core::DataWindow
    readWindow(const std::vector<hsize_t>& offset,
               const std::vector<hsize_t>& count) const override
    {
        const std::vector<hsize_t> clamped = clamp(offset, count);
        record(Call{Call::Kind::Window, offset, clamped});

        h5core::DataWindow window;
        window.offset = offset;
        window.count = clamped;
        forEach(offset, clamped, [&](const std::vector<hsize_t>& at) {
            window.cells.push_back(std::format("{}", value(at)));
        });
        return window;
    }

    [[nodiscard]] h5core::NumericWindow
    readNumericWindow(const std::vector<hsize_t>& offset,
                      const std::vector<hsize_t>& count) const override
    {
        const std::vector<hsize_t> clamped = clamp(offset, count);
        record(Call{Call::Kind::Numeric, offset, clamped});

        h5core::NumericWindow window;
        window.offset = offset;
        window.count = clamped;
        forEach(offset, clamped, [&](const std::vector<hsize_t>& at) {
            window.values.push_back(value(at));
        });
        return window;
    }

    [[nodiscard]] h5core::ElementValue
    readElement(const std::vector<hsize_t>& offset) const override
    {
        record(Call{Call::Kind::Element, offset,
                    std::vector<hsize_t>(info_.shape.size(), 1)});

        h5core::ElementValue element;
        element.offset = offset;
        element.text = std::format("{}", value(offset));
        element.json = element.text;
        return element;
    }

private:
    /// `count` cut to the shape, as a real dataset's own read does -- so a
    /// viewport hanging over the edge is counted as the smaller read it is.
    [[nodiscard]] std::vector<hsize_t> clamp(const std::vector<hsize_t>& offset,
                                             const std::vector<hsize_t>& count) const
    {
        std::vector<hsize_t> out = count;
        out.resize(info_.shape.size(), 1);
        for (std::size_t d = 0; d < info_.shape.size(); ++d) {
            const hsize_t start = (d < offset.size()) ? offset[d] : 0;
            out[d] = (start >= info_.shape[d])
                         ? 0
                         : std::min(out[d], info_.shape[d] - start);
        }
        return out;
    }

    template<typename F>
    void forEach(const std::vector<hsize_t>& offset,
                 const std::vector<hsize_t>& count, F&& body) const
    {
        const std::size_t rank = info_.shape.size();
        if (rank == 0) {
            body(std::vector<hsize_t>{});
            return;
        }
        const hsize_t total = std::accumulate(count.begin(), count.end(),
                                              static_cast<hsize_t>(1),
                                              std::multiplies<>{});
        std::vector<hsize_t> index(rank, 0);
        std::vector<hsize_t> at(rank, 0);
        for (hsize_t n = 0; n < total; ++n) {
            for (std::size_t d = 0; d < rank; ++d) {
                at[d] = ((d < offset.size()) ? offset[d] : 0) + index[d];
            }
            body(at);
            for (std::size_t d = rank; d-- > 0;) {
                if (++index[d] < count[d]) {
                    break;
                }
                index[d] = 0;
            }
        }
    }

    void record(Call call) const
    {
        const hsize_t elements = call.elements();
        reads_.fetch_add(1);
        switch (call.kind) {
        case Call::Kind::Window:  windows_.fetch_add(1); break;
        case Call::Kind::Numeric: numerics_.fetch_add(1); break;
        case Call::Kind::Element: elementReads_.fetch_add(1); break;
        }
        elementsRead_.fetch_add(elements);
        hsize_t largest = largestRead_.load();
        while (elements > largest
               && !largestRead_.compare_exchange_weak(largest, elements)) {
        }
        const std::lock_guard<std::mutex> held(mutex_);
        calls_.push_back(std::move(call));
    }

    h5core::DatasetInfo info_;
    std::string path_ = "/counted";

    // Written on the HDF5 thread, read on the caller's after it has drained.
    // Atomic rather than relying on that ordering, because a test that gets
    // this wrong fails intermittently and is worse than no test.
    mutable std::atomic<int> reads_{0};
    mutable std::atomic<int> windows_{0};
    mutable std::atomic<int> numerics_{0};
    mutable std::atomic<int> elementReads_{0};
    mutable std::atomic<hsize_t> elementsRead_{0};
    mutable std::atomic<hsize_t> largestRead_{0};
    mutable std::mutex mutex_;
    mutable std::vector<Call> calls_;
};

} // namespace h5test

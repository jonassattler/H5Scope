// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Signals.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>

namespace demo {
namespace {

/// The walk's generator: the C library's, so every language's example can
/// make the same line.
struct Lcg
{
    std::uint32_t state;

    double next()
    {
        state = state * 1103515245u + 12345u;
        return static_cast<double>((state >> 16) & 0x7fffu) / 32767.0;
    }
};

} // namespace

Signal spikedSine(long long count)
{
    Signal out{"sine with a spike", std::vector<double>(static_cast<std::size_t>(count))};
    for (long long i = 0; i < count; ++i) {
        out.values[static_cast<std::size_t>(i)] =
            std::sin(2.0 * std::numbers::pi * static_cast<double>(i) / 5000.0);
    }
    out.values[static_cast<std::size_t>(count * 7 / 10)] = 100.0;
    return out;
}

Signal squareWave(long long count)
{
    Signal out{"square wave", std::vector<double>(static_cast<std::size_t>(count))};
    for (long long i = 0; i < count; ++i) {
        out.values[static_cast<std::size_t>(i)] = (i / 250) % 2 == 0 ? -1.0 : 1.0;
    }
    return out;
}

Signal withGaps(long long count)
{
    Signal out{"with gaps", std::vector<double>(static_cast<std::size_t>(count))};
    for (long long i = 0; i < count; ++i) {
        out.values[static_cast<std::size_t>(i)] =
            (i / 400) % 5 == 4 ? std::numeric_limits<double>::quiet_NaN()
                               : std::cos(static_cast<double>(i) / 300.0) + 0.001 * i / 10.0;
    }
    return out;
}

Signal manyDecades(long long count)
{
    Signal out{"twelve decades", std::vector<double>(static_cast<std::size_t>(count))};
    for (long long i = 0; i < count; ++i) {
        const double at = static_cast<double>(i) / static_cast<double>(std::max(count - 1, 1LL));
        out.values[static_cast<std::size_t>(i)] =
            std::pow(10.0, -6.0 + 12.0 * at) * (1.0 + 0.3 * std::sin(40.0 * at));
    }
    return out;
}

Signal randomWalk(long long count, unsigned seed)
{
    Signal out{"random walk", std::vector<double>(static_cast<std::size_t>(count))};
    Lcg random{seed};
    double at = 0.0;
    for (long long i = 0; i < count; ++i) {
        at += random.next() - 0.5;
        out.values[static_cast<std::size_t>(i)] = (i % 997 == 996) ? at + 500.0 : at;
    }
    return out;
}

Signal tenMillion()
{
    constexpr long long count = 10'000'000;
    Signal out{"ten million", std::vector<double>(static_cast<std::size_t>(count))};
    Lcg random{2026};
    for (long long i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(count);
        out.values[static_cast<std::size_t>(i)] =
            std::sin(2.0 * std::numbers::pi * (20.0 * t + 2000.0 * t * t)) +
            0.2 * (random.next() - 0.5);
    }
    // One sample in ten million, far from where anyone would think to zoom.
    out.values[7'777'777] = 6.0;
    out.values[123'456] = -6.0;
    return out;
}

std::vector<double> regularTime(long long count, double step)
{
    std::vector<double> out(static_cast<std::size_t>(count));
    for (long long i = 0; i < count; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<double>(i) * step;
    }
    return out;
}

std::vector<double> irregularTime(long long count, unsigned seed)
{
    std::vector<double> out(static_cast<std::size_t>(count));
    Lcg random{seed};
    double at = 0.0;
    for (long long i = 0; i < count; ++i) {
        out[static_cast<std::size_t>(i)] = at;
        at += 0.0002 + 0.0018 * random.next() * random.next();
    }
    return out;
}

// --- files --------------------------------------------------------------------

std::optional<std::vector<Signal>> loadNpy(const std::string& path, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return std::nullopt;
    }
    char magic[6];
    file.read(magic, 6);
    if (!file || std::memcmp(magic, "\x93NUMPY", 6) != 0) {
        error = "not a .npy file";
        return std::nullopt;
    }
    unsigned char version[2];
    file.read(reinterpret_cast<char*>(version), 2);
    std::uint32_t headerLength = 0;
    if (version[0] == 1) {
        unsigned char bytes[2];
        file.read(reinterpret_cast<char*>(bytes), 2);
        headerLength = bytes[0] | (bytes[1] << 8);
    }
    else {
        unsigned char bytes[4];
        file.read(reinterpret_cast<char*>(bytes), 4);
        headerLength = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) |
                       (static_cast<std::uint32_t>(bytes[3]) << 24);
    }
    std::string header(headerLength, '\0');
    file.read(header.data(), headerLength);
    if (!file) {
        error = "a .npy header that ends early";
        return std::nullopt;
    }

    const auto field = [&](const std::string& key) {
        const auto at = header.find("'" + key + "'");
        return at == std::string::npos ? std::string() : header.substr(at + key.size() + 2);
    };
    std::string descr = field("descr");
    const auto quote = descr.find('\'');
    descr = quote == std::string::npos
                ? ""
                : descr.substr(quote + 1, descr.find('\'', quote + 1) - quote - 1);
    const bool fortran = field("fortran_order").find("True") < field("fortran_order").find(',');
    std::string shapeText = field("shape");
    shapeText =
        shapeText.substr(shapeText.find('(') + 1, shapeText.find(')') - shapeText.find('(') - 1);
    std::vector<long long> shape;
    std::stringstream shapeStream(shapeText);
    for (std::string part; std::getline(shapeStream, part, ',');) {
        if (part.find_first_of("0123456789") != std::string::npos) {
            shape.push_back(std::stoll(part));
        }
    }
    if (descr.size() < 3 || shape.empty() || shape.size() > 2) {
        error =
            "only 1-D and 2-D arrays of numbers are plotted, not " + descr + " (" + shapeText + ")";
        return std::nullopt;
    }
    const char order = descr[0];
    const char kind = descr[1];
    const int width = std::stoi(descr.substr(2));
    const bool swap = (order == '>' && std::endian::native == std::endian::little) ||
                      (order == '<' && std::endian::native == std::endian::big);
    if ((kind != 'f' && kind != 'i' && kind != 'u') || (kind == 'f' && width != 4 && width != 8) ||
        (width != 1 && width != 2 && width != 4 && width != 8)) {
        error = "not a type this example reads: " + descr;
        return std::nullopt;
    }
    const long long rows = shape[0];
    const long long columns = shape.size() == 2 ? shape[1] : 1;
    std::vector<unsigned char> raw(static_cast<std::size_t>(rows * columns * width));
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!file) {
        error = "a .npy file shorter than its shape";
        return std::nullopt;
    }
    const auto valueAt = [&](long long index) {
        unsigned char bytes[8];
        std::memcpy(bytes, raw.data() + index * width, static_cast<std::size_t>(width));
        if (swap) {
            std::reverse(bytes, bytes + width);
        }
        if (kind == 'f') {
            if (width == 4) {
                float f;
                std::memcpy(&f, bytes, 4);
                return static_cast<double>(f);
            }
            double d;
            std::memcpy(&d, bytes, 8);
            return d;
        }
        std::uint64_t bits = 0;
        std::memcpy(&bits, bytes, static_cast<std::size_t>(width));
        if (kind == 'u') {
            return static_cast<double>(bits);
        }
        const int shift = 64 - 8 * width;
        return static_cast<double>(static_cast<std::int64_t>(bits << shift) >> shift);
    };
    std::vector<Signal> out(static_cast<std::size_t>(columns));
    for (long long c = 0; c < columns; ++c) {
        Signal& signal = out[static_cast<std::size_t>(c)];
        signal.name = columns == 1 ? "npy" : "column " + std::to_string(c);
        signal.values.resize(static_cast<std::size_t>(rows));
        for (long long r = 0; r < rows; ++r) {
            const long long index = fortran ? c * rows + r : r * columns + c;
            signal.values[static_cast<std::size_t>(r)] = valueAt(index);
        }
    }
    return out;
}

std::optional<std::vector<Signal>> loadCsv(const std::string& path, std::string& error)
{
    std::ifstream file(path);
    if (!file) {
        error = "cannot open " + path;
        return std::nullopt;
    }
    std::vector<Signal> out;
    std::string line;
    bool first = true;
    const auto cells = [](const std::string& text) {
        std::vector<std::string> parts;
        std::string part;
        const char separator = text.find(';') != std::string::npos ? ';' : ',';
        std::stringstream stream(text);
        while (std::getline(stream, part, separator)) {
            parts.push_back(part);
        }
        return parts;
    };
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> parts = cells(line);
        if (first) {
            out.resize(parts.size());
            bool numbers = true;
            for (const std::string& part : parts) {
                char* end = nullptr;
                std::strtod(part.c_str(), &end);
                numbers = numbers && end != part.c_str();
            }
            for (std::size_t i = 0; i < parts.size(); ++i) {
                out[i].name = numbers ? "column " + std::to_string(i) : parts[i];
            }
            first = false;
            if (!numbers) {
                continue;
            }
        }
        for (std::size_t i = 0; i < out.size(); ++i) {
            double value = std::numeric_limits<double>::quiet_NaN();
            if (i < parts.size()) {
                char* end = nullptr;
                const double parsed = std::strtod(parts[i].c_str(), &end);
                if (end != parts[i].c_str()) {
                    value = parsed;
                }
            }
            out[i].values.push_back(value);
        }
    }
    if (out.empty() || out.front().values.empty()) {
        error = "no numbers in " + path;
        return std::nullopt;
    }
    return out;
}

} // namespace demo

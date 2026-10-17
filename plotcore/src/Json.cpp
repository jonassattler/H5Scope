// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Json.hpp"

#include "plotcore/Format.hpp"

#include <charconv>
#include <cmath>

namespace plotcore::json {
namespace {

void escape(const std::string& text, std::string& out)
{
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static const char* const hex = "0123456789abcdef";
                out += "\\u00";
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            }
            else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

void writeInto(const Value& value, std::string& out)
{
    switch (value.kind) {
    case Value::Kind::Null:
        out += "null";
        return;
    case Value::Kind::Bool:
        out += value.boolean ? "true" : "false";
        return;
    case Value::Kind::Number:
        out += std::isfinite(value.number) ? numberText(value.number) : "null";
        return;
    case Value::Kind::String:
        escape(value.text, out);
        return;
    case Value::Kind::Array:
        out.push_back('[');
        for (std::size_t i = 0; i < value.items.size(); ++i) {
            if (i > 0) {
                out.push_back(',');
            }
            writeInto(value.items[i], out);
        }
        out.push_back(']');
        return;
    case Value::Kind::Object:
        out.push_back('{');
        for (std::size_t i = 0; i < value.members.size(); ++i) {
            if (i > 0) {
                out.push_back(',');
            }
            escape(value.members[i].first, out);
            out.push_back(':');
            writeInto(value.members[i].second, out);
        }
        out.push_back('}');
        return;
    }
}

class Reader
{
public:
    explicit Reader(std::string_view text) : text_(text) {}

    std::optional<Value> document()
    {
        std::optional<Value> value = read();
        space();
        if (!value.has_value() || at_ != text_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    void space()
    {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' ||
                                      text_[at_] == '\r' || text_[at_] == '\t')) {
            ++at_;
        }
    }

    bool literal(std::string_view word)
    {
        if (text_.substr(at_, word.size()) != word) {
            return false;
        }
        at_ += word.size();
        return true;
    }

    std::optional<std::string> string()
    {
        if (at_ >= text_.size() || text_[at_] != '"') {
            return std::nullopt;
        }
        ++at_;
        std::string out;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at_ >= text_.size()) {
                return std::nullopt;
            }
            const char e = text_[at_++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out.push_back(e);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                if (at_ + 4 > text_.size()) {
                    return std::nullopt;
                }
                unsigned code = 0;
                const auto parsed =
                    std::from_chars(text_.data() + at_, text_.data() + at_ + 4, code, 16);
                if (parsed.ptr != text_.data() + at_ + 4) {
                    return std::nullopt;
                }
                at_ += 4;
                // UTF-8 for the code unit; a surrogate pair is two of them,
                // which a saved view's own names never contain.
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                }
                else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<Value> read()
    {
        space();
        if (at_ >= text_.size()) {
            return std::nullopt;
        }
        const char c = text_[at_];
        if (c == '{') {
            ++at_;
            Value object = Value::object();
            space();
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
                return object;
            }
            for (;;) {
                space();
                std::optional<std::string> key = string();
                space();
                if (!key.has_value() || at_ >= text_.size() || text_[at_] != ':') {
                    return std::nullopt;
                }
                ++at_;
                std::optional<Value> member = read();
                if (!member.has_value()) {
                    return std::nullopt;
                }
                object.set(std::move(*key), std::move(*member));
                space();
                if (at_ < text_.size() && text_[at_] == ',') {
                    ++at_;
                    continue;
                }
                if (at_ < text_.size() && text_[at_] == '}') {
                    ++at_;
                    return object;
                }
                return std::nullopt;
            }
        }
        if (c == '[') {
            ++at_;
            Value array = Value::array();
            space();
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
                return array;
            }
            for (;;) {
                std::optional<Value> item = read();
                if (!item.has_value()) {
                    return std::nullopt;
                }
                array.items.push_back(std::move(*item));
                space();
                if (at_ < text_.size() && text_[at_] == ',') {
                    ++at_;
                    continue;
                }
                if (at_ < text_.size() && text_[at_] == ']') {
                    ++at_;
                    return array;
                }
                return std::nullopt;
            }
        }
        if (c == '"') {
            std::optional<std::string> text = string();
            if (!text.has_value()) {
                return std::nullopt;
            }
            return Value::of(std::move(*text));
        }
        if (literal("true")) {
            return Value::of(true);
        }
        if (literal("false")) {
            return Value::of(false);
        }
        if (literal("null")) {
            return Value{};
        }
        double number = 0.0;
        const auto parsed =
            std::from_chars(text_.data() + at_, text_.data() + text_.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr == text_.data() + at_) {
            return std::nullopt;
        }
        at_ = static_cast<std::size_t>(parsed.ptr - text_.data());
        return Value::of(number);
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

} // namespace

Value Value::of(bool value)
{
    Value v;
    v.kind = Kind::Bool;
    v.boolean = value;
    return v;
}

Value Value::of(double value)
{
    Value v;
    v.kind = Kind::Number;
    v.number = value;
    return v;
}

Value Value::of(std::string value)
{
    Value v;
    v.kind = Kind::String;
    v.text = std::move(value);
    return v;
}

Value Value::array()
{
    Value v;
    v.kind = Kind::Array;
    return v;
}

Value Value::object()
{
    Value v;
    v.kind = Kind::Object;
    return v;
}

Value& Value::set(std::string key, Value value)
{
    for (auto& member : members) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    members.emplace_back(std::move(key), std::move(value));
    return *this;
}

const Value* Value::find(std::string_view key) const
{
    for (const auto& member : members) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

double Value::numberOr(std::string_view key, double fallback) const
{
    const Value* found = find(key);
    return found != nullptr && found->kind == Kind::Number ? found->number : fallback;
}

bool Value::boolOr(std::string_view key, bool fallback) const
{
    const Value* found = find(key);
    return found != nullptr && found->kind == Kind::Bool ? found->boolean : fallback;
}

std::string Value::textOr(std::string_view key, std::string fallback) const
{
    const Value* found = find(key);
    return found != nullptr && found->kind == Kind::String ? found->text : std::move(fallback);
}

std::string write(const Value& value)
{
    std::string out;
    writeInto(value, out);
    return out;
}

std::optional<Value> parse(std::string_view text)
{
    return Reader(text).document();
}

} // namespace plotcore::json

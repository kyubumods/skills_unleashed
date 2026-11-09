// The numeric-literal scan of a time, a range, a damage, a cost or a cooldown is written in lib/tuning.lua and
// nowhere else, so src/abilities/ and src/logic/ may hold no numeric literal other than 0, 1 and 2. Comments and strings are
// not code; identifiers with digits (x2, vec3) are not literals. Values are compared, so 1.0 and 0x2 pass and 0x10 does not.
#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace su_test {

struct Literal {
    int line;
    std::string text;
};

inline std::vector<Literal> numeric_literals_beyond_012(const std::string& s) {
    std::vector<Literal> out;
    const size_t n = s.size();
    int line = 1;
    size_t i = 0;
    auto long_bracket = [&](size_t at, size_t& level) -> bool {  // at: the '[' ; level: count of '='
        if (at >= n || s[at] != '[') return false;
        size_t j = at + 1;
        level = 0;
        while (j < n && s[j] == '=') { ++level; ++j; }
        return j < n && s[j] == '[';
    };
    auto skip_long = [&](size_t at, size_t level) {  // at: the first '[' of the opening bracket; returns past the closing one
        const std::string close = "]" + std::string(level, '=') + "]";
        size_t j = at + level + 2;
        size_t e = s.find(close, j);
        if (e == std::string::npos) e = n;
        else e += close.size();
        for (size_t k = at; k < e && k < n; ++k) if (s[k] == '\n') ++line;
        return e;
    };
    while (i < n) {
        const char c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '-' && i + 1 < n && s[i + 1] == '-') {
            size_t level = 0;
            if (long_bracket(i + 2, level)) { i = skip_long(i + 2, level); continue; }
            while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            ++i;
            while (i < n && s[i] != c && s[i] != '\n') { if (s[i] == '\\') ++i; ++i; }
            if (i < n && s[i] == c) ++i;
            continue;
        }
        if (c == '[') {
            size_t level = 0;
            if (long_bracket(i, level)) { i = skip_long(i, level); continue; }
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            while (i < n && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
            continue;
        }
        const bool dot_digit = c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1])) &&
                               (i == 0 || (s[i - 1] != '.' && !std::isalnum(static_cast<unsigned char>(s[i - 1])) && s[i - 1] != '_'));
        if (std::isdigit(static_cast<unsigned char>(c)) || dot_digit) {
            const size_t start = i;
            if (c == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                i += 2;
                while (i < n && std::isxdigit(static_cast<unsigned char>(s[i]))) ++i;
            } else {
                while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                if (i < n && s[i] == '.' && !(i + 1 < n && s[i + 1] == '.')) {
                    ++i;
                    while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                }
                if (i < n && (s[i] == 'e' || s[i] == 'E')) {
                    size_t j = i + 1;
                    if (j < n && (s[j] == '+' || s[j] == '-')) ++j;
                    if (j < n && std::isdigit(static_cast<unsigned char>(s[j]))) {
                        i = j;
                        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                    }
                }
            }
            const std::string text = s.substr(start, i - start);
            const double v = std::strtod(text.c_str(), nullptr);
            if (v != 0.0 && v != 1.0 && v != 2.0) out.push_back({line, text});
            continue;
        }
        ++i;
    }
    return out;
}

}  // namespace su_test

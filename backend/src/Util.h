#pragma once

#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <optional>
#include <cstdint>

namespace vtracer {

std::wstring utf8ToWide(std::string_view utf8);
std::string  wideToUtf8(std::wstring_view w);

// Minimal JSON writer for messages on the WebView2 bridge.
// Scope auto-closes the brace/bracket via RAII.
class JsonWriter {
public:
    struct Scope {
        JsonWriter* w;
        char close;
        bool active{true};
        Scope(JsonWriter* w_, char close_) : w(w_), close(close_) {}
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& other) noexcept : w(other.w), close(other.close), active(other.active) { other.active = false; }
        ~Scope() {
            if (active) {
                w->raw_(close);
                w->popFirst_();
            }
        }
    };

    [[nodiscard]] Scope obj() { sepIfNeeded_(); raw_('{'); pushFirst_(); return Scope{this, '}'}; }
    [[nodiscard]] Scope arr() { sepIfNeeded_(); raw_('['); pushFirst_(); return Scope{this, ']'}; }

    void key(std::string_view k) {
        sepIfNeeded_();
        emitString_(k);
        raw_(':');
        // Next emitted token is the value for this key, so skip the comma once.
        suppressNextSep_ = true;
    }

    void str(std::string_view v) { sepIfNeeded_(); emitString_(v); }
    void num(double v) {
        sepIfNeeded_();
        std::ostringstream s;
        s.imbue(std::locale::classic());
        s.precision(6);
        s << v;
        out_ << s.str();
    }
    void num(int64_t v)  { sepIfNeeded_(); out_ << v; }
    void num(int v)      { num(static_cast<int64_t>(v)); }
    void boolean(bool v) { sepIfNeeded_(); out_ << (v ? "true" : "false"); }
    void null_()         { sepIfNeeded_(); out_ << "null"; }

    std::string str_() &&     { return std::move(out_).str(); }
    std::string str_() const& { return out_.str(); }

private:
    void raw_(char c) { out_ << c; }
    void emitString_(std::string_view s) {
        out_ << '"';
        for (char c : s) {
            switch (c) {
                case '"':  out_ << "\\\""; break;
                case '\\': out_ << "\\\\"; break;
                case '\b': out_ << "\\b"; break;
                case '\f': out_ << "\\f"; break;
                case '\n': out_ << "\\n"; break;
                case '\r': out_ << "\\r"; break;
                case '\t': out_ << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
                        out_ << buf;
                    } else {
                        out_ << c;
                    }
            }
        }
        out_ << '"';
    }

    void pushFirst_() { firstStack_.push_back(true); suppressNextSep_ = false; }
    void popFirst_()  { if (!firstStack_.empty()) firstStack_.pop_back(); suppressNextSep_ = false; }

    void sepIfNeeded_() {
        if (suppressNextSep_) {
            suppressNextSep_ = false;
            return;
        }
        if (!firstStack_.empty()) {
            if (firstStack_.back()) {
                firstStack_.back() = false;
            } else {
                raw_(',');
            }
        }
    }

    std::ostringstream out_;
    std::vector<bool> firstStack_;
    bool suppressNextSep_{false};
};

inline std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return std::string(s.substr(a, b - a));
}

// Accepts both `.` and `,` as decimal separator (Excel German vs English).
inline std::optional<double> parseNumber(std::string_view s) {
    std::string t = trim(s);
    if (t.empty()) return std::nullopt;
    for (auto& c : t) if (c == ',') c = '.';
    try {
        size_t pos = 0;
        double v = std::stod(t, &pos);
        if (pos != t.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace vtracer

#include "ModelDatabase.h"
#include "Util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace vtracer {

namespace {

bool looksLikeHeaderToken(std::string_view t) {
    std::string s = trim(t);
    if (s.empty()) return false;
    static const std::vector<std::string_view> tokens = {
        "Mod.", "Mod", "Model", "Modell",
        "Boxbreite", "Boxhöhe", "Boxhoehe",
        "A", "B",
        "Einführung", "Einfuehrung", "Intro", "Introduction",
        "TZ", "L", "R",
    };
    for (auto& t2 : tokens) {
        if (s == t2) return true;
    }
    return false;
}

char detectDelimiter(std::string_view text) {
    size_t scLines = 0, commaLines = 0;
    size_t lineStart = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            std::string_view line = text.substr(lineStart, i - lineStart);
            size_t sc = 0, cm = 0;
            for (char c : line) { if (c == ';') ++sc; else if (c == ',') ++cm; }
            if (sc > 0) ++scLines;
            else if (cm > 0) ++commaLines;
            lineStart = i + 1;
        }
    }
    return (scLines >= commaLines) ? ';' : ',';
}

std::vector<std::string> splitLine(std::string_view line, char delim) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    for (char c : line) {
        if (c == '"') {
            inQuote = !inQuote;
        } else if (c == delim && !inQuote) {
            out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(std::move(cur));
    for (auto& s : out) s = trim(s);
    return out;
}

// True if all non-empty cells past index 0 are blank.  Used to detect the
// leading "brand" row.
bool allBlankAfterFirst(const std::vector<std::string>& cells) {
    for (size_t i = 1; i < cells.size(); ++i) {
        if (!cells[i].empty()) return false;
    }
    return true;
}

// True if the row contains any header-ish token.
bool isHeaderRow(const std::vector<std::string>& cells) {
    int hits = 0;
    for (auto& c : cells) if (looksLikeHeaderToken(c)) ++hits;
    return hits >= 2;
}

bool isLikelyDataRow(const std::vector<std::string>& cells) {
    // First cell looks like a model number (digits), and at least two further
    // cells parse as numbers.
    if (cells.empty()) return false;
    const std::string& first = cells[0];
    if (first.empty()) return false;
    bool digit = false;
    for (char c : first) {
        if (std::isdigit(static_cast<unsigned char>(c))) digit = true;
        else if (c != ' ') { /* non-digit, non-space -> still ok if mostly digits */ }
    }
    if (!digit) return false;

    int numericCount = 0;
    for (size_t i = 1; i < cells.size() && i <= 4; ++i) {
        if (parseNumber(cells[i]).has_value()) ++numericCount;
    }
    return numericCount >= 2;
}

// Heuristic: a non-numeric cell at column 3+ on a data-shaped row marks the
// row as a comment row (e.g. "mit Formscheibe testen").
bool hasCommentInsteadOfNumbers(const std::vector<std::string>& cells) {
    for (size_t i = 1; i < cells.size() && i < 5; ++i) {
        const auto& c = cells[i];
        if (c.empty()) continue;
        if (parseNumber(c).has_value()) continue;
        // Found a non-number, non-empty cell where a number was expected.
        return true;
    }
    return false;
}

// Convert raw bytes from disk to a UTF-8 std::string.  Tries UTF-8 first
// (with BOM detection), falls back to Windows-1252.
std::string toUtf8(const std::vector<unsigned char>& raw) {
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        return std::string(reinterpret_cast<const char*>(raw.data() + 3), raw.size() - 3);
    }
    // Quick UTF-8 validity scan.
    auto isUtf8 = [&]() {
        size_t i = 0;
        while (i < raw.size()) {
            unsigned char c = raw[i];
            if (c < 0x80) { ++i; continue; }
            int extra = 0;
            if      ((c & 0xE0) == 0xC0) extra = 1;
            else if ((c & 0xF0) == 0xE0) extra = 2;
            else if ((c & 0xF8) == 0xF0) extra = 3;
            else return false;
            if (i + extra >= raw.size()) return false;
            for (int k = 1; k <= extra; ++k) {
                if ((raw[i + k] & 0xC0) != 0x80) return false;
            }
            i += extra + 1;
        }
        return true;
    };
    if (isUtf8()) {
        return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
    }
    // Treat as Win-1252 (Excel default on German Windows).
    std::wstring w;
    w.reserve(raw.size());
    for (unsigned char c : raw) w.push_back(static_cast<wchar_t>(c));
    return wideToUtf8(w);
}

} // anonymous namespace

ModelDatabase::FileLoad ModelDatabase::parseInto_(
    std::string_view text,
    std::string_view filePath,
    std::vector<ModelEntry>& outEntries) const
{
    FileLoad fl;
    fl.path = std::string{filePath};

    if (text.empty()) {
        fl.error = "empty file";
        return fl;
    }

    char delim = detectDelimiter(text);

    std::vector<std::string> rawLines;
    {
        std::string cur;
        for (char c : text) {
            if (c == '\n') { rawLines.push_back(std::move(cur)); cur.clear(); }
            else if (c != '\r') cur.push_back(c);
        }
        if (!cur.empty()) rawLines.push_back(std::move(cur));
    }

    // Brand row: first non-empty row whose cell[0] is text and the rest empty.
    for (auto& line : rawLines) {
        auto cells = splitLine(line, delim);
        if (cells.empty()) continue;
        bool empty = true;
        for (auto& c : cells) if (!c.empty()) { empty = false; break; }
        if (empty) continue;
        if (!cells[0].empty() && !parseNumber(cells[0]).has_value() &&
            !looksLikeHeaderToken(cells[0]) && allBlankAfterFirst(cells)) {
            fl.brand = cells[0];
        }
        break;
    }

    int colModel = 0, colAL = 1, colBL = 2, colAR = 3, colBR = 4, colIntro = 5;
    for (auto& line : rawLines) {
        auto cells = splitLine(line, delim);
        if (cells.empty()) continue;
        if (isHeaderRow(cells)) {
            int boxWidthCols  = 0;
            int boxHeightCols = 0;
            for (size_t i = 0; i < cells.size(); ++i) {
                const std::string& c = cells[i];
                if (c == "Boxbreite" || c == "A" || c == "Width")  ++boxWidthCols;
                if (c == "Boxhöhe"   || c == "Boxhoehe" || c == "B" || c == "Height") ++boxHeightCols;
                if (c == "Mod." || c == "Mod" || c == "Model" || c == "Modell") colModel = static_cast<int>(i);
                if (c == "Einführung" || c == "Einfuehrung" || c == "Intro" || c == "Introduction") colIntro = static_cast<int>(i);
            }
            if (boxWidthCols == 1 && boxHeightCols == 1) {
                for (size_t i = 0; i < cells.size(); ++i) {
                    const auto& c = cells[i];
                    if (c == "Boxbreite" || c == "A" || c == "Width")  colAL = static_cast<int>(i);
                    if (c == "Boxhöhe"   || c == "Boxhoehe" || c == "B" || c == "Height") colBL = static_cast<int>(i);
                }
                colAR = colAL;
                colBR = colBL;
            } else {
                std::vector<int> widths, heights;
                for (size_t i = 0; i < cells.size(); ++i) {
                    const auto& c = cells[i];
                    if (c == "Boxbreite" || c == "A" || c == "Width")  widths.push_back(static_cast<int>(i));
                    if (c == "Boxhöhe"   || c == "Boxhoehe" || c == "B" || c == "Height") heights.push_back(static_cast<int>(i));
                }
                if (widths.size() >= 2 && heights.size() >= 2) {
                    colAL = widths[0];  colBL = heights[0];
                    colAR = widths[1];  colBR = heights[1];
                }
            }
            break;
        }
    }

    for (auto& line : rawLines) {
        auto cells = splitLine(line, delim);
        if (cells.empty()) continue;
        bool empty = true;
        for (auto& c : cells) if (!c.empty()) { empty = false; break; }
        if (empty) continue;

        if (isHeaderRow(cells))         { ++fl.skipped; continue; }
        if (allBlankAfterFirst(cells) && !parseNumber(cells[0]).has_value()) {
            ++fl.skipped;
            continue;
        }
        if (!isLikelyDataRow(cells))    { ++fl.skipped; continue; }
        if (hasCommentInsteadOfNumbers(cells)) { ++fl.skipped; continue; }

        ++fl.total;

        ModelEntry e;
        e.brand = fl.brand;
        e.model = trim(cells[colModel]);

        auto cell = [&](int idx) -> std::string {
            return (idx >= 0 && idx < static_cast<int>(cells.size())) ? cells[idx] : std::string{};
        };

        auto al = parseNumber(cell(colAL));
        auto bl = parseNumber(cell(colBL));
        auto ar = parseNumber(cell(colAR));
        auto br = parseNumber(cell(colBR));

        if (!al || !bl) { ++fl.skipped; --fl.total; continue; }
        if (!ar) ar = al;
        if (!br) br = bl;

        e.aBoxL = *al;
        e.bBoxL = *bl;
        e.aBoxR = *ar;
        e.bBoxR = *br;

        if (colIntro >= 0 && colIntro < static_cast<int>(cells.size())) {
            std::string intro = cells[colIntro];
            if (!intro.empty() && intro.size() <= 12 &&
                std::any_of(intro.begin(), intro.end(),
                            [](char c){ return std::isdigit(static_cast<unsigned char>(c)); })) {
                e.introduction = intro;
            }
        }

        double diffA = std::abs(e.aBoxL - e.aBoxR);
        double diffB = std::abs(e.bBoxL - e.bBoxR);
        if (diffA > FLAG_LR_DIFF_MM || diffB > FLAG_LR_DIFF_MM) {
            e.flagged = true;
            std::ostringstream r;
            r.imbue(std::locale::classic());
            r.precision(2);
            r << std::fixed << "L/R differ by " << std::max(diffA, diffB) << " mm";
            e.flagReason = r.str();
            ++fl.flagged;
        } else {
            ++fl.accepted;
        }

        outEntries.push_back(std::move(e));
    }

    return fl;
}

ModelDatabase::LoadStats ModelDatabase::load(std::string_view text, std::string_view filePath) {
    entries_.clear();
    stats_ = LoadStats{};

    FileLoad fl = parseInto_(text, filePath, entries_);
    stats_.total    += fl.total;
    stats_.accepted += fl.accepted;
    stats_.flagged  += fl.flagged;
    stats_.skipped  += fl.skipped;
    if (!fl.error.empty() && stats_.error.empty()) stats_.error = fl.error;
    stats_.files.push_back(std::move(fl));
    return stats_;
}

ModelDatabase::LoadStats ModelDatabase::loadFile(std::string_view filePath) {
    std::wstring wpath = utf8ToWide(filePath);
    std::ifstream in(wpath, std::ios::binary);
    if (!in) {
        entries_.clear();
        stats_ = LoadStats{};
        stats_.error = std::string{"Could not open "} + std::string{filePath};
        FileLoad fl;
        fl.path = std::string{filePath};
        fl.error = stats_.error;
        stats_.files.push_back(std::move(fl));
        return stats_;
    }
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    std::string text = toUtf8(raw);
    return load(text, filePath);
}

ModelDatabase::LoadStats ModelDatabase::loadDirectory(std::string_view dir) {
    entries_.clear();
    stats_ = LoadStats{};

    std::error_code ec;
    std::filesystem::path root = utf8ToWide(dir);
    if (!std::filesystem::is_directory(root, ec)) {
        stats_.error = "Catalog directory not found: " + std::string{dir};
        return stats_;
    }

    std::vector<std::filesystem::path> csvFiles;
    for (auto& e : std::filesystem::directory_iterator(root, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (ext == ".csv") csvFiles.push_back(e.path());
    }
    std::sort(csvFiles.begin(), csvFiles.end());

    if (csvFiles.empty()) {
        stats_.error = "No .csv files found in " + std::string{dir};
        return stats_;
    }

    for (auto& p : csvFiles) {
        std::ifstream in(p, std::ios::binary);
        std::string utf8Path = wideToUtf8(p.wstring());
        if (!in) {
            FileLoad fl;
            fl.path = utf8Path;
            fl.error = "Could not open file";
            stats_.files.push_back(std::move(fl));
            continue;
        }
        std::vector<unsigned char> raw((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        std::string text = toUtf8(raw);
        FileLoad fl = parseInto_(text, utf8Path, entries_);
        stats_.total    += fl.total;
        stats_.accepted += fl.accepted;
        stats_.flagged  += fl.flagged;
        stats_.skipped  += fl.skipped;
        stats_.files.push_back(std::move(fl));
    }

    if (entries_.empty() && stats_.error.empty()) {
        stats_.error = "Found .csv files but parsed 0 models.";
    }
    return stats_;
}

MatchResult ModelDatabase::findBest(double measuredA, double measuredB) const {
    MatchResult best;
    best.matched = false;
    best.score = std::numeric_limits<double>::infinity();

    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (e.flagged) continue;

        struct Side { char id; double a; double b; };
        const Side sides[2] = {
            {'L', e.aBoxL, e.bBoxL},
            {'R', e.aBoxR, e.bBoxR},
        };
        for (auto& s : sides) {
            double dA = s.a - measuredA;
            double dB = s.b - measuredB;
            double score = std::sqrt(dA * dA + dB * dB);
            bool within = std::abs(dA) <= TOLERANCE_MM && std::abs(dB) <= TOLERANCE_MM;

            // We want the best match that is within tolerance.  If we already
            // have a within-tolerance candidate, only replace with a closer
            // within-tolerance candidate.  Otherwise track the overall closest.
            if (best.matched) {
                if (within && score < best.score) {
                    best.index = static_cast<int>(i);
                    best.side  = s.id;
                    best.aDelta = dA;
                    best.bDelta = dB;
                    best.score = score;
                }
            } else {
                if (within) {
                    best.matched = true;
                    best.index = static_cast<int>(i);
                    best.side  = s.id;
                    best.aDelta = dA;
                    best.bDelta = dB;
                    best.score = score;
                } else if (score < best.score) {
                    best.index = static_cast<int>(i);
                    best.side  = s.id;
                    best.aDelta = dA;
                    best.bDelta = dB;
                    best.score = score;
                }
            }
        }
    }

    return best;
}

} // namespace vtracer

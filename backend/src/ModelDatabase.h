#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vtracer {

struct ModelEntry {
    std::string brand;       // e.g. "Roy Robson"
    std::string model;       // e.g. "10074"
    std::string introduction;// e.g. "08.2024" or empty
    double aBoxL{0.0};
    double bBoxL{0.0};
    double aBoxR{0.0};
    double bBoxR{0.0};
    bool   flagged{false};   // true if L/R differ by > FLAG_LR_DIFF_MM
    std::string flagReason;
};

struct MatchResult {
    bool matched{false};
    int  index{-1};          // index into ModelDatabase::entries() of the winner
    char side{'?'};          // 'L' or 'R' - which side fitted
    double aDelta{0.0};      // matched-side A - measured A  (signed)
    double bDelta{0.0};      // matched-side B - measured B  (signed)
    double score{0.0};       // sqrt(a^2 + b^2) for the matched side
    // If `matched == false`, these still describe the closest model so the UI
    // can show "no match - closest was X off by Ymm".
};

class ModelDatabase {
public:
    // Tolerance applied to BOTH dimensions independently.
    static constexpr double TOLERANCE_MM = 0.50;

    // L/R rows differing by more than this on either A or B are considered
    // dirty data (likely typo) and excluded from matching.  See 60136.
    static constexpr double FLAG_LR_DIFF_MM = 2.00;

    // Stats for a single CSV file.
    struct FileLoad {
        std::string path;
        std::string brand;     // brand auto-detected from this file
        size_t total{0};
        size_t accepted{0};    // matchable rows
        size_t flagged{0};
        size_t skipped{0};
        std::string error;     // non-empty if this file failed to load
    };

    // Aggregate stats for the whole catalog (all loaded files).
    struct LoadStats {
        size_t total{0};
        size_t accepted{0};
        size_t flagged{0};
        size_t skipped{0};
        std::string error;     // populated only when no files loaded successfully
        std::vector<FileLoad> files;
    };

    // Single-file loaders.  Replace any previous contents.
    LoadStats load(std::string_view text, std::string_view filePath = {});
    LoadStats loadFile(std::string_view filePath);

    // Multi-file loader.  Reads every *.csv in `dir` (non-recursive) and
    // appends entries from each.  Each entry keeps the brand from its file.
    LoadStats loadDirectory(std::string_view dir);

    const std::vector<ModelEntry>& entries() const { return entries_; }
    const LoadStats& stats() const { return stats_; }

    // Match a measurement against the whole catalog (across all files).
    MatchResult findBest(double measuredA, double measuredB) const;

private:
    // Parse a single CSV's text into `outEntries` and a per-file FileLoad.
    // Does not touch `entries_` - the public loaders combine the result.
    FileLoad parseInto_(std::string_view text,
                        std::string_view filePath,
                        std::vector<ModelEntry>& outEntries) const;

    std::vector<ModelEntry> entries_;
    LoadStats stats_;
};

} // namespace vtracer

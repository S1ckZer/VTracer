#include "NidekTracer.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <thread>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vtracer {

namespace {

constexpr uint8_t ENQ = 0x05;
constexpr uint8_t ACK = 0x06;
constexpr uint8_t NAK = 0x15;
constexpr uint8_t STX = 0x02;
constexpr uint8_t ETX = 0x03;

uint8_t computeBcc(const uint8_t* data, size_t len) {
    uint8_t bcc = 0;
    for (size_t i = 0; i < len; ++i) bcc ^= data[i];
    return bcc;
}

std::string toAscii(const std::vector<uint8_t>& bytes) {
    std::string s;
    s.reserve(bytes.size());
    for (uint8_t b : bytes) {
        if (b >= 0x20 && b < 0x7f) s.push_back(static_cast<char>(b));
        else s.push_back('.');
    }
    return s;
}

std::optional<double> findKeyed(const std::string& body, const std::string& key) {
    std::regex re(key + R"(\s*=\s*([+-]?\d+(?:[.,]\d+)?))");
    std::smatch m;
    if (std::regex_search(body, m, re) && m.size() >= 2) {
        return parseNumber(m[1].str());
    }
    return std::nullopt;
}

// Pull every number that follows `key=` up to the next `WORD=` or end of text.
std::vector<double> grabNumberBlock(const std::string& text, const std::string& key) {
    std::vector<double> out;
    std::string needle = key + "=";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return out;
    pos += needle.size();
    size_t end = text.size();
    std::regex nextKey(R"(\b[A-Z][A-Z0-9_]*\s*=)");
    std::smatch m;
    std::string sub = text.substr(pos);
    if (std::regex_search(sub, m, nextKey)) {
        end = pos + static_cast<size_t>(m.position(0));
    }
    std::string block = text.substr(pos, end - pos);
    std::regex numRe(R"([+-]?\d+(?:[.,]\d+)?)");
    auto begin = std::sregex_iterator(block.begin(), block.end(), numRe);
    auto stop  = std::sregex_iterator();
    for (auto it = begin; it != stop; ++it) {
        std::string s = it->str();
        std::replace(s.begin(), s.end(), ',', '.');
        try { out.push_back(std::stod(s)); } catch (...) {}
    }
    return out;
}

std::vector<NidekTracer::TracePoint> polarToXY(const std::vector<double>& radii) {
    std::vector<NidekTracer::TracePoint> pts;
    if (radii.size() < 8) return pts;
    pts.reserve(radii.size());
    const double step = 2.0 * M_PI / static_cast<double>(radii.size());
    for (size_t i = 0; i < radii.size(); ++i) {
        // Nidek emits 0 for "no measurement at this angle".
        if (!(radii[i] > 0.5)) continue;
        const double t = static_cast<double>(i) * step;
        pts.push_back({radii[i] * std::cos(t), radii[i] * std::sin(t)});
    }
    return pts;
}

// Single source of truth for REQ command keywords. If a particular firmware
// rejects TRCL/TRCR with NAK, sniff the OEM tool and adjust here.
const char* reqKeywordFor(NidekTracer::TraceMode mode) {
    using TM = NidekTracer::TraceMode;
    switch (mode) {
        case TM::Left:   return "TRCL";
        case TM::Right:  return "TRCR";
        case TM::Center: return "TRC";
        case TM::Both:
        default:         return "TRC";
    }
}

} // anonymous namespace

std::vector<uint8_t> NidekTracer::buildRequest(TraceMode mode) {
    std::string body = std::string{"REQ="} + reqKeywordFor(mode);

    std::vector<uint8_t> req;
    req.reserve(body.size() + 3);
    req.push_back(STX);
    for (char c : body) req.push_back(static_cast<uint8_t>(c));
    req.push_back(ETX);

    uint8_t bcc = computeBcc(req.data() + 1, req.size() - 1);
    req.push_back(bcc);
    return req;
}

std::string NidekTracer::open(std::string_view portName) {
    SerialPort::Settings s;
    s.baud = 9600;
    s.dataBits = 8;
    s.parityEnabled = false;
    s.stopBits = ONESTOPBIT;
    s.readIntervalMs = 50;
    s.readTotalMs = 1500;
    s.writeTotalMs = 1500;

    std::string err = port_.open(portName, s);
    if (!err.empty()) {
        log_error(err);
        return err;
    }
    log_info(std::string{"Opened "} + std::string{portName} + " @9600 8N1");
    return {};
}

NidekTracer::TraceResult NidekTracer::runTrace(TraceMode mode) {
    TraceResult result;
    if (!port_.isOpen()) {
        result.error = "port not open";
        return result;
    }

    port_.purge();

    // 1. ENQ -> ACK.
    {
        uint8_t enq = ENQ;
        if (port_.write(&enq, 1) != 1) {
            result.error = "Failed to send ENQ: " + port_.errMsg();
            log_error(result.error);
            return result;
        }
        log_debug("PC -> ENQ");

        auto reply = port_.readUntilQuiet(150, 8);
        if (reply.empty()) {
            result.error = "No response to ENQ. Is the LT-980 powered, idle, and on the right COM port?";
            log_error(result.error);
            return result;
        }
        bool gotAck = false;
        for (uint8_t b : reply) {
            if (b == ACK) { gotAck = true; break; }
            if (b == NAK) {
                result.error = "Tracer replied NAK to ENQ.";
                log_error(result.error);
                return result;
            }
        }
        if (!gotAck) {
            bool gotStx = false;
            for (uint8_t b : reply) if (b == STX) { gotStx = true; break; }
            if (!gotStx) {
                result.error = "Unexpected reply to ENQ (got " + std::to_string(reply.size()) + " bytes, no ACK).";
                log_error(result.error);
                return result;
            }
            log_warn("No ACK seen; proceeding anyway because STX was present.");
        } else {
            log_debug("LT -> ACK");
        }
    }

    // 2. REQ=TRC* (mode-dependent).
    {
        auto req = buildRequest(mode);
        if (port_.write(req.data(), req.size()) != static_cast<int>(req.size())) {
            result.error = "Failed to send REQ: " + port_.errMsg();
            log_error(result.error);
            return result;
        }
        std::string keyword = reqKeywordFor(mode);
        log_debug("PC -> STX REQ=" + keyword + " ETX BCC");
    }

    // 3. Read the response.
    auto bytes = port_.readUntilQuiet(400, 256 * 1024);
    if (bytes.empty()) {
        result.error = "Tracer accepted REQ but returned no data.";
        log_error(result.error);
        return result;
    }
    log_info("Received " + std::to_string(bytes.size()) + " bytes from tracer");

    return tryParseResponse(bytes);
}

NidekTracer::TraceResult NidekTracer::tryParseResponse(const std::vector<uint8_t>& bytes) {
    TraceResult r;

    size_t start = 0, end = bytes.size();
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == STX) { start = i + 1; break; }
    }
    for (size_t i = bytes.size(); i > 0; --i) {
        if (bytes[i - 1] == ETX) { end = i - 1; break; }
    }
    if (end <= start) end = bytes.size();

    std::vector<uint8_t> body(bytes.begin() + start, bytes.begin() + end);
    r.rawText = toAscii(body);

    std::string text;
    text.reserve(body.size());
    for (uint8_t b : body) {
        if (b == '\r' || b == '\n' || b == ';' || b == ',' ||
            (b >= 0x20 && b < 0x7f)) {
            text.push_back(static_cast<char>(b));
            if (b == ';' || b == ',') text.push_back('\n');
        }
    }

    auto first = [&](std::initializer_list<const char*> keys) -> std::optional<double> {
        for (const char* k : keys) {
            if (auto v = findKeyed(text, k)) return v;
        }
        return std::nullopt;
    };

    r.aBoxL          = first({"ABL", "AL", "FRL", "WL"});
    r.bBoxL          = first({"BBL", "BL", "HL"});
    r.aBoxR          = first({"ABR", "AR", "FRR", "WR"});
    r.bBoxR          = first({"BBR", "BR", "HR"});
    r.dbl            = first({"DBL", "FPD", "BR"});
    r.circumferenceL = first({"CIRL", "CIRCUL", "PERIL"});
    r.circumferenceR = first({"CIRR", "CIRCUR", "PERIR"});

    if (!r.aBoxR && r.aBoxL) r.aBoxR = r.aBoxL;
    if (!r.bBoxR && r.bBoxL) r.bBoxR = r.bBoxL;
    if (!r.aBoxL && r.aBoxR) r.aBoxL = r.aBoxR;
    if (!r.bBoxL && r.bBoxR) r.bBoxL = r.bBoxR;

    // Polar outline. Try several common key variants per side; the first one
    // that yields >=8 numbers wins.
    auto firstPolar = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys) {
            auto nums = grabNumberBlock(text, k);
            if (nums.size() >= 8) return nums;
        }
        return std::vector<double>{};
    };
    auto radiiL = firstPolar({"RL", "TRCL", "PL", "POLL", "PTL"});
    auto radiiR = firstPolar({"RR", "TRCR", "PR", "POLR", "PTR"});
    // Shared block (some firmwares emit just one R= for both eyes).
    if (radiiL.empty() && radiiR.empty()) {
        auto shared = firstPolar({"R", "TRC", "POL", "PT"});
        if (!shared.empty()) {
            // If clearly twice as many points as expected for one lens, split.
            if (shared.size() >= 200 && shared.size() % 2 == 0) {
                size_t half = shared.size() / 2;
                radiiL.assign(shared.begin(), shared.begin() + half);
                radiiR.assign(shared.begin() + half, shared.end());
            } else {
                radiiL = shared;
                radiiR = shared;
            }
        }
    }
    r.pointsL = polarToXY(radiiL);
    r.pointsR = polarToXY(radiiR);

    if (r.aBoxL && r.bBoxL && r.aBoxR && r.bBoxR) {
        r.success = true;
    } else {
        r.error = "Could not parse A/B box from tracer response. "
                  "If your firmware uses different keys, see README protocol section.";
    }
    return r;
}

} // namespace vtracer

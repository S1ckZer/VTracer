#include "NidekTracer.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <regex>
#include <sstream>
#include <thread>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vtracer {

namespace {

// ASCII control bytes that show up in both protocols.
constexpr uint8_t ENQ = 0x05;
constexpr uint8_t ACK = 0x06;
constexpr uint8_t NAK = 0x15;
constexpr uint8_t STX = 0x02;
constexpr uint8_t ETX = 0x03;
constexpr uint8_t EOT = 0x04;
constexpr uint8_t FS  = 0x1C; // VCA / OMA frame start

// Nidek native fixed packets. The 5th byte is XOR of the first 4 (BCC).
//   READ  = "give me the shape currently in the tracer"
//   GOOD  = "OK, here comes data"
//   EMPTY = "no shape in memory" (operator hasn't traced anything yet)
constexpr std::array<uint8_t, 5> kReadCmd  {{0x52, 0x00, 0x00, 0x09, 0x5B}};
constexpr std::array<uint8_t, 5> kGoodAck  {{0x52, 0x21, 0x00, 0x00, 0x73}};
constexpr std::array<uint8_t, 5> kEmptyAck {{0x52, 0x00, 0x00, 0x00, 0x52}};

// Nidek native trace payload geometry.
constexpr size_t kChunkSize  = 128;
constexpr size_t kChunkCount = 9;
constexpr size_t kPayloadLen = kChunkSize * kChunkCount; // 1152

std::string toAscii(const std::vector<uint8_t>& bytes) {
    std::string s;
    s.reserve(bytes.size());
    for (uint8_t b : bytes) {
        if (b >= 0x20 && b < 0x7f) s.push_back(static_cast<char>(b));
        else s.push_back('.');
    }
    return s;
}

std::string hexByte(uint8_t b) {
    static const char* hex = "0123456789ABCDEF";
    char buf[3] = {hex[b >> 4], hex[b & 0xF], 0};
    return buf;
}

std::string hexBytes(const uint8_t* data, size_t n) {
    std::string s;
    s.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        if (i) s.push_back(' ');
        s += hexByte(data[i]);
    }
    return s;
}

// VCA tag parsers (simple ASCII tag=value extraction).
std::optional<double> vcaFindKeyed(const std::string& body, const std::string& key) {
    std::regex re(key + R"(\s*=\s*([+-]?\d+(?:[.,]\d+)?))");
    std::smatch m;
    if (std::regex_search(body, m, re) && m.size() >= 2) {
        return parseNumber(m[1].str());
    }
    return std::nullopt;
}

} // anonymous namespace

uint8_t NidekTracer::xorBcc(const uint8_t* data, size_t len) {
    uint8_t bcc = 0;
    for (size_t i = 0; i < len; ++i) bcc ^= data[i];
    return bcc;
}

std::vector<uint8_t> NidekTracer::buildNidekReadPacket() {
    return std::vector<uint8_t>(kReadCmd.begin(), kReadCmd.end());
}

std::string NidekTracer::open(std::string_view portName) {
    SerialPort::Settings s;
    s.baud = 9600;
    s.dataBits = 8;
    s.parityEnabled = false;
    s.stopBits = ONESTOPBIT;
    // Per-call timeouts are tight; the higher-level reads loop with their
    // own deadline so that a stuck device fails fast without holding the
    // read forever.
    s.readIntervalMs = 50;
    s.readTotalMs    = 250;
    s.writeTotalMs   = 1000;

    std::string err = port_.open(portName, s);
    if (!err.empty()) {
        log_error(err);
        return err;
    }
    log_info(std::string{"Opened "} + std::string{portName} + " @9600 8N1");
    return {};
}

NidekTracer::TraceResult NidekTracer::runTrace(TraceMode mode) {
    if (protocol_ == Protocol::Vca) return runVca_(mode);
    return runNidekNative_(mode);
}

// ----------------------------------------------------------------------------
// Protocol A -- Nidek LT-980 native (5-byte XOR-BCC handshake + 1152 bytes)
//
//   PC -> ENQ
//   LT -> ACK
//   PC -> 52 00 00 09 5B            (READ command)
//   LT -> ACK
//   PC -> EOT
//   (operator presses scan button on the tracer)
//   LT -> ENQ
//   PC -> ACK
//   LT -> 5-byte status              (52 21 00 00 73 good, 52 00 00 00 52 empty)
//   PC -> ACK
//   loop 9x:  LT -> 128 data bytes ; LT -> 1 BCC byte ; PC -> ACK
//   LT -> EOT
//   PC -> ACK
// ----------------------------------------------------------------------------
NidekTracer::TraceResult NidekTracer::runNidekNative_(TraceMode mode) {
    TraceResult r;
    r.protocolUsed = Protocol::NidekNative;

    if (!port_.isOpen()) {
        r.error = "port not open";
        return r;
    }
    port_.purge();

    auto sendByte = [&](uint8_t b, const char* label) -> bool {
        if (port_.write(&b, 1) != 1) {
            r.error = std::string{"write failed ("} + label + "): " + port_.errMsg();
            log_error(r.error);
            return false;
        }
        log_debug(std::string{"PC -> "} + label);
        return true;
    };

    auto recvByte = [&](uint32_t totalMs, const char* what) -> std::optional<uint8_t> {
        auto bytes = port_.readExact(1, totalMs);
        if (bytes.size() != 1) {
            r.error = std::string{"timeout waiting for "} + what;
            log_error(r.error);
            return std::nullopt;
        }
        log_debug(std::string{"LT -> 0x"} + hexByte(bytes[0]));
        return bytes[0];
    };

    // --- handshake ---------------------------------------------------------
    if (!sendByte(ENQ, "ENQ")) return r;
    auto ack1 = recvByte(2000, "ACK after ENQ");
    if (!ack1) return r;
    if (*ack1 != ACK) {
        r.error = "expected ACK after ENQ, got 0x" + hexByte(*ack1);
        log_error(r.error);
        return r;
    }

    if (port_.write(kReadCmd.data(), kReadCmd.size()) != static_cast<int>(kReadCmd.size())) {
        r.error = "write failed (READ cmd): " + port_.errMsg();
        log_error(r.error);
        return r;
    }
    log_debug("PC -> READ 52 00 00 09 5B");

    auto ack2 = recvByte(2000, "ACK after READ");
    if (!ack2) return r;
    if (*ack2 != ACK) {
        r.error = "expected ACK after READ cmd, got 0x" + hexByte(*ack2);
        log_error(r.error);
        return r;
    }

    if (!sendByte(EOT, "EOT")) return r;

    // --- wait for the operator to press scan ------------------------------
    log_info("Press the scan button on the LT-980 to start the trace");
    auto enq = recvByte(120000, "device ENQ (press the scan key on the tracer)");
    if (!enq) return r;
    if (*enq != ENQ) {
        r.error = "expected ENQ from device, got 0x" + hexByte(*enq);
        log_error(r.error);
        return r;
    }
    if (!sendByte(ACK, "ACK")) return r;

    // --- 5-byte status packet ---------------------------------------------
    auto status = port_.readExact(5, 5000);
    if (status.size() != 5) {
        r.error = "timeout reading 5-byte status packet";
        log_error(r.error);
        return r;
    }
    log_debug("LT -> status " + hexBytes(status.data(), status.size()));

    if (std::equal(status.begin(), status.end(), kEmptyAck.begin())) {
        r.error = "Tracer has no shape in memory.";
        log_error(r.error);
        return r;
    }
    if (!std::equal(status.begin(), status.end(), kGoodAck.begin())) {
        r.error = "Unexpected status packet: " + hexBytes(status.data(), status.size());
        log_error(r.error);
        return r;
    }
    if (!sendByte(ACK, "ACK")) return r;

    // --- 9 x (128 data bytes + 1 BCC byte) --------------------------------
    std::vector<uint8_t> payload;
    payload.reserve(kPayloadLen);

    for (size_t i = 0; i < kChunkCount; ++i) {
        auto chunk = port_.readExact(kChunkSize, 5000);
        if (chunk.size() != kChunkSize) {
            r.error = "short chunk " + std::to_string(i + 1) + "/" +
                      std::to_string(kChunkCount) + " (got " +
                      std::to_string(chunk.size()) + " bytes)";
            log_error(r.error);
            r.rawPayload = std::move(payload);
            return r;
        }
        // The 1-byte trailer is a per-chunk BCC. We log it but don't enforce
        // it yet -- if a firmware revision changes the algorithm we'd rather
        // capture the data than reject it. Once we've validated a real
        // session against the BCC formula this turns into a check.
        auto bccBytes = port_.readExact(1, 2000);
        if (bccBytes.size() != 1) {
            r.error = "timeout reading chunk BCC " + std::to_string(i + 1);
            log_error(r.error);
            r.rawPayload = std::move(payload);
            return r;
        }
        uint8_t expected = xorBcc(chunk.data(), chunk.size());
        if (bccBytes[0] != expected) {
            log_warn("chunk " + std::to_string(i + 1) + " BCC mismatch (got 0x" +
                     hexByte(bccBytes[0]) + ", xor=0x" + hexByte(expected) + ")");
        }
        payload.insert(payload.end(), chunk.begin(), chunk.end());
        if (!sendByte(ACK, "ACK")) {
            r.rawPayload = std::move(payload);
            return r;
        }
    }

    // --- final EOT --------------------------------------------------------
    auto eot = recvByte(2000, "trailing EOT");
    if (!eot) {
        r.rawPayload = std::move(payload);
        return r;
    }
    if (*eot != EOT) {
        log_warn("expected EOT (0x04) at end, got 0x" + hexByte(*eot));
    }
    sendByte(ACK, "ACK"); // best-effort, ignore failure

    log_info("Received " + std::to_string(payload.size()) + " bytes from tracer");

    r.rawPayload = std::move(payload);
    r.rawText    = toAscii(r.rawPayload);
    bestEffortParseNidekPayload_(r.rawPayload, r, mode);
    // We mark success as soon as we got the full payload. Parsing into A/B
    // box values is best-effort while we lock down the field layout from
    // real captures; the UI shows whatever we extracted plus the raw hex.
    r.success = (r.rawPayload.size() == kPayloadLen);
    if (!r.success && r.error.empty()) {
        r.error = "short payload";
    }
    return r;
}

// ----------------------------------------------------------------------------
// Best-effort parse of the Nidek native 1152-byte payload.
//
// The OEM client encoder (`sub_BFEC00` in FRWTracerClient.exe) reveals the
// shape of the format: a header byte ('F' or 'P'), a few packed shorts, a
// polar-point array (angle + radius as multi-byte deltas), and a 0x88-filled
// tail. We don't yet have a confirmed byte-by-byte layout, so this function
// only does what we can be sure about. The full parser drops in here once we
// have a sniffed capture to validate against.
// ----------------------------------------------------------------------------
void NidekTracer::bestEffortParseNidekPayload_(const std::vector<uint8_t>& bytes,
                                               TraceResult& /*out*/,
                                               TraceMode /*mode*/) {
    if (bytes.size() < 16) return;

    // The byte at offset 0 is the format / job tag ('F' = frame, 'P' = pattern).
    char fmt = static_cast<char>(bytes[0]);
    if (fmt == 'F' || fmt == 'P') {
        log_debug(std::string{"Nidek payload format = '"} + fmt + "'");
    }

    // Anything else we don't trust yet -- leave aBoxL/bBoxL/.../points empty.
    // The UI will show "received N bytes" plus the raw hex dump and the user
    // (or we) can finalize the layout from one sniffed capture.
}

// ----------------------------------------------------------------------------
// Protocol B -- VCA / OMA passive listener.
//
// The PC opens the port and waits. When the operator triggers the trace, the
// device sends one or more frames:
//
//     FS  <body...>  ETX  BCC
//
// Body is a CRLF/RS-separated list of ASCII "TAG=value" pairs. BCC is the
// XOR of every byte from the byte after FS up to and including ETX.
//
// We accept any single frame here and parse the tag/value pairs we know.
// Multi-frame (init handshake / job-request) flows used by Visionix-style
// stations are TODO -- the LT-980 doesn't use them and we don't have one to
// sniff against.
// ----------------------------------------------------------------------------
NidekTracer::TraceResult NidekTracer::runVca_(TraceMode mode) {
    TraceResult r;
    r.protocolUsed = Protocol::Vca;

    if (!port_.isOpen()) {
        r.error = "port not open";
        return r;
    }
    port_.purge();

    log_info("VCA mode: waiting for FS-framed message from device "
             "(press the scan key on the tracer)");

    // Discard bytes until we see FS, then collect until ETX (then read BCC).
    std::vector<uint8_t> body;
    bool started = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < deadline) {
        auto chunk = port_.readExact(1, 500);
        if (chunk.empty()) continue;
        uint8_t b = chunk[0];
        if (!started) {
            if (b == FS) {
                started = true;
                body.clear();
            }
            continue;
        }
        body.push_back(b);
        if (b == ETX) {
            auto bccBytes = port_.readExact(1, 1000);
            if (bccBytes.size() != 1) {
                r.error = "timeout reading VCA BCC";
                log_error(r.error);
                return r;
            }
            uint8_t got = bccBytes[0];
            uint8_t expected = xorBcc(body.data(), body.size());
            if (got != expected) {
                log_warn("VCA BCC mismatch (got 0x" + hexByte(got) +
                         ", computed 0x" + hexByte(expected) + ") -- NAK");
                uint8_t nak = NAK;
                port_.write(&nak, 1);
                r.error = "BCC mismatch";
                return r;
            }
            uint8_t ack = ACK;
            port_.write(&ack, 1);
            // Strip the trailing ETX for storage / parsing.
            body.pop_back();
            r.rawPayload = std::move(body);
            r.rawText = toAscii(r.rawPayload);

            // Pull the tags we know from the VCA spec.
            r.aBoxL = vcaFindKeyed(r.rawText, "HBOX");
            r.bBoxL = vcaFindKeyed(r.rawText, "VBOX");
            r.aBoxR = r.aBoxL;
            r.bBoxR = r.bBoxL;
            r.dbl   = vcaFindKeyed(r.rawText, "DBL");
            auto circ = vcaFindKeyed(r.rawText, "CIRC");
            r.circumferenceL = circ;
            r.circumferenceR = circ;

            r.success = r.aBoxL.has_value() && r.bBoxL.has_value();
            if (!r.success) {
                r.error = "VCA frame parsed but HBOX/VBOX not present";
            }
            log_info("VCA: received " + std::to_string(r.rawPayload.size()) +
                     " bytes; ACK sent");
            return r;
        }
        if (body.size() > 16 * 1024) {
            r.error = "VCA body exceeded 16K without ETX";
            log_error(r.error);
            return r;
        }
    }

    r.error = "VCA listener timed out (no FS-framed message in 120 s)";
    log_error(r.error);
    (void)mode;
    return r;
}

NidekTracer::TraceResult NidekTracer::tryParseVcaResponse(const std::vector<uint8_t>& bytes) {
    TraceResult r;
    r.protocolUsed = Protocol::Vca;
    r.rawPayload = bytes;
    r.rawText = toAscii(bytes);
    r.aBoxL = vcaFindKeyed(r.rawText, "HBOX");
    r.bBoxL = vcaFindKeyed(r.rawText, "VBOX");
    r.aBoxR = r.aBoxL;
    r.bBoxR = r.bBoxL;
    r.dbl   = vcaFindKeyed(r.rawText, "DBL");
    r.success = r.aBoxL.has_value() && r.bBoxL.has_value();
    return r;
}

// ----------------------------------------------------------------------------
// Sniffer / monitor mode. Open the port (if not yet), then loop forwarding
// every received byte to the wire callback. Returns when *stop becomes true.
// ----------------------------------------------------------------------------
std::string NidekTracer::monitor(std::atomic<bool>* stop) {
    if (!port_.isOpen()) return "port not open";
    log_info("Sniffer started -- listening passively");
    while (stop && !stop->load()) {
        // 250 ms timeout -- we just want the wire callback to fire as bytes
        // arrive. readExact() invokes the callback on every ReadFile chunk.
        (void)port_.readExact(512, 250);
    }
    log_info("Sniffer stopped");
    return {};
}

} // namespace vtracer

#pragma once

#include "SerialPort.h"

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vtracer {

// Drives a Nidek lens tracer (LT-980 family, no-display variant) over a
// serial port. Blocking; safe to run on a worker thread.
//
// Protocol selection is a runtime switch:
//   * NidekNative -- the 5-byte XOR-BCC binary protocol that ships with the
//     LT-980 firmware. PC initiates the read, then the operator presses the
//     scan button on the device and the tracer streams 1152 bytes.
//   * Vca         -- the open VCA / OMA standard (FS-framed ASCII messages).
//     Used by newer Visionix / Briot / Weco models. Passive: we listen for
//     the device-initiated frame, validate BCC, ack.
//
// Both flows wait on the operator pressing the trace button on the device.
class NidekTracer {
public:
    enum class Protocol { NidekNative, Vca };
    enum class TraceMode { Both, Left, Right, Center };

    // Centered on the lens geometric center, math-style axes (+x right, +y up).
    struct TracePoint { double x; double y; };

    struct TraceResult {
        bool success{false};
        std::string error;
        Protocol protocolUsed{Protocol::NidekNative};

        std::optional<double> aBoxL;
        std::optional<double> bBoxL;
        std::optional<double> aBoxR;
        std::optional<double> bBoxR;

        std::optional<double> dbl;
        std::optional<double> circumferenceL;
        std::optional<double> circumferenceR;

        std::vector<TracePoint> pointsL;
        std::vector<TracePoint> pointsR;

        // ASCII rendering of rawPayload, dots in place of non-printables.
        std::string rawText;

        // Verbatim bytes of the trace payload, post-handshake. For Nidek
        // native this is the 1152-byte binary blob; for VCA this is the
        // FS-framed message body (between FS and ETX, BCC excluded).
        std::vector<uint8_t> rawPayload;
    };

    using LogFn = std::function<void(std::string_view level, std::string_view msg)>;

    explicit NidekTracer(LogFn log = {}) : log_(std::move(log)) {}

    std::string open(std::string_view portName);
    void close() { port_.close(); }
    [[nodiscard]] bool isOpen() const { return port_.isOpen(); }

    void setProtocol(Protocol p) { protocol_ = p; }
    [[nodiscard]] Protocol protocol() const { return protocol_; }

    // Forward every wire byte (read or written) to the sniffer hook.
    // Must be set before open() if you want to capture the handshake too.
    void setWireCallback(SerialPort::WireCallback cb) { port_.setWireCallback(std::move(cb)); }

    [[nodiscard]] TraceResult runTrace(TraceMode mode = TraceMode::Both);

    // Passive sniffer: opens the port (if not already open) and dumps every
    // received byte to the wire callback until *stop becomes true. Returns
    // empty error on clean stop.
    std::string monitor(std::atomic<bool>* stop);

    // Exposed so a unit test can verify framing without hardware.
    [[nodiscard]] static std::vector<uint8_t> buildNidekReadPacket();
    [[nodiscard]] static uint8_t xorBcc(const uint8_t* data, size_t len);
    [[nodiscard]] static TraceResult tryParseVcaResponse(const std::vector<uint8_t>& bytes);

private:
    void log_info(std::string_view m)  const { if (log_) log_("info",  m); }
    void log_warn(std::string_view m)  const { if (log_) log_("warn",  m); }
    void log_error(std::string_view m) const { if (log_) log_("error", m); }
    void log_debug(std::string_view m) const { if (log_) log_("debug", m); }

    TraceResult runNidekNative_(TraceMode mode);
    TraceResult runVca_(TraceMode mode);

    void bestEffortParseNidekPayload_(const std::vector<uint8_t>& bytes,
                                      TraceResult& out,
                                      TraceMode mode);

    SerialPort port_;
    LogFn log_;
    Protocol protocol_{Protocol::NidekNative};
};

} // namespace vtracer

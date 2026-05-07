#pragma once

#include "SerialPort.h"

#include <optional>
#include <string>
#include <vector>
#include <functional>

namespace vtracer {

// Drives a Nidek LT-980 lens tracer over a serial port. Blocking; safe to
// run on a worker thread (does not touch the UI).
class NidekTracer {
public:
    enum class TraceMode { Both, Left, Right, Center };

    // Centered on the lens geometric center, math-style axes (+x right, +y up).
    struct TracePoint { double x; double y; };

    struct TraceResult {
        bool success{false};
        std::string error;

        std::optional<double> aBoxL;
        std::optional<double> bBoxL;
        std::optional<double> aBoxR;
        std::optional<double> bBoxR;

        std::optional<double> dbl;
        std::optional<double> circumferenceL;
        std::optional<double> circumferenceR;

        // Polar outline projected to cartesian. Empty when the firmware
        // doesn't emit them under any recognized key.
        std::vector<TracePoint> pointsL;
        std::vector<TracePoint> pointsR;

        std::string rawText;
    };

    using LogFn = std::function<void(std::string_view level, std::string_view msg)>;

    explicit NidekTracer(LogFn log = {}) : log_(std::move(log)) {}

    std::string open(std::string_view portName);
    void close() { port_.close(); }
    [[nodiscard]] bool isOpen() const { return port_.isOpen(); }

    [[nodiscard]] TraceResult runTrace(TraceMode mode = TraceMode::Both);

    // Exposed so a serial sniffer comparison can replay parsing offline.
    [[nodiscard]] static std::vector<uint8_t> buildRequest(TraceMode mode = TraceMode::Both);
    [[nodiscard]] static TraceResult tryParseResponse(const std::vector<uint8_t>& bytes);

private:
    void log_info(std::string_view m)  const { if (log_) log_("info",  m); }
    void log_warn(std::string_view m)  const { if (log_) log_("warn",  m); }
    void log_error(std::string_view m) const { if (log_) log_("error", m); }
    void log_debug(std::string_view m) const { if (log_) log_("debug", m); }

    SerialPort port_;
    LogFn log_;
};

} // namespace vtracer

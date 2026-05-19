#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace vtracer {

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort() { close(); }
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    struct Settings {
        uint32_t baud{9600};
        uint8_t  dataBits{8};
        bool     evenParity{false};
        bool     parityEnabled{false};
        uint8_t  stopBits{ONESTOPBIT}; // ONESTOPBIT, ONE5STOPBITS, TWOSTOPBITS
        // Per-byte read timeout in ms.  0 disables.
        uint32_t readIntervalMs{50};
        // Total timeout for any single ReadFile call (ms).
        uint32_t readTotalMs{2000};
        uint32_t writeTotalMs{2000};
    };

    // Sniffer hook. dir is 't' for bytes the PC sent, 'r' for bytes received.
    // Fires once per ReadFile/WriteFile chunk on the worker thread; the
    // callback must be thread-safe and non-blocking.
    using WireCallback = std::function<void(char dir, const uint8_t* data, size_t n)>;
    void setWireCallback(WireCallback cb) { wireCb_ = std::move(cb); }

    // portName: "COM3", "COM12", etc.  Returns empty error on success.
    std::string open(std::string_view portName, const Settings& s);
    bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
    void close();

    // Returns number of bytes read (>= 0) or -1 on error.  errMsg() is set on -1.
    int read(uint8_t* buf, size_t maxBytes);
    int write(const uint8_t* buf, size_t bytes);

    // Convenience.
    int write(std::string_view s) {
        return write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    // Read exactly `n` bytes, or fewer if `totalMs` elapses first. Used by
    // fixed-length protocols (Nidek native: 128-byte chunks, 5-byte status, ...).
    std::vector<uint8_t> readExact(size_t n, uint32_t totalMs);

    // Drain whatever is currently in the input buffer.  Stops once the device
    // has been silent for `quietMs` milliseconds or `maxBytes` is hit.
    std::vector<uint8_t> readUntilQuiet(uint32_t quietMs, size_t maxBytes = 64 * 1024);

    // Read until `terminator` byte appears, or timeout.  Includes the terminator.
    std::vector<uint8_t> readUntilByte(uint8_t terminator, uint32_t totalMs, size_t maxBytes = 64 * 1024);

    void purge();
    const std::string& errMsg() const { return err_; }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::string err_;
    WireCallback wireCb_;
    void setLastError_(std::string_view ctx);
    void emitWire_(char dir, const uint8_t* data, size_t n);
};

std::vector<std::string> listSerialPorts();

} // namespace vtracer

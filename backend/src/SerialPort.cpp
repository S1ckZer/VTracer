#include "SerialPort.h"
#include "Util.h"

#include <chrono>
#include <thread>

namespace vtracer {

void SerialPort::setLastError_(std::string_view ctx) {
    DWORD code = ::GetLastError();
    char  buf[256] = {0};
    ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                     buf, sizeof(buf), nullptr);
    err_.assign(ctx);
    err_.append(": ");
    err_.append(buf);
    if (!err_.empty() && err_.back() == '\n') err_.pop_back();
    if (!err_.empty() && err_.back() == '\r') err_.pop_back();
}

std::string SerialPort::open(std::string_view portName, const Settings& s) {
    close();

    // \\.\ namespace is required for COM10 and above.
    std::string path = "\\\\.\\";
    path.append(portName);
    std::wstring wpath = utf8ToWide(path);

    handle_ = ::CreateFileW(wpath.c_str(),
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        setLastError_("Open serial port");
        return err_;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!::GetCommState(handle_, &dcb)) {
        setLastError_("GetCommState");
        close();
        return err_;
    }
    dcb.BaudRate = s.baud;
    dcb.ByteSize = s.dataBits;
    dcb.fParity  = s.parityEnabled ? TRUE : FALSE;
    dcb.Parity   = s.parityEnabled ? (s.evenParity ? EVENPARITY : ODDPARITY) : NOPARITY;
    dcb.StopBits = s.stopBits;
    dcb.fBinary  = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!::SetCommState(handle_, &dcb)) {
        setLastError_("SetCommState");
        close();
        return err_;
    }

    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout         = s.readIntervalMs;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = s.readTotalMs;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = s.writeTotalMs;
    if (!::SetCommTimeouts(handle_, &to)) {
        setLastError_("SetCommTimeouts");
        close();
        return err_;
    }

    purge();
    err_.clear();
    return {};
}

void SerialPort::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

int SerialPort::read(uint8_t* buf, size_t maxBytes) {
    if (!isOpen()) { err_ = "port not open"; return -1; }
    DWORD got = 0;
    BOOL ok = ::ReadFile(handle_, buf, static_cast<DWORD>(maxBytes), &got, nullptr);
    if (!ok) { setLastError_("ReadFile"); return -1; }
    if (got > 0) emitWire_('r', buf, got);
    return static_cast<int>(got);
}

int SerialPort::write(const uint8_t* buf, size_t bytes) {
    if (!isOpen()) { err_ = "port not open"; return -1; }
    DWORD wrote = 0;
    BOOL ok = ::WriteFile(handle_, buf, static_cast<DWORD>(bytes), &wrote, nullptr);
    if (!ok) { setLastError_("WriteFile"); return -1; }
    if (wrote > 0) emitWire_('t', buf, wrote);
    return static_cast<int>(wrote);
}

void SerialPort::purge() {
    if (!isOpen()) return;
    ::PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);
}

void SerialPort::emitWire_(char dir, const uint8_t* data, size_t n) {
    if (wireCb_ && n) wireCb_(dir, data, n);
}

std::vector<uint8_t> SerialPort::readExact(size_t n, uint32_t totalMs) {
    std::vector<uint8_t> out;
    out.reserve(n);
    if (!isOpen()) return out;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(totalMs);
    uint8_t tmp[512];
    while (out.size() < n) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        size_t want = std::min(n - out.size(), sizeof(tmp));
        DWORD got = 0;
        BOOL ok = ::ReadFile(handle_, tmp, static_cast<DWORD>(want), &got, nullptr);
        if (!ok) { setLastError_("ReadFile"); break; }
        if (got > 0) {
            emitWire_('r', tmp, got);
            out.insert(out.end(), tmp, tmp + got);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return out;
}

std::vector<uint8_t> SerialPort::readUntilQuiet(uint32_t quietMs, size_t maxBytes) {
    std::vector<uint8_t> out;
    if (!isOpen()) return out;

    auto lastByte = std::chrono::steady_clock::now();
    uint8_t tmp[512];
    while (out.size() < maxBytes) {
        DWORD got = 0;
        BOOL ok = ::ReadFile(handle_, tmp, sizeof(tmp), &got, nullptr);
        if (!ok) { setLastError_("ReadFile"); break; }
        if (got > 0) {
            emitWire_('r', tmp, got);
            out.insert(out.end(), tmp, tmp + got);
            lastByte = std::chrono::steady_clock::now();
        } else {
            auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - lastByte).count();
            if (idle >= quietMs) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    return out;
}

std::vector<uint8_t> SerialPort::readUntilByte(uint8_t terminator, uint32_t totalMs, size_t maxBytes) {
    std::vector<uint8_t> out;
    if (!isOpen()) return out;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(totalMs);
    uint8_t tmp[256];
    while (out.size() < maxBytes && std::chrono::steady_clock::now() < deadline) {
        DWORD got = 0;
        BOOL ok = ::ReadFile(handle_, tmp, sizeof(tmp), &got, nullptr);
        if (!ok) { setLastError_("ReadFile"); break; }
        if (got > 0) emitWire_('r', tmp, got);
        for (DWORD i = 0; i < got; ++i) {
            out.push_back(tmp[i]);
            if (tmp[i] == terminator) return out;
        }
        if (got == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return out;
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> ports;
    HKEY key{};
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                        L"HARDWARE\\DEVICEMAP\\SERIALCOMM",
                        0, KEY_READ, &key) != ERROR_SUCCESS) {
        return ports;
    }
    wchar_t name[256];
    BYTE    data[256];
    DWORD i = 0;
    for (;; ++i) {
        DWORD nameLen = 256;
        DWORD dataLen = sizeof(data);
        DWORD type = 0;
        LONG  rc = ::RegEnumValueW(key, i, name, &nameLen, nullptr, &type, data, &dataLen);
        if (rc != ERROR_SUCCESS) break;
        if (type == REG_SZ) {
            std::wstring_view w{reinterpret_cast<const wchar_t*>(data), dataLen / sizeof(wchar_t)};
            while (!w.empty() && w.back() == 0) w.remove_suffix(1);
            ports.push_back(wideToUtf8(w));
        }
    }
    ::RegCloseKey(key);
    return ports;
}

} // namespace vtracer

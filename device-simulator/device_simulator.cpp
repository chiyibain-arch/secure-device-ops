// SecureDeviceOps - Synthetic Device Simulator
//
// Represents embedded/device-side software for several synthetic medical
// devices. Each simulated device periodically generates a plausible-looking
// (but entirely fake) telemetry reading and POSTs it as JSON to the
// SecureDeviceOps FastAPI backend.
//
// DEPENDENCY NOTE:
//   This file sends HTTP requests using WinHTTP, which ships with Windows
//   and requires no external HTTP library. It links against winhttp.lib
//   (pulled in below via #pragma comment) and is Windows-only.
//
//   To build on Linux/macOS instead, replace the HTTP transport (the
//   postTelemetry function) with a library such as libcurl, and remove the
//   <windows.h>/<winhttp.h> includes and the #pragma comment line.
//
// BUILD (Windows, Developer Command Prompt / MSVC):
//   cl /EHsc device_simulator.cpp
//
// BUILD (MinGW-w64 g++):
//   g++ -std=c++17 device_simulator.cpp -o device_simulator.exe -lwinhttp
//
// RUN:
//   device_simulator.exe
//
// The FastAPI backend must already be running at http://127.0.0.1:8000
// (see README.md) before telemetry can be delivered successfully.

// NOMINMAX prevents windows.h from defining min/max macros that would
// otherwise clash with std::min/std::max used below.
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

constexpr wchar_t kApiHost[] = L"127.0.0.1";
constexpr INTERNET_PORT kApiPort = 8000;
constexpr wchar_t kApiPath[] = L"/api/v1/telemetry";
constexpr int kSecondsBetweenReadings = 3;

// Mutable, per-device simulated state. Values drift randomly each tick to
// mimic a real device rather than jumping to unrelated random numbers.
struct SimulatedDevice {
    std::string device_id;
    std::string device_type;
    std::string firmware_version;
    double heart_rate_bpm;
    double battery_percent;
    double temperature_c;
};

std::mt19937& rng() {
    static std::mt19937 generator(std::random_device{}());
    return generator;
}

double randomDelta(double magnitude) {
    std::uniform_real_distribution<double> dist(-magnitude, magnitude);
    return dist(rng());
}

double clampValue(double value, double min_value, double max_value) {
    return std::max(min_value, std::min(max_value, value));
}

// Applies a small random walk to each reading and keeps results within the
// ranges accepted by the backend's Pydantic model (heart rate 30-220,
// battery 0-100, temperature 30.0-45.0 Celsius).
void advanceTelemetry(SimulatedDevice& device) {
    device.heart_rate_bpm = clampValue(
        device.heart_rate_bpm + randomDelta(3.0), 30.0, 220.0);

    // Battery drains slowly over time, with rare small recharges.
    device.battery_percent = clampValue(
        device.battery_percent - (randomDelta(0.6) + 0.15), 0.0, 100.0);

    device.temperature_c = clampValue(
        device.temperature_c + randomDelta(0.15), 30.0, 45.0);
}

// Derives a clinically-flavored (but synthetic) status from the current
// readings so the dashboard shows a realistic mix of NORMAL/WARNING/CRITICAL.
std::string deriveStatus(const SimulatedDevice& device) {
    const bool heart_rate_critical =
        device.heart_rate_bpm < 45.0 || device.heart_rate_bpm > 130.0;
    const bool heart_rate_warning =
        device.heart_rate_bpm < 55.0 || device.heart_rate_bpm > 110.0;

    const bool battery_critical = device.battery_percent < 10.0;
    const bool battery_warning = device.battery_percent < 25.0;

    const bool temperature_critical = device.temperature_c > 39.0;
    const bool temperature_warning = device.temperature_c > 38.0;

    if (heart_rate_critical || battery_critical || temperature_critical) {
        return "CRITICAL";
    }

    if (heart_rate_warning || battery_warning || temperature_warning) {
        return "WARNING";
    }

    return "NORMAL";
}

std::string formatFixed(double value, int decimals) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(decimals);
    stream << value;
    return stream.str();
}

std::string buildTelemetryJson(const SimulatedDevice& device, const std::string& status) {
    std::ostringstream json;
    json << "{"
         << "\"device_id\":\"" << device.device_id << "\","
         << "\"device_type\":\"" << device.device_type << "\","
         << "\"firmware_version\":\"" << device.firmware_version << "\","
         << "\"heart_rate_bpm\":" << static_cast<int>(std::lround(device.heart_rate_bpm)) << ","
         << "\"battery_percent\":" << static_cast<int>(std::lround(device.battery_percent)) << ","
         << "\"temperature_c\":" << formatFixed(device.temperature_c, 1) << ","
         << "\"status\":\"" << status << "\""
         << "}";
    return json.str();
}

// Sends one JSON telemetry payload to the backend over HTTP using WinHTTP.
// Returns true on a 2xx response; logs a clear message and returns false on
// any connection, request, or server-side failure so the simulator can keep
// running instead of crashing when the API is unreachable.
bool postTelemetry(const std::string& json_body) {
    HINTERNET session = WinHttpOpen(
        L"SecureDeviceOps-Simulator/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!session) {
        std::cerr << "[simulator] Failed to initialize WinHTTP session (error "
                  << GetLastError() << ")\n";
        return false;
    }

    HINTERNET connection = WinHttpConnect(session, kApiHost, kApiPort, 0);
    if (!connection) {
        std::cerr << "[simulator] Unable to reach API host " << "127.0.0.1:8000"
                  << " (error " << GetLastError() << "). Is the backend running?\n";
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        kApiPath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0);

    if (!request) {
        std::cerr << "[simulator] Failed to create HTTP request (error "
                  << GetLastError() << ")\n";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    const std::wstring headers = L"Content-Type: application/json\r\n";

    const BOOL sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(-1),
        const_cast<char*>(json_body.data()),
        static_cast<DWORD>(json_body.size()),
        static_cast<DWORD>(json_body.size()),
        0);

    bool success = false;

    if (!sent) {
        std::cerr << "[simulator] Failed to send telemetry request (error "
                  << GetLastError() << ")\n";
    } else if (!WinHttpReceiveResponse(request, nullptr)) {
        std::cerr << "[simulator] Failed to receive API response (error "
                  << GetLastError() << ")\n";
    } else {
        DWORD status_code = 0;
        DWORD size = sizeof(status_code);

        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &size,
            WINHTTP_NO_HEADER_INDEX);

        success = status_code >= 200 && status_code < 300;

        if (!success) {
            std::cerr << "[simulator] API rejected telemetry (HTTP "
                      << status_code << ")\n";
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    return success;
}

std::vector<SimulatedDevice> createFleet() {
    return {
        {"MD-SIM-1001", "patient_monitor", "3.2.1", 72.0, 92.0, 36.8},
        {"MD-SIM-2001", "infusion_pump", "2.8.4", 78.0, 85.0, 37.0},
        {"MD-SIM-3001", "cardiac_monitor", "4.0.0", 68.0, 97.0, 36.6},
    };
}

}  // namespace

int main() {
    std::vector<SimulatedDevice> fleet = createFleet();

    std::cout << "SecureDeviceOps device simulator starting.\n"
              << "Sending telemetry to http://127.0.0.1:8000/api/v1/telemetry\n"
              << "Simulating " << fleet.size() << " devices. Press Ctrl+C to stop.\n\n";
    std::cout.flush();

    while (true) {
        for (SimulatedDevice& device : fleet) {
            advanceTelemetry(device);
            const std::string status = deriveStatus(device);
            const std::string json_body = buildTelemetryJson(device, status);

            const bool delivered = postTelemetry(json_body);

            std::cout << "[" << device.device_id << "] status=" << status
                      << " heart_rate=" << static_cast<int>(std::lround(device.heart_rate_bpm))
                      << " battery=" << static_cast<int>(std::lround(device.battery_percent))
                      << " temp=" << formatFixed(device.temperature_c, 1)
                      << " -> " << (delivered ? "delivered" : "DELIVERY FAILED")
                      << "\n";
            std::cout.flush();
        }

        std::this_thread::sleep_for(std::chrono::seconds(kSecondsBetweenReadings));
    }

    return 0;
}

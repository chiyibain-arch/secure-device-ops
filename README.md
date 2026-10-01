# SecureDeviceOps

SecureDeviceOps is a realistic **synthetic** device-platform application used
to model an enterprise environment where embedded/device software generates
operational telemetry and sends it to a backend platform.

> **Synthetic-data disclaimer:** This project does not represent any real
> company's architecture and does not contain real patient data or PHI. All
> device identifiers, firmware versions, and telemetry values are randomly
> generated for demonstration and DevSecOps-training purposes only.

## 1. System Purpose

The application models a small fleet of medical-style devices that report
operational telemetry (heart rate, battery, temperature, status) to a central
backend, which persists the data and exposes it to an operations dashboard.

## 2. Architecture

```
Device Simulator (C++)
        |
        | JSON telemetry over HTTP POST
        v
FastAPI Backend (Python)
        |
        v
SQLite Database (secure_device.db)
        |
        v
FastAPI (HTTP GET / JSON)
        |
        v
Browser Operations Dashboard (HTML/JS)
```

## 3. Components

| Component | Location | Description |
|---|---|---|
| Backend API | `app/main.py` | FastAPI service that validates, stores, and serves telemetry |
| Database | `app/secure_device.db` | SQLite file, created automatically on first run (not committed) |
| Device Simulator | `device-simulator/device_simulator.cpp` | C++ program simulating 3 synthetic devices that POST telemetry |
| Dashboard | `app/dashboard.html` | Static HTML/JS page that polls the API and renders telemetry |

## 4. Requirements

### Backend
- Python 3.10+
- Dependencies listed in [app/requirements.txt](app/requirements.txt)

### Device Simulator
- Windows with the MSVC toolchain (Visual Studio "Desktop development with
  C++" workload) **or** MinGW-w64 (`g++`)
- Uses **WinHTTP** (`winhttp.h` / `winhttp.lib`), which ships with Windows —
  no external HTTP library needs to be installed. This makes the simulator
  Windows-only as written.
  - To port it to Linux/macOS, replace the `postTelemetry` function (WinHTTP
    calls) with an equivalent using a library such as libcurl, and remove the
    `<windows.h>` / `<winhttp.h>` includes.

## 5. Running the Backend

```powershell
cd app
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn main:app --host 127.0.0.1 --port 8000
```

The SQLite database file `secure_device.db` is created automatically in the
`app/` directory on first startup, and telemetry persists across restarts.

## 6. Building and Running the C++ Simulator

From a **Developer PowerShell/Command Prompt for VS** (or after running
`vcvars64.bat`):

```powershell
cd device-simulator
cl /EHsc device_simulator.cpp
.\device_simulator.exe
```

Or with MinGW-w64:

```powershell
cd device-simulator
g++ -std=c++17 device_simulator.cpp -o device_simulator.exe -lwinhttp
.\device_simulator.exe
```

The simulator continuously sends synthetic telemetry for three devices
(`MD-SIM-1001` patient_monitor, `MD-SIM-2001` infusion_pump, `MD-SIM-3001`
cardiac_monitor) to `http://127.0.0.1:8000/api/v1/telemetry` every few
seconds, and logs delivery success/failure to the console. The backend must
already be running, or requests will fail (the simulator logs this and keeps
retrying on the next cycle instead of crashing).

## 7. Using the Dashboard

With the backend running, open `app/dashboard.html` directly in a browser
(double-click it, or use `Start-Process app/dashboard.html` from
PowerShell). It polls `GET /api/v1/telemetry` every 5 seconds and displays:

- Total telemetry records
- Number of unique devices
- Number of WARNING events
- Number of CRITICAL events
- A table of all telemetry with status color-coding (green/orange/red)

If the API is unreachable, the dashboard shows an inline error message
instead of failing silently.

## 8. API Endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/healthz` | Application health check |
| GET | `/readyz` | Verifies database connectivity |
| POST | `/api/v1/telemetry` | Submit a telemetry reading (validated via Pydantic) |
| GET | `/api/v1/telemetry` | List all stored telemetry records |
| GET | `/api/v1/devices/{device_id}/telemetry` | Telemetry for one device (404 if none) |

### Telemetry schema

```json
{
  "device_id": "MD-SIM-1001",
  "device_type": "patient_monitor",
  "firmware_version": "3.2.1",
  "heart_rate_bpm": 72,
  "battery_percent": 91,
  "temperature_c": 36.8,
  "status": "NORMAL"
}
```

- `device_type`: `patient_monitor` | `infusion_pump` | `cardiac_monitor`
- `status`: `NORMAL` | `WARNING` | `CRITICAL`
- `received_at` is set server-side and returned in responses

## 9. Validation Performed

The following was manually verified during development:

- Backend starts and creates `secure_device.db` automatically
- `/healthz` and `/readyz` return 200
- `POST /api/v1/telemetry` validates input and persists records
- `GET /api/v1/telemetry` and `GET /api/v1/devices/{id}/telemetry` return
  stored data (404 for unknown devices)
- The compiled C++ simulator successfully delivers telemetry over HTTP to
  the running backend
- The dashboard, opened as a local file, retrieves and renders live
  telemetry via CORS-enabled API calls

## 10. Out of Scope (for now)

CI/CD, Docker, Kubernetes, Helm, Terraform, Argo CD, and security scanning
are intentionally **not** included yet. These will be added separately as
part of a later DevSecOps delivery-lifecycle phase.

import sqlite3
from datetime import datetime, timezone
from typing import Literal

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field


# =========================================================
# APPLICATION CONFIGURATION
# =========================================================

app = FastAPI(
    title="SecureDeviceOps API",
    description="Synthetic medical-device telemetry service for DevSecOps training",
    version="1.0.0",
)

# Permissive CORS is acceptable here because the dashboard is a local
# file:// page used only for this synthetic training environment.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
)

DATABASE = "secure_device.db"


# =========================================================
# TELEMETRY DATA MODEL AND VALIDATION
# =========================================================

class DeviceTelemetry(BaseModel):
    device_id: str = Field(min_length=3, max_length=50)

    device_type: Literal[
        "patient_monitor",
        "infusion_pump",
        "cardiac_monitor"
    ]

    firmware_version: str

    heart_rate_bpm: int = Field(
        ge=30,
        le=220
    )

    battery_percent: int = Field(
        ge=0,
        le=100
    )

    temperature_c: float = Field(
        ge=30.0,
        le=45.0
    )

    status: Literal[
        "NORMAL",
        "WARNING",
        "CRITICAL"
    ]


# =========================================================
# DATABASE
# =========================================================

def get_db_connection():
    connection = sqlite3.connect(DATABASE)
    connection.row_factory = sqlite3.Row
    return connection


def initialize_database():
    connection = get_db_connection()

    connection.execute("""
        CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT NOT NULL,
            device_type TEXT NOT NULL,
            firmware_version TEXT NOT NULL,
            heart_rate_bpm INTEGER NOT NULL,
            battery_percent INTEGER NOT NULL,
            temperature_c REAL NOT NULL,
            status TEXT NOT NULL,
            received_at TEXT NOT NULL
        )
    """)

    connection.commit()
    connection.close()


initialize_database()


# =========================================================
# OPERATIONAL ENDPOINTS
# =========================================================

@app.get("/healthz")
def health_check():
    return {
        "status": "healthy",
        "service": "secure-device-api",
        "version": "1.0.0"
    }


@app.get("/readyz")
def readiness_check():

    try:
        connection = get_db_connection()
        connection.execute("SELECT 1")
        connection.close()

        return {
            "status": "ready",
            "service": "secure-device-api",
            "database": "connected"
        }

    except sqlite3.Error as error:
        raise HTTPException(
            status_code=503,
            detail="Database unavailable"
        ) from error


# =========================================================
# RECEIVE DEVICE TELEMETRY
# =========================================================

@app.post("/api/v1/telemetry")
def receive_telemetry(telemetry: DeviceTelemetry):

    received_at = datetime.now(
        timezone.utc
    ).isoformat()

    connection = get_db_connection()

    cursor = connection.execute(
        """
        INSERT INTO telemetry (
            device_id,
            device_type,
            firmware_version,
            heart_rate_bpm,
            battery_percent,
            temperature_c,
            status,
            received_at
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            telemetry.device_id,
            telemetry.device_type,
            telemetry.firmware_version,
            telemetry.heart_rate_bpm,
            telemetry.battery_percent,
            telemetry.temperature_c,
            telemetry.status,
            received_at
        )
    )

    connection.commit()
    record_id = cursor.lastrowid
    connection.close()

    return {
        "message": "Telemetry received successfully",
        "record_id": record_id,
        "device_id": telemetry.device_id,
        "status": telemetry.status,
        "received_at": received_at
    }


# =========================================================
# RETRIEVE DEVICE TELEMETRY
# =========================================================

@app.get("/api/v1/telemetry")
def get_telemetry():

    connection = get_db_connection()

    records = connection.execute(
        """
        SELECT
            id,
            device_id,
            device_type,
            firmware_version,
            heart_rate_bpm,
            battery_percent,
            temperature_c,
            status,
            received_at
        FROM telemetry
        ORDER BY id DESC
        """
    ).fetchall()

    connection.close()

    return {
        "total_records": len(records),
        "records": [
            dict(record)
            for record in records
        ]
    }


# =========================================================
# RETRIEVE TELEMETRY FOR ONE DEVICE
# =========================================================

@app.get("/api/v1/devices/{device_id}/telemetry")
def get_device_telemetry(device_id: str):

    connection = get_db_connection()

    records = connection.execute(
        """
        SELECT *
        FROM telemetry
        WHERE device_id = ?
        ORDER BY id DESC
        """,
        (device_id,)
    ).fetchall()

    connection.close()

    if not records:
        raise HTTPException(
            status_code=404,
            detail="No telemetry found for this device"
        )

    return {
        "device_id": device_id,
        "total_records": len(records),
        "records": [
            dict(record)
            for record in records
        ]
    }
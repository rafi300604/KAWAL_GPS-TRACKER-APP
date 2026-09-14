from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field


class TelemetryIn(BaseModel):
    """Payload yang dikirim device -- nama field persis seperti yang sudah
    dikirim firmware ke Firestore, supaya tidak perlu ubah kode device."""

    model_config = ConfigDict(populate_by_name=True)

    sn: str = Field(..., max_length=50, description="DEVICE_SN, jadi identitas device")
    lat: float
    lng: float
    speed_knots: float = Field(0, validation_alias="speedKnots")
    heading: float = Field(0, description="Derajat; 0 kalau modul belum melaporkan arah")
    timestamp: datetime = Field(..., description="Jam satelit GPS (UTC, ISO-8601), bukan NTP")
    name: str | None = Field(None, description="Nama kapal/alat dari config portal")
    power_source: str | None = Field(None, validation_alias="powerSource")
    data_source: str = Field(
        ...,
        validation_alias="dataSource",
        max_length=50,
        description='Biasanya "GSM"/"WIFI", tapi bisa juga string deskriptif seperti '
        '"WIFI (GSM: kuota diduga habis)" -- lihat FIRMWARE_CONTRACT.md',
    )
    status: str | None = Field(None, description='Biasanya "active", khusus jalur WiFi')


class TelemetryOut(BaseModel):
    """Satu baris riwayat dari tabel telemetry (dipakai di /telemetry/{sn}/history).

    validation_alias = nama kolom di tabel SQLAlchemy (input, lewat from_attributes).
    serialization_alias = nama key di JSON response (output, ke klien)."""

    model_config = ConfigDict(from_attributes=True, populate_by_name=True)

    id: int
    sn: str = Field(validation_alias="device_sn")
    lat: float
    lng: float
    speed_knots: float | None = Field(None, serialization_alias="speedKnots")
    heading: float | None = None
    timestamp: datetime = Field(validation_alias="ts")
    data_source: str | None = Field(None, serialization_alias="dataSource")
    power_source: str | None = Field(None, serialization_alias="powerSource")
    received_at: datetime


class DeviceState(BaseModel):
    """State/posisi terkini satu device -- setara satu dokumen Firestore,
    dipakai di /devices dan /devices/{sn}.

    validation_alias = nama kolom di tabel `devices` (input).
    serialization_alias = nama key di JSON response (output)."""

    model_config = ConfigDict(from_attributes=True, populate_by_name=True)

    sn: str
    name: str | None = None
    power_source: str | None = Field(None, serialization_alias="powerSource")
    data_source: str | None = Field(None, serialization_alias="dataSource")
    status: str | None = None
    lat: float | None = Field(None, validation_alias="last_lat")
    lng: float | None = Field(None, validation_alias="last_lng")
    speed_knots: float | None = Field(
        None, validation_alias="last_speed_knots", serialization_alias="speedKnots"
    )
    heading: float | None = Field(None, validation_alias="last_heading")
    last_transmitted_at: datetime | None = Field(None, serialization_alias="lastTransmittedAt")


# ===================================================================
# Auth / users -- menggantikan Firebase Auth + Firestore users/{uid}
# ===================================================================


class UserLogin(BaseModel):
    email: str
    password: str


class UserCreate(BaseModel):
    """Admin-only (POST /users) -- role bebas dipilih admin."""

    name: str = Field(..., min_length=1, max_length=100)
    email: str = Field(..., max_length=200)
    password: str = Field(..., min_length=6)
    role: str = Field("user")


class UserUpdate(BaseModel):
    """PATCH /users/{id} (admin: name+role) atau PATCH /users/me (self: cuma
    name -- kalau role dikirim di endpoint /users/me, diabaikan server-side,
    lihat main.py)."""

    name: str = Field(..., min_length=1, max_length=100)
    role: str | None = None


class UserOut(BaseModel):
    model_config = ConfigDict(from_attributes=True, populate_by_name=True)

    id: int
    name: str
    email: str
    role: str
    created_at: datetime = Field(serialization_alias="createdAt")


class TokenOut(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    access_token: str = Field(serialization_alias="accessToken")
    token_type: str = Field("bearer", serialization_alias="tokenType")
    user: UserOut


# ===================================================================
# Waypoints -- menggantikan Firestore devices/{sn}/waypoints
# ===================================================================


class WaypointIn(BaseModel):
    name: str = Field(..., min_length=1, max_length=100)
    lat: float
    lng: float


class WaypointOut(BaseModel):
    model_config = ConfigDict(from_attributes=True, populate_by_name=True)

    id: int
    name: str
    lat: float
    lng: float
    created_at: datetime = Field(serialization_alias="createdAt")


# ===================================================================
# Device alerts -- notifikasi otomatis (mis. kuota SIM menipis), dibuat
# backend saat /telemetry menerima nilai di bawah ambang batas.
# ===================================================================


class DeviceAlertOut(BaseModel):
    model_config = ConfigDict(from_attributes=True, populate_by_name=True)

    id: int
    device_sn: str = Field(serialization_alias="deviceSn")
    type: str
    message: str
    value: float | None = None
    created_at: datetime = Field(serialization_alias="createdAt")

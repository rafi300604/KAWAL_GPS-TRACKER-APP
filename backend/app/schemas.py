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
    data_source: str = Field(..., validation_alias="dataSource", description='"GSM" atau "WIFI"')
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

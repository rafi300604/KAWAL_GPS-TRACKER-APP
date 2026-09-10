import os
import secrets

from fastapi import HTTPException, Security, status
from fastapi.security import APIKeyHeader

DEVICE_API_KEY = os.getenv("DEVICE_API_KEY")
ADMIN_API_KEY = os.getenv("ADMIN_API_KEY")

_device_api_key_header = APIKeyHeader(name="X-API-Key", auto_error=False)
_admin_api_key_header = APIKeyHeader(name="X-Admin-Key", auto_error=False)


def require_device(api_key: str | None = Security(_device_api_key_header)) -> None:
    """Satu shared secret untuk semua device di fleet -- identitas device
    sendiri (sn) datang dari body request, bukan dari key ini."""
    if not DEVICE_API_KEY:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="DEVICE_API_KEY belum diset di server (lihat .env)",
        )
    if not api_key or not secrets.compare_digest(api_key, DEVICE_API_KEY):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="X-API-Key tidak valid",
        )


def require_admin(api_key: str | None = Security(_admin_api_key_header)) -> None:
    if not ADMIN_API_KEY:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="ADMIN_API_KEY belum diset di server (lihat .env)",
        )
    if not api_key or not secrets.compare_digest(api_key, ADMIN_API_KEY):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="X-Admin-Key tidak valid",
        )

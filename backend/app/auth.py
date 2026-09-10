import os
import secrets
from datetime import datetime, timedelta, timezone

import bcrypt
import jwt
from fastapi import Depends, HTTPException, Security, status
from fastapi.security import APIKeyHeader, HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from . import models
from .database import get_db

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


# ===================================================================
# Login pengguna (JWT) -- menggantikan Firebase Auth. Beda sama sekali dari
# DEVICE_API_KEY/ADMIN_API_KEY di atas (itu shared secret statis buat
# device/admin-read; ini per-user, dari email+password, exp terbatas).
# ===================================================================

JWT_SECRET = os.getenv("JWT_SECRET")
JWT_ALGORITHM = "HS256"
JWT_EXPIRE_DAYS = 30  # app tidak punya refresh-token flow - token umur panjang, re-login kalau habis

_bearer_scheme = HTTPBearer(auto_error=False)


def hash_password(password: str) -> str:
    return bcrypt.hashpw(password.encode("utf-8"), bcrypt.gensalt()).decode("utf-8")


def verify_password(password: str, password_hash: str) -> bool:
    return bcrypt.checkpw(password.encode("utf-8"), password_hash.encode("utf-8"))


def create_access_token(user_id: int) -> str:
    if not JWT_SECRET:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="JWT_SECRET belum diset di server (lihat .env)",
        )
    payload = {
        "sub": str(user_id),
        "exp": datetime.now(timezone.utc) + timedelta(days=JWT_EXPIRE_DAYS),
    }
    return jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)


async def get_current_user(
    credentials: HTTPAuthorizationCredentials | None = Security(_bearer_scheme),
    db: AsyncSession = Depends(get_db),
) -> models.User:
    """Dependency dasar: decode JWT dari header Authorization: Bearer <token>,
    ambil User dari database. Dipakai langsung (siapa saja yang login) atau
    lewat require_admin_user (khusus role admin)."""
    unauthorized = HTTPException(
        status_code=status.HTTP_401_UNAUTHORIZED,
        detail="Token tidak valid atau kedaluwarsa",
        headers={"WWW-Authenticate": "Bearer"},
    )
    if credentials is None or not JWT_SECRET:
        raise unauthorized
    try:
        payload = jwt.decode(credentials.credentials, JWT_SECRET, algorithms=[JWT_ALGORITHM])
        user_id = int(payload["sub"])
    except (jwt.PyJWTError, KeyError, ValueError):
        raise unauthorized
    result = await db.execute(select(models.User).where(models.User.id == user_id))
    user = result.scalar_one_or_none()
    if user is None:
        raise unauthorized
    return user


async def require_admin_user(user: models.User = Depends(get_current_user)) -> models.User:
    """Beda dari require_admin() di atas (X-Admin-Key statis) -- ini cek
    role='admin' pada user yang login lewat JWT. Dipakai endpoint manajemen
    pengguna (GET/POST/PATCH/DELETE /users)."""
    if user.role != "admin":
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Hanya admin yang boleh mengakses ini",
        )
    return user

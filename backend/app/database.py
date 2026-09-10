import os

from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine
from sqlalchemy.orm import DeclarativeBase

# Semua konfigurasi diambil dari .env (di-inject oleh docker compose) --
# TIDAK ada nilai hardcode di sini. Kalau DATABASE_URL belum diset, fail
# cepat dengan pesan jelas, bukan diam-diam pakai value default.
DATABASE_URL = os.getenv("DATABASE_URL")
if not DATABASE_URL:
    raise RuntimeError(
        "DATABASE_URL belum diset -- harus diambil dari .env. "
        "Salin .env.example jadi .env, isi nilainya, lalu jalankan lewat "
        "'docker compose up' (compose yang memasukkan nilainya ke container)."
    )

engine = create_async_engine(DATABASE_URL, echo=False)
async_session = async_sessionmaker(engine, expire_on_commit=False)


class Base(DeclarativeBase):
    pass


async def get_db():
    async with async_session() as session:
        yield session

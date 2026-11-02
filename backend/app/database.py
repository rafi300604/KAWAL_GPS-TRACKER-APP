import os
from urllib.parse import quote_plus

from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine
from sqlalchemy.orm import DeclarativeBase

# Jika credential individual tersedia, buat DATABASE_URL yang di-quote agar aman dari karakter spesial
user = os.getenv("POSTGRES_USER")
password = os.getenv("POSTGRES_PASSWORD")
dbname = os.getenv("POSTGRES_DB")
host = os.getenv("POSTGRES_HOST", "db")
port = os.getenv("POSTGRES_PORT", "5432")

if user and password and dbname:
    DATABASE_URL = f"postgresql+asyncpg://{quote_plus(user)}:{quote_plus(password)}@{host}:{port}/{dbname}"
else:
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

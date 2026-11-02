import asyncio
import os
import sys
from logging.config import fileConfig

from sqlalchemy import pool
from sqlalchemy.ext.asyncio import async_engine_from_config

from alembic import context

# Supaya `from app...` bisa diimport saat dijalankan dari root project
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

from app import models  # noqa: E402,F401  (import supaya metadata terisi)
from app.database import Base  # noqa: E402

config = context.config

if config.config_file_name is not None:
    fileConfig(config.config_file_name)

target_metadata = Base.metadata

# DATABASE_URL datang dari .env (di-inject docker compose ke container)
from urllib.parse import quote_plus

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
        "'docker compose up' (compose inject DATABASE_URL ke container)."
    )

# configparser di alembic butuh %% untuk escape karakter % dari URL-encoding (misal %23 dari tanda #)
config.set_main_option("sqlalchemy.url", DATABASE_URL.replace("%", "%%"))


def run_migrations_offline() -> None:
    context.configure(
        url=DATABASE_URL,
        target_metadata=target_metadata,
        literal_binds=True,
        dialect_opts={"paramstyle": "named"},
    )
    with context.begin_transaction():
        context.run_migrations()


def do_run_migrations(connection) -> None:
    context.configure(connection=connection, target_metadata=target_metadata)
    with context.begin_transaction():
        context.run_migrations()


async def run_migrations_online() -> None:
    from sqlalchemy.ext.asyncio import create_async_engine

    connectable = create_async_engine(
        DATABASE_URL,
        poolclass=pool.NullPool,
    )

    async with connectable.connect() as connection:
        await connection.run_sync(do_run_migrations)

    await connectable.dispose()


if context.is_offline_mode():
    run_migrations_offline()
else:
    asyncio.run(run_migrations_online())

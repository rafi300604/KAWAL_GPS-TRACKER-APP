#!/bin/sh
set -e


echo "Menambah database aplikasi kalau belum exist..."
python3 - <<'PY'
import asyncio
import os
import sys
from urllib.parse import urlsplit
import asyncpg

user = os.environ.get("POSTGRES_USER", "").strip()
password = os.environ.get("POSTGRES_PASSWORD", "").strip()
dbname = os.environ.get("POSTGRES_DB", "").strip()
host = os.environ.get("POSTGRES_HOST", "db").strip()
port_raw = os.environ.get("POSTGRES_PORT", "5432").strip()
port = int(port_raw) if port_raw.isdigit() else 5432

if not (user and password and dbname):
    raw = os.environ.get("DATABASE_URL", "").strip()
    if not raw:
        sys.exit(
            "DATABASE_URL atau POSTGRES_USER/PASSWORD belum diset di environment container."
        )
    dsn = raw.replace("postgresql+asyncpg://", "postgresql://", 1)
    parts = urlsplit(dsn)
    dbname = parts.path.lstrip("/") or "postgres"
    user = parts.username or user or "postgres"
    password = parts.password or password
    host = parts.hostname or host or "db"
    port = parts.port or port or 5432


async def main():
    # Menggunakan kwargs langsung agar terhindar dari error parsing karakter khusus dalam password
    conn = await asyncpg.connect(
        user=user,
        password=password,
        host=host,
        port=port,
        database="postgres",
    )
    try:
        exists = await conn.fetchval(
            "SELECT 1 FROM pg_database WHERE datname = $1", dbname
        )
        if exists:
            print(f"Database '{dbname}' sudah exist -- skip create.")
            return
        # CREATE DATABASE tidak boleh parameterized -- quote identifier manual.
        quoted = '"' + dbname.replace('"', '""') + '"'
        await conn.execute(f"CREATE DATABASE {quoted}")
        print(f"Database '{dbname}' berhasil dibuat.")
    finally:
        await conn.close()


asyncio.run(main())
PY

echo "Menjalankan migrasi database (alembic upgrade head)..."
alembic upgrade head

echo "Menjalankan API..."
exec uvicorn app.main:app --host 0.0.0.0 --port 8000

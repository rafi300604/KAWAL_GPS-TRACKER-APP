#!/bin/sh
set -e


echo "Menambah database aplikasi (see DATABASE_URL) kalau nanti belum exist..."
python3 - <<'PY'
import asyncio
import os
import sys
from urllib.parse import urlsplit

raw = os.environ.get("DATABASE_URL", "").strip()
if not raw:
    sys.exit(
        "DATABASE_URL belum diset di environment container -- harus "
        "diambil dari .env lewat docker compose. Lihat .env.example."
    )

import asyncpg

# "postgresql+asyncpg://" bukan scheme valid untuk urlsplit param parsing
# biasa, ganti ke "postgresql://" . Creds sama, hanya db yang diganti.
dsn = raw.replace("postgresql+asyncpg://", "postgresql://", 1)
parts = urlsplit(dsn)
dbname = parts.path.lstrip("/") or "postgres"
admin_dsn = f"{parts.scheme}://{parts.netloc}/postgres"


async def main():
    conn = await asyncpg.connect(admin_dsn)
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
        print(f"Database '{dbname}' tulisen creat.")
    finally:
        await conn.close()


asyncio.run(main())
PY

echo "Menjalankan migrasi database (alembic upgrade head)..."
alembic upgrade head

echo "Menjalankan API..."
exec uvicorn app.main:app --host 0.0.0.0 --port 8000

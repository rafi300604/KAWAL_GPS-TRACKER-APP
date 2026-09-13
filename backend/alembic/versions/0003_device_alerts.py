"""widen data_source + device_alerts table

Revision ID: 0003
Revises: 0002
Create Date: 2026-09-13

"""
from alembic import op
import sqlalchemy as sa

revision = "0003"
down_revision = "0002"
branch_labels = None
depends_on = None


def upgrade() -> None:
    # data_source dulu VARCHAR(10), cukup untuk "GSM"/"WIFI" polos. Firmware
    # (skripsi.ino) sekarang juga bisa kirim string deskriptif seperti
    # "WIFI (GSM: kuota diduga habis)" saat mendeteksi heuristik kuota SIM
    # habis - VARCHAR(10) akan GAGAL (StringDataRightTruncation) begitu
    # string itu masuk. Lihat FIRMWARE_CONTRACT.md.
    op.alter_column("devices", "data_source", type_=sa.String(length=50))
    op.alter_column("telemetry", "data_source", type_=sa.String(length=50))

    op.create_table(
        "device_alerts",
        sa.Column("id", sa.Integer(), primary_key=True),
        sa.Column("device_sn", sa.String(length=50), sa.ForeignKey("devices.sn"), nullable=False),
        sa.Column("type", sa.String(length=30), nullable=False),
        sa.Column("message", sa.String(length=200), nullable=False),
        sa.Column("value", sa.Float(), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
    )
    op.create_index("ix_device_alerts_device_sn", "device_alerts", ["device_sn"])
    op.create_index("ix_device_alerts_created_at", "device_alerts", ["created_at"])


def downgrade() -> None:
    op.drop_index("ix_device_alerts_created_at", table_name="device_alerts")
    op.drop_index("ix_device_alerts_device_sn", table_name="device_alerts")
    op.drop_table("device_alerts")
    op.alter_column("telemetry", "data_source", type_=sa.String(length=10))
    op.alter_column("devices", "data_source", type_=sa.String(length=10))

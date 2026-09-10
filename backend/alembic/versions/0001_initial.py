"""initial schema

Revision ID: 0001
Revises:
Create Date: 2026-09-09

"""
from alembic import op
import sqlalchemy as sa

revision = "0001"
down_revision = None
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "devices",
        sa.Column("id", sa.Integer(), primary_key=True),
        sa.Column("sn", sa.String(length=50), nullable=False),
        sa.Column("name", sa.String(length=100), nullable=True),
        sa.Column("power_source", sa.String(length=20), nullable=True),
        sa.Column("data_source", sa.String(length=10), nullable=True),
        sa.Column("status", sa.String(length=20), nullable=True),
        sa.Column("last_lat", sa.Float(), nullable=True),
        sa.Column("last_lng", sa.Float(), nullable=True),
        sa.Column("last_speed_knots", sa.Float(), nullable=True),
        sa.Column("last_heading", sa.Float(), nullable=True),
        sa.Column("last_transmitted_at", sa.DateTime(timezone=True), nullable=True),
    )
    op.create_index("ix_devices_sn", "devices", ["sn"], unique=True)

    op.create_table(
        "telemetry",
        sa.Column("id", sa.Integer(), primary_key=True),
        sa.Column("device_sn", sa.String(length=50), sa.ForeignKey("devices.sn"), nullable=False),
        sa.Column("ts", sa.DateTime(timezone=True), nullable=False),
        sa.Column("lat", sa.Float(), nullable=False),
        sa.Column("lng", sa.Float(), nullable=False),
        sa.Column("speed_knots", sa.Float(), nullable=True),
        sa.Column("heading", sa.Float(), nullable=True),
        sa.Column("data_source", sa.String(length=10), nullable=True),
        sa.Column("received_at", sa.DateTime(timezone=True), nullable=False),
    )
    op.create_index("ix_telemetry_device_sn", "telemetry", ["device_sn"])
    op.create_index("ix_telemetry_ts", "telemetry", ["ts"])


def downgrade() -> None:
    op.drop_index("ix_telemetry_ts", table_name="telemetry")
    op.drop_index("ix_telemetry_device_sn", table_name="telemetry")
    op.drop_table("telemetry")
    op.drop_index("ix_devices_sn", table_name="devices")
    op.drop_table("devices")

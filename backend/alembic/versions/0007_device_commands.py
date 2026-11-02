"""device_commands (perintah server -> device lewat MQTT)

Revision ID: 0007
Revises: 0006
Create Date: 2026-10-20

"""
from alembic import op
import sqlalchemy as sa

revision = "0007"
down_revision = "0006"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "device_commands",
        sa.Column("id", sa.Integer(), primary_key=True),
        sa.Column("device_sn", sa.String(50), sa.ForeignKey("devices.sn"), nullable=False),
        sa.Column("cmd", sa.String(20), nullable=False),
        sa.Column("interval_s", sa.Integer(), nullable=True),
        sa.Column("duration_min", sa.Integer(), nullable=True),
        # Default di level database supaya INSERT manual (DBeaver) cukup isi
        # device_sn/cmd/interval_s/duration_min.
        sa.Column("status", sa.String(20), nullable=False, server_default="pending"),
        sa.Column("ack_message", sa.String(200), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.func.now()),
        sa.Column("sent_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("acked_at", sa.DateTime(timezone=True), nullable=True),
    )
    op.create_index("ix_device_commands_device_sn", "device_commands", ["device_sn"])
    op.create_index("ix_device_commands_status", "device_commands", ["status"])
    op.create_index("ix_device_commands_created_at", "device_commands", ["created_at"])


def downgrade() -> None:
    op.drop_table("device_commands")

"""devices.online (status koneksi MQTT dari status/<SN> + Last Will)

Revision ID: 0009
Revises: 0008
Create Date: 2026-11-01

"""
from alembic import op
import sqlalchemy as sa

revision = "0009"
down_revision = "0008"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column("devices", sa.Column("online", sa.Boolean(), nullable=True))


def downgrade() -> None:
    op.drop_column("devices", "online")

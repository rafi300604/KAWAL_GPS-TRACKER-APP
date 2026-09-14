"""telemetry.power_source per-transmission

Revision ID: 0004
Revises: 0003
Create Date: 2026-09-14

"""
from alembic import op
import sqlalchemy as sa

revision = "0004"
down_revision = "0003"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column("telemetry", sa.Column("power_source", sa.String(length=20), nullable=True))


def downgrade() -> None:
    op.drop_column("telemetry", "power_source")

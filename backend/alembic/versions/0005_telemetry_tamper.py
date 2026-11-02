"""telemetry.tamper per-transmission

Revision ID: 0005
Revises: 0004
Create Date: 2026-09-21

"""
from alembic import op
import sqlalchemy as sa

revision = "0005"
down_revision = "0004"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column(
        "telemetry",
        sa.Column("tamper", sa.Boolean(), nullable=False, server_default=sa.false()),
    )


def downgrade() -> None:
    op.drop_column("telemetry", "tamper")

"""device_commands.duration_s (lama bunyi buzzer untuk cmd "buzzer")

Revision ID: 0008
Revises: 0007
Create Date: 2026-10-21

"""
from alembic import op
import sqlalchemy as sa

revision = "0008"
down_revision = "0007"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column("device_commands", sa.Column("duration_s", sa.Integer(), nullable=True))


def downgrade() -> None:
    op.drop_column("device_commands", "duration_s")

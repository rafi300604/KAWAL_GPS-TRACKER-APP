"""users + waypoints tables

Revision ID: 0002
Revises: 0001
Create Date: 2026-09-10

"""
from alembic import op
import sqlalchemy as sa

revision = "0002"
down_revision = "0001"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "users",
        sa.Column("id", sa.Integer(), primary_key=True),
        sa.Column("email", sa.String(length=200), nullable=False),
        sa.Column("password_hash", sa.String(length=200), nullable=False),
        sa.Column("name", sa.String(length=100), nullable=False),
        sa.Column("role", sa.String(length=20), nullable=False, server_default="user"),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
    )
    op.create_index("ix_users_email", "users", ["email"], unique=True)

    op.create_table(
        "waypoints",
        sa.Column("id", sa.Integer(), primary_key=True),
        sa.Column("device_sn", sa.String(length=50), sa.ForeignKey("devices.sn"), nullable=False),
        sa.Column("name", sa.String(length=100), nullable=False),
        sa.Column("lat", sa.Float(), nullable=False),
        sa.Column("lng", sa.Float(), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
    )
    op.create_index("ix_waypoints_device_sn", "waypoints", ["device_sn"])


def downgrade() -> None:
    op.drop_index("ix_waypoints_device_sn", table_name="waypoints")
    op.drop_table("waypoints")
    op.drop_index("ix_users_email", table_name="users")
    op.drop_table("users")

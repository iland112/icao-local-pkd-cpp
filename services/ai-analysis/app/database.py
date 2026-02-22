import json
import logging
from contextlib import asynccontextmanager

from sqlalchemy import create_engine
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine
from sqlalchemy.orm import DeclarativeBase, sessionmaker

from app.config import get_settings

logger = logging.getLogger(__name__)


def safe_isna(val) -> bool:
    """Safely check if a value is NA/NaN/None, handling non-scalar values.

    pd.isna() raises ValueError on array-like inputs. This function
    handles that case gracefully, returning False for non-scalar values.
    """
    import pandas as pd

    if val is None:
        return True
    try:
        return bool(pd.isna(val))
    except (ValueError, TypeError):
        return False


def safe_json_loads(value, default=None):
    """Safely parse JSON from DB value (PostgreSQL JSONB dict or Oracle CLOB string).

    PostgreSQL JSONB columns return Python dict/list directly.
    Oracle CLOB columns return JSON as string, requiring json.loads().
    """
    if value is None:
        return default if default is not None else {}
    if not isinstance(value, str):
        return value  # Already parsed (PostgreSQL JSONB → dict/list)
    value = value.strip()
    if not value:
        return default if default is not None else {}
    try:
        return json.loads(value)
    except (json.JSONDecodeError, ValueError):
        logger.warning("Failed to parse JSON from DB value: %.100s...", value)
        return default if default is not None else {}


class Base(DeclarativeBase):
    pass


settings = get_settings()

# Async engine for FastAPI endpoints (PostgreSQL only)
if settings.db_type == "postgres":
    async_engine = create_async_engine(
        settings.database_url,
        pool_size=5,
        max_overflow=10,
        echo=False,
    )
    AsyncSessionLocal = async_sessionmaker(
        bind=async_engine,
        class_=AsyncSession,
        expire_on_commit=False,
    )
else:
    # Oracle uses synchronous engine
    async_engine = None
    AsyncSessionLocal = None

# Sync engine for pandas batch operations and Oracle
sync_engine = create_engine(
    settings.sync_database_url,
    pool_size=5,
    max_overflow=10,
    echo=False,
)
SyncSessionLocal = sessionmaker(bind=sync_engine)


async def get_async_session():
    """Dependency for async endpoints (PostgreSQL)."""
    if AsyncSessionLocal is None:
        raise RuntimeError("Async sessions not available for Oracle. Use sync session.")
    async with AsyncSessionLocal() as session:
        yield session


def get_sync_session():
    """Get synchronous session for batch operations."""
    session = SyncSessionLocal()
    try:
        yield session
    finally:
        session.close()


@asynccontextmanager
async def get_managed_async_session():
    """Context manager for async sessions outside FastAPI dependency injection."""
    if AsyncSessionLocal is None:
        raise RuntimeError("Async sessions not available for Oracle.")
    async with AsyncSessionLocal() as session:
        yield session

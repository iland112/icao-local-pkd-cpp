import logging
from contextlib import asynccontextmanager

from sqlalchemy import create_engine
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine
from sqlalchemy.orm import DeclarativeBase, sessionmaker

from app.config import get_settings

logger = logging.getLogger(__name__)


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

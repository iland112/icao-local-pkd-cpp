import logging
from datetime import datetime, timezone

from fastapi import APIRouter

from app.config import get_settings
from app.schemas.health import DbPoolMetrics, HealthResponse, InternalMetricsResponse

logger = logging.getLogger(__name__)
router = APIRouter()


@router.get("/health", response_model=HealthResponse)
async def health_check():
    settings = get_settings()

    # Quick DB connectivity check
    db_connected = False
    try:
        from sqlalchemy import text

        from app.database import sync_engine

        with sync_engine.connect() as conn:
            conn.execute(text("SELECT 1"))
        db_connected = True
    except Exception as e:
        logger.warning("Health check: database not reachable: %s", e)

    return HealthResponse(
        status="healthy",
        service=settings.service_name,
        version=settings.model_version,
        db_type=settings.db_type,
        analysis_enabled=settings.analysis_enabled,
        db_connected=db_connected,
    )


@router.get("/internal/metrics", response_model=InternalMetricsResponse)
async def internal_metrics():
    """Internal metrics endpoint for monitoring service."""
    from app.database import sync_engine

    pool = sync_engine.pool
    return InternalMetricsResponse(
        service="ai-analysis",
        timestamp=datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S"),
        dbPool=DbPoolMetrics(
            available=pool.checkedin(),
            total=pool.checkedout() + pool.checkedin(),
            max=pool.size() + pool._max_overflow,
        ),
    )

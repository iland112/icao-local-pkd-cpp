import logging
from datetime import datetime, timezone

from fastapi import APIRouter

from app.config import get_settings
from app.schemas.health import HealthResponse

logger = logging.getLogger(__name__)
router = APIRouter()


@router.get("/health", response_model=HealthResponse)
async def health_check():
    settings = get_settings()
    return HealthResponse(
        status="healthy",
        service=settings.service_name,
        version=settings.model_version,
        db_type=settings.db_type,
        analysis_enabled=settings.analysis_enabled,
    )


@router.get("/internal/metrics")
async def internal_metrics():
    """Internal metrics endpoint for monitoring service."""
    from app.database import sync_engine

    pool = sync_engine.pool
    return {
        "service": "ai-analysis",
        "timestamp": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S"),
        "dbPool": {
            "available": pool.checkedin(),
            "total": pool.checkedout() + pool.checkedin(),
            "max": pool.size() + pool._max_overflow,
        },
    }

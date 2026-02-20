import logging

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

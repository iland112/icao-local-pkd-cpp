import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.config import get_settings
from app.routers import health

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(application: FastAPI):
    settings = get_settings()
    logger.info(
        "Starting %s (port=%d, db=%s, analysis=%s)",
        settings.service_name,
        settings.server_port,
        settings.db_type,
        "enabled" if settings.analysis_enabled else "disabled",
    )

    # Start background scheduler if enabled
    if settings.analysis_enabled:
        try:
            from app.tasks.scheduler import start_scheduler

            start_scheduler()
            logger.info("Analysis scheduler started (hour=%d)", settings.analysis_schedule_hour)
        except Exception as e:
            logger.warning("Failed to start scheduler: %s", e)

    yield

    # Shutdown
    logger.info("Shutting down %s", settings.service_name)
    try:
        from app.tasks.scheduler import stop_scheduler

        stop_scheduler()
    except Exception:
        pass


app = FastAPI(
    title="ICAO PKD AI Analysis Engine",
    description="ML-based certificate anomaly detection and pattern analysis",
    version=get_settings().model_version,
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Register routers
app.include_router(health.router, prefix="/api/ai", tags=["health"])

# Lazy-load analysis and report routers to avoid import errors during startup
try:
    from app.routers import analysis, reports

    app.include_router(analysis.router, prefix="/api/ai", tags=["analysis"])
    app.include_router(reports.router, prefix="/api/ai", tags=["reports"])
except Exception as e:
    logger.warning("Could not load analysis/report routers: %s", e)

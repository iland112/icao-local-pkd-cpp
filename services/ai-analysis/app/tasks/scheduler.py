"""Background scheduler for daily analysis."""

import logging

from apscheduler.schedulers.background import BackgroundScheduler

from app.config import get_settings

logger = logging.getLogger(__name__)

_scheduler: BackgroundScheduler | None = None


def _run_scheduled_analysis() -> None:
    """Run analysis as a scheduled job (guards against concurrent execution)."""
    from app.routers.analysis import _job_lock, _job_status, _run_analysis

    with _job_lock:
        if _job_status["status"] == "RUNNING":
            logger.warning("Scheduled analysis skipped: another analysis is already running")
            return
        _job_status["status"] = "RUNNING"
        _job_status["started_at"] = __import__("datetime").datetime.now(
            __import__("datetime").timezone.utc
        ).isoformat()
        _job_status["progress"] = 0.0
        _job_status["error_message"] = None

    logger.info("Scheduled analysis triggered")
    try:
        _run_analysis()
    except Exception as e:
        logger.error("Scheduled analysis failed: %s", e, exc_info=True)


def start_scheduler() -> None:
    """Start the background analysis scheduler."""
    global _scheduler
    settings = get_settings()

    if not settings.analysis_enabled:
        logger.info("Analysis scheduler disabled")
        return

    _scheduler = BackgroundScheduler()
    _scheduler.add_job(
        _run_scheduled_analysis,
        "cron",
        hour=settings.analysis_schedule_hour,
        minute=0,
        id="daily_analysis",
        name="Daily Certificate Analysis",
        replace_existing=True,
        max_instances=1,
    )
    _scheduler.start()
    logger.info(
        "Scheduler started: daily analysis at %02d:00",
        settings.analysis_schedule_hour,
    )


def stop_scheduler() -> None:
    """Stop the background scheduler."""
    global _scheduler
    if _scheduler:
        _scheduler.shutdown(wait=False)
        _scheduler = None
        logger.info("Scheduler stopped")

"""Background scheduler for daily analysis."""

import logging

from apscheduler.schedulers.background import BackgroundScheduler

from app.config import get_settings

logger = logging.getLogger(__name__)

_scheduler: BackgroundScheduler | None = None


def _run_scheduled_analysis():
    """Run analysis as a scheduled job."""
    logger.info("Scheduled analysis triggered")
    try:
        from app.routers.analysis import _run_analysis

        _run_analysis()
    except Exception as e:
        logger.error("Scheduled analysis failed: %s", e, exc_info=True)


def start_scheduler():
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
    )
    _scheduler.start()
    logger.info(
        "Scheduler started: daily analysis at %02d:00",
        settings.analysis_schedule_hour,
    )


def stop_scheduler():
    """Stop the background scheduler."""
    global _scheduler
    if _scheduler:
        _scheduler.shutdown(wait=False)
        _scheduler = None
        logger.info("Scheduler stopped")

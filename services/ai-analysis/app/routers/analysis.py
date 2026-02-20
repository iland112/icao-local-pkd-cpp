"""Analysis API endpoints: trigger analysis, get results, list anomalies."""

import logging
import threading
import uuid
from datetime import datetime, timezone
from typing import Optional

from fastapi import APIRouter, HTTPException, Query
from sqlalchemy import text

from app.config import get_settings
from app.database import sync_engine
from app.schemas.analysis import (
    AnalysisJobStatus,
    AnalysisStatistics,
    AnomalyListResponse,
    CertificateAnalysis,
)

logger = logging.getLogger(__name__)
router = APIRouter()

# Global analysis job state
_job_lock = threading.Lock()
_job_status = {
    "status": "IDLE",
    "progress": 0.0,
    "total_certificates": 0,
    "processed_certificates": 0,
    "started_at": None,
    "completed_at": None,
    "error_message": None,
}


def _run_analysis():
    """Execute full analysis pipeline in background thread."""
    global _job_status
    settings = get_settings()

    try:
        with _job_lock:
            _job_status["status"] = "RUNNING"
            _job_status["progress"] = 0.0
            _job_status["started_at"] = datetime.now(timezone.utc).isoformat()
            _job_status["completed_at"] = None
            _job_status["error_message"] = None

        from app.services.anomaly_detector import AnomalyDetector, classify_anomaly
        from app.services.feature_engineering import (
            FEATURE_NAMES,
            engineer_features,
            load_certificate_data,
        )
        from app.services.risk_scorer import classify_risk, compute_risk_scores

        # Step 1: Load data
        logger.info("Analysis: loading certificate data...")
        df = load_certificate_data(sync_engine)
        total = len(df)

        with _job_lock:
            _job_status["total_certificates"] = total
            _job_status["progress"] = 0.1

        if total == 0:
            with _job_lock:
                _job_status["status"] = "COMPLETED"
                _job_status["progress"] = 1.0
                _job_status["completed_at"] = datetime.now(timezone.utc).isoformat()
            return

        # Step 2: Feature engineering
        logger.info("Analysis: engineering features for %d certificates...", total)
        metadata, features = engineer_features(df)
        with _job_lock:
            _job_status["progress"] = 0.3

        # Step 3: Anomaly detection
        logger.info("Analysis: running anomaly detection...")
        detector = AnomalyDetector()
        combined_scores, if_scores, lof_scores, explanations = detector.fit_predict(features)
        with _job_lock:
            _job_status["progress"] = 0.6

        # Step 4: Risk scoring
        logger.info("Analysis: computing risk scores...")
        risk_scores, risk_factors = compute_risk_scores(df, combined_scores)
        with _job_lock:
            _job_status["progress"] = 0.8

        # Step 5: Save results to DB
        logger.info("Analysis: saving results to DB...")
        _save_results(
            metadata, features, combined_scores, if_scores, lof_scores,
            explanations, risk_scores, risk_factors, settings.model_version,
        )

        with _job_lock:
            _job_status["status"] = "COMPLETED"
            _job_status["progress"] = 1.0
            _job_status["processed_certificates"] = total
            _job_status["completed_at"] = datetime.now(timezone.utc).isoformat()

        logger.info("Analysis complete: %d certificates processed", total)

    except Exception as e:
        logger.error("Analysis failed: %s", e, exc_info=True)
        with _job_lock:
            _job_status["status"] = "FAILED"
            _job_status["error_message"] = str(e)


def _save_results(
    metadata, features, combined_scores, if_scores, lof_scores,
    explanations, risk_scores, risk_factors, model_version,
):
    """Batch upsert analysis results into ai_analysis_result table."""
    import json

    from app.services.anomaly_detector import classify_anomaly
    from app.services.feature_engineering import FEATURE_NAMES
    from app.services.risk_scorer import classify_risk

    settings = get_settings()
    batch_size = settings.batch_size
    n = len(combined_scores)

    with sync_engine.connect() as conn:
        for start in range(0, n, batch_size):
            end = min(start + batch_size, n)
            values = []

            for i in range(start, end):
                fp = metadata.iloc[i]["fingerprint_sha256"]
                ct = metadata.iloc[i].get("certificate_type") or ""
                cc = metadata.iloc[i].get("country_code") or ""

                feature_dict = {
                    FEATURE_NAMES[j]: round(float(features[i, j]), 6)
                    for j in range(features.shape[1])
                }

                values.append({
                    "id": str(uuid.uuid4()),
                    "certificate_fingerprint": fp,
                    "certificate_type": ct,
                    "country_code": cc,
                    "anomaly_score": round(float(combined_scores[i]), 6),
                    "anomaly_label": classify_anomaly(combined_scores[i]),
                    "isolation_forest_score": round(float(if_scores[i]), 6),
                    "lof_score": round(float(lof_scores[i]), 6),
                    "risk_score": round(float(risk_scores[i]), 2),
                    "risk_level": classify_risk(risk_scores[i]),
                    "risk_factors": json.dumps(risk_factors[i]),
                    "feature_vector": json.dumps(feature_dict),
                    "anomaly_explanations": json.dumps(explanations[i]),
                    "analysis_version": model_version,
                })

            if not values:
                continue

            if settings.db_type == "oracle":
                # Oracle: MERGE for upsert
                for v in values:
                    conn.execute(
                        text("""
                            MERGE INTO ai_analysis_result t
                            USING (SELECT :certificate_fingerprint AS cf FROM DUAL) s
                            ON (t.certificate_fingerprint = s.cf)
                            WHEN MATCHED THEN UPDATE SET
                                anomaly_score = :anomaly_score,
                                anomaly_label = :anomaly_label,
                                isolation_forest_score = :isolation_forest_score,
                                lof_score = :lof_score,
                                risk_score = :risk_score,
                                risk_level = :risk_level,
                                risk_factors = :risk_factors,
                                feature_vector = :feature_vector,
                                anomaly_explanations = :anomaly_explanations,
                                analysis_version = :analysis_version,
                                analyzed_at = SYSTIMESTAMP
                            WHEN NOT MATCHED THEN INSERT (
                                id, certificate_fingerprint, certificate_type, country_code,
                                anomaly_score, anomaly_label, isolation_forest_score, lof_score,
                                risk_score, risk_level, risk_factors,
                                feature_vector, anomaly_explanations, analysis_version
                            ) VALUES (
                                :id, :certificate_fingerprint, :certificate_type, :country_code,
                                :anomaly_score, :anomaly_label, :isolation_forest_score, :lof_score,
                                :risk_score, :risk_level, :risk_factors,
                                :feature_vector, :anomaly_explanations, :analysis_version
                            )
                        """),
                        v,
                    )
            else:
                # PostgreSQL: INSERT ON CONFLICT
                for v in values:
                    conn.execute(
                        text("""
                            INSERT INTO ai_analysis_result (
                                id, certificate_fingerprint, certificate_type, country_code,
                                anomaly_score, anomaly_label, isolation_forest_score, lof_score,
                                risk_score, risk_level, risk_factors,
                                feature_vector, anomaly_explanations, analysis_version
                            ) VALUES (
                                :id, :certificate_fingerprint, :certificate_type, :country_code,
                                :anomaly_score, :anomaly_label, :isolation_forest_score, :lof_score,
                                :risk_score, :risk_level, CAST(:risk_factors AS JSONB),
                                CAST(:feature_vector AS JSONB), CAST(:anomaly_explanations AS JSONB),
                                :analysis_version
                            )
                            ON CONFLICT (certificate_fingerprint) DO UPDATE SET
                                anomaly_score = EXCLUDED.anomaly_score,
                                anomaly_label = EXCLUDED.anomaly_label,
                                isolation_forest_score = EXCLUDED.isolation_forest_score,
                                lof_score = EXCLUDED.lof_score,
                                risk_score = EXCLUDED.risk_score,
                                risk_level = EXCLUDED.risk_level,
                                risk_factors = EXCLUDED.risk_factors,
                                feature_vector = EXCLUDED.feature_vector,
                                anomaly_explanations = EXCLUDED.anomaly_explanations,
                                analysis_version = EXCLUDED.analysis_version,
                                analyzed_at = NOW()
                        """),
                        v,
                    )

            conn.commit()

            with _job_lock:
                _job_status["processed_certificates"] = end
                _job_status["progress"] = 0.8 + 0.2 * (end / n)

        logger.info("Saved %d analysis results to DB", n)


@router.post("/analyze")
async def trigger_analysis():
    """Trigger full certificate analysis (runs in background)."""
    with _job_lock:
        if _job_status["status"] == "RUNNING":
            raise HTTPException(status_code=409, detail="Analysis already running")

    thread = threading.Thread(target=_run_analysis, daemon=True)
    thread.start()

    return {"success": True, "message": "Analysis started"}


@router.get("/analyze/status", response_model=AnalysisJobStatus)
async def get_analysis_status():
    """Get current analysis job status."""
    with _job_lock:
        return AnalysisJobStatus(**_job_status)


@router.get("/certificate/{fingerprint}", response_model=CertificateAnalysis)
async def get_certificate_analysis(fingerprint: str):
    """Get AI analysis result for a specific certificate."""
    with sync_engine.connect() as conn:
        result = conn.execute(
            text(
                "SELECT * FROM ai_analysis_result WHERE certificate_fingerprint = :fp"
            ),
            {"fp": fingerprint},
        ).fetchone()

    if not result:
        raise HTTPException(status_code=404, detail="Analysis not found for this certificate")

    row = result._mapping
    import json

    risk_factors = row.get("risk_factors") or "{}"
    if isinstance(risk_factors, str):
        risk_factors = json.loads(risk_factors)

    explanations = row.get("anomaly_explanations") or "[]"
    if isinstance(explanations, str):
        explanations = json.loads(explanations)

    return CertificateAnalysis(
        fingerprint=row["certificate_fingerprint"],
        certificate_type=row.get("certificate_type"),
        country_code=row.get("country_code"),
        anomaly_score=row.get("anomaly_score", 0),
        anomaly_label=row.get("anomaly_label", "NORMAL"),
        risk_score=row.get("risk_score", 0),
        risk_level=row.get("risk_level", "LOW"),
        risk_factors=risk_factors,
        anomaly_explanations=explanations,
        analyzed_at=row.get("analyzed_at"),
    )


@router.get("/anomalies", response_model=AnomalyListResponse)
async def list_anomalies(
    country: Optional[str] = Query(None),
    cert_type: Optional[str] = Query(None, alias="type"),
    label: Optional[str] = Query(None),
    risk_level: Optional[str] = Query(None),
    page: int = Query(1, ge=1),
    size: int = Query(20, ge=1, le=100),
):
    """List anomalous certificates with filters and pagination."""
    settings = get_settings()
    conditions = []
    params = {}

    if country:
        conditions.append("country_code = :country")
        params["country"] = country
    if cert_type:
        conditions.append("certificate_type = :cert_type")
        params["cert_type"] = cert_type
    if label:
        conditions.append("anomaly_label = :label")
        params["label"] = label
    if risk_level:
        conditions.append("risk_level = :risk_level")
        params["risk_level"] = risk_level

    where = " AND ".join(conditions) if conditions else "1=1"
    offset = (page - 1) * size

    if settings.db_type == "oracle":
        pagination = f"OFFSET {offset} ROWS FETCH NEXT {size} ROWS ONLY"
    else:
        pagination = f"LIMIT {size} OFFSET {offset}"

    with sync_engine.connect() as conn:
        # Count
        count_result = conn.execute(
            text(f"SELECT COUNT(*) FROM ai_analysis_result WHERE {where}"),
            params,
        ).scalar()

        # Items
        rows = conn.execute(
            text(
                f"SELECT * FROM ai_analysis_result WHERE {where} "
                f"ORDER BY anomaly_score DESC {pagination}"
            ),
            params,
        ).fetchall()

    import json

    items = []
    for row in rows:
        r = row._mapping
        rf = r.get("risk_factors") or "{}"
        if isinstance(rf, str):
            rf = json.loads(rf)
        ae = r.get("anomaly_explanations") or "[]"
        if isinstance(ae, str):
            ae = json.loads(ae)

        items.append(
            CertificateAnalysis(
                fingerprint=r["certificate_fingerprint"],
                certificate_type=r.get("certificate_type"),
                country_code=r.get("country_code"),
                anomaly_score=r.get("anomaly_score", 0),
                anomaly_label=r.get("anomaly_label", "NORMAL"),
                risk_score=r.get("risk_score", 0),
                risk_level=r.get("risk_level", "LOW"),
                risk_factors=rf,
                anomaly_explanations=ae,
                analyzed_at=r.get("analyzed_at"),
            )
        )

    return AnomalyListResponse(
        success=True,
        items=items,
        total=count_result or 0,
        page=page,
        size=size,
    )


@router.get("/statistics", response_model=AnalysisStatistics)
async def get_statistics():
    """Get overall analysis statistics."""
    settings = get_settings()

    with sync_engine.connect() as conn:
        # Total + label counts
        total = conn.execute(text("SELECT COUNT(*) FROM ai_analysis_result")).scalar() or 0
        normal = conn.execute(
            text("SELECT COUNT(*) FROM ai_analysis_result WHERE anomaly_label = 'NORMAL'")
        ).scalar() or 0
        suspicious = conn.execute(
            text("SELECT COUNT(*) FROM ai_analysis_result WHERE anomaly_label = 'SUSPICIOUS'")
        ).scalar() or 0
        anomalous = conn.execute(
            text("SELECT COUNT(*) FROM ai_analysis_result WHERE anomaly_label = 'ANOMALOUS'")
        ).scalar() or 0

        # Risk distribution
        risk_rows = conn.execute(
            text("SELECT risk_level, COUNT(*) as cnt FROM ai_analysis_result GROUP BY risk_level")
        ).fetchall()
        risk_dist = {row._mapping["risk_level"]: int(row._mapping["cnt"]) for row in risk_rows}

        # Average risk
        avg_risk = conn.execute(
            text("SELECT AVG(risk_score) FROM ai_analysis_result")
        ).scalar() or 0.0

        # Top anomalous countries
        top_countries_rows = conn.execute(
            text("""
                SELECT country_code,
                       COUNT(*) as total,
                       SUM(CASE WHEN anomaly_label = 'ANOMALOUS' THEN 1 ELSE 0 END) as anomalous_count
                FROM ai_analysis_result
                GROUP BY country_code
                HAVING SUM(CASE WHEN anomaly_label = 'ANOMALOUS' THEN 1 ELSE 0 END) > 0
                ORDER BY SUM(CASE WHEN anomaly_label = 'ANOMALOUS' THEN 1 ELSE 0 END) DESC
            """)
        ).fetchall()

        top_countries = [
            {
                "country": row._mapping["country_code"],
                "total": int(row._mapping["total"]),
                "anomalous": int(row._mapping["anomalous_count"]),
                "anomaly_rate": round(
                    int(row._mapping["anomalous_count"]) / max(int(row._mapping["total"]), 1), 4
                ),
            }
            for row in top_countries_rows[:10]
        ]

        # Last analysis time
        last_at = conn.execute(
            text("SELECT MAX(analyzed_at) FROM ai_analysis_result")
        ).scalar()

    return AnalysisStatistics(
        total_analyzed=total,
        normal_count=normal,
        suspicious_count=suspicious,
        anomalous_count=anomalous,
        risk_distribution=risk_dist,
        avg_risk_score=round(float(avg_risk), 2),
        top_anomalous_countries=top_countries,
        last_analysis_at=last_at,
        model_version=settings.model_version,
    )

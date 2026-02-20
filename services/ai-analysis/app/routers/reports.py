"""Report API endpoints: country maturity, algorithm trends, etc."""

import json
import logging
from typing import Optional

from fastapi import APIRouter, HTTPException, Query
from sqlalchemy import text

from app.config import get_settings
from app.database import sync_engine
from app.schemas.analysis import (
    AlgorithmTrend,
    CountryDetail,
    CountryMaturity,
    KeySizeDistribution,
    RiskDistribution,
)

logger = logging.getLogger(__name__)
router = APIRouter()


@router.get("/reports/country-maturity", response_model=list[CountryMaturity])
async def get_country_maturity():
    """Get PKI maturity scores ranked by country."""
    from app.services.feature_engineering import load_certificate_data
    from app.services.pattern_analyzer import compute_country_maturity

    try:
        df = load_certificate_data(sync_engine)
    except Exception as e:
        logger.error("Failed to load data: %s", e)
        raise HTTPException(status_code=500, detail="Failed to load certificate data")

    results = compute_country_maturity(df)

    return [
        CountryMaturity(
            country_code=r["country_code"],
            country_name=r["country_code"],  # Frontend resolves name via i18n
            maturity_score=r["maturity_score"],
            algorithm_score=r["algorithm_score"],
            key_size_score=r["key_size_score"],
            compliance_score=r["compliance_score"],
            extension_score=r["extension_score"],
            freshness_score=r["freshness_score"],
            certificate_count=r["certificate_count"],
        )
        for r in results
    ]


@router.get("/reports/algorithm-trends", response_model=list[AlgorithmTrend])
async def get_algorithm_trends():
    """Get algorithm usage trends by year."""
    from app.services.feature_engineering import load_certificate_data
    from app.services.pattern_analyzer import compute_algorithm_trends

    df = load_certificate_data(sync_engine)
    return [AlgorithmTrend(**r) for r in compute_algorithm_trends(df)]


@router.get("/reports/key-size-distribution", response_model=list[KeySizeDistribution])
async def get_key_size_distribution():
    """Get key size distribution by algorithm family."""
    from app.services.feature_engineering import load_certificate_data
    from app.services.pattern_analyzer import compute_key_size_distribution

    df = load_certificate_data(sync_engine)
    return [KeySizeDistribution(**r) for r in compute_key_size_distribution(df)]


@router.get("/reports/risk-distribution", response_model=list[RiskDistribution])
async def get_risk_distribution():
    """Get risk level distribution from analysis results."""
    with sync_engine.connect() as conn:
        total = conn.execute(text("SELECT COUNT(*) FROM ai_analysis_result")).scalar() or 0
        if total == 0:
            return []

        rows = conn.execute(
            text("""
                SELECT risk_level, COUNT(*) as cnt, AVG(anomaly_score) as avg_anomaly
                FROM ai_analysis_result
                GROUP BY risk_level
                ORDER BY CASE risk_level
                    WHEN 'CRITICAL' THEN 1
                    WHEN 'HIGH' THEN 2
                    WHEN 'MEDIUM' THEN 3
                    WHEN 'LOW' THEN 4
                    ELSE 5
                END
            """)
        ).fetchall()

    return [
        RiskDistribution(
            risk_level=row._mapping["risk_level"] or "UNKNOWN",
            count=int(row._mapping["cnt"]),
            percentage=round(100.0 * int(row._mapping["cnt"]) / total, 2),
            avg_anomaly_score=round(float(row._mapping["avg_anomaly"] or 0), 4),
        )
        for row in rows
    ]


@router.get("/reports/country/{code}")
async def get_country_report(code: str):
    """Get detailed analysis for a specific country."""
    from app.services.feature_engineering import load_certificate_data
    from app.services.pattern_analyzer import compute_country_detail, compute_country_maturity

    df = load_certificate_data(sync_engine)
    detail = compute_country_detail(df, code.upper())
    if not detail:
        raise HTTPException(status_code=404, detail=f"No data for country {code}")

    # Get risk/anomaly distribution from analysis results
    with sync_engine.connect() as conn:
        risk_rows = conn.execute(
            text(
                "SELECT risk_level, COUNT(*) as cnt FROM ai_analysis_result "
                "WHERE country_code = :cc GROUP BY risk_level"
            ),
            {"cc": code.upper()},
        ).fetchall()
        risk_dist = {r._mapping["risk_level"]: int(r._mapping["cnt"]) for r in risk_rows}

        anomaly_rows = conn.execute(
            text(
                "SELECT anomaly_label, COUNT(*) as cnt FROM ai_analysis_result "
                "WHERE country_code = :cc GROUP BY anomaly_label"
            ),
            {"cc": code.upper()},
        ).fetchall()
        anomaly_dist = {r._mapping["anomaly_label"]: int(r._mapping["cnt"]) for r in anomaly_rows}

        # Top anomalies
        top_rows = conn.execute(
            text(
                "SELECT * FROM ai_analysis_result "
                "WHERE country_code = :cc ORDER BY anomaly_score DESC "
                + ("FETCH FIRST 5 ROWS ONLY" if get_settings().db_type == "oracle" else "LIMIT 5")
            ),
            {"cc": code.upper()},
        ).fetchall()

    top_anomalies = []
    for r in top_rows:
        rm = r._mapping
        rf = rm.get("risk_factors") or "{}"
        if isinstance(rf, str):
            rf = json.loads(rf)
        ae = rm.get("anomaly_explanations") or "[]"
        if isinstance(ae, str):
            ae = json.loads(ae)
        top_anomalies.append({
            "fingerprint": rm["certificate_fingerprint"],
            "certificate_type": rm.get("certificate_type"),
            "country_code": rm.get("country_code"),
            "anomaly_score": rm.get("anomaly_score", 0),
            "anomaly_label": rm.get("anomaly_label", "NORMAL"),
            "risk_score": rm.get("risk_score", 0),
            "risk_level": rm.get("risk_level", "LOW"),
            "risk_factors": rf,
            "anomaly_explanations": ae,
            "analyzed_at": str(rm.get("analyzed_at") or ""),
        })

    # Maturity for this country
    maturity_list = compute_country_maturity(df)
    maturity = next((m for m in maturity_list if m["country_code"] == code.upper()), None)

    detail["risk_distribution"] = risk_dist
    detail["anomaly_distribution"] = anomaly_dist
    detail["maturity"] = maturity
    detail["top_anomalies"] = top_anomalies

    return {"success": True, **detail}

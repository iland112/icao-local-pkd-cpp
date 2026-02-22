"""Report API endpoints: country maturity, algorithm trends, etc."""

import logging
from typing import Optional

from fastapi import APIRouter, HTTPException, Query
from sqlalchemy import text

from app.config import get_settings
from app.database import safe_json_loads, sync_engine
from app.schemas.analysis import (
    AlgorithmTrend,
    CountryDetail,
    CountryMaturity,
    ExtensionAnomaly,
    ForensicSummary,
    IssuerProfile,
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
        rf = safe_json_loads(rm.get("risk_factors"), {})
        ae = safe_json_loads(rm.get("anomaly_explanations"), [])
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


@router.get("/reports/issuer-profiles", response_model=list[IssuerProfile])
async def get_issuer_profiles():
    """Get issuer profile report with anomaly indicators."""
    from app.services.feature_engineering import load_certificate_data
    from app.services.issuer_profiler import build_issuer_profiles, get_issuer_profile_report

    try:
        df = load_certificate_data(sync_engine)
    except Exception as e:
        logger.error("Failed to load data: %s", e)
        raise HTTPException(status_code=500, detail="Failed to load certificate data")

    profiles = build_issuer_profiles(df)
    report = get_issuer_profile_report(df, profiles)

    return [IssuerProfile(**r) for r in report]


@router.get("/reports/forensic-summary", response_model=ForensicSummary)
async def get_forensic_summary():
    """Get forensic analysis summary from stored results."""
    with sync_engine.connect() as conn:
        total = conn.execute(
            text("SELECT COUNT(*) FROM ai_analysis_result WHERE forensic_risk_level IS NOT NULL")
        ).scalar() or 0

        if total == 0:
            return ForensicSummary(
                total_analyzed=0,
                forensic_level_distribution={},
                category_avg_scores={},
            )

        # Level distribution
        level_rows = conn.execute(
            text(
                "SELECT forensic_risk_level, COUNT(*) as cnt "
                "FROM ai_analysis_result WHERE forensic_risk_level IS NOT NULL "
                "GROUP BY forensic_risk_level"
            )
        ).fetchall()
        level_dist = {
            r._mapping["forensic_risk_level"]: int(r._mapping["cnt"])
            for r in level_rows
        }

        # Average scores per category from forensic_findings (JSONB on PostgreSQL, CLOB on Oracle)
        # Unified Python-side parsing — works for both databases
        findings_rows = conn.execute(
            text("SELECT forensic_findings FROM ai_analysis_result WHERE forensic_findings IS NOT NULL")
        ).fetchall()

        from collections import defaultdict
        cat_totals = defaultdict(float)
        cat_counts = defaultdict(int)
        sev_counts = defaultdict(int)
        finding_freq = defaultdict(int)

        for row in findings_rows:
            ff = safe_json_loads(row._mapping.get("forensic_findings"), {})
            for cat, score in ff.get("categories", {}).items():
                if isinstance(score, (int, float)):
                    cat_totals[cat] += score
                    cat_counts[cat] += 1
            for f in ff.get("findings", []):
                sev_counts[f.get("severity", "LOW")] += 1
                finding_freq[f.get("message", "")] += 1

        cat_avgs = {
            cat: round(cat_totals[cat] / max(cat_counts[cat], 1), 2)
            for cat in cat_totals
        }
        top_findings = sorted(finding_freq.items(), key=lambda x: -x[1])[:10]

    return ForensicSummary(
        total_analyzed=total,
        forensic_level_distribution=level_dist,
        category_avg_scores=cat_avgs,
        severity_distribution=dict(sev_counts) if sev_counts else None,
        top_findings=[{"message": m, "count": c} for m, c in top_findings] if top_findings else None,
    )


@router.get("/reports/extension-anomalies", response_model=list[ExtensionAnomaly])
async def get_extension_anomalies(
    cert_type: Optional[str] = Query(None, alias="type"),
    country: Optional[str] = Query(None),
    limit: int = Query(50, ge=1, le=200),
):
    """Get extension rule violations list."""
    from app.services.extension_rules_engine import compute_extension_anomalies
    from app.services.feature_engineering import load_certificate_data

    try:
        df = load_certificate_data(sync_engine)
    except Exception as e:
        logger.error("Failed to load data: %s", e)
        raise HTTPException(status_code=500, detail="Failed to load certificate data")

    if cert_type:
        df = df[df["certificate_type"] == cert_type]
    if country:
        df = df[df["country_code"] == country.upper()]

    results = compute_extension_anomalies(df)

    return [ExtensionAnomaly(**r) for r in results[:limit]]

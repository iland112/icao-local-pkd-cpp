"""Report API endpoints: country maturity, algorithm trends, etc."""

import asyncio
import logging
import re
from typing import Optional

from fastapi import APIRouter, HTTPException, Query

# Input validation patterns (security hardening)
_COUNTRY_RE = re.compile(r"^[A-Z]{2,3}$")
_CERT_TYPE_RE = re.compile(r"^(CSCA|DSC|DSC_NC|MLSC|LC)$")
from sqlalchemy import text

from app.config import get_settings
from app.database import safe_json_loads, sync_engine
from app.schemas.analysis import (
    AlgorithmTrend,
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

    def _compute():
        df = load_certificate_data(sync_engine)
        return compute_country_maturity(df)

    try:
        results = await asyncio.to_thread(_compute)
    except Exception as e:
        logger.error("Failed to load data: %s", e)
        raise HTTPException(status_code=500, detail="Failed to load certificate data")

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

    def _compute():
        df = load_certificate_data(sync_engine)
        return compute_algorithm_trends(df)

    try:
        results = await asyncio.to_thread(_compute)
    except Exception as e:
        logger.error("Failed to compute algorithm trends: %s", e)
        raise HTTPException(status_code=503, detail="Failed to load certificate data")

    return [AlgorithmTrend(**r) for r in results]


@router.get("/reports/key-size-distribution", response_model=list[KeySizeDistribution])
async def get_key_size_distribution():
    """Get key size distribution by algorithm family."""
    from app.services.feature_engineering import load_certificate_data
    from app.services.pattern_analyzer import compute_key_size_distribution

    def _compute():
        df = load_certificate_data(sync_engine)
        return compute_key_size_distribution(df)

    try:
        results = await asyncio.to_thread(_compute)
    except Exception as e:
        logger.error("Failed to compute key size distribution: %s", e)
        raise HTTPException(status_code=503, detail="Failed to load certificate data")

    return [KeySizeDistribution(**r) for r in results]


@router.get("/reports/risk-distribution", response_model=list[RiskDistribution])
async def get_risk_distribution():
    """Get risk level distribution from analysis results."""

    def _query():
        with sync_engine.connect() as conn:
            total = conn.execute(text("SELECT COUNT(*) FROM ai_analysis_result")).scalar() or 0
            if total == 0:
                return 0, []

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
            return total, rows

    try:
        total, rows = await asyncio.to_thread(_query)
    except Exception as e:
        logger.error("Failed to query risk distribution: %s", e)
        raise HTTPException(status_code=503, detail="Database query failed")

    if total == 0:
        return []

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
    if not _COUNTRY_RE.match(code.upper()):
        raise HTTPException(status_code=400, detail="Invalid country code format")

    country_upper = code.upper()

    def _query_country_detail():
        with sync_engine.connect() as conn:
            # DB-level aggregation for country detail (avoids loading all certificates)
            total = conn.execute(
                text("SELECT COUNT(*) FROM certificate WHERE country_code = :cc"),
                {"cc": country_upper},
            ).scalar() or 0
            if total == 0:
                return None

            type_rows = conn.execute(
                text(
                    "SELECT certificate_type, COUNT(*) as cnt FROM certificate "
                    "WHERE country_code = :cc GROUP BY certificate_type"
                ),
                {"cc": country_upper},
            ).fetchall()

            alg_rows = conn.execute(
                text(
                    "SELECT signature_algorithm, COUNT(*) as cnt FROM certificate "
                    "WHERE country_code = :cc GROUP BY signature_algorithm"
                ),
                {"cc": country_upper},
            ).fetchall()

            ks_rows = conn.execute(
                text(
                    "SELECT public_key_size, COUNT(*) as cnt FROM certificate "
                    "WHERE country_code = :cc AND public_key_size IS NOT NULL "
                    "GROUP BY public_key_size"
                ),
                {"cc": country_upper},
            ).fetchall()

            return {
                "country_code": country_upper,
                "total_certificates": total,
                "type_distribution": {
                    str(r._mapping["certificate_type"]): int(r._mapping["cnt"])
                    for r in type_rows
                },
                "algorithm_distribution": {
                    str(r._mapping["signature_algorithm"]): int(r._mapping["cnt"])
                    for r in alg_rows
                },
                "key_size_distribution": {
                    str(int(r._mapping["public_key_size"])): int(r._mapping["cnt"])
                    for r in ks_rows
                },
            }

    try:
        detail = await asyncio.to_thread(_query_country_detail)
    except Exception as e:
        logger.error("Failed to query country detail for %s: %s", code, e)
        raise HTTPException(status_code=500, detail="Database query failed")

    if not detail:
        raise HTTPException(status_code=404, detail=f"No data for country {code}")

    # Compute maturity (requires full dataset for relative scoring)
    def _compute_maturity():
        from app.services.feature_engineering import load_certificate_data
        from app.services.pattern_analyzer import compute_country_maturity

        df = load_certificate_data(sync_engine)
        maturity_list = compute_country_maturity(df)
        return next((m for m in maturity_list if m["country_code"] == country_upper), None)

    try:
        maturity = await asyncio.to_thread(_compute_maturity)
    except Exception:
        maturity = None

    # Get risk/anomaly distribution from analysis results
    def _query_analysis():
        with sync_engine.connect() as conn:
            risk_rows = conn.execute(
                text(
                    "SELECT risk_level, COUNT(*) as cnt FROM ai_analysis_result "
                    "WHERE country_code = :cc GROUP BY risk_level"
                ),
                {"cc": country_upper},
            ).fetchall()
            risk_dist = {r._mapping["risk_level"]: int(r._mapping["cnt"]) for r in risk_rows}

            anomaly_rows = conn.execute(
                text(
                    "SELECT anomaly_label, COUNT(*) as cnt FROM ai_analysis_result "
                    "WHERE country_code = :cc GROUP BY anomaly_label"
                ),
                {"cc": country_upper},
            ).fetchall()
            anomaly_dist = {r._mapping["anomaly_label"]: int(r._mapping["cnt"]) for r in anomaly_rows}

            # Top anomalies (parameterized limit)
            if get_settings().db_type == "oracle":
                top_sql = (
                    "SELECT * FROM ai_analysis_result "
                    "WHERE country_code = :cc ORDER BY anomaly_score DESC "
                    "FETCH FIRST :top_n ROWS ONLY"
                )
            else:
                top_sql = (
                    "SELECT * FROM ai_analysis_result "
                    "WHERE country_code = :cc ORDER BY anomaly_score DESC "
                    "LIMIT :top_n"
                )
            top_rows = conn.execute(
                text(top_sql),
                {"cc": country_upper, "top_n": 5},
            ).fetchall()
            return risk_dist, anomaly_dist, top_rows

    try:
        risk_dist, anomaly_dist, top_rows = await asyncio.to_thread(_query_analysis)
    except Exception as e:
        logger.error("Failed to query analysis for %s: %s", code, e)
        raise HTTPException(status_code=500, detail="Database query failed")

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

    def _compute():
        df = load_certificate_data(sync_engine)
        profiles = build_issuer_profiles(df)
        return get_issuer_profile_report(df, profiles)

    try:
        report = await asyncio.to_thread(_compute)
    except Exception as e:
        logger.error("Failed to load data: %s", e)
        raise HTTPException(status_code=500, detail="Failed to load certificate data")

    return [IssuerProfile(**r) for r in report]


@router.get("/reports/forensic-summary", response_model=ForensicSummary)
async def get_forensic_summary():
    """Get forensic analysis summary from stored results."""

    def _query():
        with sync_engine.connect() as conn:
            total = conn.execute(
                text("SELECT COUNT(*) FROM ai_analysis_result WHERE forensic_risk_level IS NOT NULL")
            ).scalar() or 0

            if total == 0:
                return None

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

            return {
                "total": total, "level_dist": level_dist, "cat_avgs": cat_avgs,
                "sev_counts": dict(sev_counts), "top_findings": top_findings,
            }

    try:
        result = await asyncio.to_thread(_query)
    except Exception as e:
        logger.error("Failed to query forensic summary: %s", e)
        raise HTTPException(status_code=503, detail="Database query failed")

    if result is None:
        return ForensicSummary(
            total_analyzed=0,
            forensic_level_distribution={},
            category_avg_scores={},
        )

    return ForensicSummary(
        total_analyzed=result["total"],
        forensic_level_distribution=result["level_dist"],
        category_avg_scores=result["cat_avgs"],
        severity_distribution=result["sev_counts"] if result["sev_counts"] else None,
        top_findings=[{"message": m, "count": c} for m, c in result["top_findings"]] if result["top_findings"] else None,
    )


@router.get("/reports/extension-anomalies", response_model=list[ExtensionAnomaly])
async def get_extension_anomalies(
    cert_type: Optional[str] = Query(None, alias="type"),
    country: Optional[str] = Query(None),
    limit: int = Query(50, ge=1, le=200),
):
    """Get extension rule violations list."""
    # Input validation
    if cert_type and not _CERT_TYPE_RE.match(cert_type):
        raise HTTPException(status_code=400, detail="Invalid certificate type")
    if country and not _COUNTRY_RE.match(country.upper()):
        raise HTTPException(status_code=400, detail="Invalid country code format")

    from app.services.extension_rules_engine import compute_extension_anomalies
    from app.services.feature_engineering import load_certificate_data

    def _compute():
        df = load_certificate_data(sync_engine)
        if cert_type:
            filtered_df = df[df["certificate_type"] == cert_type]
        else:
            filtered_df = df
        if country:
            filtered_df = filtered_df[filtered_df["country_code"] == country.upper()]
        return compute_extension_anomalies(filtered_df)

    try:
        results = await asyncio.to_thread(_compute)
    except Exception as e:
        logger.error("Failed to load data: %s", e)
        raise HTTPException(status_code=500, detail="Failed to load certificate data")

    return [ExtensionAnomaly(**r) for r in results[:limit]]

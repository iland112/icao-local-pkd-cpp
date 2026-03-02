"""Issuer profiling: behavioral pattern analysis per issuer DN.

Uses DBSCAN clustering to establish "normal" issuer profiles,
then flags certificates that deviate from their issuer's typical pattern.
"""

import logging
import re
from collections import defaultdict

from app.database import safe_isna

import numpy as np
import pandas as pd
from sklearn.preprocessing import StandardScaler

logger = logging.getLogger(__name__)


def _extract_country_from_dn(dn: str) -> str:
    """Extract country code from a DN string (slash or RFC 2253 format)."""
    if not dn:
        return ""
    # Slash format: /C=KR/O=...
    m = re.search(r"/C=([A-Z]{2})", dn, re.IGNORECASE)
    if m:
        return m.group(1).upper()
    # RFC 2253: C=KR,...
    m = re.search(r"(?:^|,)\s*C=([A-Z]{2})", dn, re.IGNORECASE)
    if m:
        return m.group(1).upper()
    return ""


def _count_dn_fields(dn: str) -> int:
    """Count fields in a DN string."""
    if not dn:
        return 0
    dn = str(dn).strip()
    if dn.startswith("/"):
        # Slash format: /C=KR/O=Gov/CN=Name
        return len([p for p in dn.split("/") if "=" in p])
    else:
        # RFC 2253: CN=Name, O=Gov, C=KR
        return len([p for p in dn.split(",") if "=" in p])


def _detect_dn_format(dn: str) -> int:
    """Detect DN format type: 0=RFC 2253, 1=slash, 2=other."""
    if not dn:
        return 2
    dn = str(dn).strip()
    if dn.startswith("/"):
        return 1
    if "," in dn and "=" in dn:
        return 0
    return 2


def _has_email_in_dn(dn: str) -> bool:
    """Check if DN contains an email field."""
    if not dn:
        return False
    dn_lower = str(dn).lower()
    return "emailaddress=" in dn_lower or "email=" in dn_lower or "e=" in dn_lower


def build_issuer_profiles(df: pd.DataFrame) -> dict:
    """Build behavioral profiles for each issuer.

    Returns:
        Dict mapping issuer_dn to profile dict with statistics.
    """
    if "issuer_dn" not in df.columns:
        logger.warning("issuer_dn column not found, skipping issuer profiling")
        return {}

    profiles = {}
    issuer_groups = df.groupby("issuer_dn")

    for issuer_dn, group in issuer_groups:
        if safe_isna(issuer_dn) or not str(issuer_dn).strip():
            continue

        n = len(group)
        cert_types = group["certificate_type"].value_counts().to_dict()
        sig_algs = group["signature_algorithm"].value_counts()
        key_sizes = group["public_key_size"].dropna()
        countries = group["country_code"].value_counts()

        # Compliance stats
        icao_ok = group["icao_compliant"].apply(
            lambda x: str(x).lower() in ("true", "1")
        ).mean()
        expired = group["validation_status"].apply(
            lambda x: str(x).upper() in ("EXPIRED", "EXPIRED_VALID")
        ).mean()

        profiles[issuer_dn] = {
            "cert_count": n,
            "type_diversity": len(cert_types),
            "types": cert_types,
            "dominant_algorithm": sig_algs.index[0] if len(sig_algs) > 0 else "",
            "algorithm_diversity": len(sig_algs),
            "avg_key_size": float(key_sizes.mean()) if len(key_sizes) > 0 else 0,
            "std_key_size": float(key_sizes.std()) if len(key_sizes) > 1 else 0,
            "country_count": len(countries),
            "dominant_country": countries.index[0] if len(countries) > 0 else "",
            "compliance_rate": round(icao_ok, 4),
            "expired_rate": round(expired, 4),
            "anomaly_proxy": round(1.0 - icao_ok + expired * 0.5, 4),
        }

    logger.info("Built profiles for %d issuers", len(profiles))
    return profiles


def compute_issuer_anomaly_scores(
    df: pd.DataFrame, profiles: dict
) -> np.ndarray:
    """Compute per-certificate issuer anomaly score.

    Certificates are scored based on how much they deviate from
    their issuer's typical pattern.

    Uses vectorized pandas/numpy operations instead of row iteration.

    Returns:
        Array of issuer_anomaly_score (0.0 ~ 1.0) per certificate.
    """
    n = len(df)
    scores = np.zeros(n, dtype=np.float64)

    if "issuer_dn" not in df.columns or not profiles:
        return scores

    issuer_dns = df["issuer_dn"]

    # Map each issuer_dn to its profile (None if not found)
    profile_series = issuer_dns.map(profiles)

    # Mask: issuer_dn is missing or not in profiles
    dn_missing = issuer_dns.isna() | (issuer_dns.astype(str).str.strip() == "")
    no_profile = profile_series.isna() & ~dn_missing

    # Unknown / unmatched issuers get 0.3
    scores[dn_missing.values] = 0.3
    scores[no_profile.values] = 0.3

    # Rows that have a valid profile
    has_profile = ~dn_missing & ~no_profile
    if not has_profile.any():
        logger.info(
            "Issuer anomaly scores: avg=%.3f, max=%.3f, >0.5=%d",
            scores.mean(), scores.max(), int(np.sum(scores > 0.5)),
        )
        return scores

    idx = has_profile.values  # boolean mask for numpy indexing

    # Extract profile fields as numpy arrays for the matched subset
    profiles_matched = profile_series[has_profile]
    cert_counts = np.array([p["cert_count"] for p in profiles_matched], dtype=np.float64)
    avg_key_sizes = np.array([p["avg_key_size"] for p in profiles_matched], dtype=np.float64)
    std_key_sizes = np.array([p["std_key_size"] for p in profiles_matched], dtype=np.float64)
    dominant_algs = np.array([p["dominant_algorithm"] for p in profiles_matched])
    alg_diversities = np.array([p["algorithm_diversity"] for p in profiles_matched], dtype=np.float64)
    anomaly_proxies = np.array([p["anomaly_proxy"] for p in profiles_matched], dtype=np.float64)
    dominant_countries = np.array([p["dominant_country"] for p in profiles_matched])
    country_counts = np.array([p["country_count"] for p in profiles_matched], dtype=np.float64)

    sub_scores = np.zeros(int(np.sum(idx)), dtype=np.float64)

    # 1. Rare issuer (few certs = higher risk)
    sub_scores += np.where(cert_counts < 3, 0.15, np.where(cert_counts < 10, 0.05, 0.0))

    # 2. Key size deviation from issuer average
    key_sizes_col = df.loc[has_profile, "public_key_size"].fillna(0).values.astype(np.float64) \
        if "public_key_size" in df.columns else np.zeros(int(np.sum(idx)), dtype=np.float64)
    valid_ks = (avg_key_sizes > 0) & (std_key_sizes > 0) & (key_sizes_col > 0)
    z_scores = np.zeros_like(sub_scores)
    z_scores[valid_ks] = np.abs(key_sizes_col[valid_ks] - avg_key_sizes[valid_ks]) / std_key_sizes[valid_ks]
    sub_scores += np.where(valid_ks & (z_scores > 3), 0.2, np.where(valid_ks & (z_scores > 2), 0.1, 0.0))

    # 3. Algorithm mismatch from issuer dominant
    sig_algs_col = df.loc[has_profile, "signature_algorithm"].fillna("").values \
        if "signature_algorithm" in df.columns else np.array([""] * int(np.sum(idx)))
    alg_mismatch = (sig_algs_col != "") & (sig_algs_col != dominant_algs) & (alg_diversities <= 2)
    sub_scores += np.where(alg_mismatch, 0.15, 0.0)

    # 4. Issuer overall anomaly proxy (compliance + expiry)
    sub_scores += anomaly_proxies * 0.2

    # 5. Country mismatch (issuer issues certs outside its usual country)
    countries_col = df.loc[has_profile, "country_code"].fillna("").values \
        if "country_code" in df.columns else np.array([""] * int(np.sum(idx)))
    country_mismatch = (countries_col != "") & (countries_col != dominant_countries) & (country_counts == 1)
    sub_scores += np.where(country_mismatch, 0.15, 0.0)

    scores[idx] = np.minimum(sub_scores, 1.0)

    logger.info(
        "Issuer anomaly scores: avg=%.3f, max=%.3f, >0.5=%d",
        scores.mean(),
        scores.max(),
        int(np.sum(scores > 0.5)),
    )
    return scores


def get_issuer_profile_report(df: pd.DataFrame, profiles: dict) -> list[dict]:
    """Generate issuer profile report for API.

    Returns sorted list of issuer profiles with anomaly indicators.
    """
    report = []

    for issuer_dn, profile in profiles.items():
        # Compute risk indicator
        risk_indicator = "LOW"
        proxy = profile["anomaly_proxy"]
        if proxy > 0.7:
            risk_indicator = "HIGH"
        elif proxy > 0.3:
            risk_indicator = "MEDIUM"

        report.append({
            "issuer_dn": str(issuer_dn)[:200],  # truncate long DNs
            "cert_count": profile["cert_count"],
            "type_diversity": profile["type_diversity"],
            "types": profile["types"],
            "dominant_algorithm": profile["dominant_algorithm"],
            "avg_key_size": int(profile["avg_key_size"]),
            "compliance_rate": profile["compliance_rate"],
            "expired_rate": profile["expired_rate"],
            "risk_indicator": risk_indicator,
            "country": profile["dominant_country"],
        })

    report.sort(key=lambda x: x["compliance_rate"])
    return report


# DN utility functions exported for feature_engineering
extract_country_from_dn = _extract_country_from_dn
count_dn_fields = _count_dn_fields
detect_dn_format = _detect_dn_format
has_email_in_dn = _has_email_in_dn

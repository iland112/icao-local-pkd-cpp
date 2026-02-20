"""Pattern analysis: country maturity, algorithm trends, key size distribution."""

import logging
from collections import defaultdict

import pandas as pd

logger = logging.getLogger(__name__)


def compute_country_maturity(df: pd.DataFrame) -> list[dict]:
    """Compute PKI maturity scores per country (0~100)."""
    results = []

    for country, group in df.groupby("country_code"):
        n = len(group)
        if n < 3:
            continue

        # 1. Algorithm score (0~100): % of certs using SHA-256+
        modern_algs = group["signature_algorithm"].apply(
            lambda x: "sha256" in str(x).lower()
            or "sha384" in str(x).lower()
            or "sha512" in str(x).lower()
            or "pss" in str(x).lower()
        )
        alg_score = modern_algs.mean() * 100

        # ECDSA adoption bonus
        ecdsa_ratio = group["public_key_algorithm"].apply(
            lambda x: "ec" in str(x).lower()
        ).mean()
        alg_score = min(alg_score + ecdsa_ratio * 20, 100)

        # 2. Key size score (0~100)
        def key_size_quality(row):
            ks = row.get("public_key_size") or 0
            alg = str(row.get("public_key_algorithm") or "").lower()
            if "rsa" in alg:
                if ks >= 4096:
                    return 1.0
                elif ks >= 3072:
                    return 0.8
                elif ks >= 2048:
                    return 0.6
                return 0.1
            elif "ec" in alg:
                if ks >= 384:
                    return 1.0
                elif ks >= 256:
                    return 0.7
                return 0.2
            return 0.3

        ks_score = group.apply(key_size_quality, axis=1).mean() * 100

        # 3. Compliance score (0~100): ICAO compliant + trust chain valid
        icao_ok = group["icao_compliant"].apply(
            lambda x: str(x).lower() in ("true", "1")
        ).mean()
        trust_ok = group["trust_chain_valid"].apply(
            lambda x: str(x).lower() in ("true", "1")
        ).mean()
        compliance_score = ((icao_ok + trust_ok) / 2) * 100

        # 4. Extension score (0~100): CRL DP + AKI + SKI usage rates
        has_cdp = group["crl_distribution_points"].notna().mean()
        has_aki = group["authority_key_identifier"].notna().mean()
        has_ski = group["subject_key_identifier"].notna().mean()
        ext_score = ((has_cdp + has_aki + has_ski) / 3) * 100

        # 5. Freshness score (0~100): inverse of expired ratio
        expired = group["validation_status"].apply(
            lambda x: str(x).upper() in ("EXPIRED", "EXPIRED_VALID")
        ).mean()
        freshness_score = (1.0 - expired) * 100

        # Weighted composite
        maturity = (
            0.25 * alg_score
            + 0.20 * ks_score
            + 0.25 * compliance_score
            + 0.15 * ext_score
            + 0.15 * freshness_score
        )

        results.append(
            {
                "country_code": country,
                "maturity_score": round(maturity, 1),
                "algorithm_score": round(alg_score, 1),
                "key_size_score": round(ks_score, 1),
                "compliance_score": round(compliance_score, 1),
                "extension_score": round(ext_score, 1),
                "freshness_score": round(freshness_score, 1),
                "certificate_count": n,
            }
        )

    results.sort(key=lambda x: x["maturity_score"], reverse=True)
    logger.info("Computed maturity scores for %d countries", len(results))
    return results


def compute_algorithm_trends(df: pd.DataFrame) -> list[dict]:
    """Compute algorithm distribution by issuance year."""
    df_copy = df.copy()
    df_copy["year"] = pd.to_datetime(df_copy["not_before"], errors="coerce").dt.year
    df_copy = df_copy.dropna(subset=["year"])
    df_copy["year"] = df_copy["year"].astype(int)

    results = []
    for year, group in df_copy.groupby("year"):
        if year < 2000 or year > 2030:
            continue
        alg_counts = group["signature_algorithm"].value_counts().to_dict()
        results.append(
            {
                "year": int(year),
                "algorithms": {str(k): int(v) for k, v in alg_counts.items()},
                "total": len(group),
            }
        )

    results.sort(key=lambda x: x["year"])
    return results


def compute_key_size_distribution(df: pd.DataFrame) -> list[dict]:
    """Compute key size distribution by algorithm family."""
    results = []
    total = len(df)

    for (alg, ks), group in df.groupby(["public_key_algorithm", "public_key_size"]):
        if pd.isna(alg) or pd.isna(ks):
            continue
        count = len(group)
        results.append(
            {
                "algorithm": str(alg),
                "key_size": int(ks),
                "count": count,
                "percentage": round(100.0 * count / total, 2) if total > 0 else 0,
            }
        )

    results.sort(key=lambda x: (-x["count"],))
    return results


def compute_country_detail(df: pd.DataFrame, country_code: str) -> dict | None:
    """Compute detailed analysis for a specific country."""
    country_df = df[df["country_code"] == country_code]
    if country_df.empty:
        return None

    type_dist = country_df["certificate_type"].value_counts().to_dict()
    alg_dist = country_df["signature_algorithm"].value_counts().to_dict()
    ks_dist = country_df["public_key_size"].dropna().astype(int).value_counts().to_dict()

    return {
        "country_code": country_code,
        "total_certificates": len(country_df),
        "type_distribution": {str(k): int(v) for k, v in type_dist.items()},
        "algorithm_distribution": {str(k): int(v) for k, v in alg_dist.items()},
        "key_size_distribution": {str(k): int(v) for k, v in ks_dist.items()},
    }

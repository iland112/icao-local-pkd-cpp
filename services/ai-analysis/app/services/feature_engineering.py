"""Feature engineering pipeline: Certificate DB rows → ML feature vectors."""

import hashlib
import logging
from collections import defaultdict
from datetime import datetime, timezone

import numpy as np
import pandas as pd
from sqlalchemy import text

from app.config import get_settings
from app.database import safe_isna

logger = logging.getLogger(__name__)

# Algorithm quality scores (higher = better/newer)
ALGORITHM_SCORES = {
    "sha512WithRSAEncryption": 1.0,
    "ecdsa-with-SHA512": 1.0,
    "sha384WithRSAEncryption": 0.9,
    "ecdsa-with-SHA384": 0.9,
    "sha256WithRSAEncryption": 0.8,
    "ecdsa-with-SHA256": 0.8,
    "id-RSASSA-PSS": 0.85,
    "sha1WithRSAEncryption": 0.2,
    "ecdsa-with-SHA1": 0.2,
}

# Certificate type encoding
CERT_TYPE_MAP = {"CSCA": 0, "DSC": 1, "DSC_NC": 2, "MLSC": 3}

# Feature names for explainability (45 features)
FEATURE_NAMES = [
    # Original 25 features (indices 0-24)
    "key_size_normalized",
    "algorithm_age_score",
    "is_ecdsa",
    "is_rsa_pss",
    "validity_days",
    "validity_ratio",
    "days_until_expiry",
    "is_expired",
    "icao_compliant",
    "trust_chain_valid",
    "icao_violation_count",
    "key_usage_compliant",
    "algorithm_compliant",
    "extension_count",
    "has_crl_dp",
    "has_ocsp",
    "has_aki",
    "is_ca",
    "is_self_signed",
    "version",
    "path_len",
    "key_size_vs_country_avg",
    "validity_vs_country_avg",
    "country_cert_count",
    "cert_type_encoded",
    # Issuer profile (4) — indices 25-28
    "issuer_cert_count",
    "issuer_anomaly_rate",
    "issuer_type_diversity",
    "issuer_country_match",
    # Temporal pattern (4) — indices 29-32
    "issuance_month",
    "validity_period_zscore",
    "issuance_rate_deviation",
    "cert_age_ratio",
    # DN structure (4) — indices 33-36
    "subject_dn_field_count",
    "issuer_dn_field_count",
    "dn_format_type",
    "subject_has_email",
    # Extension profile (4) — indices 37-40
    "extension_set_hash",
    "unexpected_extensions",
    "missing_required_extensions",
    "critical_extension_count",
    # Cross-certificate (4) — indices 41-44
    "same_issuer_key_size_dev",
    "same_issuer_algo_match",
    "country_peer_risk_avg",
    "type_peer_extension_match",
]

# Korean feature explanations for anomaly reasons
FEATURE_EXPLANATIONS_KO = {
    "key_size_normalized": "키 크기",
    "algorithm_age_score": "알고리즘 권장 수준",
    "is_ecdsa": "ECDSA 사용 여부",
    "is_rsa_pss": "RSA-PSS 사용 여부",
    "validity_days": "유효기간 일수",
    "validity_ratio": "유형 평균 대비 유효기간",
    "days_until_expiry": "만료까지 남은 일수",
    "is_expired": "만료 여부",
    "icao_compliant": "ICAO 9303 준수",
    "trust_chain_valid": "신뢰 체인 유효성",
    "icao_violation_count": "ICAO 위반 항목 수",
    "key_usage_compliant": "Key Usage 준수",
    "algorithm_compliant": "알고리즘 준수",
    "extension_count": "확장 필드 수",
    "has_crl_dp": "CRL Distribution Point 존재",
    "has_ocsp": "OCSP Responder 존재",
    "has_aki": "Authority Key Identifier 존재",
    "is_ca": "CA 인증서 여부",
    "is_self_signed": "자체 서명 여부",
    "version": "X.509 버전",
    "path_len": "pathLen 제약",
    "key_size_vs_country_avg": "국가 평균 대비 키 크기 편차",
    "validity_vs_country_avg": "국가 평균 대비 유효기간 편차",
    "country_cert_count": "해당 국가 인증서 수",
    "cert_type_encoded": "인증서 유형",
    # New forensic features
    "issuer_cert_count": "발급자 인증서 수",
    "issuer_anomaly_rate": "발급자 이상 비율",
    "issuer_type_diversity": "발급자 유형 다양성",
    "issuer_country_match": "발급자/주체 국가 일치",
    "issuance_month": "발급 월",
    "validity_period_zscore": "유효기간 Z-score",
    "issuance_rate_deviation": "발급 속도 편차",
    "cert_age_ratio": "인증서 수명 비율",
    "subject_dn_field_count": "Subject DN 필드 수",
    "issuer_dn_field_count": "Issuer DN 필드 수",
    "dn_format_type": "DN 포맷 유형",
    "subject_has_email": "Subject 이메일 포함",
    "extension_set_hash": "확장 조합 해시",
    "unexpected_extensions": "예상 외 확장 수",
    "missing_required_extensions": "필수 확장 누락 수",
    "critical_extension_count": "Critical 확장 수",
    "same_issuer_key_size_dev": "동일 발급자 키 크기 편차",
    "same_issuer_algo_match": "동일 발급자 알고리즘 일치",
    "country_peer_risk_avg": "국가 평균 리스크",
    "type_peer_extension_match": "유형 확장 패턴 일치도",
}

# Extension fields for set hash
_EXT_FIELDS = [
    "key_usage", "extended_key_usage", "subject_key_identifier",
    "authority_key_identifier", "crl_distribution_points", "ocsp_responder_url",
]


def load_certificate_data(engine) -> pd.DataFrame:
    """Load certificate + validation_result data from DB via SQL JOIN."""
    settings = get_settings()

    # v2.19.0: Added subject_dn, issuer_dn, serial_number for forensic features
    if settings.db_type == "oracle":
        query = """
            SELECT c.fingerprint_sha256, c.certificate_type, c.country_code,
                   c.version, c.signature_algorithm, c.public_key_algorithm,
                   c.public_key_size, c.public_key_curve,
                   c.key_usage, c.extended_key_usage,
                   c.is_ca, c.path_len_constraint, c.is_self_signed,
                   c.subject_key_identifier, c.authority_key_identifier,
                   c.crl_distribution_points, c.ocsp_responder_url,
                   c.not_before, c.not_after, c.validation_status,
                   DBMS_LOB.SUBSTR(c.subject_dn, 2000, 1) AS subject_dn,
                   DBMS_LOB.SUBSTR(c.issuer_dn, 2000, 1) AS issuer_dn,
                   c.serial_number,
                   v.trust_chain_valid, v.icao_compliant, v.icao_violations,
                   v.icao_key_usage_compliant, v.icao_algorithm_compliant,
                   v.icao_key_size_compliant, v.icao_extensions_compliant,
                   v.signature_valid
            FROM certificate c
            LEFT JOIN validation_result v ON c.fingerprint_sha256 = v.certificate_id
            WHERE c.certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC')
        """
    else:
        # PostgreSQL: validation_result.certificate_id is UUID FK → certificate.id
        # Must JOIN on c.id = v.certificate_id (UUID = UUID), not fingerprint = UUID
        query = """
            SELECT c.fingerprint_sha256, c.certificate_type, c.country_code,
                   c.version, c.signature_algorithm, c.public_key_algorithm,
                   c.public_key_size, c.public_key_curve,
                   c.key_usage, c.extended_key_usage,
                   c.is_ca, c.path_len_constraint, c.is_self_signed,
                   c.subject_key_identifier, c.authority_key_identifier,
                   c.crl_distribution_points, c.ocsp_responder_url,
                   c.not_before, c.not_after, c.validation_status,
                   c.subject_dn, c.issuer_dn, c.serial_number,
                   v.trust_chain_valid, v.icao_compliant, v.icao_violations,
                   v.icao_key_usage_compliant, v.icao_algorithm_compliant,
                   v.icao_key_size_compliant, v.icao_extensions_compliant,
                   v.signature_valid
            FROM certificate c
            LEFT JOIN validation_result v ON c.id = v.certificate_id
            WHERE c.certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC')
        """

    logger.info("Loading certificate data from DB...")
    df = pd.read_sql(text(query), engine)
    # Deduplicate: PostgreSQL LEFT JOIN may produce multiple rows per certificate
    # when multiple validation_results exist (UNIQUE on certificate_id + upload_id)
    orig_len = len(df)
    df = df.drop_duplicates(subset=["fingerprint_sha256"], keep="first")
    if len(df) < orig_len:
        logger.info("Deduplicated %d → %d certificates", orig_len, len(df))
    logger.info("Loaded %d certificates", len(df))
    return df


def _safe_bool(val) -> float:
    """Convert various boolean representations to float (0.0 or 1.0)."""
    if val is None:
        return 0.0
    if isinstance(val, bool):
        return 1.0 if val else 0.0
    if isinstance(val, (int, float)):
        return 1.0 if val else 0.0
    if isinstance(val, str):
        return 1.0 if val.lower() in ("true", "1", "t", "yes") else 0.0
    return 0.0


def _count_extensions(row) -> int:
    """Count present extension fields."""
    count = 0
    for f in _EXT_FIELDS:
        val = row.get(f)
        if val and not safe_isna(val):
            count += 1
    return count


def _count_violations(violations_str) -> int:
    """Count ICAO violations from pipe-separated string."""
    if not violations_str or safe_isna(violations_str):
        return 0
    return len(str(violations_str).split("|"))


def _extension_set_hash(row) -> float:
    """Compute a hash-based identifier for the combination of extensions present."""
    parts = []
    for f in _EXT_FIELDS:
        val = row.get(f)
        parts.append("1" if val and not safe_isna(val) else "0")
    h = int(hashlib.md5("".join(parts).encode()).hexdigest()[:8], 16)
    return (h % 1000) / 1000.0  # normalize to 0~1


def _count_critical_extensions(row) -> int:
    """Count extensions marked as critical (from key_usage string)."""
    count = 0
    ku = str(row.get("key_usage") or "")
    if "critical" in ku.lower():
        count += 1
    eku = str(row.get("extended_key_usage") or "")
    if "critical" in eku.lower():
        count += 1
    return count


def engineer_features(df: pd.DataFrame) -> tuple[pd.DataFrame, np.ndarray]:
    """Transform raw certificate DataFrame into ML feature vectors.

    Returns:
        (metadata_df, feature_matrix) where metadata_df has fingerprint/type/country
        and feature_matrix is the numpy array of features (n x 45).
    """
    from app.services.extension_rules_engine import (
        count_missing_required,
        count_unexpected_extensions,
    )
    from app.services.issuer_profiler import (
        count_dn_fields,
        detect_dn_format,
        extract_country_from_dn,
        has_email_in_dn,
    )

    now = datetime.now(timezone.utc)
    n = len(df)
    features = np.zeros((n, len(FEATURE_NAMES)), dtype=np.float64)

    # Pre-compute country-level averages
    country_avg_key_size = df.groupby("country_code")["public_key_size"].mean().to_dict()
    type_avg_validity = {}
    type_std_validity = {}
    country_avg_validity = {}
    country_cert_counts = df["country_code"].value_counts().to_dict()

    # Compute validity days
    df["_validity_days"] = pd.NaT
    valid_mask = df["not_before"].notna() & df["not_after"].notna()
    if valid_mask.any():
        df.loc[valid_mask, "_validity_days"] = (
            pd.to_datetime(df.loc[valid_mask, "not_after"])
            - pd.to_datetime(df.loc[valid_mask, "not_before"])
        ).dt.total_seconds() / 86400.0

    for cert_type in CERT_TYPE_MAP:
        mask = df["certificate_type"] == cert_type
        if mask.any():
            vals = df.loc[mask, "_validity_days"].dropna()
            type_avg_validity[cert_type] = vals.mean() if len(vals) > 0 else 0.0
            type_std_validity[cert_type] = vals.std() if len(vals) > 1 else 1.0

    for country in df["country_code"].unique():
        mask = df["country_code"] == country
        if mask.any():
            country_avg_validity[country] = df.loc[mask, "_validity_days"].mean()

    max_key_size = df["public_key_size"].max() if df["public_key_size"].max() > 0 else 4096

    # Pre-compute issuer-level statistics
    issuer_stats = {}
    if "issuer_dn" in df.columns:
        for issuer_dn, group in df.groupby("issuer_dn"):
            if safe_isna(issuer_dn) or not str(issuer_dn).strip():
                continue
            g_ks = group["public_key_size"].dropna()
            g_algs = group["signature_algorithm"].value_counts()
            # Anomaly proxy: proportion non-compliant or expired
            icao_ok = group["icao_compliant"].apply(
                lambda x: str(x).lower() in ("true", "1")
            ).mean()
            expired = group["validation_status"].apply(
                lambda x: str(x).upper() in ("EXPIRED", "EXPIRED_VALID")
            ).mean()
            issuer_stats[issuer_dn] = {
                "count": len(group),
                "type_diversity": group["certificate_type"].nunique(),
                "avg_key_size": float(g_ks.mean()) if len(g_ks) > 0 else 0,
                "std_key_size": float(g_ks.std()) if len(g_ks) > 1 else 0,
                "dominant_algo": g_algs.index[0] if len(g_algs) > 0 else "",
                "anomaly_rate": round(1.0 - icao_ok + expired * 0.5, 4),
            }

    # Pre-compute country issuance rates (certs per year per country)
    country_issuance_rates = defaultdict(list)
    if "not_before" in df.columns:
        df["_issue_year"] = pd.to_datetime(df["not_before"], errors="coerce").dt.year
        for country, group in df.groupby("country_code"):
            year_counts = group["_issue_year"].dropna().value_counts().to_dict()
            if year_counts:
                avg_rate = sum(year_counts.values()) / max(len(year_counts), 1)
                country_issuance_rates[country] = {
                    "avg_rate": avg_rate,
                    "year_counts": year_counts,
                }

    # Pre-compute type-level extension pattern (for extension match scoring)
    type_ext_pattern = {}
    for cert_type in CERT_TYPE_MAP:
        mask = df["certificate_type"] == cert_type
        if mask.any():
            type_group = df.loc[mask]
            ext_counts = {}
            for f in _EXT_FIELDS:
                vals = type_group[f].apply(
                    lambda x: 1 if x and not (isinstance(x, float) and safe_isna(x)) else 0
                )
                ext_counts[f] = vals.mean()
            type_ext_pattern[cert_type] = ext_counts

    # Pre-compute country compliance proxy for country_peer_risk_avg
    country_risk_proxy = {}
    for country, group in df.groupby("country_code"):
        icao_ok = group["icao_compliant"].apply(
            lambda x: str(x).lower() in ("true", "1")
        ).mean()
        expired = group["validation_status"].apply(
            lambda x: str(x).upper() in ("EXPIRED", "EXPIRED_VALID")
        ).mean()
        country_risk_proxy[country] = round((1.0 - icao_ok) * 0.6 + expired * 0.4, 4)

    for i, (_, row) in enumerate(df.iterrows()):
        key_size = row.get("public_key_size") or 0
        sig_alg = row.get("signature_algorithm") or ""
        pub_alg = row.get("public_key_algorithm") or ""
        cert_type = row.get("certificate_type") or ""
        country = row.get("country_code") or ""
        issuer_dn = str(row.get("issuer_dn") or "")
        subject_dn = str(row.get("subject_dn") or "")

        # Validity period
        validity_days = row.get("_validity_days")
        if safe_isna(validity_days):
            validity_days = 0.0

        # Days until expiry
        not_after = row.get("not_after")
        if not_after and not safe_isna(not_after):
            not_after_dt = pd.to_datetime(not_after)
            if not_after_dt.tzinfo is None:
                not_after_dt = not_after_dt.replace(tzinfo=timezone.utc)
            days_until = (not_after_dt - now).total_seconds() / 86400.0
        else:
            days_until = 0.0

        is_expired = 1.0 if days_until < 0 else 0.0

        # ===== Original 25 features (indices 0-24) =====
        features[i, 0] = key_size / max_key_size if max_key_size > 0 else 0.0
        features[i, 1] = ALGORITHM_SCORES.get(sig_alg, 0.5)
        features[i, 2] = 1.0 if "ecdsa" in pub_alg.lower() or "ec" == pub_alg.lower() else 0.0
        features[i, 3] = 1.0 if "pss" in sig_alg.lower() else 0.0
        features[i, 4] = validity_days / 365.25
        features[i, 5] = (
            validity_days / type_avg_validity.get(cert_type, validity_days or 1.0)
            if validity_days > 0
            else 0.0
        )
        features[i, 6] = max(days_until / 365.25, -5.0)
        features[i, 7] = is_expired
        features[i, 8] = _safe_bool(row.get("icao_compliant"))
        features[i, 9] = _safe_bool(row.get("trust_chain_valid"))
        features[i, 10] = _count_violations(row.get("icao_violations"))
        features[i, 11] = _safe_bool(row.get("icao_key_usage_compliant"))
        features[i, 12] = _safe_bool(row.get("icao_algorithm_compliant"))
        features[i, 13] = _count_extensions(row)
        features[i, 14] = 1.0 if row.get("crl_distribution_points") else 0.0
        features[i, 15] = 1.0 if row.get("ocsp_responder_url") else 0.0
        features[i, 16] = 1.0 if row.get("authority_key_identifier") else 0.0
        features[i, 17] = _safe_bool(row.get("is_ca"))
        features[i, 18] = _safe_bool(row.get("is_self_signed"))
        features[i, 19] = float(row.get("version") or 0)
        features[i, 20] = float(row.get("path_len_constraint") or -1)
        country_avg_ks = country_avg_key_size.get(country, key_size or 1.0)
        features[i, 21] = (
            (key_size - country_avg_ks) / country_avg_ks if country_avg_ks > 0 else 0.0
        )
        country_avg_v = country_avg_validity.get(country, validity_days or 1.0)
        features[i, 22] = (
            (validity_days - country_avg_v) / country_avg_v
            if country_avg_v > 0
            else 0.0
        )
        features[i, 23] = float(country_cert_counts.get(country, 0))
        features[i, 24] = float(CERT_TYPE_MAP.get(cert_type, -1))

        # ===== Issuer profile (4) — indices 25-28 =====
        issuer_info = issuer_stats.get(issuer_dn, {})
        features[i, 25] = float(issuer_info.get("count", 0))  # issuer_cert_count
        features[i, 26] = float(issuer_info.get("anomaly_rate", 0.5))  # issuer_anomaly_rate
        features[i, 27] = float(issuer_info.get("type_diversity", 0))  # issuer_type_diversity
        # issuer_country_match: do issuer C= and subject C= match?
        issuer_country = extract_country_from_dn(issuer_dn)
        subject_country = extract_country_from_dn(subject_dn)
        features[i, 28] = 1.0 if (issuer_country and issuer_country == subject_country) else 0.0

        # ===== Temporal pattern (4) — indices 29-32 =====
        not_before = row.get("not_before")
        if not_before and not safe_isna(not_before):
            nb_dt = pd.to_datetime(not_before)
            features[i, 29] = float(nb_dt.month) / 12.0  # issuance_month (normalized)
        else:
            features[i, 29] = 0.0

        # validity_period_zscore
        t_avg = type_avg_validity.get(cert_type, 0)
        t_std = type_std_validity.get(cert_type, 1.0)
        if t_std > 0 and validity_days > 0:
            features[i, 30] = (validity_days - t_avg) / t_std
        else:
            features[i, 30] = 0.0

        # issuance_rate_deviation
        issue_year = row.get("_issue_year")
        if country in country_issuance_rates and issue_year and not safe_isna(issue_year):
            cr = country_issuance_rates[country]
            year_count = cr.get("year_counts", {}).get(int(issue_year), 0)
            avg_rate = cr.get("avg_rate", 1.0) or 1.0
            features[i, 31] = (year_count - avg_rate) / avg_rate
        else:
            features[i, 31] = 0.0

        # cert_age_ratio: elapsed / total validity
        if not_before and not safe_isna(not_before) and validity_days > 0:
            nb_dt = pd.to_datetime(not_before)
            if nb_dt.tzinfo is None:
                nb_dt = nb_dt.replace(tzinfo=timezone.utc)
            elapsed = (now - nb_dt).total_seconds() / 86400.0
            features[i, 32] = min(max(elapsed / validity_days, 0.0), 2.0)
        else:
            features[i, 32] = 0.0

        # ===== DN structure (4) — indices 33-36 =====
        features[i, 33] = float(count_dn_fields(subject_dn))
        features[i, 34] = float(count_dn_fields(issuer_dn))
        features[i, 35] = float(detect_dn_format(subject_dn))
        features[i, 36] = 1.0 if has_email_in_dn(subject_dn) else 0.0

        # ===== Extension profile (4) — indices 37-40 =====
        features[i, 37] = _extension_set_hash(row)
        features[i, 38] = float(count_unexpected_extensions(row))
        features[i, 39] = float(count_missing_required(row))
        features[i, 40] = float(_count_critical_extensions(row))

        # ===== Cross-certificate (4) — indices 41-44 =====
        # same_issuer_key_size_dev
        issuer_avg_ks = issuer_info.get("avg_key_size", 0)
        issuer_std_ks = issuer_info.get("std_key_size", 0)
        if issuer_avg_ks > 0 and key_size > 0:
            features[i, 41] = (key_size - issuer_avg_ks) / (issuer_avg_ks or 1.0)
        else:
            features[i, 41] = 0.0

        # same_issuer_algo_match
        dominant_algo = issuer_info.get("dominant_algo", "")
        features[i, 42] = 1.0 if (sig_alg and sig_alg == dominant_algo) else 0.0

        # country_peer_risk_avg
        features[i, 43] = float(country_risk_proxy.get(country, 0.5))

        # type_peer_extension_match: how similar cert's extensions to type norm
        ext_pattern = type_ext_pattern.get(cert_type, {})
        if ext_pattern:
            match_score = 0.0
            for f in _EXT_FIELDS:
                val = row.get(f)
                has = 1.0 if val and not (isinstance(val, float) and safe_isna(val)) else 0.0
                expected = ext_pattern.get(f, 0.5)
                match_score += 1.0 - abs(has - round(expected))
            features[i, 44] = match_score / len(_EXT_FIELDS)
        else:
            features[i, 44] = 0.5

    # Replace NaN/inf with 0
    features = np.nan_to_num(features, nan=0.0, posinf=0.0, neginf=0.0)

    metadata = df[["fingerprint_sha256", "certificate_type", "country_code"]].copy()
    metadata = metadata.reset_index(drop=True)

    logger.info("Engineered %d features for %d certificates", len(FEATURE_NAMES), n)
    return metadata, features

"""Feature engineering pipeline: Certificate DB rows → ML feature vectors."""

import hashlib
import logging
import threading
import time
from collections import defaultdict
from datetime import datetime, timezone

import numpy as np
import pandas as pd
from sqlalchemy import text

from app.config import get_settings
from app.database import safe_isna

logger = logging.getLogger(__name__)

# TTL cache for load_certificate_data() — avoids reloading 31K rows on every report request
_data_cache: dict = {"df": None, "timestamp": 0.0, "ttl": 300, "loading": False}  # 5 minutes
_data_cache_lock = threading.Lock()

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


def invalidate_certificate_data_cache():
    """Invalidate cached data — call after analysis run or data change."""
    with _data_cache_lock:
        _data_cache["df"] = None
        _data_cache["timestamp"] = 0.0
    logger.debug("Certificate data cache invalidated")


def load_certificate_data(engine, upload_id: str = None) -> pd.DataFrame:
    """Load certificate + validation_result data from DB via SQL JOIN.

    Uses a TTL cache (5 min) for full-dataset loads to avoid repeated 31K row queries.
    Incremental loads (upload_id) always bypass cache.

    Args:
        engine: SQLAlchemy engine.
        upload_id: Optional upload ID to filter certificates by upload.
                   When provided, only certificates associated with the given
                   upload (via validation_result.upload_id) are loaded.
    """
    # Cache hit for full-dataset loads (prevents concurrent duplicate loads)
    if upload_id is None:
        with _data_cache_lock:
            if _data_cache["df"] is not None and (time.time() - _data_cache["timestamp"]) < _data_cache["ttl"]:
                logger.debug("Using cached certificate data (%d rows, age=%.0fs)",
                             len(_data_cache["df"]), time.time() - _data_cache["timestamp"])
                return _data_cache["df"].copy()
            # Mark as loading to prevent concurrent threads from also starting a load
            if _data_cache.get("loading"):
                logger.debug("Another thread is loading certificate data, waiting...")
            _data_cache["loading"] = True

    settings = get_settings()
    params = {}

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
        if upload_id:
            query += " AND c.fingerprint_sha256 IN (SELECT certificate_id FROM validation_result WHERE upload_id = :upload_id)"
            params["upload_id"] = upload_id
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
        if upload_id:
            query += " AND c.id IN (SELECT certificate_id FROM validation_result WHERE upload_id = :upload_id)"
            params["upload_id"] = upload_id

    logger.info("Loading certificate data from DB%s...", f" (upload_id={upload_id})" if upload_id else "")
    df = pd.read_sql(text(query), engine, params=params)
    # Deduplicate: PostgreSQL LEFT JOIN may produce multiple rows per certificate
    # when multiple validation_results exist (UNIQUE on certificate_id + upload_id)
    orig_len = len(df)
    df = df.drop_duplicates(subset=["fingerprint_sha256"], keep="first")
    if len(df) < orig_len:
        logger.info("Deduplicated %d → %d certificates", orig_len, len(df))
    # Reset index after dedup to ensure position-based alignment with numpy arrays
    df = df.reset_index(drop=True)
    logger.info("Loaded %d certificates", len(df))

    # Cache full-dataset loads
    if upload_id is None:
        with _data_cache_lock:
            _data_cache["df"] = df.copy()
            _data_cache["timestamp"] = time.time()
            _data_cache["loading"] = False
        logger.debug("Certificate data cached (%d rows)", len(df))

    return df


_REQUIRED_COLUMNS = [
    "fingerprint_sha256", "certificate_type", "country_code",
    "public_key_size", "signature_algorithm", "not_before", "not_after",
]


def engineer_features(df: pd.DataFrame) -> tuple[pd.DataFrame, np.ndarray]:
    """Transform raw certificate DataFrame into ML feature vectors.

    Returns:
        (metadata_df, feature_matrix) where metadata_df has fingerprint/type/country
        and feature_matrix is the numpy array of features (n x 45).
    """
    from app.services.extension_rules_engine import (
        EXPECTED_EXTENSIONS,
    )
    from app.services.issuer_profiler import (
        count_dn_fields,
        detect_dn_format,
        extract_country_from_dn,
        has_email_in_dn,
    )

    # Validate required columns
    missing = [c for c in _REQUIRED_COLUMNS if c not in df.columns]
    if missing:
        raise ValueError(f"Missing required columns: {missing}")

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
    df["_validity_days"] = np.nan
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

    # =========================================================================
    # Vectorized feature extraction (replaces per-row iterrows loop)
    # =========================================================================

    # --- Vectorized helper: _safe_bool for a column ---
    def _vec_safe_bool(series: pd.Series) -> np.ndarray:
        """Vectorized _safe_bool: convert column to float 0.0/1.0.

        Handles bool, int/float, and string columns without Python for-loops.
        """
        s = series.fillna("")
        # Convert to string uniformly for vectorized comparison
        str_vals = s.astype(str).str.lower()
        return np.where(
            str_vals.isin(["true", "1", "1.0", "t", "yes"]),
            1.0,
            0.0,
        ).astype(np.float64)

    # --- Vectorized helper: check if column values are non-empty (truthy) ---
    def _vec_has_value(series: pd.Series) -> np.ndarray:
        """Return 1.0 where column has a truthy non-NaN value, else 0.0.

        Uses pandas vectorized string ops instead of Python for-loops.
        """
        not_null = series.notna()
        # For string columns: also check non-empty after strip
        str_vals = series.astype(str).str.strip()
        non_empty = str_vals.ne("") & str_vals.ne("None") & str_vals.ne("nan")
        return np.where(not_null & non_empty, 1.0, 0.0).astype(np.float64)

    # --- Pre-extract columns as arrays for fast access ---
    key_size_arr = pd.to_numeric(df["public_key_size"], errors="coerce").fillna(0).values.astype(np.float64)
    sig_alg_arr = df["signature_algorithm"].fillna("").astype(str).values
    pub_alg_arr = df["public_key_algorithm"].fillna("").astype(str).values
    cert_type_arr = df["certificate_type"].fillna("").astype(str).values
    country_arr = df["country_code"].fillna("").astype(str).values
    issuer_dn_arr = df["issuer_dn"].fillna("").astype(str).values
    subject_dn_arr = df["subject_dn"].fillna("").astype(str).values
    validity_days_arr = pd.to_numeric(df["_validity_days"], errors="coerce").fillna(0.0).values.astype(np.float64)

    # --- Days until expiry (vectorized) ---
    not_after_dt = pd.to_datetime(df["not_after"], errors="coerce", utc=True)
    days_until_arr = np.where(
        not_after_dt.notna(),
        (not_after_dt - now).dt.total_seconds().values / 86400.0,
        0.0,
    )
    is_expired_arr = np.where(days_until_arr < 0, 1.0, 0.0)

    # ===== Original 25 features (indices 0-24) =====

    # [0] key_size_normalized
    features[:, 0] = key_size_arr / max_key_size if max_key_size > 0 else 0.0

    # [1] algorithm_age_score
    features[:, 1] = np.array([ALGORITHM_SCORES.get(a, 0.5) for a in sig_alg_arr], dtype=np.float64)

    # [2] is_ecdsa
    pub_alg_lower = np.array([a.lower() for a in pub_alg_arr])
    features[:, 2] = np.where(
        np.char.find(pub_alg_lower, "ecdsa") >= 0,
        1.0,
        np.where(pub_alg_lower == "ec", 1.0, 0.0),
    )

    # [3] is_rsa_pss
    sig_alg_lower = np.array([a.lower() for a in sig_alg_arr])
    features[:, 3] = np.where(np.char.find(sig_alg_lower, "pss") >= 0, 1.0, 0.0)

    # [4] validity_days (normalized to years)
    features[:, 4] = validity_days_arr / 365.25

    # [5] validity_ratio (vs type average)
    type_avg_mapped = np.array(
        [type_avg_validity.get(ct, validity_days_arr[i] or 1.0) for i, ct in enumerate(cert_type_arr)],
        dtype=np.float64,
    )
    # Avoid division by zero: where validity_days > 0 AND type_avg > 0
    safe_type_avg = np.where(type_avg_mapped > 0, type_avg_mapped, 1.0)
    features[:, 5] = np.where(validity_days_arr > 0, validity_days_arr / safe_type_avg, 0.0)

    # [6] days_until_expiry (clipped, normalized to years)
    features[:, 6] = np.maximum(days_until_arr / 365.25, -5.0)

    # [7] is_expired
    features[:, 7] = is_expired_arr

    # [8] icao_compliant
    features[:, 8] = _vec_safe_bool(df["icao_compliant"]) if "icao_compliant" in df.columns else 0.0

    # [9] trust_chain_valid
    features[:, 9] = _vec_safe_bool(df["trust_chain_valid"]) if "trust_chain_valid" in df.columns else 0.0

    # [10] icao_violation_count
    icao_violations_arr = df["icao_violations"].fillna("").astype(str).values if "icao_violations" in df.columns else np.full(n, "")
    features[:, 10] = np.array(
        [len(v.split("|")) if v and v.strip() else 0 for v in icao_violations_arr],
        dtype=np.float64,
    )

    # [11] key_usage_compliant
    features[:, 11] = _vec_safe_bool(df["icao_key_usage_compliant"]) if "icao_key_usage_compliant" in df.columns else 0.0

    # [12] algorithm_compliant
    features[:, 12] = _vec_safe_bool(df["icao_algorithm_compliant"]) if "icao_algorithm_compliant" in df.columns else 0.0

    # [13] extension_count - count of non-empty extension fields
    ext_has_arrays = [_vec_has_value(df[f]) for f in _EXT_FIELDS]
    features[:, 13] = np.sum(ext_has_arrays, axis=0)

    # [14] has_crl_dp
    features[:, 14] = _vec_has_value(df["crl_distribution_points"])

    # [15] has_ocsp
    features[:, 15] = _vec_has_value(df["ocsp_responder_url"])

    # [16] has_aki
    features[:, 16] = _vec_has_value(df["authority_key_identifier"])

    # [17] is_ca
    features[:, 17] = _vec_safe_bool(df["is_ca"])

    # [18] is_self_signed
    features[:, 18] = _vec_safe_bool(df["is_self_signed"])

    # [19] version
    features[:, 19] = pd.to_numeric(df["version"], errors="coerce").fillna(0).values.astype(np.float64)

    # [20] path_len
    features[:, 20] = pd.to_numeric(df["path_len_constraint"], errors="coerce").fillna(-1).values.astype(np.float64)

    # [21] key_size_vs_country_avg
    country_avg_ks_arr = np.array(
        [country_avg_key_size.get(c, key_size_arr[i] or 1.0) for i, c in enumerate(country_arr)],
        dtype=np.float64,
    )
    safe_country_avg_ks = np.where(country_avg_ks_arr > 0, country_avg_ks_arr, 1.0)
    features[:, 21] = (key_size_arr - country_avg_ks_arr) / safe_country_avg_ks

    # [22] validity_vs_country_avg
    country_avg_v_arr = np.array(
        [country_avg_validity.get(c, validity_days_arr[i] or 1.0) for i, c in enumerate(country_arr)],
        dtype=np.float64,
    )
    safe_country_avg_v = np.where(country_avg_v_arr > 0, country_avg_v_arr, 1.0)
    features[:, 22] = np.where(
        country_avg_v_arr > 0,
        (validity_days_arr - country_avg_v_arr) / safe_country_avg_v,
        0.0,
    )

    # [23] country_cert_count
    features[:, 23] = np.array(
        [float(country_cert_counts.get(c, 0)) for c in country_arr], dtype=np.float64,
    )

    # [24] cert_type_encoded
    features[:, 24] = np.array(
        [float(CERT_TYPE_MAP.get(ct, -1)) for ct in cert_type_arr], dtype=np.float64,
    )

    # ===== Issuer profile (4) — indices 25-28 =====

    # Pre-map issuer stats for all rows
    issuer_count_arr = np.array(
        [float(issuer_stats.get(idn, {}).get("count", 0)) for idn in issuer_dn_arr],
        dtype=np.float64,
    )
    issuer_anomaly_rate_arr = np.array(
        [float(issuer_stats.get(idn, {}).get("anomaly_rate", 0.5)) for idn in issuer_dn_arr],
        dtype=np.float64,
    )
    issuer_type_diversity_arr = np.array(
        [float(issuer_stats.get(idn, {}).get("type_diversity", 0)) for idn in issuer_dn_arr],
        dtype=np.float64,
    )

    # [25] issuer_cert_count
    features[:, 25] = issuer_count_arr

    # [26] issuer_anomaly_rate
    features[:, 26] = issuer_anomaly_rate_arr

    # [27] issuer_type_diversity
    features[:, 27] = issuer_type_diversity_arr

    # [28] issuer_country_match
    issuer_countries = np.array([extract_country_from_dn(idn) for idn in issuer_dn_arr])
    subject_countries = np.array([extract_country_from_dn(sdn) for sdn in subject_dn_arr])
    features[:, 28] = np.where(
        (issuer_countries != "") & (issuer_countries == subject_countries),
        1.0, 0.0,
    )

    # ===== Temporal pattern (4) — indices 29-32 =====

    # [29] issuance_month (normalized)
    not_before_dt = pd.to_datetime(df["not_before"], errors="coerce")
    issuance_month = not_before_dt.dt.month
    features[:, 29] = np.where(issuance_month.notna(), issuance_month.fillna(0).values / 12.0, 0.0)

    # [30] validity_period_zscore
    type_avg_for_zscore = np.array(
        [type_avg_validity.get(ct, 0) for ct in cert_type_arr], dtype=np.float64,
    )
    type_std_for_zscore = np.array(
        [type_std_validity.get(ct, 1.0) for ct in cert_type_arr], dtype=np.float64,
    )
    features[:, 30] = np.where(
        (type_std_for_zscore > 0) & (validity_days_arr > 0),
        (validity_days_arr - type_avg_for_zscore) / np.where(type_std_for_zscore > 0, type_std_for_zscore, 1.0),
        0.0,
    )

    # [31] issuance_rate_deviation
    issue_year_arr = df["_issue_year"].values if "_issue_year" in df.columns else np.full(n, np.nan)
    issuance_rate_dev = np.zeros(n, dtype=np.float64)
    for i in range(n):
        c = country_arr[i]
        iy = issue_year_arr[i]
        if c in country_issuance_rates and iy is not None and not (isinstance(iy, float) and np.isnan(iy)):
            cr = country_issuance_rates[c]
            year_count = cr.get("year_counts", {}).get(int(iy), 0)
            avg_rate = cr.get("avg_rate", 1.0) or 1.0
            issuance_rate_dev[i] = (year_count - avg_rate) / avg_rate
    features[:, 31] = issuance_rate_dev

    # [32] cert_age_ratio: elapsed / total validity
    not_before_utc = pd.to_datetime(df["not_before"], errors="coerce", utc=True)
    elapsed_days = np.where(
        not_before_utc.notna(),
        (now - not_before_utc).dt.total_seconds().values / 86400.0,
        0.0,
    )
    features[:, 32] = np.where(
        (not_before_utc.notna().values) & (validity_days_arr > 0),
        np.clip(elapsed_days / np.where(validity_days_arr > 0, validity_days_arr, 1.0), 0.0, 2.0),
        0.0,
    )

    # ===== DN structure (4) — indices 33-36 =====

    # [33] subject_dn_field_count
    features[:, 33] = np.array(
        [float(count_dn_fields(sdn)) for sdn in subject_dn_arr], dtype=np.float64,
    )

    # [34] issuer_dn_field_count
    features[:, 34] = np.array(
        [float(count_dn_fields(idn)) for idn in issuer_dn_arr], dtype=np.float64,
    )

    # [35] dn_format_type
    features[:, 35] = np.array(
        [float(detect_dn_format(sdn)) for sdn in subject_dn_arr], dtype=np.float64,
    )

    # [36] subject_has_email
    features[:, 36] = np.array(
        [1.0 if has_email_in_dn(sdn) else 0.0 for sdn in subject_dn_arr], dtype=np.float64,
    )

    # ===== Extension profile (4) — indices 37-40 =====

    # [37] extension_set_hash (vectorized over rows)
    # Build a 2D boolean matrix of which extensions are present, then hash per row
    ext_presence = np.column_stack(ext_has_arrays)  # shape (n, len(_EXT_FIELDS))
    ext_hash_arr = np.zeros(n, dtype=np.float64)
    for i in range(n):
        parts = "".join("1" if ext_presence[i, j] > 0.5 else "0" for j in range(len(_EXT_FIELDS)))
        h = int(hashlib.md5(parts.encode()).hexdigest()[:8], 16)
        ext_hash_arr[i] = (h % 1000) / 1000.0
    features[:, 37] = ext_hash_arr

    # [38] unexpected_extensions (vectorized)
    # For each cert type, determine which extension fields are expected
    _expected_fields_by_type = {}
    for ct_name, ct_profile in EXPECTED_EXTENSIONS.items():
        _expected_fields_by_type[ct_name] = set(
            ct_profile.get("required", []) + ct_profile.get("recommended", [])
        )
    unexpected_arr = np.zeros(n, dtype=np.float64)
    for j, f in enumerate(_EXT_FIELDS):
        has_col = ext_has_arrays[j]  # 1.0 where field is present
        for ct_name, expected_set in _expected_fields_by_type.items():
            if f not in expected_set:
                # This field is unexpected for this cert type — count where present
                ct_mask = cert_type_arr == ct_name
                unexpected_arr += has_col * ct_mask
        # Cert types not in EXPECTED_EXTENSIONS: no expected set, skip counting
    features[:, 38] = unexpected_arr

    # [39] missing_required_extensions (vectorized)
    missing_req_arr = np.zeros(n, dtype=np.float64)
    is_ca_bool = _vec_safe_bool(df["is_ca"])
    for ct_name, ct_profile in EXPECTED_EXTENSIONS.items():
        ct_mask = cert_type_arr == ct_name
        if not np.any(ct_mask):
            continue
        for req_field in ct_profile.get("required", []):
            if req_field == "is_ca":
                # Missing if is_ca is False (0.0)
                missing_req_arr += ct_mask * (1.0 - is_ca_bool)
            else:
                # Missing if field is not present
                f_idx = _EXT_FIELDS.index(req_field) if req_field in _EXT_FIELDS else -1
                if f_idx >= 0:
                    missing_req_arr += ct_mask * (1.0 - ext_has_arrays[f_idx])
    features[:, 39] = missing_req_arr

    # [40] critical_extension_count (vectorized)
    key_usage_str_arr = df["key_usage"].fillna("").astype(str).values
    eku_str_arr = df["extended_key_usage"].fillna("").astype(str).values
    crit_count = np.zeros(n, dtype=np.float64)
    crit_count += np.array([1.0 if "critical" in ku.lower() else 0.0 for ku in key_usage_str_arr])
    crit_count += np.array([1.0 if "critical" in eku.lower() else 0.0 for eku in eku_str_arr])
    features[:, 40] = crit_count

    # ===== Cross-certificate (4) — indices 41-44 =====

    # Pre-map issuer avg/std key size for all rows
    issuer_avg_ks_arr = np.array(
        [float(issuer_stats.get(idn, {}).get("avg_key_size", 0)) for idn in issuer_dn_arr],
        dtype=np.float64,
    )
    issuer_dominant_algo_arr = np.array(
        [issuer_stats.get(idn, {}).get("dominant_algo", "") for idn in issuer_dn_arr],
    )

    # [41] same_issuer_key_size_dev
    safe_issuer_avg_ks = np.where(issuer_avg_ks_arr > 0, issuer_avg_ks_arr, 1.0)
    features[:, 41] = np.where(
        (issuer_avg_ks_arr > 0) & (key_size_arr > 0),
        (key_size_arr - issuer_avg_ks_arr) / safe_issuer_avg_ks,
        0.0,
    )

    # [42] same_issuer_algo_match
    features[:, 42] = np.where(
        (sig_alg_arr != "") & (sig_alg_arr == issuer_dominant_algo_arr),
        1.0, 0.0,
    )

    # [43] country_peer_risk_avg
    features[:, 43] = np.array(
        [float(country_risk_proxy.get(c, 0.5)) for c in country_arr], dtype=np.float64,
    )

    # [44] type_peer_extension_match
    ext_match_arr = np.full(n, 0.5, dtype=np.float64)
    for ct_name, ext_pattern in type_ext_pattern.items():
        ct_mask = cert_type_arr == ct_name
        if not np.any(ct_mask):
            continue
        match_scores = np.zeros(n, dtype=np.float64)
        for j, f in enumerate(_EXT_FIELDS):
            has_col = ext_has_arrays[j]
            expected_val = ext_pattern.get(f, 0.5)
            match_scores += 1.0 - np.abs(has_col - round(expected_val))
        match_scores /= len(_EXT_FIELDS)
        ext_match_arr = np.where(ct_mask, match_scores, ext_match_arr)
    features[:, 44] = ext_match_arr

    # Replace NaN/inf with 0
    features = np.nan_to_num(features, nan=0.0, posinf=0.0, neginf=0.0)

    metadata = df[["fingerprint_sha256", "certificate_type", "country_code"]].copy()
    metadata = metadata.reset_index(drop=True)

    logger.info("Engineered %d features for %d certificates", len(FEATURE_NAMES), n)
    return metadata, features

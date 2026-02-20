"""Feature engineering pipeline: Certificate DB rows → ML feature vectors."""

import logging
from datetime import datetime, timezone

import numpy as np
import pandas as pd
from sqlalchemy import text

from app.config import get_settings

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

# Feature names for explainability
FEATURE_NAMES = [
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
}


def load_certificate_data(engine) -> pd.DataFrame:
    """Load certificate + validation_result data from DB via SQL JOIN."""
    settings = get_settings()

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
                   v.trust_chain_valid, v.icao_compliant, v.icao_violations,
                   v.icao_key_usage_compliant, v.icao_algorithm_compliant,
                   v.icao_key_size_compliant, v.icao_extensions_compliant,
                   v.signature_valid
            FROM certificate c
            LEFT JOIN validation_result v ON c.fingerprint_sha256 = v.certificate_id
            WHERE c.certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC')
        """
    else:
        query = """
            SELECT c.fingerprint_sha256, c.certificate_type, c.country_code,
                   c.version, c.signature_algorithm, c.public_key_algorithm,
                   c.public_key_size, c.public_key_curve,
                   c.key_usage, c.extended_key_usage,
                   c.is_ca, c.path_len_constraint, c.is_self_signed,
                   c.subject_key_identifier, c.authority_key_identifier,
                   c.crl_distribution_points, c.ocsp_responder_url,
                   c.not_before, c.not_after, c.validation_status,
                   v.trust_chain_valid, v.icao_compliant, v.icao_violations,
                   v.icao_key_usage_compliant, v.icao_algorithm_compliant,
                   v.icao_key_size_compliant, v.icao_extensions_compliant,
                   v.signature_valid
            FROM certificate c
            LEFT JOIN validation_result v ON c.fingerprint_sha256 = v.certificate_id
            WHERE c.certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC')
        """

    logger.info("Loading certificate data from DB...")
    df = pd.read_sql(text(query), engine)
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
    if row.get("key_usage"):
        count += 1
    if row.get("extended_key_usage"):
        count += 1
    if row.get("authority_key_identifier"):
        count += 1
    if row.get("subject_key_identifier"):
        count += 1
    if row.get("crl_distribution_points"):
        count += 1
    if row.get("ocsp_responder_url"):
        count += 1
    return count


def _count_violations(violations_str) -> int:
    """Count ICAO violations from pipe-separated string."""
    if not violations_str or pd.isna(violations_str):
        return 0
    return len(str(violations_str).split("|"))


def engineer_features(df: pd.DataFrame) -> tuple[pd.DataFrame, np.ndarray]:
    """Transform raw certificate DataFrame into ML feature vectors.

    Returns:
        (metadata_df, feature_matrix) where metadata_df has fingerprint/type/country
        and feature_matrix is the numpy array of features.
    """
    now = datetime.now(timezone.utc)
    n = len(df)
    features = np.zeros((n, len(FEATURE_NAMES)), dtype=np.float64)

    # Pre-compute country-level averages
    country_avg_key_size = df.groupby("country_code")["public_key_size"].mean().to_dict()
    type_avg_validity = {}
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
            type_avg_validity[cert_type] = df.loc[mask, "_validity_days"].mean()

    for country in df["country_code"].unique():
        mask = df["country_code"] == country
        if mask.any():
            country_avg_validity[country] = df.loc[mask, "_validity_days"].mean()

    max_key_size = df["public_key_size"].max() if df["public_key_size"].max() > 0 else 4096

    for i, (_, row) in enumerate(df.iterrows()):
        key_size = row.get("public_key_size") or 0
        sig_alg = row.get("signature_algorithm") or ""
        pub_alg = row.get("public_key_algorithm") or ""
        cert_type = row.get("certificate_type") or ""
        country = row.get("country_code") or ""

        # Validity period
        validity_days = row.get("_validity_days")
        if pd.isna(validity_days):
            validity_days = 0.0

        # Days until expiry
        not_after = row.get("not_after")
        if not_after and not pd.isna(not_after):
            not_after_dt = pd.to_datetime(not_after)
            if not_after_dt.tzinfo is None:
                not_after_dt = not_after_dt.replace(tzinfo=timezone.utc)
            days_until = (not_after_dt - now).total_seconds() / 86400.0
        else:
            days_until = 0.0

        is_expired = 1.0 if days_until < 0 else 0.0

        # Feature extraction
        features[i, 0] = key_size / max_key_size if max_key_size > 0 else 0.0  # key_size_normalized
        features[i, 1] = ALGORITHM_SCORES.get(sig_alg, 0.5)  # algorithm_age_score
        features[i, 2] = 1.0 if "ecdsa" in pub_alg.lower() or "ec" == pub_alg.lower() else 0.0  # is_ecdsa
        features[i, 3] = 1.0 if "pss" in sig_alg.lower() else 0.0  # is_rsa_pss
        features[i, 4] = validity_days / 365.25  # validity_days (in years, for normalization)
        features[i, 5] = (
            validity_days / type_avg_validity.get(cert_type, validity_days or 1.0)
            if validity_days > 0
            else 0.0
        )  # validity_ratio
        features[i, 6] = max(days_until / 365.25, -5.0)  # days_until_expiry (years, capped)
        features[i, 7] = is_expired  # is_expired
        features[i, 8] = _safe_bool(row.get("icao_compliant"))  # icao_compliant
        features[i, 9] = _safe_bool(row.get("trust_chain_valid"))  # trust_chain_valid
        features[i, 10] = _count_violations(row.get("icao_violations"))  # icao_violation_count
        features[i, 11] = _safe_bool(row.get("icao_key_usage_compliant"))  # key_usage_compliant
        features[i, 12] = _safe_bool(row.get("icao_algorithm_compliant"))  # algorithm_compliant
        features[i, 13] = _count_extensions(row)  # extension_count
        features[i, 14] = 1.0 if row.get("crl_distribution_points") else 0.0  # has_crl_dp
        features[i, 15] = 1.0 if row.get("ocsp_responder_url") else 0.0  # has_ocsp
        features[i, 16] = 1.0 if row.get("authority_key_identifier") else 0.0  # has_aki
        features[i, 17] = _safe_bool(row.get("is_ca"))  # is_ca
        features[i, 18] = _safe_bool(row.get("is_self_signed"))  # is_self_signed
        features[i, 19] = float(row.get("version") or 0)  # version
        features[i, 20] = float(row.get("path_len_constraint") or -1)  # path_len
        # Country-relative features
        country_avg_ks = country_avg_key_size.get(country, key_size or 1.0)
        features[i, 21] = (
            (key_size - country_avg_ks) / country_avg_ks if country_avg_ks > 0 else 0.0
        )  # key_size_vs_country_avg
        country_avg_v = country_avg_validity.get(country, validity_days or 1.0)
        features[i, 22] = (
            (validity_days - country_avg_v) / country_avg_v
            if country_avg_v > 0
            else 0.0
        )  # validity_vs_country_avg
        features[i, 23] = float(country_cert_counts.get(country, 0))  # country_cert_count
        features[i, 24] = float(CERT_TYPE_MAP.get(cert_type, -1))  # cert_type_encoded

    # Replace NaN/inf with 0
    features = np.nan_to_num(features, nan=0.0, posinf=0.0, neginf=0.0)

    metadata = df[["fingerprint_sha256", "certificate_type", "country_code"]].copy()
    metadata = metadata.reset_index(drop=True)

    logger.info("Engineered %d features for %d certificates", len(FEATURE_NAMES), n)
    return metadata, features

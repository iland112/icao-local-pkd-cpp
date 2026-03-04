"""Composite risk scoring for certificates.

v2.19.0: 6 → 10 risk categories with forensic findings.
v2.25.6: Vectorized risk scoring — iterrows loop replaced with numpy/pandas ops.
"""

import logging

import numpy as np
import pandas as pd


logger = logging.getLogger(__name__)

# Algorithm risk weights
ALGORITHM_RISK = {
    "sha1WithRSAEncryption": 40,
    "ecdsa-with-SHA1": 40,
    "sha256WithRSAEncryption": 5,
    "ecdsa-with-SHA256": 5,
    "sha384WithRSAEncryption": 0,
    "ecdsa-with-SHA384": 0,
    "sha512WithRSAEncryption": 0,
    "ecdsa-with-SHA512": 0,
    "id-RSASSA-PSS": 2,
}

# Default risk score for algorithms not in ALGORITHM_RISK
_UNKNOWN_ALG_RISK = 15

# Maximum raw score across all 10 categories
_MAX_RAW_SCORE = 200.0

# Threshold for generating detailed forensic findings (MEDIUM and above)
_FINDINGS_THRESHOLD = 20.0


def _vectorized_algorithm_risk(df: pd.DataFrame) -> np.ndarray:
    """Category 1: Algorithm risk (0~40) — vectorized."""
    sig_alg = df["signature_algorithm"].fillna("")
    return sig_alg.map(ALGORITHM_RISK).fillna(_UNKNOWN_ALG_RISK).to_numpy(dtype=np.float64)


def _vectorized_key_size_risk(df: pd.DataFrame) -> np.ndarray:
    """Category 2: Key size risk (0~40) — vectorized."""
    key_size = df["public_key_size"].fillna(0).to_numpy(dtype=np.float64)
    pub_alg = df["public_key_algorithm"].fillna("").str.lower()

    is_rsa = pub_alg.str.contains("rsa", na=False).to_numpy()
    is_ec = pub_alg.str.contains("ec", na=False).to_numpy()

    # RSA key size risk
    rsa_risk = np.select(
        [key_size < 2048, key_size < 3072, key_size < 4096],
        [40.0, 10.0, 3.0],
        default=0.0,
    )

    # EC key size risk
    ec_risk = np.select(
        [key_size < 256, key_size < 384],
        [35.0, 5.0],
        default=0.0,
    )

    # Combine: RSA branch, EC branch, unknown = 15
    return np.where(is_rsa, rsa_risk, np.where(is_ec, ec_risk, 15.0))


def _vectorized_compliance_risk(df: pd.DataFrame) -> np.ndarray:
    """Category 3: ICAO compliance risk (0~20) — vectorized."""
    icao_col = df["icao_compliant"] if "icao_compliant" in df.columns else pd.Series(
        [None] * len(df), dtype=object,
    )
    # Normalize to string for uniform comparison
    icao_str = icao_col.astype(str).str.lower()

    is_false = icao_str.isin(["false", "0"])
    is_none = icao_col.isna()

    return np.select(
        [is_false, is_none],
        [20.0, 5.0],
        default=0.0,
    )


def _vectorized_validity_risk(df: pd.DataFrame) -> np.ndarray:
    """Category 4: Validity risk (0~15) — vectorized."""
    n = len(df)
    not_after_dt = pd.to_datetime(df["not_after"], errors="coerce", utc=True)
    now = pd.Timestamp.now(tz="UTC")
    days_left = (not_after_dt - now).dt.total_seconds() / 86400.0

    has_date = not_after_dt.notna().to_numpy()
    days_arr = days_left.to_numpy(dtype=np.float64)

    risk = np.full(n, 5.0)  # default for missing date
    valid_mask = has_date
    risk[valid_mask] = np.select(
        [days_arr[valid_mask] < 0, days_arr[valid_mask] < 30, days_arr[valid_mask] < 90],
        [15.0, 10.0, 5.0],
        default=0.0,
    )
    return risk


def _vectorized_extension_risk(df: pd.DataFrame) -> np.ndarray:
    """Category 5: Extension risk (0~15) — vectorized."""
    risk = np.zeros(len(df), dtype=np.float64)

    for col, penalty in [
        ("crl_distribution_points", 5),
        ("authority_key_identifier", 5),
        ("subject_key_identifier", 3),
        ("ocsp_responder_url", 2),
    ]:
        if col in df.columns:
            missing = df[col].isna() | (df[col].astype(str).str.strip() == "")
            risk += np.where(missing, penalty, 0.0)
        else:
            risk += penalty  # column absent = field missing for all rows

    return np.minimum(risk, 15.0)


def _vectorized_anomaly_risk(anomaly_scores: np.ndarray) -> np.ndarray:
    """Category 6: Anomaly risk (0~15) — vectorized."""
    return np.round(anomaly_scores.astype(np.float64) * 15.0, 1)


def _vectorized_issuer_risk(issuer_scores: np.ndarray) -> np.ndarray:
    """Category 7: Issuer reputation (0~15) — vectorized."""
    return np.round(issuer_scores.astype(np.float64) * 15.0, 1)


def _vectorized_structural_risk(structural_scores: np.ndarray) -> np.ndarray:
    """Category 8: Structural consistency (0~20) — vectorized."""
    return np.round(structural_scores.astype(np.float64) * 20.0, 1)


def _vectorized_temporal_risk(df: pd.DataFrame) -> np.ndarray:
    """Category 9: Temporal pattern (0~10) — vectorized."""
    n = len(df)
    not_before_dt = pd.to_datetime(df["not_before"], errors="coerce", utc=True)
    not_after_dt = pd.to_datetime(df["not_after"], errors="coerce", utc=True)

    validity_days = (not_after_dt - not_before_dt).dt.total_seconds() / 86400.0
    days_arr = validity_days.to_numpy(dtype=np.float64)
    both_valid = (not_before_dt.notna() & not_after_dt.notna()).to_numpy()

    cert_type = df["certificate_type"].fillna("").to_numpy()

    risk = np.zeros(n, dtype=np.float64)

    # Apply conditions in priority order (matches the original if/elif chain)
    # DSC with validity > 15 years
    cond_dsc_long = both_valid & (cert_type == "DSC") & (days_arr > 365 * 15)
    # CSCA with validity < 1 year
    cond_csca_short = both_valid & (cert_type == "CSCA") & (days_arr < 365) & ~cond_dsc_long
    # Any cert with validity < 30 days
    cond_very_short = both_valid & (days_arr < 30) & ~cond_dsc_long & ~cond_csca_short
    # Any cert with validity > 30 years
    cond_very_long = both_valid & (days_arr > 365 * 30) & ~cond_dsc_long & ~cond_csca_short & ~cond_very_short

    risk = np.select(
        [cond_dsc_long, cond_csca_short, cond_very_short, cond_very_long],
        [8.0, 6.0, 5.0, 7.0],
        default=0.0,
    )

    return np.minimum(risk, 10.0)


def _vectorized_dn_risk(df: pd.DataFrame) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Category 10: DN consistency (0~10) — vectorized.

    Returns:
        (dn_risk, country_mismatch_mask, dn_subject_countries)
        - dn_risk: per-certificate dn risk scores
        - country_mismatch_mask: boolean mask where subject country != certificate country
        - dn_subject_countries: extracted subject country codes (for findings messages)
    """
    from app.services.issuer_profiler import count_dn_fields, extract_country_from_dn

    subject_dn_arr = df["subject_dn"].fillna("").astype(str).to_numpy()
    country_arr = df["country_code"].fillna("").astype(str).to_numpy()

    n = len(df)
    risk = np.zeros(n, dtype=np.float64)

    # Extract subject countries and compute field counts (element-wise, but via list comp — no per-row DataFrame access)
    subj_countries = np.array([extract_country_from_dn(s) for s in subject_dn_arr])
    field_counts = np.array([count_dn_fields(s) for s in subject_dn_arr], dtype=np.float64)

    # Country mismatch: subject_dn has country AND it differs from country_code
    has_subject = subject_dn_arr != ""
    has_country = country_arr != ""
    has_subj_c = subj_countries != ""
    country_mismatch = has_subject & has_country & has_subj_c & (subj_countries != country_arr)
    risk += np.where(country_mismatch, 5.0, 0.0)

    # DN field count checks
    has_dn = has_subject
    risk += np.where(has_dn & (field_counts < 2), 3.0, 0.0)
    risk += np.where(has_dn & (field_counts > 10), 2.0, 0.0)

    return np.minimum(risk, 10.0), country_mismatch, subj_countries


def _build_factors_and_findings(
    idx: int,
    alg_risk_arr: np.ndarray,
    ks_risk_arr: np.ndarray,
    compliance_risk_arr: np.ndarray,
    validity_risk_arr: np.ndarray,
    ext_risk_arr: np.ndarray,
    anomaly_risk_arr: np.ndarray,
    issuer_risk_arr: np.ndarray,
    structural_risk_arr: np.ndarray,
    temporal_risk_arr: np.ndarray,
    dn_risk_arr: np.ndarray,
    anomaly_scores: np.ndarray,
    issuer_scores: np.ndarray,
    country_mismatch: np.ndarray,
    subj_countries: np.ndarray,
    df: pd.DataFrame,
    forensic_score: float,
    detailed: bool,
) -> tuple[dict, dict]:
    """Build risk_factors dict and forensic_findings dict for a single certificate.

    When ``detailed=False``, findings list is empty (low-risk certificate).
    """
    alg = float(alg_risk_arr[idx])
    ks = float(ks_risk_arr[idx])
    comp = float(compliance_risk_arr[idx])
    val = float(validity_risk_arr[idx])
    ext = float(ext_risk_arr[idx])
    anom = float(anomaly_risk_arr[idx])
    iss = float(issuer_risk_arr[idx])
    struc = float(structural_risk_arr[idx])
    temp = float(temporal_risk_arr[idx])
    dn = float(dn_risk_arr[idx])

    # Build factors dict (only include non-zero categories)
    factors: dict = {}
    if alg > 0:
        factors["algorithm"] = alg
    if ks > 0:
        factors["key_size"] = ks
    if comp > 0:
        factors["compliance"] = comp
    if val > 0:
        factors["validity"] = val
    if ext > 0:
        factors["extensions"] = ext
    if anom > 1:
        factors["anomaly"] = anom
    if iss > 1:
        factors["issuer_reputation"] = iss
    if struc > 1:
        factors["structural_consistency"] = struc
    if temp > 1:
        factors["temporal_pattern"] = temp
    if dn > 1:
        factors["dn_consistency"] = dn

    findings: list[dict] = []

    if detailed:
        row = df.iloc[idx]

        # Algorithm findings
        if alg >= 30:
            sig_alg = row.get("signature_algorithm") or ""
            findings.append({
                "category": "algorithm",
                "severity": "CRITICAL",
                "message": f"취약한 서명 알고리즘: {sig_alg}",
            })

        # Key size findings
        if ks >= 30:
            pub_alg = str(row.get("public_key_algorithm") or "").lower()
            key_size = row.get("public_key_size") or 0
            findings.append({
                "category": "key_size",
                "severity": "CRITICAL",
                "message": f"취약한 키 크기: {pub_alg.upper()} {key_size}bit",
            })

        # Compliance findings
        if comp >= 15:
            findings.append({
                "category": "compliance",
                "severity": "HIGH",
                "message": "ICAO 9303 규격 미준수",
            })

        # Validity findings
        if val >= 15:
            findings.append({
                "category": "validity",
                "severity": "HIGH",
                "message": "인증서 만료됨",
            })

        # Anomaly findings
        if anom >= 10:
            findings.append({
                "category": "anomaly",
                "severity": "HIGH",
                "message": f"ML 이상 탐지 점수: {anomaly_scores[idx]:.2f}",
            })

        # Issuer reputation findings
        if iss >= 10:
            findings.append({
                "category": "issuer_reputation",
                "severity": "MEDIUM",
                "message": f"발급자 이상 점수: {issuer_scores[idx]:.2f}",
            })

        # Structural consistency findings
        if struc >= 15:
            findings.append({
                "category": "structural_consistency",
                "severity": "HIGH",
                "message": "확장 프로파일 규칙 위반",
            })

        # Temporal pattern findings
        if temp >= 6:
            findings.append({
                "category": "temporal_pattern",
                "severity": "MEDIUM",
                "message": "비정상적 유효기간 패턴",
            })

        # DN consistency findings
        if country_mismatch[idx]:
            country_code = str(df.iloc[idx].get("country_code") or "")
            findings.append({
                "category": "dn_consistency",
                "severity": "MEDIUM",
                "message": f"Subject 국가({subj_countries[idx]})와 인증서 국가({country_code}) 불일치",
            })

    forensic_level = classify_forensic_risk(forensic_score)

    forensic_findings = {
        "score": round(forensic_score, 1),
        "level": forensic_level,
        "findings": findings,
        "categories": {
            "algorithm": alg,
            "key_size": ks,
            "compliance": comp,
            "validity": val,
            "extensions": ext,
            "anomaly": anom,
            "issuer_reputation": iss,
            "structural_consistency": struc,
            "temporal_pattern": temp,
            "dn_consistency": dn,
        },
    }

    return factors, forensic_findings


def compute_risk_scores(
    df: pd.DataFrame,
    anomaly_scores: np.ndarray,
    structural_scores: np.ndarray | None = None,
    issuer_scores: np.ndarray | None = None,
) -> tuple[np.ndarray, list[dict], np.ndarray, list[dict]]:
    """Compute composite risk scores for all certificates.

    Uses vectorized numpy/pandas operations for score computation across all
    rows, then builds detailed findings only for certificates above the
    forensic risk threshold.

    Args:
        df: Certificate DataFrame
        anomaly_scores: ML anomaly scores (0~1)
        structural_scores: Extension rules engine scores (0~1), optional
        issuer_scores: Issuer profiling scores (0~1), optional

    Returns:
        (risk_scores, risk_factors_list, forensic_scores, forensic_findings_list)
        - risk_scores: 0~100 array (backward compatible)
        - risk_factors_list: dicts with contributing factor scores
        - forensic_scores: 0~100 array (new forensic composite)
        - forensic_findings_list: detailed forensic finding dicts
    """
    n = len(df)

    if structural_scores is None:
        structural_scores = np.zeros(n, dtype=np.float64)
    if issuer_scores is None:
        issuer_scores = np.zeros(n, dtype=np.float64)

    # ===== Vectorized score computation for all 10 categories =====
    alg_risk_arr = _vectorized_algorithm_risk(df)           # (0~40)
    ks_risk_arr = _vectorized_key_size_risk(df)             # (0~40)
    compliance_risk_arr = _vectorized_compliance_risk(df)   # (0~20)
    validity_risk_arr = _vectorized_validity_risk(df)       # (0~15)
    ext_risk_arr = _vectorized_extension_risk(df)           # (0~15)
    anomaly_risk_arr = _vectorized_anomaly_risk(anomaly_scores)    # (0~15)
    issuer_risk_arr = _vectorized_issuer_risk(issuer_scores)       # (0~15)
    structural_risk_arr = _vectorized_structural_risk(structural_scores)  # (0~20)
    temporal_risk_arr = _vectorized_temporal_risk(df)        # (0~10)
    dn_risk_arr, country_mismatch, subj_countries = _vectorized_dn_risk(df)  # (0~10)

    # ===== Original risk score (backward compatible, max 100 from 6 categories) =====
    risk_scores = np.minimum(
        alg_risk_arr + ks_risk_arr + compliance_risk_arr
        + validity_risk_arr + ext_risk_arr + anomaly_risk_arr,
        100.0,
    )

    # ===== Forensic score (all 10 categories, normalized to 0-100) =====
    raw_total = (
        alg_risk_arr + ks_risk_arr + compliance_risk_arr
        + validity_risk_arr + ext_risk_arr + anomaly_risk_arr
        + issuer_risk_arr + structural_risk_arr + temporal_risk_arr + dn_risk_arr
    )
    forensic_scores = np.minimum(raw_total / _MAX_RAW_SCORE * 100.0, 100.0)

    # ===== Build factors and findings per certificate =====
    # Detailed findings are only generated for certificates above the threshold
    # (typically <10% of total), keeping the loop lightweight.
    risk_factors_list: list[dict] = [None] * n  # type: ignore[list-item]
    forensic_findings_list: list[dict] = [None] * n  # type: ignore[list-item]

    for i in range(n):
        detailed = forensic_scores[i] >= _FINDINGS_THRESHOLD
        factors, forensic_entry = _build_factors_and_findings(
            idx=i,
            alg_risk_arr=alg_risk_arr,
            ks_risk_arr=ks_risk_arr,
            compliance_risk_arr=compliance_risk_arr,
            validity_risk_arr=validity_risk_arr,
            ext_risk_arr=ext_risk_arr,
            anomaly_risk_arr=anomaly_risk_arr,
            issuer_risk_arr=issuer_risk_arr,
            structural_risk_arr=structural_risk_arr,
            temporal_risk_arr=temporal_risk_arr,
            dn_risk_arr=dn_risk_arr,
            anomaly_scores=anomaly_scores,
            issuer_scores=issuer_scores,
            country_mismatch=country_mismatch,
            subj_countries=subj_countries,
            df=df,
            forensic_score=float(forensic_scores[i]),
            detailed=detailed,
        )
        risk_factors_list[i] = factors
        forensic_findings_list[i] = forensic_entry

    logger.info(
        "Risk scoring complete: avg=%.1f, max=%.1f, critical=%d | "
        "Forensic: avg=%.1f, max=%.1f",
        risk_scores.mean(),
        risk_scores.max(),
        np.sum(risk_scores >= 76),
        forensic_scores.mean(),
        forensic_scores.max(),
    )

    return risk_scores, risk_factors_list, forensic_scores, forensic_findings_list


def classify_risk(score: float) -> str:
    """Classify risk score into level (backward compatible)."""
    if score >= 76:
        return "CRITICAL"
    elif score >= 51:
        return "HIGH"
    elif score >= 26:
        return "MEDIUM"
    return "LOW"


def classify_forensic_risk(score: float) -> str:
    """Classify forensic risk score into level."""
    if score >= 60:
        return "CRITICAL"
    elif score >= 40:
        return "HIGH"
    elif score >= 20:
        return "MEDIUM"
    return "LOW"

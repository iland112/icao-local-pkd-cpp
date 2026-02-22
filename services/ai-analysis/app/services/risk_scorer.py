"""Composite risk scoring for certificates.

v2.19.0: 6 → 10 risk categories with forensic findings.
"""

import logging

import numpy as np
import pandas as pd

from app.database import safe_isna

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

# Maximum raw score across all 10 categories
_MAX_RAW_SCORE = 200.0


def compute_risk_scores(
    df: pd.DataFrame,
    anomaly_scores: np.ndarray,
    structural_scores: np.ndarray | None = None,
    issuer_scores: np.ndarray | None = None,
) -> tuple[np.ndarray, list[dict], np.ndarray, list[dict]]:
    """Compute composite risk scores for all certificates.

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
    risk_scores = np.zeros(n, dtype=np.float64)
    forensic_scores = np.zeros(n, dtype=np.float64)
    risk_factors_list = []
    forensic_findings_list = []

    if structural_scores is None:
        structural_scores = np.zeros(n, dtype=np.float64)
    if issuer_scores is None:
        issuer_scores = np.zeros(n, dtype=np.float64)

    for i, (_, row) in enumerate(df.iterrows()):
        factors = {}
        findings = []

        # ===== Original 6 categories =====

        # 1. Algorithm risk (0~40)
        sig_alg = row.get("signature_algorithm") or ""
        alg_risk = ALGORITHM_RISK.get(sig_alg, 15)  # unknown = moderate risk
        if alg_risk > 0:
            factors["algorithm"] = alg_risk
            if alg_risk >= 30:
                findings.append({
                    "category": "algorithm",
                    "severity": "CRITICAL",
                    "message": f"취약한 서명 알고리즘: {sig_alg}",
                })

        # 2. Key size risk (0~40)
        key_size = row.get("public_key_size") or 0
        pub_alg = str(row.get("public_key_algorithm") or "").lower()
        if "rsa" in pub_alg:
            if key_size < 2048:
                ks_risk = 40
            elif key_size < 3072:
                ks_risk = 10
            elif key_size < 4096:
                ks_risk = 3
            else:
                ks_risk = 0
        elif "ec" in pub_alg:
            if key_size < 256:
                ks_risk = 35
            elif key_size < 384:
                ks_risk = 5
            else:
                ks_risk = 0
        else:
            ks_risk = 15  # unknown algorithm type
        if ks_risk > 0:
            factors["key_size"] = ks_risk
            if ks_risk >= 30:
                findings.append({
                    "category": "key_size",
                    "severity": "CRITICAL",
                    "message": f"취약한 키 크기: {pub_alg.upper()} {key_size}bit",
                })

        # 3. ICAO compliance risk (0~20)
        icao_compliant = row.get("icao_compliant")
        if icao_compliant is False or str(icao_compliant).lower() in ("false", "0"):
            compliance_risk = 20
        elif icao_compliant is None:
            compliance_risk = 5
        else:
            compliance_risk = 0
        if compliance_risk > 0:
            factors["compliance"] = compliance_risk
            if compliance_risk >= 15:
                findings.append({
                    "category": "compliance",
                    "severity": "HIGH",
                    "message": "ICAO 9303 규격 미준수",
                })

        # 4. Validity risk (0~15)
        not_after = row.get("not_after")
        if not_after and not safe_isna(not_after):
            not_after_dt = pd.to_datetime(not_after, utc=True)
            days_left = (not_after_dt - pd.Timestamp.now(tz="UTC")).total_seconds() / 86400
            if days_left < 0:
                validity_risk = 15
            elif days_left < 30:
                validity_risk = 10
            elif days_left < 90:
                validity_risk = 5
            else:
                validity_risk = 0
        else:
            validity_risk = 5
        if validity_risk > 0:
            factors["validity"] = validity_risk
            if validity_risk >= 15:
                findings.append({
                    "category": "validity",
                    "severity": "HIGH",
                    "message": "인증서 만료됨",
                })

        # 5. Extension risk (0~15)
        ext_risk = 0
        if not row.get("crl_distribution_points"):
            ext_risk += 5
        if not row.get("authority_key_identifier"):
            ext_risk += 5
        if not row.get("subject_key_identifier"):
            ext_risk += 3
        if not row.get("ocsp_responder_url"):
            ext_risk += 2
        ext_risk = min(ext_risk, 15)
        if ext_risk > 0:
            factors["extensions"] = ext_risk

        # 6. Anomaly risk (0~15)
        anomaly_risk = round(float(anomaly_scores[i]) * 15, 1)
        if anomaly_risk > 1:
            factors["anomaly"] = anomaly_risk
            if anomaly_risk >= 10:
                findings.append({
                    "category": "anomaly",
                    "severity": "HIGH",
                    "message": f"ML 이상 탐지 점수: {anomaly_scores[i]:.2f}",
                })

        # ===== New 4 forensic categories =====

        # 7. Issuer reputation (0~15)
        issuer_risk = round(float(issuer_scores[i]) * 15, 1)
        if issuer_risk > 1:
            factors["issuer_reputation"] = issuer_risk
            if issuer_risk >= 10:
                findings.append({
                    "category": "issuer_reputation",
                    "severity": "MEDIUM",
                    "message": f"발급자 이상 점수: {issuer_scores[i]:.2f}",
                })

        # 8. Structural consistency (0~20)
        structural_risk = round(float(structural_scores[i]) * 20, 1)
        if structural_risk > 1:
            factors["structural_consistency"] = structural_risk
            if structural_risk >= 15:
                findings.append({
                    "category": "structural_consistency",
                    "severity": "HIGH",
                    "message": "확장 프로파일 규칙 위반",
                })

        # 9. Temporal pattern (0~10)
        temporal_risk = 0.0
        not_before = row.get("not_before")
        if not_before and not safe_isna(not_before) and not_after and not safe_isna(not_after):
            nb_dt = pd.to_datetime(not_before, utc=True)
            na_dt = pd.to_datetime(not_after, utc=True)
            validity_days_val = (na_dt - nb_dt).total_seconds() / 86400
            cert_type = row.get("certificate_type") or ""
            # Flag unusually short or long validity periods
            if cert_type == "DSC" and validity_days_val > 365 * 15:
                temporal_risk = 8.0
            elif cert_type == "CSCA" and validity_days_val < 365:
                temporal_risk = 6.0
            elif validity_days_val < 30:
                temporal_risk = 5.0
            elif validity_days_val > 365 * 30:
                temporal_risk = 7.0
        temporal_risk = min(temporal_risk, 10.0)
        if temporal_risk > 1:
            factors["temporal_pattern"] = temporal_risk
            if temporal_risk >= 6:
                findings.append({
                    "category": "temporal_pattern",
                    "severity": "MEDIUM",
                    "message": "비정상적 유효기간 패턴",
                })

        # 10. DN consistency (0~10)
        dn_risk = 0.0
        subject_dn = str(row.get("subject_dn") or "")
        issuer_dn = str(row.get("issuer_dn") or "")
        country = row.get("country_code") or ""

        # Check if subject country matches certificate country_code
        if subject_dn and country:
            from app.services.issuer_profiler import extract_country_from_dn
            subj_c = extract_country_from_dn(subject_dn)
            if subj_c and subj_c != country:
                dn_risk += 5.0
                findings.append({
                    "category": "dn_consistency",
                    "severity": "MEDIUM",
                    "message": f"Subject 국가({subj_c})와 인증서 국가({country}) 불일치",
                })

        # Check DN field count (too few = suspicious)
        if subject_dn:
            from app.services.issuer_profiler import count_dn_fields
            field_count = count_dn_fields(subject_dn)
            if field_count < 2:
                dn_risk += 3.0
            if field_count > 10:
                dn_risk += 2.0

        dn_risk = min(dn_risk, 10.0)
        if dn_risk > 1:
            factors["dn_consistency"] = dn_risk

        # ===== Compute scores =====
        # Original risk score (backward compatible, max 100 from 6 categories)
        original_total = min(
            alg_risk + ks_risk + compliance_risk + validity_risk + ext_risk + anomaly_risk,
            100.0,
        )
        risk_scores[i] = original_total

        # Forensic score (all 10 categories, normalized to 0-100)
        raw_total = (
            alg_risk + ks_risk + compliance_risk + validity_risk + ext_risk
            + anomaly_risk + issuer_risk + structural_risk + temporal_risk + dn_risk
        )
        forensic_scores[i] = min(raw_total / _MAX_RAW_SCORE * 100.0, 100.0)

        risk_factors_list.append(factors)
        forensic_findings_list.append({
            "score": round(forensic_scores[i], 1),
            "level": classify_forensic_risk(forensic_scores[i]),
            "findings": findings,
            "categories": {
                "algorithm": alg_risk,
                "key_size": ks_risk,
                "compliance": compliance_risk,
                "validity": validity_risk,
                "extensions": ext_risk,
                "anomaly": anomaly_risk,
                "issuer_reputation": issuer_risk,
                "structural_consistency": structural_risk,
                "temporal_pattern": temporal_risk,
                "dn_consistency": dn_risk,
            },
        })

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

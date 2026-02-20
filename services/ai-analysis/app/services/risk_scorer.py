"""Composite risk scoring for certificates."""

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


def compute_risk_scores(
    df: pd.DataFrame,
    anomaly_scores: np.ndarray,
) -> tuple[np.ndarray, list[dict]]:
    """Compute composite risk scores for all certificates.

    Returns:
        (risk_scores, risk_factors_list) where risk_scores is 0~100 array
        and risk_factors_list is a list of dicts with contributing factors.
    """
    n = len(df)
    risk_scores = np.zeros(n, dtype=np.float64)
    risk_factors_list = []

    for i, (_, row) in enumerate(df.iterrows()):
        factors = {}

        # 1. Algorithm risk (0~40)
        sig_alg = row.get("signature_algorithm") or ""
        alg_risk = ALGORITHM_RISK.get(sig_alg, 15)  # unknown = moderate risk
        if alg_risk > 0:
            factors["algorithm"] = alg_risk

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

        # 3. ICAO compliance risk (0~20)
        icao_compliant = row.get("icao_compliant")
        if icao_compliant is False or str(icao_compliant).lower() in ("false", "0"):
            compliance_risk = 20
        elif icao_compliant is None:
            compliance_risk = 5  # not yet checked
        else:
            compliance_risk = 0
        if compliance_risk > 0:
            factors["compliance"] = compliance_risk

        # 4. Validity risk (0~15)
        not_after = row.get("not_after")
        if not_after and not pd.isna(not_after):
            not_after_dt = pd.to_datetime(not_after)
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

        # Sum (capped at 100)
        total = min(
            alg_risk + ks_risk + compliance_risk + validity_risk + ext_risk + anomaly_risk,
            100.0,
        )
        risk_scores[i] = total
        risk_factors_list.append(factors)

    logger.info(
        "Risk scoring complete: avg=%.1f, max=%.1f, critical=%d",
        risk_scores.mean(),
        risk_scores.max(),
        np.sum(risk_scores >= 76),
    )

    return risk_scores, risk_factors_list


def classify_risk(score: float) -> str:
    """Classify risk score into level."""
    if score >= 76:
        return "CRITICAL"
    elif score >= 51:
        return "HIGH"
    elif score >= 26:
        return "MEDIUM"
    return "LOW"

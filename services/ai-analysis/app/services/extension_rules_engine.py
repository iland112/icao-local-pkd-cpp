"""ICAO Doc 9303 extension profile rules engine.

Checks certificates against expected extension profiles per type.
Produces structural_anomaly_score (0.0 ~ 1.0).
"""

import logging
from collections import defaultdict

import pandas as pd

logger = logging.getLogger(__name__)

# ICAO Doc 9303 expected extensions per certificate type.
# Each rule specifies required, recommended, and forbidden extensions.
EXPECTED_EXTENSIONS = {
    "CSCA": {
        "required": [
            "key_usage",
            "subject_key_identifier",
            "is_ca",
        ],
        "recommended": [
            "authority_key_identifier",
            "crl_distribution_points",
        ],
        "forbidden_flags": {
            "is_ca": False,  # CSCA must have CA=TRUE
        },
        "required_key_usage_bits": ["keyCertSign", "cRLSign"],
    },
    "DSC": {
        "required": [
            "key_usage",
            "authority_key_identifier",
        ],
        "recommended": [
            "crl_distribution_points",
            "ocsp_responder_url",
        ],
        "forbidden_flags": {
            "is_ca": True,  # DSC must NOT be CA
        },
        "required_key_usage_bits": ["digitalSignature"],
    },
    "MLSC": {
        "required": [
            "extended_key_usage",
        ],
        "recommended": [
            "authority_key_identifier",
            "subject_key_identifier",
        ],
        "forbidden_flags": {},
        "required_key_usage_bits": [],
    },
    "DSC_NC": {
        "required": [],
        "recommended": [
            "authority_key_identifier",
            "key_usage",
        ],
        "forbidden_flags": {},
        "required_key_usage_bits": [],
    },
}

# Extension field names present in the certificate table
EXTENSION_FIELDS = [
    "key_usage",
    "extended_key_usage",
    "subject_key_identifier",
    "authority_key_identifier",
    "crl_distribution_points",
    "ocsp_responder_url",
]


def _has_field(row, field: str) -> bool:
    """Check if a certificate row has a non-empty field."""
    val = row.get(field)
    if val is None:
        return False
    if isinstance(val, str) and val.strip() == "":
        return False
    if pd.isna(val):
        return False
    return True


def _safe_bool(val) -> bool:
    """Convert various boolean representations."""
    if val is None:
        return False
    if isinstance(val, bool):
        return val
    if isinstance(val, (int, float)):
        return bool(val)
    if isinstance(val, str):
        return val.lower() in ("true", "1", "t", "yes")
    return False


def check_extension_compliance(row) -> dict:
    """Check a single certificate against its type's extension profile.

    Returns:
        {
            "missing_required": ["field1", ...],
            "missing_recommended": ["field2", ...],
            "forbidden_violations": ["flag1", ...],
            "key_usage_violations": ["bit1", ...],
            "structural_score": 0.0~1.0,
            "violations_detail": [{"rule": ..., "severity": ...}, ...],
        }
    """
    cert_type = row.get("certificate_type") or ""
    profile = EXPECTED_EXTENSIONS.get(cert_type, EXPECTED_EXTENSIONS.get("DSC_NC", {}))

    missing_required = []
    missing_recommended = []
    forbidden_violations = []
    key_usage_violations = []
    violations_detail = []

    # Check required extensions
    for field in profile.get("required", []):
        if field == "is_ca":
            if not _safe_bool(row.get("is_ca")):
                missing_required.append(field)
                violations_detail.append({
                    "rule": f"Required: {field}",
                    "severity": "CRITICAL",
                })
        elif not _has_field(row, field):
            missing_required.append(field)
            violations_detail.append({
                "rule": f"Required extension missing: {field}",
                "severity": "HIGH",
            })

    # Check recommended extensions
    for field in profile.get("recommended", []):
        if not _has_field(row, field):
            missing_recommended.append(field)
            violations_detail.append({
                "rule": f"Recommended extension missing: {field}",
                "severity": "MEDIUM",
            })

    # Check forbidden flags
    for flag, forbidden_value in profile.get("forbidden_flags", {}).items():
        actual = _safe_bool(row.get(flag))
        if actual == forbidden_value:
            forbidden_violations.append(flag)
            violations_detail.append({
                "rule": f"Forbidden: {flag}={forbidden_value}",
                "severity": "CRITICAL",
            })

    # Check key usage bits
    key_usage_str = str(row.get("key_usage") or "").lower()
    for bit in profile.get("required_key_usage_bits", []):
        if bit.lower() not in key_usage_str:
            key_usage_violations.append(bit)
            violations_detail.append({
                "rule": f"Missing key usage bit: {bit}",
                "severity": "HIGH",
            })

    # Compute structural anomaly score (0.0 = fully compliant, 1.0 = severe violations)
    score = 0.0
    score += len(missing_required) * 0.25
    score += len(forbidden_violations) * 0.30
    score += len(key_usage_violations) * 0.15
    score += len(missing_recommended) * 0.05
    score = min(score, 1.0)

    return {
        "missing_required": missing_required,
        "missing_recommended": missing_recommended,
        "forbidden_violations": forbidden_violations,
        "key_usage_violations": key_usage_violations,
        "structural_score": round(score, 4),
        "violations_detail": violations_detail,
    }


def count_unexpected_extensions(row) -> int:
    """Count extensions present that are unusual for the certificate type."""
    cert_type = row.get("certificate_type") or ""
    profile = EXPECTED_EXTENSIONS.get(cert_type, {})
    expected = set(profile.get("required", []) + profile.get("recommended", []))

    unexpected = 0
    for field in EXTENSION_FIELDS:
        if _has_field(row, field) and field not in expected:
            unexpected += 1
    return unexpected


def count_missing_required(row) -> int:
    """Count required extensions missing for this certificate type."""
    cert_type = row.get("certificate_type") or ""
    profile = EXPECTED_EXTENSIONS.get(cert_type, {})

    missing = 0
    for field in profile.get("required", []):
        if field == "is_ca":
            if not _safe_bool(row.get("is_ca")):
                missing += 1
        elif not _has_field(row, field):
            missing += 1
    return missing


def compute_extension_anomalies(df: pd.DataFrame) -> list[dict]:
    """Compute extension compliance for all certificates.

    Returns list of dicts with fingerprint + violation details.
    """
    results = []
    violation_counts = defaultdict(int)

    for _, row in df.iterrows():
        compliance = check_extension_compliance(row)
        if compliance["structural_score"] > 0:
            results.append({
                "fingerprint": row.get("fingerprint_sha256"),
                "certificate_type": row.get("certificate_type"),
                "country_code": row.get("country_code"),
                "structural_score": compliance["structural_score"],
                "missing_required": compliance["missing_required"],
                "missing_recommended": compliance["missing_recommended"],
                "forbidden_violations": compliance["forbidden_violations"],
                "key_usage_violations": compliance["key_usage_violations"],
                "violations_detail": compliance["violations_detail"],
            })
            for v in compliance["violations_detail"]:
                violation_counts[v["rule"]] += 1

    results.sort(key=lambda x: x["structural_score"], reverse=True)

    logger.info(
        "Extension compliance check: %d/%d certificates with violations",
        len(results),
        len(df),
    )
    return results


def get_extension_anomaly_summary(df: pd.DataFrame) -> dict:
    """Get summary of extension anomalies by type and severity."""
    by_type = defaultdict(lambda: {"total": 0, "with_violations": 0})
    by_severity = defaultdict(int)

    for _, row in df.iterrows():
        cert_type = row.get("certificate_type") or "UNKNOWN"
        by_type[cert_type]["total"] += 1
        compliance = check_extension_compliance(row)
        if compliance["structural_score"] > 0:
            by_type[cert_type]["with_violations"] += 1
            for v in compliance["violations_detail"]:
                by_severity[v["severity"]] += 1

    return {
        "by_type": dict(by_type),
        "by_severity": dict(by_severity),
        "total_checked": len(df),
    }

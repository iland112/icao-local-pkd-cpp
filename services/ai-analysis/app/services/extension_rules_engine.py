"""ICAO Doc 9303 extension profile rules engine.

Checks certificates against expected extension profiles per type.
Produces structural_anomaly_score (0.0 ~ 1.0).
"""

import hashlib
import logging
from collections import defaultdict

import numpy as np
import pandas as pd

from app.database import safe_isna

logger = logging.getLogger(__name__)

# Module-level cache: maps DataFrame identity hash -> (anomalies list, per-row compliance list)
_compliance_cache: dict[str, tuple[list[dict], list[dict]]] = {}
_CACHE_MAX_SIZE = 4

def _df_cache_key(df: pd.DataFrame) -> str:
    """Compute a lightweight identity key for a DataFrame."""
    # Use shape + id + first/last fingerprints for fast identity check
    parts = [str(len(df)), str(id(df))]
    if len(df) > 0 and "fingerprint_sha256" in df.columns:
        first_fp = str(df.iloc[0].get("fingerprint_sha256", ""))
        last_fp = str(df.iloc[-1].get("fingerprint_sha256", ""))
        parts.extend([first_fp[:16], last_fp[:16]])
    return hashlib.md5("|".join(parts).encode()).hexdigest()


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
    if safe_isna(val):
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


def _compute_all_compliance(df: pd.DataFrame) -> tuple[list[dict], list[dict]]:
    """Compute extension compliance for every row, returning (anomalies, all_compliance).

    Uses vectorized boolean masks for score pre-computation where possible,
    then falls back to per-row detail extraction only for violating rows.

    Returns:
        (anomalies_sorted, all_compliance) where all_compliance has one entry per row.
    """
    n = len(df)
    if n == 0:
        return [], []

    cert_types = df["certificate_type"].fillna("") if "certificate_type" in df.columns else pd.Series([""] * n)

    # Vectorized boolean masks for field presence (non-null, non-empty-string)
    field_present: dict[str, np.ndarray] = {}
    for field in EXTENSION_FIELDS:
        if field in df.columns:
            col = df[field]
            mask = col.notna()
            # Also exclude empty strings
            str_mask = col.astype(str).str.strip().ne("")
            field_present[field] = (mask & str_mask).values
        else:
            field_present[field] = np.zeros(n, dtype=bool)

    # is_ca boolean conversion (vectorized)
    if "is_ca" in df.columns:
        is_ca_vals = df["is_ca"].apply(_safe_bool).values
    else:
        is_ca_vals = np.zeros(n, dtype=bool)

    # Pre-compute structural scores vectorized
    scores = np.zeros(n, dtype=np.float64)
    type_arr = cert_types.values

    for cert_type, profile in EXPECTED_EXTENSIONS.items():
        type_mask = type_arr == cert_type
        if not np.any(type_mask):
            continue

        # Missing required count
        missing_req_count = np.zeros(n, dtype=np.float64)
        for field in profile.get("required", []):
            if field == "is_ca":
                missing_req_count += type_mask & ~is_ca_vals
            elif field in field_present:
                missing_req_count += type_mask & ~field_present[field]

        # Forbidden violations count
        forbidden_count = np.zeros(n, dtype=np.float64)
        for flag, forbidden_value in profile.get("forbidden_flags", {}).items():
            if flag == "is_ca":
                forbidden_count += type_mask & (is_ca_vals == forbidden_value)

        # Key usage violations count
        ku_violation_count = np.zeros(n, dtype=np.float64)
        required_bits = profile.get("required_key_usage_bits", [])
        if required_bits and "key_usage" in df.columns:
            ku_lower = df["key_usage"].fillna("").astype(str).str.lower()
            for bit in required_bits:
                bit_lower = bit.lower()
                ku_violation_count += type_mask & ~ku_lower.str.contains(bit_lower, regex=False).values

        # Missing recommended count
        missing_rec_count = np.zeros(n, dtype=np.float64)
        for field in profile.get("recommended", []):
            if field in field_present:
                missing_rec_count += type_mask & ~field_present[field]

        scores += missing_req_count * 0.25 + forbidden_count * 0.30 + ku_violation_count * 0.15 + missing_rec_count * 0.05

    # Handle certificates with unknown type -> fall through to DSC_NC profile
    known_types = set(EXPECTED_EXTENSIONS.keys())
    unknown_mask = ~np.isin(type_arr, list(known_types)) & (type_arr != "")
    if np.any(unknown_mask):
        dsc_nc_profile = EXPECTED_EXTENSIONS.get("DSC_NC", {})
        # DSC_NC has no required, no forbidden_flags, no required_key_usage_bits
        # Only recommended: authority_key_identifier, key_usage
        for field in dsc_nc_profile.get("recommended", []):
            if field in field_present:
                scores += (unknown_mask & ~field_present[field]) * 0.05

    scores = np.minimum(scores, 1.0)
    scores = np.round(scores, 4)

    # Build per-row compliance and anomaly results
    # Only call the detailed check_extension_compliance() for rows with score > 0
    all_compliance = [None] * n
    results = []
    violation_indices = np.where(scores > 0)[0]

    # Pre-extract columns as arrays for faster row access
    fp_col = df["fingerprint_sha256"].values if "fingerprint_sha256" in df.columns else [None] * n
    cc_col = df["country_code"].values if "country_code" in df.columns else [""] * n

    for i in violation_indices:
        row = df.iloc[i]
        compliance = check_extension_compliance(row)
        # Use the vectorized score (identical logic, avoids floating-point divergence)
        compliance["structural_score"] = float(scores[i])
        all_compliance[i] = compliance
        results.append({
            "fingerprint": fp_col[i],
            "certificate_type": type_arr[i],
            "country_code": cc_col[i],
            "structural_score": float(scores[i]),
            "missing_required": compliance["missing_required"],
            "missing_recommended": compliance["missing_recommended"],
            "forbidden_violations": compliance["forbidden_violations"],
            "key_usage_violations": compliance["key_usage_violations"],
            "violations_detail": compliance["violations_detail"],
        })

    # Fill in compliant rows with empty compliance dicts
    empty_compliance = {
        "missing_required": [],
        "missing_recommended": [],
        "forbidden_violations": [],
        "key_usage_violations": [],
        "structural_score": 0.0,
        "violations_detail": [],
    }
    for i in range(n):
        if all_compliance[i] is None:
            all_compliance[i] = empty_compliance

    results.sort(key=lambda x: x["structural_score"], reverse=True)
    return results, all_compliance


def compute_extension_anomalies(df: pd.DataFrame) -> list[dict]:
    """Compute extension compliance for all certificates.

    Returns list of dicts with fingerprint + violation details.
    Results are cached so repeated calls with the same DataFrame are free.
    """
    global _compliance_cache

    cache_key = _df_cache_key(df)
    if cache_key in _compliance_cache:
        results, _ = _compliance_cache[cache_key]
        return results

    results, all_compliance = _compute_all_compliance(df)

    # Evict oldest entries if cache is full
    if len(_compliance_cache) >= _CACHE_MAX_SIZE:
        oldest_key = next(iter(_compliance_cache))
        del _compliance_cache[oldest_key]
    _compliance_cache[cache_key] = (results, all_compliance)

    logger.info(
        "Extension compliance check: %d/%d certificates with violations",
        len(results),
        len(df),
    )
    return results


def get_extension_anomaly_summary(df: pd.DataFrame) -> dict:
    """Get summary of extension anomalies by type and severity.

    Reuses cached compliance results from compute_extension_anomalies()
    and uses pandas groupby for aggregation instead of manual iteration.
    """
    if len(df) == 0:
        return {"by_type": {}, "by_severity": {}, "total_checked": 0}

    # Ensure compliance is computed (will use cache if available)
    cache_key = _df_cache_key(df)
    if cache_key in _compliance_cache:
        _, all_compliance = _compliance_cache[cache_key]
    else:
        # Trigger computation and caching
        compute_extension_anomalies(df)
        _, all_compliance = _compliance_cache[cache_key]

    # Build a lightweight Series of structural scores and cert types
    cert_types = df["certificate_type"].fillna("UNKNOWN").replace("", "UNKNOWN")
    structural_scores = np.array(
        [c["structural_score"] for c in all_compliance], dtype=np.float64
    )
    has_violation = structural_scores > 0

    # by_type: use pandas groupby on cert_types
    type_series = cert_types.values
    unique_types = np.unique(type_series)
    by_type = {}
    for ct in unique_types:
        ct_mask = type_series == ct
        by_type[ct] = {
            "total": int(np.sum(ct_mask)),
            "with_violations": int(np.sum(ct_mask & has_violation)),
        }

    # by_severity: aggregate from violation details of violating rows only
    by_severity = defaultdict(int)
    violation_indices = np.where(has_violation)[0]
    for i in violation_indices:
        for v in all_compliance[i]["violations_detail"]:
            by_severity[v["severity"]] += 1

    return {
        "by_type": by_type,
        "by_severity": dict(by_severity),
        "total_checked": len(df),
    }

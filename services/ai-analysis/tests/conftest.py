"""Pytest fixtures for AI Analysis service tests."""

import os

import numpy as np
import pandas as pd
import pytest
from fastapi.testclient import TestClient

# Override DB settings before importing app to avoid real DB connection attempts
os.environ.setdefault("DB_TYPE", "postgres")
os.environ.setdefault("DB_HOST", "localhost")
os.environ.setdefault("DB_PASSWORD", "test")
os.environ.setdefault("ANALYSIS_ENABLED", "false")


@pytest.fixture()
def client():
    """Create a test client for the FastAPI application."""
    from app.main import app

    with TestClient(app) as c:
        yield c


@pytest.fixture()
def sample_certificate_df():
    """Create a small DataFrame mimicking certificate data from the database.

    Contains 5 rows with realistic certificate-like data covering
    CSCA, DSC, DSC_NC, and MLSC types.
    """
    return pd.DataFrame({
        "fingerprint_sha256": [
            "a" * 64,
            "b" * 64,
            "c" * 64,
            "d" * 64,
            "e" * 64,
        ],
        "certificate_type": ["CSCA", "DSC", "DSC", "DSC_NC", "MLSC"],
        "country_code": ["KR", "KR", "US", "DE", "FR"],
        "version": [3, 3, 3, 3, 3],
        "signature_algorithm": [
            "sha256WithRSAEncryption",
            "sha256WithRSAEncryption",
            "ecdsa-with-SHA384",
            "sha1WithRSAEncryption",
            "sha256WithRSAEncryption",
        ],
        "public_key_algorithm": ["rsaEncryption", "rsaEncryption", "EC", "rsaEncryption", "rsaEncryption"],
        "public_key_size": [4096, 2048, 384, 1024, 2048],
        "public_key_curve": [None, None, "secp384r1", None, None],
        "key_usage": [
            "critical, keyCertSign, cRLSign",
            "critical, digitalSignature",
            "critical, digitalSignature",
            "digitalSignature",
            None,
        ],
        "extended_key_usage": [None, None, None, None, "2.23.136.1.1.3"],
        "is_ca": [True, False, False, False, False],
        "path_len_constraint": [0, None, None, None, None],
        "is_self_signed": [True, False, False, False, False],
        "subject_key_identifier": ["AB:CD:EF", "12:34:56", "78:9A:BC", None, "DE:F0:12"],
        "authority_key_identifier": [None, "AB:CD:EF", "78:9A:BC", None, "AB:CD:EF"],
        "crl_distribution_points": [
            "http://crl.example.kr/csca.crl",
            "http://crl.example.kr/dsc.crl",
            "http://crl.example.us/dsc.crl",
            None,
            None,
        ],
        "ocsp_responder_url": [None, "http://ocsp.example.kr", None, None, None],
        "not_before": pd.to_datetime([
            "2020-01-01", "2022-06-15", "2023-03-01", "2018-01-01", "2024-01-01",
        ]),
        "not_after": pd.to_datetime([
            "2035-01-01", "2025-06-15", "2028-03-01", "2020-01-01", "2027-01-01",
        ]),
        "validation_status": ["VALID", "VALID", "VALID", "EXPIRED", "VALID"],
        "subject_dn": [
            "/C=KR/O=Government/CN=Korea CSCA",
            "/C=KR/O=Government/CN=Korea DSC 001",
            "CN=US DSC,O=State Dept,C=US",
            "/C=DE/CN=German DSC NC",
            "/C=FR/O=ANTS/CN=France MLSC",
        ],
        "issuer_dn": [
            "/C=KR/O=Government/CN=Korea CSCA",
            "/C=KR/O=Government/CN=Korea CSCA",
            "CN=US CSCA,O=State Dept,C=US",
            "/C=DE/CN=German CSCA",
            "/C=FR/O=ANTS/CN=France CSCA",
        ],
        "serial_number": ["01", "1000", "2000", "500", "3000"],
        "trust_chain_valid": [True, True, True, False, True],
        "icao_compliant": [True, True, True, False, True],
        "icao_violations": [None, None, None, "KEY_SIZE|ALGORITHM", None],
        "icao_key_usage_compliant": [True, True, True, False, True],
        "icao_algorithm_compliant": [True, True, True, False, True],
        "icao_key_size_compliant": [True, True, True, False, True],
        "icao_extensions_compliant": [True, True, True, False, True],
        "signature_valid": [True, True, True, None, True],
    })


@pytest.fixture()
def sample_analysis_results():
    """Create sample AI analysis result dicts for testing."""
    return [
        {
            "certificate_fingerprint": "a" * 64,
            "certificate_type": "CSCA",
            "country_code": "KR",
            "anomaly_score": 0.1,
            "anomaly_label": "NORMAL",
            "risk_score": 5.0,
            "risk_level": "LOW",
            "risk_factors": {"extensions": 5.0},
            "anomaly_explanations": [],
            "forensic_risk_score": 8.5,
            "forensic_risk_level": "LOW",
            "forensic_findings": {
                "score": 8.5,
                "level": "LOW",
                "findings": [],
                "categories": {
                    "algorithm": 5.0,
                    "key_size": 0.0,
                    "compliance": 0.0,
                    "validity": 0.0,
                    "extensions": 5.0,
                    "anomaly": 1.5,
                    "issuer_reputation": 0.0,
                    "structural_consistency": 0.0,
                    "temporal_pattern": 0.0,
                    "dn_consistency": 0.0,
                },
            },
        },
        {
            "certificate_fingerprint": "d" * 64,
            "certificate_type": "DSC_NC",
            "country_code": "DE",
            "anomaly_score": 0.85,
            "anomaly_label": "ANOMALOUS",
            "risk_score": 80.0,
            "risk_level": "CRITICAL",
            "risk_factors": {
                "algorithm": 40.0,
                "key_size": 40.0,
                "compliance": 20.0,
            },
            "anomaly_explanations": [
                {"feature": "key_size_normalized", "sigma": 3.2, "description": "Small key size"},
            ],
            "forensic_risk_score": 65.0,
            "forensic_risk_level": "CRITICAL",
            "forensic_findings": {
                "score": 65.0,
                "level": "CRITICAL",
                "findings": [
                    {"category": "algorithm", "severity": "CRITICAL", "message": "Weak algorithm"},
                    {"category": "key_size", "severity": "CRITICAL", "message": "Weak key size"},
                ],
                "categories": {
                    "algorithm": 40.0,
                    "key_size": 40.0,
                    "compliance": 20.0,
                    "validity": 15.0,
                    "extensions": 10.0,
                    "anomaly": 12.8,
                    "issuer_reputation": 5.0,
                    "structural_consistency": 10.0,
                    "temporal_pattern": 0.0,
                    "dn_consistency": 0.0,
                },
            },
        },
    ]


@pytest.fixture()
def mock_sync_engine(monkeypatch):
    """Mock sync_engine so DB-dependent endpoints do not connect to a real database.

    Returns a mock engine that raises on connect(), which allows tests to
    verify 503 responses for DB-dependent endpoints.
    """
    from unittest.mock import MagicMock

    mock_engine = MagicMock()
    mock_engine.connect.side_effect = Exception("No database available in test")

    monkeypatch.setattr("app.routers.analysis.sync_engine", mock_engine)
    monkeypatch.setattr("app.routers.reports.sync_engine", mock_engine)
    return mock_engine

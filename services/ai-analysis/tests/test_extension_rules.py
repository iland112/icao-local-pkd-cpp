"""Tests for app.services.extension_rules_engine -- ICAO Doc 9303 extension rules."""

import numpy as np
import pandas as pd
import pytest


class TestExpectedExtensions:
    """Test that EXPECTED_EXTENSIONS is properly defined."""

    def test_csca_has_required_extensions(self):
        from app.services.extension_rules_engine import EXPECTED_EXTENSIONS
        csca = EXPECTED_EXTENSIONS["CSCA"]
        assert "key_usage" in csca["required"]
        assert "subject_key_identifier" in csca["required"]
        assert "is_ca" in csca["required"]

    def test_dsc_has_required_extensions(self):
        from app.services.extension_rules_engine import EXPECTED_EXTENSIONS
        dsc = EXPECTED_EXTENSIONS["DSC"]
        assert "key_usage" in dsc["required"]
        assert "authority_key_identifier" in dsc["required"]

    def test_csca_forbidden_flags(self):
        """CSCA must have CA=TRUE, so is_ca=False is forbidden."""
        from app.services.extension_rules_engine import EXPECTED_EXTENSIONS
        csca = EXPECTED_EXTENSIONS["CSCA"]
        assert csca["forbidden_flags"]["is_ca"] is False

    def test_dsc_forbidden_flags(self):
        """DSC must NOT be CA, so is_ca=True is forbidden."""
        from app.services.extension_rules_engine import EXPECTED_EXTENSIONS
        dsc = EXPECTED_EXTENSIONS["DSC"]
        assert dsc["forbidden_flags"]["is_ca"] is True

    def test_mlsc_requires_eku(self):
        from app.services.extension_rules_engine import EXPECTED_EXTENSIONS
        mlsc = EXPECTED_EXTENSIONS["MLSC"]
        assert "extended_key_usage" in mlsc["required"]

    def test_all_types_defined(self):
        from app.services.extension_rules_engine import EXPECTED_EXTENSIONS
        assert "CSCA" in EXPECTED_EXTENSIONS
        assert "DSC" in EXPECTED_EXTENSIONS
        assert "MLSC" in EXPECTED_EXTENSIONS
        assert "DSC_NC" in EXPECTED_EXTENSIONS


class TestSafeBool:
    """Test _safe_bool() conversion."""

    def test_none_returns_false(self):
        from app.services.extension_rules_engine import _safe_bool
        assert _safe_bool(None) is False

    def test_true_bool(self):
        from app.services.extension_rules_engine import _safe_bool
        assert _safe_bool(True) is True

    def test_false_bool(self):
        from app.services.extension_rules_engine import _safe_bool
        assert _safe_bool(False) is False

    def test_int_one(self):
        from app.services.extension_rules_engine import _safe_bool
        assert _safe_bool(1) is True

    def test_int_zero(self):
        from app.services.extension_rules_engine import _safe_bool
        assert _safe_bool(0) is False

    @pytest.mark.parametrize("val", ["true", "True", "TRUE", "1", "t", "yes"])
    def test_truthy_strings(self, val):
        from app.services.extension_rules_engine import _safe_bool
        assert _safe_bool(val) is True

    @pytest.mark.parametrize("val", ["false", "False", "0", "no", ""])
    def test_falsy_strings(self, val):
        from app.services.extension_rules_engine import _safe_bool
        assert _safe_bool(val) is False


class TestHasField:
    """Test _has_field() field presence check."""

    def test_none_returns_false(self):
        from app.services.extension_rules_engine import _has_field
        assert _has_field({"key_usage": None}, "key_usage") is False

    def test_empty_string_returns_false(self):
        from app.services.extension_rules_engine import _has_field
        assert _has_field({"key_usage": ""}, "key_usage") is False

    def test_whitespace_returns_false(self):
        from app.services.extension_rules_engine import _has_field
        assert _has_field({"key_usage": "  "}, "key_usage") is False

    def test_valid_value_returns_true(self):
        from app.services.extension_rules_engine import _has_field
        assert _has_field({"key_usage": "critical, keyCertSign"}, "key_usage") is True

    def test_missing_key_returns_false(self):
        from app.services.extension_rules_engine import _has_field
        assert _has_field({}, "key_usage") is False


class TestCheckExtensionCompliance:
    """Test check_extension_compliance() for individual certificate types."""

    def test_compliant_csca(self):
        """A fully compliant CSCA should have score 0.0."""
        from app.services.extension_rules_engine import check_extension_compliance
        row = {
            "certificate_type": "CSCA",
            "key_usage": "critical, keyCertSign, cRLSign",
            "subject_key_identifier": "AB:CD:EF",
            "authority_key_identifier": "12:34:56",
            "crl_distribution_points": "http://crl.example.com",
            "is_ca": True,
            "extended_key_usage": None,
            "ocsp_responder_url": None,
        }
        result = check_extension_compliance(row)
        assert result["structural_score"] == 0.0
        assert len(result["missing_required"]) == 0
        assert len(result["forbidden_violations"]) == 0

    def test_csca_missing_key_usage(self):
        """CSCA without key_usage should have violations."""
        from app.services.extension_rules_engine import check_extension_compliance
        row = {
            "certificate_type": "CSCA",
            "key_usage": None,
            "subject_key_identifier": "AB:CD:EF",
            "authority_key_identifier": "12:34:56",
            "crl_distribution_points": "http://crl.example.com",
            "is_ca": True,
            "extended_key_usage": None,
            "ocsp_responder_url": None,
        }
        result = check_extension_compliance(row)
        assert result["structural_score"] > 0.0
        assert "key_usage" in result["missing_required"]

    def test_csca_not_ca_triggers_forbidden(self):
        """CSCA with is_ca=False should trigger a forbidden violation."""
        from app.services.extension_rules_engine import check_extension_compliance
        row = {
            "certificate_type": "CSCA",
            "key_usage": "critical, keyCertSign, cRLSign",
            "subject_key_identifier": "AB:CD:EF",
            "authority_key_identifier": None,
            "crl_distribution_points": None,
            "is_ca": False,
            "extended_key_usage": None,
            "ocsp_responder_url": None,
        }
        result = check_extension_compliance(row)
        assert "is_ca" in result["missing_required"] or "is_ca" in result["forbidden_violations"]
        assert result["structural_score"] > 0.0

    def test_compliant_dsc(self):
        """A fully compliant DSC should have low score."""
        from app.services.extension_rules_engine import check_extension_compliance
        row = {
            "certificate_type": "DSC",
            "key_usage": "critical, digitalSignature",
            "authority_key_identifier": "12:34:56",
            "crl_distribution_points": "http://crl.example.com",
            "ocsp_responder_url": "http://ocsp.example.com",
            "is_ca": False,
            "subject_key_identifier": None,
            "extended_key_usage": None,
        }
        result = check_extension_compliance(row)
        assert result["structural_score"] == 0.0

    def test_dsc_with_ca_true_triggers_forbidden(self):
        """DSC with is_ca=True should trigger a forbidden violation."""
        from app.services.extension_rules_engine import check_extension_compliance
        row = {
            "certificate_type": "DSC",
            "key_usage": "critical, digitalSignature",
            "authority_key_identifier": "12:34:56",
            "crl_distribution_points": "http://crl.example.com",
            "ocsp_responder_url": None,
            "is_ca": True,
            "subject_key_identifier": None,
            "extended_key_usage": None,
        }
        result = check_extension_compliance(row)
        assert "is_ca" in result["forbidden_violations"]
        assert result["structural_score"] > 0.0

    def test_score_capped_at_1(self):
        """Structural score should never exceed 1.0."""
        from app.services.extension_rules_engine import check_extension_compliance
        # A certificate missing everything
        row = {
            "certificate_type": "CSCA",
            "key_usage": None,
            "subject_key_identifier": None,
            "authority_key_identifier": None,
            "crl_distribution_points": None,
            "ocsp_responder_url": None,
            "is_ca": False,
            "extended_key_usage": None,
        }
        result = check_extension_compliance(row)
        assert result["structural_score"] <= 1.0


class TestComputeExtensionAnomalies:
    """Test compute_extension_anomalies() with a DataFrame."""

    def test_returns_sorted_by_score(self, sample_certificate_df):
        """Results should be sorted by structural_score descending."""
        from app.services.extension_rules_engine import compute_extension_anomalies

        # Clear cache for clean test
        from app.services.extension_rules_engine import _compliance_cache, _cache_lock
        with _cache_lock:
            _compliance_cache.clear()

        results = compute_extension_anomalies(sample_certificate_df)
        if len(results) > 1:
            scores = [r["structural_score"] for r in results]
            assert scores == sorted(scores, reverse=True)

    def test_empty_dataframe_returns_empty_list(self):
        """Empty DataFrame should return empty results."""
        from app.services.extension_rules_engine import compute_extension_anomalies, _compliance_cache, _cache_lock
        with _cache_lock:
            _compliance_cache.clear()

        empty_df = pd.DataFrame(columns=[
            "fingerprint_sha256", "certificate_type", "country_code",
            "key_usage", "extended_key_usage", "subject_key_identifier",
            "authority_key_identifier", "crl_distribution_points",
            "ocsp_responder_url", "is_ca",
        ])
        results = compute_extension_anomalies(empty_df)
        assert results == []

    def test_cache_returns_same_results(self, sample_certificate_df):
        """Second call with same DataFrame should return cached results."""
        from app.services.extension_rules_engine import compute_extension_anomalies, _compliance_cache, _cache_lock
        with _cache_lock:
            _compliance_cache.clear()

        results1 = compute_extension_anomalies(sample_certificate_df)
        results2 = compute_extension_anomalies(sample_certificate_df)

        # Should be the same list object (from cache)
        assert results1 is results2


class TestComputeAllCompliance:
    """Test _compute_all_compliance() internals."""

    def test_all_compliance_length_matches_df(self, sample_certificate_df):
        """all_compliance list should have one entry per row."""
        from app.services.extension_rules_engine import _compute_all_compliance
        _, all_compliance = _compute_all_compliance(sample_certificate_df)
        assert len(all_compliance) == len(sample_certificate_df)

    def test_all_compliance_entries_have_score(self, sample_certificate_df):
        """Each compliance entry should have a structural_score key."""
        from app.services.extension_rules_engine import _compute_all_compliance
        _, all_compliance = _compute_all_compliance(sample_certificate_df)
        for entry in all_compliance:
            assert "structural_score" in entry
            assert 0.0 <= entry["structural_score"] <= 1.0

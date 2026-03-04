"""Tests for app.services.risk_scorer -- composite risk scoring."""

import numpy as np
import pandas as pd
import pytest


class TestClassifyRisk:
    """Test classify_risk() threshold boundaries."""

    def test_low_range(self):
        from app.services.risk_scorer import classify_risk
        assert classify_risk(0.0) == "LOW"
        assert classify_risk(25.0) == "LOW"
        assert classify_risk(25.9) == "LOW"

    def test_medium_range(self):
        from app.services.risk_scorer import classify_risk
        assert classify_risk(26.0) == "MEDIUM"
        assert classify_risk(50.0) == "MEDIUM"
        assert classify_risk(50.9) == "MEDIUM"

    def test_high_range(self):
        from app.services.risk_scorer import classify_risk
        assert classify_risk(51.0) == "HIGH"
        assert classify_risk(75.0) == "HIGH"
        assert classify_risk(75.9) == "HIGH"

    def test_critical_range(self):
        from app.services.risk_scorer import classify_risk
        assert classify_risk(76.0) == "CRITICAL"
        assert classify_risk(100.0) == "CRITICAL"

    @pytest.mark.parametrize("score,expected", [
        (0, "LOW"),
        (25, "LOW"),
        (26, "MEDIUM"),
        (50, "MEDIUM"),
        (51, "HIGH"),
        (75, "HIGH"),
        (76, "CRITICAL"),
        (100, "CRITICAL"),
    ])
    def test_boundary_values(self, score, expected):
        from app.services.risk_scorer import classify_risk
        assert classify_risk(score) == expected


class TestClassifyForensicRisk:
    """Test classify_forensic_risk() threshold boundaries."""

    @pytest.mark.parametrize("score,expected", [
        (0, "LOW"),
        (19.9, "LOW"),
        (20, "MEDIUM"),
        (39.9, "MEDIUM"),
        (40, "HIGH"),
        (59.9, "HIGH"),
        (60, "CRITICAL"),
        (100, "CRITICAL"),
    ])
    def test_forensic_boundaries(self, score, expected):
        from app.services.risk_scorer import classify_forensic_risk
        assert classify_forensic_risk(score) == expected


class TestAlgorithmRisk:
    """Test _vectorized_algorithm_risk() scores."""

    def _make_df(self, algorithms):
        return pd.DataFrame({"signature_algorithm": algorithms})

    def test_sha1_gets_high_risk(self):
        from app.services.risk_scorer import _vectorized_algorithm_risk
        df = self._make_df(["sha1WithRSAEncryption"])
        result = _vectorized_algorithm_risk(df)
        assert result[0] == 40.0

    def test_sha256_gets_low_risk(self):
        from app.services.risk_scorer import _vectorized_algorithm_risk
        df = self._make_df(["sha256WithRSAEncryption"])
        result = _vectorized_algorithm_risk(df)
        assert result[0] == 5.0

    def test_sha512_gets_zero_risk(self):
        from app.services.risk_scorer import _vectorized_algorithm_risk
        df = self._make_df(["sha512WithRSAEncryption"])
        result = _vectorized_algorithm_risk(df)
        assert result[0] == 0.0

    def test_unknown_algorithm_gets_default_risk(self):
        from app.services.risk_scorer import _vectorized_algorithm_risk
        df = self._make_df(["unknownAlgorithm"])
        result = _vectorized_algorithm_risk(df)
        assert result[0] == 15.0

    def test_none_algorithm_gets_default_risk(self):
        from app.services.risk_scorer import _vectorized_algorithm_risk
        df = self._make_df([None])
        result = _vectorized_algorithm_risk(df)
        assert result[0] == 15.0


class TestKeySizeRisk:
    """Test _vectorized_key_size_risk() scores."""

    def _make_df(self, key_sizes, algorithms):
        return pd.DataFrame({
            "public_key_size": key_sizes,
            "public_key_algorithm": algorithms,
        })

    def test_rsa_1024_gets_max_risk(self):
        from app.services.risk_scorer import _vectorized_key_size_risk
        df = self._make_df([1024], ["rsaEncryption"])
        result = _vectorized_key_size_risk(df)
        assert result[0] == 40.0

    def test_rsa_2048_gets_moderate_risk(self):
        from app.services.risk_scorer import _vectorized_key_size_risk
        df = self._make_df([2048], ["rsaEncryption"])
        result = _vectorized_key_size_risk(df)
        assert result[0] == 10.0

    def test_rsa_4096_gets_zero_risk(self):
        from app.services.risk_scorer import _vectorized_key_size_risk
        df = self._make_df([4096], ["rsaEncryption"])
        result = _vectorized_key_size_risk(df)
        assert result[0] == 0.0

    def test_ec_256_gets_moderate_risk(self):
        from app.services.risk_scorer import _vectorized_key_size_risk
        df = self._make_df([256], ["EC"])
        result = _vectorized_key_size_risk(df)
        assert result[0] == 5.0

    def test_ec_384_gets_zero_risk(self):
        from app.services.risk_scorer import _vectorized_key_size_risk
        df = self._make_df([384], ["EC"])
        result = _vectorized_key_size_risk(df)
        assert result[0] == 0.0

    def test_ec_192_gets_high_risk(self):
        from app.services.risk_scorer import _vectorized_key_size_risk
        df = self._make_df([192], ["EC"])
        result = _vectorized_key_size_risk(df)
        assert result[0] == 35.0


class TestComputeRiskScores:
    """Test the full compute_risk_scores pipeline."""

    def test_output_shapes(self, sample_certificate_df):
        from app.services.risk_scorer import compute_risk_scores
        n = len(sample_certificate_df)
        anomaly_scores = np.random.rand(n)
        structural_scores = np.random.rand(n) * 0.5
        issuer_scores = np.random.rand(n) * 0.3

        risk_scores, risk_factors, forensic_scores, forensic_findings = compute_risk_scores(
            sample_certificate_df, anomaly_scores, structural_scores, issuer_scores,
        )

        assert risk_scores.shape == (n,)
        assert forensic_scores.shape == (n,)
        assert len(risk_factors) == n
        assert len(forensic_findings) == n

    def test_risk_scores_bounded(self, sample_certificate_df):
        """Risk scores must be in [0, 100]."""
        from app.services.risk_scorer import compute_risk_scores
        n = len(sample_certificate_df)
        anomaly_scores = np.ones(n)  # max anomaly

        risk_scores, _, forensic_scores, _ = compute_risk_scores(
            sample_certificate_df, anomaly_scores,
        )

        assert np.all(risk_scores >= 0.0)
        assert np.all(risk_scores <= 100.0)
        assert np.all(forensic_scores >= 0.0)
        assert np.all(forensic_scores <= 100.0)

    def test_zero_anomaly_produces_lower_risk(self, sample_certificate_df):
        """Zero anomaly scores should produce lower risk than high anomaly scores."""
        from app.services.risk_scorer import compute_risk_scores
        n = len(sample_certificate_df)

        zero_scores = np.zeros(n)
        high_scores = np.ones(n)

        risk_low, _, _, _ = compute_risk_scores(sample_certificate_df, zero_scores)
        risk_high, _, _, _ = compute_risk_scores(sample_certificate_df, high_scores)

        # On average, higher anomaly scores should produce higher risk
        assert risk_high.mean() >= risk_low.mean()

    def test_risk_factors_are_dicts(self, sample_certificate_df):
        """Each risk factor entry should be a dictionary."""
        from app.services.risk_scorer import compute_risk_scores
        n = len(sample_certificate_df)
        anomaly_scores = np.random.rand(n)

        _, risk_factors, _, _ = compute_risk_scores(sample_certificate_df, anomaly_scores)
        for factors in risk_factors:
            assert isinstance(factors, dict)

    def test_forensic_findings_structure(self, sample_certificate_df):
        """Forensic findings should contain required keys."""
        from app.services.risk_scorer import compute_risk_scores
        n = len(sample_certificate_df)
        anomaly_scores = np.random.rand(n)

        _, _, _, forensic_findings = compute_risk_scores(sample_certificate_df, anomaly_scores)
        for finding in forensic_findings:
            assert "score" in finding
            assert "level" in finding
            assert "findings" in finding
            assert "categories" in finding
            assert isinstance(finding["categories"], dict)

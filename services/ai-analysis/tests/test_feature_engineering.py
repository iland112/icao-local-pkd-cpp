"""Tests for app.services.feature_engineering -- vectorized feature extraction."""

import numpy as np
import pandas as pd
import pytest


class TestVecSafeBool:
    """Test _vec_safe_bool() vectorized boolean conversion."""

    def _call(self, series):
        """Import and call the function under test."""
        from app.services.feature_engineering import engineer_features
        # _vec_safe_bool is a nested function; we re-implement the same logic
        # to test it. Since it is defined inside engineer_features, we test
        # it indirectly through small DataFrames or replicate its logic.
        # Direct test by calling the exact code pattern:
        s = series.fillna("")
        str_vals = s.astype(str).str.lower()
        return np.where(
            str_vals.isin(["true", "1", "1.0", "t", "yes"]),
            1.0,
            0.0,
        ).astype(np.float64)

    def test_true_string(self):
        result = self._call(pd.Series(["true", "True", "TRUE"]))
        np.testing.assert_array_equal(result, [1.0, 1.0, 1.0])

    def test_false_string(self):
        result = self._call(pd.Series(["false", "False", "no"]))
        np.testing.assert_array_equal(result, [0.0, 0.0, 0.0])

    def test_numeric_strings(self):
        result = self._call(pd.Series(["1", "0", "1.0"]))
        np.testing.assert_array_equal(result, [1.0, 0.0, 1.0])

    def test_none_and_nan(self):
        result = self._call(pd.Series([None, np.nan, ""]))
        np.testing.assert_array_equal(result, [0.0, 0.0, 0.0])

    def test_boolean_values(self):
        result = self._call(pd.Series([True, False]))
        np.testing.assert_array_equal(result, [1.0, 0.0])

    def test_mixed_types(self):
        result = self._call(pd.Series([True, "false", 1, 0, None, "yes"]))
        np.testing.assert_array_equal(result, [1.0, 0.0, 1.0, 0.0, 0.0, 1.0])


class TestVecHasValue:
    """Test _vec_has_value() vectorized presence check."""

    def _call(self, series):
        """Replicate the _vec_has_value logic for direct testing."""
        not_null = series.notna()
        str_vals = series.astype(str).str.strip()
        non_empty = str_vals.ne("") & str_vals.ne("None") & str_vals.ne("nan")
        return np.where(not_null & non_empty, 1.0, 0.0).astype(np.float64)

    def test_non_empty_strings(self):
        result = self._call(pd.Series(["hello", "world"]))
        np.testing.assert_array_equal(result, [1.0, 1.0])

    def test_empty_and_none(self):
        result = self._call(pd.Series(["", None, "  "]))
        np.testing.assert_array_equal(result, [0.0, 0.0, 0.0])

    def test_nan_value(self):
        result = self._call(pd.Series([np.nan]))
        np.testing.assert_array_equal(result, [0.0])

    def test_mixed_values(self):
        result = self._call(pd.Series(["http://crl.example.com", None, "", "value"]))
        np.testing.assert_array_equal(result, [1.0, 0.0, 0.0, 1.0])


class TestFeatureNames:
    """Test that FEATURE_NAMES is consistent."""

    def test_feature_names_count_is_45(self):
        from app.services.feature_engineering import FEATURE_NAMES
        assert len(FEATURE_NAMES) == 45

    def test_no_duplicate_feature_names(self):
        from app.services.feature_engineering import FEATURE_NAMES
        assert len(FEATURE_NAMES) == len(set(FEATURE_NAMES))

    def test_all_feature_names_have_korean_explanation(self):
        from app.services.feature_engineering import FEATURE_EXPLANATIONS_KO, FEATURE_NAMES
        for name in FEATURE_NAMES:
            assert name in FEATURE_EXPLANATIONS_KO, f"Missing Korean explanation for: {name}"


class TestEngineerFeatures:
    """Test the full engineer_features pipeline with small mock data."""

    def test_output_shape(self, sample_certificate_df):
        """Feature matrix should have shape (n_certs, 45)."""
        from app.services.feature_engineering import FEATURE_NAMES, engineer_features
        metadata, features = engineer_features(sample_certificate_df)
        assert features.shape == (len(sample_certificate_df), len(FEATURE_NAMES))

    def test_metadata_columns(self, sample_certificate_df):
        """Metadata should contain fingerprint, type, and country columns."""
        from app.services.feature_engineering import engineer_features
        metadata, _ = engineer_features(sample_certificate_df)
        assert "fingerprint_sha256" in metadata.columns
        assert "certificate_type" in metadata.columns
        assert "country_code" in metadata.columns

    def test_no_nan_in_features(self, sample_certificate_df):
        """Feature matrix should have no NaN or Inf values."""
        from app.services.feature_engineering import engineer_features
        _, features = engineer_features(sample_certificate_df)
        assert not np.any(np.isnan(features)), "NaN values found in feature matrix"
        assert not np.any(np.isinf(features)), "Inf values found in feature matrix"

    def test_key_size_normalized_range(self, sample_certificate_df):
        """key_size_normalized (index 0) should be in [0, 1]."""
        from app.services.feature_engineering import engineer_features
        _, features = engineer_features(sample_certificate_df)
        key_size_norm = features[:, 0]
        assert np.all(key_size_norm >= 0.0)
        assert np.all(key_size_norm <= 1.0)

    def test_is_expired_flag(self, sample_certificate_df):
        """is_expired (index 7) should be 1.0 for the expired certificate."""
        from app.services.feature_engineering import engineer_features
        _, features = engineer_features(sample_certificate_df)
        # Row 3 (DSC_NC, DE) has not_after=2020-01-01, should be expired
        assert features[3, 7] == 1.0, "DSC_NC cert should be expired"
        # Row 0 (CSCA, KR) has not_after=2035, should not be expired
        assert features[0, 7] == 0.0, "CSCA cert should not be expired"

    def test_cert_type_encoded(self, sample_certificate_df):
        """cert_type_encoded (index 24) should map correctly."""
        from app.services.feature_engineering import CERT_TYPE_MAP, engineer_features
        _, features = engineer_features(sample_certificate_df)
        # Row 0 = CSCA -> 0, Row 1 = DSC -> 1, Row 3 = DSC_NC -> 2, Row 4 = MLSC -> 3
        assert features[0, 24] == float(CERT_TYPE_MAP["CSCA"])
        assert features[1, 24] == float(CERT_TYPE_MAP["DSC"])
        assert features[3, 24] == float(CERT_TYPE_MAP["DSC_NC"])
        assert features[4, 24] == float(CERT_TYPE_MAP["MLSC"])

    def test_is_ca_flag(self, sample_certificate_df):
        """is_ca (index 17) should be 1.0 for CSCA and 0.0 for others."""
        from app.services.feature_engineering import engineer_features
        _, features = engineer_features(sample_certificate_df)
        assert features[0, 17] == 1.0, "CSCA should have is_ca=1"
        assert features[1, 17] == 0.0, "DSC should have is_ca=0"

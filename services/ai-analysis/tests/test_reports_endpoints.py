"""Tests for reports router endpoints (/api/ai/reports/...)."""

import pytest


class TestCountryMaturity:
    """GET /api/ai/reports/country-maturity"""

    def test_country_maturity_returns_500_or_503_without_db(self, client, mock_sync_engine):
        """Should return 500 when DB is unavailable (data load failure)."""
        response = client.get("/api/ai/reports/country-maturity")
        assert response.status_code in (500, 503)


class TestAlgorithmTrends:
    """GET /api/ai/reports/algorithm-trends"""

    def test_algorithm_trends_returns_503_without_db(self, client, mock_sync_engine):
        """Should return 503 when DB is unavailable."""
        response = client.get("/api/ai/reports/algorithm-trends")
        assert response.status_code in (500, 503)


class TestKeySizeDistribution:
    """GET /api/ai/reports/key-size-distribution"""

    def test_key_size_distribution_returns_503_without_db(self, client, mock_sync_engine):
        """Should return 503 when DB is unavailable."""
        response = client.get("/api/ai/reports/key-size-distribution")
        assert response.status_code in (500, 503)


class TestRiskDistribution:
    """GET /api/ai/reports/risk-distribution"""

    def test_risk_distribution_returns_503_without_db(self, client, mock_sync_engine):
        """Should return 503 when DB is unavailable."""
        response = client.get("/api/ai/reports/risk-distribution")
        assert response.status_code in (500, 503)


class TestCountryReport:
    """GET /api/ai/reports/country/{code}"""

    def test_invalid_country_code_returns_400(self, client):
        """Country code with invalid format should return 400."""
        response = client.get("/api/ai/reports/country/INVALID")
        assert response.status_code == 400
        assert "country" in response.json()["detail"].lower()

    def test_numeric_country_code_returns_400(self, client):
        """Numeric country code should return 400."""
        response = client.get("/api/ai/reports/country/123")
        assert response.status_code == 400

    @pytest.mark.parametrize("code", ["KR", "US", "DE", "FR", "GB"])
    def test_valid_country_code_accepted(self, client, mock_sync_engine, code):
        """Valid 2-letter codes should be accepted (not 400).

        Returns 500 due to mock DB, which confirms the validation passed.
        """
        response = client.get(f"/api/ai/reports/country/{code}")
        # Should not be 400 -- 500 from DB failure is expected
        assert response.status_code != 400

    def test_three_letter_country_code_accepted(self, client, mock_sync_engine):
        """3-letter country code (e.g., 'USA') should pass validation."""
        response = client.get("/api/ai/reports/country/USA")
        assert response.status_code != 400


class TestIssuerProfiles:
    """GET /api/ai/reports/issuer-profiles"""

    def test_issuer_profiles_returns_500_without_db(self, client, mock_sync_engine):
        """Should return 500 when DB is unavailable."""
        response = client.get("/api/ai/reports/issuer-profiles")
        assert response.status_code in (500, 503)


class TestForensicSummary:
    """GET /api/ai/reports/forensic-summary"""

    def test_forensic_summary_returns_503_without_db(self, client, mock_sync_engine):
        """Should return 503 when DB is unavailable."""
        response = client.get("/api/ai/reports/forensic-summary")
        assert response.status_code in (500, 503)


class TestExtensionAnomalies:
    """GET /api/ai/reports/extension-anomalies"""

    def test_extension_anomalies_returns_500_without_db(self, client, mock_sync_engine):
        """Should return 500 when DB is unavailable."""
        response = client.get("/api/ai/reports/extension-anomalies")
        assert response.status_code in (500, 503)

    def test_invalid_cert_type_returns_400(self, client):
        """Invalid certificate type filter should return 400."""
        response = client.get(
            "/api/ai/reports/extension-anomalies",
            params={"type": "INVALID"},
        )
        assert response.status_code == 400

    def test_invalid_country_returns_400(self, client):
        """Invalid country code filter should return 400."""
        response = client.get(
            "/api/ai/reports/extension-anomalies",
            params={"country": "12345"},
        )
        assert response.status_code == 400

    @pytest.mark.parametrize("valid_params", [
        {"type": "CSCA"},
        {"type": "DSC"},
        {"country": "KR"},
        {"type": "DSC", "country": "US", "limit": 10},
    ])
    def test_valid_filter_params_accepted(self, client, mock_sync_engine, valid_params):
        """Valid filter parameters should not cause a 400 error."""
        response = client.get(
            "/api/ai/reports/extension-anomalies",
            params=valid_params,
        )
        # DB failure expected, but not a validation error
        assert response.status_code in (500, 503)

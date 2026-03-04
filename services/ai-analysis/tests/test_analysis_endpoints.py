"""Tests for analysis router endpoints (/api/ai/analyze, /anomalies, /statistics, etc.)."""

from unittest.mock import MagicMock, patch

import pytest


@pytest.fixture(autouse=True)
def _reset_job_status():
    """Reset global _job_status to IDLE before and after each test."""
    from app.routers.analysis import _job_lock, _job_status

    with _job_lock:
        _job_status["status"] = "IDLE"
        _job_status["progress"] = 0.0
        _job_status["total_certificates"] = 0
        _job_status["processed_certificates"] = 0
        _job_status["started_at"] = None
        _job_status["completed_at"] = None
        _job_status["error_message"] = None

    yield

    with _job_lock:
        _job_status["status"] = "IDLE"
        _job_status["progress"] = 0.0
        _job_status["error_message"] = None


class TestTriggerAnalysis:
    """POST /api/ai/analyze -- trigger full analysis."""

    def test_trigger_analysis_returns_success(self, client):
        """Triggering analysis should return 200 with success=True."""
        with patch("app.routers.analysis.threading.Thread") as mock_thread_cls:
            mock_thread = MagicMock()
            mock_thread_cls.return_value = mock_thread

            response = client.post("/api/ai/analyze")
            assert response.status_code == 200

            data = response.json()
            assert data["success"] is True
            assert "started" in data["message"].lower() or "Analysis" in data["message"]

            # Verify a background thread was started
            mock_thread.start.assert_called_once()

    def test_trigger_analysis_conflict_when_running(self, client):
        """Triggering analysis while already running should return 409."""
        from app.routers.analysis import _job_lock, _job_status

        with _job_lock:
            _job_status["status"] = "RUNNING"

        response = client.post("/api/ai/analyze")
        assert response.status_code == 409
        assert "already running" in response.json()["detail"].lower()


class TestIncrementalAnalysis:
    """POST /api/ai/analyze/incremental -- trigger incremental analysis."""

    def test_incremental_analysis_with_upload_id(self, client):
        """Incremental analysis should accept an upload_id and return success."""
        with patch("app.routers.analysis.threading.Thread") as mock_thread_cls:
            mock_thread = MagicMock()
            mock_thread_cls.return_value = mock_thread

            response = client.post(
                "/api/ai/analyze/incremental",
                params={"upload_id": "test-upload-123"},
            )
            assert response.status_code == 200

            data = response.json()
            assert data["success"] is True
            assert data["upload_id"] == "test-upload-123"
            mock_thread.start.assert_called_once()

    def test_incremental_analysis_without_upload_id(self, client):
        """Incremental analysis without upload_id should still succeed."""
        with patch("app.routers.analysis.threading.Thread") as mock_thread_cls:
            mock_thread = MagicMock()
            mock_thread_cls.return_value = mock_thread

            response = client.post("/api/ai/analyze/incremental")
            assert response.status_code == 200
            assert response.json()["success"] is True

    def test_incremental_analysis_conflict_when_running(self, client):
        """Should return 409 if analysis is already running."""
        from app.routers.analysis import _job_lock, _job_status
        with _job_lock:
            _job_status["status"] = "RUNNING"

        response = client.post("/api/ai/analyze/incremental")
        assert response.status_code == 409


class TestAnalysisStatus:
    """GET /api/ai/analyze/status -- get job status."""

    def test_status_returns_idle_initially(self, client):
        """Status should return IDLE when no analysis has been triggered."""
        response = client.get("/api/ai/analyze/status")
        assert response.status_code == 200

        data = response.json()
        assert data["status"] == "IDLE"
        assert data["progress"] == 0.0
        assert data["total_certificates"] >= 0
        assert data["processed_certificates"] >= 0

    def test_status_structure_has_required_fields(self, client):
        """Status response must contain all AnalysisJobStatus fields."""
        response = client.get("/api/ai/analyze/status")
        assert response.status_code == 200

        data = response.json()
        required_fields = [
            "status", "progress", "total_certificates",
            "processed_certificates", "started_at", "completed_at",
            "error_message",
        ]
        for field in required_fields:
            assert field in data, f"Missing field: {field}"


class TestCertificateAnalysis:
    """GET /api/ai/certificate/{fingerprint} -- get per-certificate result."""

    def test_invalid_fingerprint_returns_400(self, client):
        """Non-hex or wrong-length fingerprint should return 400."""
        response = client.get("/api/ai/certificate/not-a-valid-fingerprint")
        assert response.status_code == 400
        assert "fingerprint" in response.json()["detail"].lower()

    def test_short_fingerprint_returns_400(self, client):
        """Fingerprint shorter than 64 hex chars should be rejected."""
        response = client.get("/api/ai/certificate/abcdef1234")
        assert response.status_code == 400

    def test_valid_fingerprint_not_found_returns_404_or_503(self, client, mock_sync_engine):
        """Valid fingerprint format but no data should return 404 or 503 (no DB)."""
        fingerprint = "a" * 64
        response = client.get(f"/api/ai/certificate/{fingerprint}")
        # 503 because mock_sync_engine raises on connect
        assert response.status_code in (404, 503)

    @pytest.mark.parametrize("bad_fp", [
        "ZZZZ" + "a" * 60,  # non-hex chars
        "a" * 63,            # 63 chars (too short)
        "a" * 65,            # 65 chars (too long)
    ])
    def test_various_invalid_fingerprints(self, client, bad_fp):
        """Various malformed fingerprints should all return 400."""
        response = client.get(f"/api/ai/certificate/{bad_fp}")
        assert response.status_code == 400


class TestCertificateForensic:
    """GET /api/ai/certificate/{fingerprint}/forensic -- forensic detail."""

    def test_invalid_fingerprint_returns_400(self, client):
        """Invalid fingerprint format should return 400."""
        response = client.get("/api/ai/certificate/invalid/forensic")
        assert response.status_code == 400

    def test_valid_fingerprint_no_data_returns_404_or_503(self, client, mock_sync_engine):
        """Valid fingerprint with no DB data returns 404 or 503."""
        fingerprint = "b" * 64
        response = client.get(f"/api/ai/certificate/{fingerprint}/forensic")
        assert response.status_code in (404, 503)


class TestAnomalies:
    """GET /api/ai/anomalies -- list anomalies with filters."""

    def test_anomalies_returns_503_without_db(self, client, mock_sync_engine):
        """Anomaly list should return 503 when DB is unavailable."""
        response = client.get("/api/ai/anomalies")
        assert response.status_code == 503

    def test_anomalies_invalid_country_returns_400(self, client):
        """Invalid country code should be rejected."""
        response = client.get("/api/ai/anomalies", params={"country": "INVALID"})
        assert response.status_code == 400

    def test_anomalies_invalid_cert_type_returns_400(self, client):
        """Invalid certificate type should be rejected."""
        response = client.get("/api/ai/anomalies", params={"type": "INVALID_TYPE"})
        assert response.status_code == 400

    def test_anomalies_invalid_risk_level_returns_400(self, client):
        """Invalid risk level should be rejected."""
        response = client.get("/api/ai/anomalies", params={"risk_level": "EXTREME"})
        assert response.status_code == 400

    def test_anomalies_invalid_label_returns_400(self, client):
        """Invalid anomaly label should be rejected."""
        response = client.get("/api/ai/anomalies", params={"label": "UNKNOWN_LABEL"})
        assert response.status_code == 400

    @pytest.mark.parametrize("valid_params", [
        {"country": "KR"},
        {"type": "DSC"},
        {"risk_level": "HIGH"},
        {"label": "ANOMALOUS"},
        {"page": 2, "size": 10},
    ])
    def test_anomalies_valid_params_accepted(self, client, mock_sync_engine, valid_params):
        """Valid filter parameters should be accepted (503 from mock DB, not 400)."""
        response = client.get("/api/ai/anomalies", params=valid_params)
        # Should not be 400 (validation error) -- 503 from mock DB is expected
        assert response.status_code == 503


class TestStatistics:
    """GET /api/ai/statistics -- get overall statistics."""

    def test_statistics_returns_503_without_db(self, client, mock_sync_engine):
        """Statistics should return 503 when DB is unavailable."""
        response = client.get("/api/ai/statistics")
        assert response.status_code == 503

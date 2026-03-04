"""Basic health endpoint tests."""


def test_health_returns_200(client):
    """Health endpoint should return 200 with expected fields."""
    response = client.get("/api/ai/health")
    assert response.status_code == 200

    data = response.json()
    assert data["status"] == "healthy"
    assert "service" in data
    assert "version" in data
    assert "db_type" in data
    assert "analysis_enabled" in data
    assert isinstance(data["db_connected"], bool)

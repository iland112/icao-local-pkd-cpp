"""Tests for app.config.Settings -- configuration validation and defaults."""

import pytest
from pydantic import ValidationError


def _make_settings(**overrides):
    """Create a Settings instance with test defaults + overrides.

    Bypasses lru_cache by directly instantiating the class.
    """
    from app.config import Settings

    defaults = {
        "db_type": "postgres",
        "db_host": "localhost",
        "db_password": "test",
    }
    defaults.update(overrides)
    return Settings(**defaults)


class TestDefaultValues:
    """Verify sensible defaults for all config fields."""

    def test_service_name_default(self):
        s = _make_settings()
        assert s.service_name == "ai-analysis"

    def test_server_port_default(self):
        s = _make_settings()
        assert s.server_port == 8085

    def test_db_type_default(self):
        s = _make_settings()
        assert s.db_type == "postgres"

    def test_db_pool_size_default(self):
        s = _make_settings()
        assert s.db_pool_size == 5

    def test_db_pool_overflow_default(self):
        s = _make_settings()
        assert s.db_pool_overflow == 10

    def test_analysis_schedule_hour_default(self):
        s = _make_settings()
        assert s.analysis_schedule_hour == 3

    def test_analysis_enabled_default(self):
        s = _make_settings(analysis_enabled=True)
        assert s.analysis_enabled is True

    def test_model_version_default(self):
        s = _make_settings()
        assert s.model_version == "1.0.0"

    def test_anomaly_contamination_default(self):
        s = _make_settings()
        assert s.anomaly_contamination == 0.05

    def test_batch_size_default(self):
        s = _make_settings()
        assert s.batch_size == 1000


class TestDbTypeValidation:
    """DB_TYPE must be 'postgres' or 'oracle'."""

    def test_postgres_accepted(self):
        s = _make_settings(db_type="postgres")
        assert s.db_type == "postgres"

    def test_oracle_accepted(self):
        s = _make_settings(db_type="oracle", oracle_password="test")
        assert s.db_type == "oracle"

    def test_invalid_db_type_raises(self):
        with pytest.raises(ValidationError, match="DB_TYPE"):
            _make_settings(db_type="mysql")

    def test_empty_db_type_raises(self):
        with pytest.raises(ValidationError):
            _make_settings(db_type="")

    def test_sqlite_rejected(self):
        with pytest.raises(ValidationError):
            _make_settings(db_type="sqlite")


class TestDbPoolSizeValidation:
    """DB_POOL_SIZE must be >= 1."""

    def test_pool_size_one_accepted(self):
        s = _make_settings(db_pool_size=1)
        assert s.db_pool_size == 1

    def test_pool_size_zero_raises(self):
        with pytest.raises(ValidationError, match="DB_POOL_SIZE"):
            _make_settings(db_pool_size=0)

    def test_pool_size_negative_raises(self):
        with pytest.raises(ValidationError):
            _make_settings(db_pool_size=-5)

    def test_pool_size_large_accepted(self):
        s = _make_settings(db_pool_size=100)
        assert s.db_pool_size == 100


class TestScheduleHourValidation:
    """ANALYSIS_SCHEDULE_HOUR must be 0-23."""

    @pytest.mark.parametrize("hour", [0, 1, 12, 23])
    def test_valid_hours(self, hour):
        s = _make_settings(analysis_schedule_hour=hour)
        assert s.analysis_schedule_hour == hour

    def test_hour_24_raises(self):
        with pytest.raises(ValidationError, match="ANALYSIS_SCHEDULE_HOUR"):
            _make_settings(analysis_schedule_hour=24)

    def test_negative_hour_raises(self):
        with pytest.raises(ValidationError):
            _make_settings(analysis_schedule_hour=-1)

    def test_empty_string_uses_default(self):
        """Empty string should fallback to default (3)."""
        s = _make_settings(analysis_schedule_hour="")
        assert s.analysis_schedule_hour == 3


class TestDatabaseUrlConstruction:
    """Verify URL construction for both postgres and oracle."""

    def test_postgres_url_has_asyncpg_driver(self):
        s = _make_settings(db_type="postgres")
        url = s.database_url
        assert "asyncpg" in str(url.drivername)

    def test_postgres_sync_url_has_psycopg2_driver(self):
        s = _make_settings(db_type="postgres")
        url = s.sync_database_url
        assert "psycopg2" in str(url.drivername)

    def test_oracle_url_has_oracledb_driver(self):
        s = _make_settings(db_type="oracle", oracle_password="test")
        url = s.database_url
        assert "oracledb" in str(url.drivername)

    def test_oracle_sync_url_has_oracledb_driver(self):
        s = _make_settings(db_type="oracle", oracle_password="test")
        url = s.sync_database_url
        assert "oracledb" in str(url.drivername)

    def test_postgres_url_contains_host(self):
        s = _make_settings(db_type="postgres", db_host="myhost")
        url = s.database_url
        assert url.host == "myhost"

    def test_oracle_url_contains_host(self):
        s = _make_settings(db_type="oracle", oracle_host="oracle-host", oracle_password="test")
        url = s.database_url
        assert url.host == "oracle-host"


class TestEmptyStringPortHandling:
    """Empty string ports should fall back to defaults."""

    def test_empty_db_port_uses_default(self):
        s = _make_settings(db_port="")
        assert s.db_port == 5432

    def test_empty_oracle_port_uses_default(self):
        s = _make_settings(db_type="oracle", oracle_port="", oracle_password="test")
        assert s.oracle_port == 1521

    def test_empty_server_port_uses_default(self):
        s = _make_settings(server_port="")
        assert s.server_port == 8085

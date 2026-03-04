import logging
from functools import lru_cache
from typing import Any

from pydantic import field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict
from sqlalchemy.engine import URL

logger = logging.getLogger(__name__)


class Settings(BaseSettings):
    # Service
    service_name: str = "ai-analysis"
    server_port: int = 8085

    # Database Type Selection (postgres or oracle)
    db_type: str = "postgres"

    # PostgreSQL Configuration
    db_host: str = "postgres"
    db_port: int = 5432
    db_name: str = "localpkd"
    db_user: str = "pkd"
    db_password: str = ""

    # Oracle Configuration
    oracle_host: str = "oracle"
    oracle_port: int = 1521
    oracle_service_name: str = "XEPDB1"
    oracle_user: str = "pkd_user"
    oracle_password: str = ""

    # Connection Pool Configuration
    db_pool_size: int = 5
    db_pool_overflow: int = 10

    # Analysis Scheduler
    analysis_schedule_hour: int = 3
    analysis_enabled: bool = True

    @field_validator("oracle_port", "db_port", "server_port", "analysis_schedule_hour", mode="before")
    @classmethod
    def empty_str_to_default(cls, v: Any, info: Any) -> Any:
        if isinstance(v, str) and v.strip() == "":
            defaults = {"oracle_port": 1521, "db_port": 5432, "server_port": 8085, "analysis_schedule_hour": 3}
            return defaults.get(info.field_name, 0)
        return v

    @field_validator("db_type", mode="after")
    @classmethod
    def validate_db_type(cls, v: str) -> str:
        if v not in ("postgres", "oracle"):
            raise ValueError(f"DB_TYPE must be 'postgres' or 'oracle', got '{v}'")
        return v

    @field_validator("db_pool_size", mode="after")
    @classmethod
    def validate_pool_size(cls, v: int) -> int:
        if v < 1:
            raise ValueError(f"DB_POOL_SIZE must be >= 1, got {v}")
        return v

    @field_validator("analysis_schedule_hour", mode="after")
    @classmethod
    def validate_schedule_hour(cls, v: int) -> int:
        if not (0 <= v <= 23):
            raise ValueError(f"ANALYSIS_SCHEDULE_HOUR must be 0-23, got {v}")
        return v

    # Model Configuration
    model_version: str = "1.0.0"
    anomaly_contamination: float = 0.05
    lof_neighbors: int = 20
    batch_size: int = 1000

    model_config = SettingsConfigDict(env_file=".env", case_sensitive=False)

    @property
    def database_url(self) -> URL:
        """Async database URL. Password is masked in repr/logs via URL.create()."""
        if self.db_type == "oracle":
            return URL.create(
                drivername="oracle+oracledb",
                username=self.oracle_user,
                password=self.oracle_password,
                host=self.oracle_host,
                port=self.oracle_port,
                query={"service_name": self.oracle_service_name},
            )
        return URL.create(
            drivername="postgresql+asyncpg",
            username=self.db_user,
            password=self.db_password,
            host=self.db_host,
            port=self.db_port,
            database=self.db_name,
        )

    @property
    def sync_database_url(self) -> URL:
        """Synchronous URL for pandas read_sql and batch operations.

        Password is masked in repr/logs via URL.create().
        """
        if self.db_type == "oracle":
            return URL.create(
                drivername="oracle+oracledb",
                username=self.oracle_user,
                password=self.oracle_password,
                host=self.oracle_host,
                port=self.oracle_port,
                query={"service_name": self.oracle_service_name},
            )
        return URL.create(
            drivername="postgresql+psycopg2",
            username=self.db_user,
            password=self.db_password,
            host=self.db_host,
            port=self.db_port,
            database=self.db_name,
        )


@lru_cache
def get_settings() -> Settings:
    return Settings()

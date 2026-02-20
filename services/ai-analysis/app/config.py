import logging
from functools import lru_cache

from pydantic_settings import BaseSettings

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

    # Analysis Scheduler
    analysis_schedule_hour: int = 3
    analysis_enabled: bool = True

    # Model Configuration
    model_version: str = "1.0.0"
    anomaly_contamination: float = 0.05
    lof_neighbors: int = 20
    batch_size: int = 1000

    class Config:
        env_file = ".env"
        case_sensitive = False

    @property
    def database_url(self) -> str:
        if self.db_type == "oracle":
            return (
                f"oracle+cx_oracle://{self.oracle_user}:{self.oracle_password}"
                f"@{self.oracle_host}:{self.oracle_port}"
                f"/?service_name={self.oracle_service_name}"
            )
        return (
            f"postgresql+asyncpg://{self.db_user}:{self.db_password}"
            f"@{self.db_host}:{self.db_port}/{self.db_name}"
        )

    @property
    def sync_database_url(self) -> str:
        """Synchronous URL for pandas read_sql and batch operations."""
        if self.db_type == "oracle":
            return (
                f"oracle+cx_oracle://{self.oracle_user}:{self.oracle_password}"
                f"@{self.oracle_host}:{self.oracle_port}"
                f"/?service_name={self.oracle_service_name}"
            )
        return (
            f"postgresql+psycopg2://{self.db_user}:{self.db_password}"
            f"@{self.db_host}:{self.db_port}/{self.db_name}"
        )


@lru_cache
def get_settings() -> Settings:
    return Settings()

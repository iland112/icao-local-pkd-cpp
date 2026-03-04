from pydantic import BaseModel


class HealthResponse(BaseModel):
    status: str
    service: str
    version: str
    db_type: str
    analysis_enabled: bool
    db_connected: bool = False


class DbPoolMetrics(BaseModel):
    available: int
    total: int
    max: int


class InternalMetricsResponse(BaseModel):
    service: str
    timestamp: str
    dbPool: DbPoolMetrics

from pydantic import BaseModel


class HealthResponse(BaseModel):
    status: str
    service: str
    version: str
    db_type: str
    analysis_enabled: bool

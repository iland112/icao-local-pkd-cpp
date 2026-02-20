"""SQLAlchemy model for ai_analysis_result table (READ-WRITE)."""

from sqlalchemy import Column, DateTime, Float, String, Text, func
from sqlalchemy.dialects.postgresql import JSONB

from app.database import Base


class AnalysisResult(Base):
    """AI analysis results per certificate."""

    __tablename__ = "ai_analysis_result"

    id = Column(String(128), primary_key=True)
    certificate_fingerprint = Column(String(128), unique=True, nullable=False)
    certificate_type = Column(String(20))
    country_code = Column(String(10))

    # Anomaly Scores
    anomaly_score = Column(Float)
    anomaly_label = Column(String(20))  # NORMAL / SUSPICIOUS / ANOMALOUS
    isolation_forest_score = Column(Float)
    lof_score = Column(Float)

    # Risk Scoring
    risk_score = Column(Float)
    risk_level = Column(String(20))  # LOW / MEDIUM / HIGH / CRITICAL
    risk_factors = Column(JSONB)

    # Analysis Metadata
    feature_vector = Column(JSONB)
    anomaly_explanations = Column(JSONB)
    analysis_version = Column(String(20))
    analyzed_at = Column(DateTime(timezone=True), server_default=func.now())

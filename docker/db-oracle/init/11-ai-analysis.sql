-- =============================================================================
-- AI Analysis Engine Schema for Oracle (v2.19.0)
-- ML-based certificate anomaly detection, pattern analysis, forensic scoring
-- =============================================================================

CONNECT pkd_user/pkd_password@ORCLPDB1;
SET SQLBLANKLINES ON;

-- Allow re-runs (skip "already exists" errors)
WHENEVER SQLERROR CONTINUE;

-- AI Analysis Results (per-certificate anomaly scores and risk assessment)
CREATE TABLE ai_analysis_result (
    id VARCHAR2(128) PRIMARY KEY,
    certificate_fingerprint VARCHAR2(128) NOT NULL,
    certificate_type VARCHAR2(20),
    country_code VARCHAR2(10),

    -- Anomaly Scores
    anomaly_score NUMBER(10,6),
    anomaly_label VARCHAR2(20),
    isolation_forest_score NUMBER(10,6),
    lof_score NUMBER(10,6),

    -- Risk Scoring
    risk_score NUMBER(5,2),
    risk_level VARCHAR2(20),
    risk_factors CLOB,

    -- Forensic Scores (v2.19.0)
    forensic_risk_score NUMBER(5,2),
    forensic_risk_level VARCHAR2(20),
    forensic_findings CLOB,
    structural_anomaly_score NUMBER(5,4),
    issuer_anomaly_score NUMBER(5,4),
    temporal_anomaly_score NUMBER(5,4),

    -- Analysis Metadata
    feature_vector CLOB,
    anomaly_explanations CLOB,
    analysis_version VARCHAR2(20),
    analyzed_at TIMESTAMP WITH TIME ZONE DEFAULT SYSTIMESTAMP,

    -- Constraints
    CONSTRAINT uq_ai_cert_fp UNIQUE (certificate_fingerprint)
);

-- Performance indexes
CREATE INDEX idx_ai_anomaly ON ai_analysis_result(anomaly_label);
CREATE INDEX idx_ai_risk ON ai_analysis_result(risk_level);
CREATE INDEX idx_ai_country ON ai_analysis_result(country_code);
CREATE INDEX idx_ai_score ON ai_analysis_result(anomaly_score DESC);
CREATE INDEX idx_ai_risk_score ON ai_analysis_result(risk_score DESC);
CREATE INDEX idx_ai_forensic ON ai_analysis_result(forensic_risk_level);
CREATE INDEX idx_ai_forensic_score ON ai_analysis_result(forensic_risk_score DESC);

COMMIT;

EXIT;

-- =============================================================================
-- AI Analysis Engine Schema (v2.18.0)
-- ML-based certificate anomaly detection and pattern analysis results
-- =============================================================================

-- AI Analysis Results (per-certificate anomaly scores and risk assessment)
CREATE TABLE IF NOT EXISTS ai_analysis_result (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    certificate_fingerprint VARCHAR(128) NOT NULL,
    certificate_type VARCHAR(20),
    country_code VARCHAR(10),

    -- Anomaly Scores
    anomaly_score FLOAT,                            -- 0.0 (normal) ~ 1.0 (highly anomalous)
    anomaly_label VARCHAR(20),                      -- NORMAL / SUSPICIOUS / ANOMALOUS
    isolation_forest_score FLOAT,                   -- Isolation Forest normalized score
    lof_score FLOAT,                                -- Local Outlier Factor normalized score

    -- Risk Scoring
    risk_score FLOAT,                               -- 0 ~ 100 composite risk score
    risk_level VARCHAR(20),                         -- LOW / MEDIUM / HIGH / CRITICAL
    risk_factors JSONB,                             -- {"algorithm": 40, "key_size": 10, ...}

    -- Analysis Metadata
    feature_vector JSONB,                           -- Stored features for explainability
    anomaly_explanations JSONB,                     -- Top contributing feature explanations
    analysis_version VARCHAR(20),                   -- Model version tracking
    analyzed_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    -- Constraints
    CONSTRAINT uq_ai_cert_fingerprint UNIQUE (certificate_fingerprint)
);

-- Performance indexes
CREATE INDEX IF NOT EXISTS idx_ai_analysis_anomaly ON ai_analysis_result(anomaly_label);
CREATE INDEX IF NOT EXISTS idx_ai_analysis_risk ON ai_analysis_result(risk_level);
CREATE INDEX IF NOT EXISTS idx_ai_analysis_country ON ai_analysis_result(country_code);
CREATE INDEX IF NOT EXISTS idx_ai_analysis_score ON ai_analysis_result(anomaly_score DESC);
CREATE INDEX IF NOT EXISTS idx_ai_analysis_risk_score ON ai_analysis_result(risk_score DESC);

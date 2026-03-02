-- =============================================================================
-- v2.25.8 Index Optimization Migration (PostgreSQL)
-- Composite indexes for multi-column queries
-- Safe for re-runs: uses CREATE INDEX IF NOT EXISTS
-- =============================================================================

-- Certificate: Reconciliation (stored_in_ldap + created_at ordering)
CREATE INDEX IF NOT EXISTS idx_certificate_stored_created
  ON certificate(stored_in_ldap, created_at ASC);

-- Certificate: Country statistics (GROUP BY country_code, certificate_type)
CREATE INDEX IF NOT EXISTS idx_certificate_country_type
  ON certificate(country_code, certificate_type);

-- Certificate: CSCA lookup (WHERE type='CSCA' ORDER BY created_at DESC)
CREATE INDEX IF NOT EXISTS idx_certificate_type_created
  ON certificate(certificate_type, created_at DESC);

-- CRL: Reconciliation (same pattern as certificate)
CREATE INDEX IF NOT EXISTS idx_crl_stored_created
  ON crl(stored_in_ldap, created_at ASC);

-- Validation Result: Statistics breakdown (GROUP BY status, country_code)
CREATE INDEX IF NOT EXISTS idx_validation_status_country
  ON validation_result(validation_status, country_code);

-- Operation Audit Log: Filter + ordering (WHERE type=$1 ORDER BY created_at DESC)
CREATE INDEX IF NOT EXISTS idx_op_audit_type_created
  ON operation_audit_log(operation_type, created_at DESC);

-- AI Analysis: Anomaly list filter + ordering (WHERE label=$1 ORDER BY score DESC)
CREATE INDEX IF NOT EXISTS idx_ai_analysis_label_score
  ON ai_analysis_result(anomaly_label, anomaly_score DESC);

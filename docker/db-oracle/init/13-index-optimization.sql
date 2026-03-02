-- =============================================================================
-- v2.25.8 Index Optimization Migration (Oracle)
-- Oracle parity indexes + composite indexes for multi-column queries
-- Safe for re-runs: catches ORA-00955 (name already used) and ORA-01408 (column list already indexed)
-- =============================================================================

SET SERVEROUTPUT ON;
WHENEVER SQLERROR CONTINUE;

-- Helper: Create index only if not exists
-- Oracle does not support CREATE INDEX IF NOT EXISTS natively

-- =============================================
-- Part A: Oracle Parity (PostgreSQL already has these)
-- =============================================

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_certificate_subject_dn ON certificate(subject_dn)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_certificate_subject_dn');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_certificate_subject_dn');
  ELSE RAISE; END IF;
END;
/

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_certificate_issuer_dn ON certificate(issuer_dn)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_certificate_issuer_dn');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_certificate_issuer_dn');
  ELSE RAISE; END IF;
END;
/

-- idx_lc_fingerprint: SKIP — covered by uq_lc_fingerprint UNIQUE constraint

-- =============================================
-- Part B: Composite Indexes (both DBs)
-- =============================================

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_certificate_stored_created ON certificate(stored_in_ldap, created_at ASC)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_certificate_stored_created');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_certificate_stored_created');
  ELSE RAISE; END IF;
END;
/

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_certificate_country_type ON certificate(country_code, certificate_type)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_certificate_country_type');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_certificate_country_type');
  ELSE RAISE; END IF;
END;
/

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_certificate_type_created ON certificate(certificate_type, created_at)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_certificate_type_created');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_certificate_type_created');
  ELSE RAISE; END IF;
END;
/

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_crl_stored_created ON crl(stored_in_ldap, created_at ASC)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_crl_stored_created');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_crl_stored_created');
  ELSE RAISE; END IF;
END;
/

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_validation_status_country ON validation_result(validation_status, country_code)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_validation_status_country');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_validation_status_country');
  ELSE RAISE; END IF;
END;
/

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_op_audit_type_created ON operation_audit_log(operation_type, created_at)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_op_audit_type_created');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_op_audit_type_created');
  ELSE RAISE; END IF;
END;
/

BEGIN
  EXECUTE IMMEDIATE 'CREATE INDEX idx_ai_analysis_label_score ON ai_analysis_result(anomaly_label, anomaly_score DESC)';
  DBMS_OUTPUT.PUT_LINE('Created: idx_ai_analysis_label_score');
EXCEPTION WHEN OTHERS THEN
  IF SQLCODE IN (-955, -1408) THEN DBMS_OUTPUT.PUT_LINE('Exists: idx_ai_analysis_label_score');
  ELSE RAISE; END IF;
END;
/

COMMIT;

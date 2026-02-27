-- ============================================================================
-- API Client Management Schema (Oracle)
-- v2.21.0: External client agent API key authentication
-- ============================================================================

CONNECT pkd_user/pkd_password@ORCLPDB1;
SET SQLBLANKLINES ON;

-- Allow re-runs (skip "already exists" errors)
WHENEVER SQLERROR CONTINUE;

-- API Client table
CREATE TABLE api_clients (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    client_name VARCHAR2(255) NOT NULL,
    api_key_hash VARCHAR2(64) NOT NULL,
    api_key_prefix VARCHAR2(16) NOT NULL,
    description VARCHAR2(4000),

    -- Access control
    permissions CLOB DEFAULT '[]',
    allowed_endpoints CLOB DEFAULT '[]',
    allowed_ips CLOB DEFAULT '[]',

    -- Rate limiting (per-client)
    rate_limit_per_minute NUMBER(10) DEFAULT 60,
    rate_limit_per_hour NUMBER(10) DEFAULT 1000,
    rate_limit_per_day NUMBER(10) DEFAULT 10000,

    -- Status
    is_active NUMBER(1) DEFAULT 1,
    expires_at TIMESTAMP,
    last_used_at TIMESTAMP,
    total_requests NUMBER(19) DEFAULT 0,

    -- Audit
    created_by VARCHAR2(36),
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    updated_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE UNIQUE INDEX idx_api_clients_key_hash ON api_clients(api_key_hash);
CREATE INDEX idx_api_clients_prefix ON api_clients(api_key_prefix);
CREATE INDEX idx_api_clients_active ON api_clients(is_active);

-- Auto-update trigger
CREATE OR REPLACE TRIGGER trg_api_clients_updated_at
BEFORE UPDATE ON api_clients
FOR EACH ROW
BEGIN
    :NEW.updated_at := SYSTIMESTAMP;
END;
/

-- API Client usage log table
CREATE TABLE api_client_usage_log (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    client_id VARCHAR2(36),
    client_name VARCHAR2(255),
    endpoint VARCHAR2(512),
    method VARCHAR2(10),
    status_code NUMBER(5),
    response_time_ms NUMBER(10),
    ip_address VARCHAR2(45),
    user_agent VARCHAR2(4000),
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_usage_log_client_time ON api_client_usage_log(client_id, created_at);
CREATE INDEX idx_usage_log_created ON api_client_usage_log(created_at);

COMMIT;

EXIT;

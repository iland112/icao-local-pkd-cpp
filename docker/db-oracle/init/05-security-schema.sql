-- =============================================================================
-- ICAO Local PKD - Security Schema (Oracle Version)
-- =============================================================================
-- Version: 2.0.0 (Oracle Migration)
-- Created: 2026-02-05
-- Description: Authentication, authorization, and audit logging
-- Converted from PostgreSQL to Oracle DDL
-- =============================================================================

-- Connect as PKD_USER
CONNECT pkd_user/pkd_password@XE;

-- =============================================================================
-- User Authentication and Authorization
-- =============================================================================

-- User accounts with RBAC permissions
CREATE TABLE users (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    username VARCHAR2(255) UNIQUE NOT NULL,
    password_hash VARCHAR2(255) NOT NULL,
    email VARCHAR2(255),
    full_name VARCHAR2(255),
    permissions CLOB DEFAULT '[]',  -- JSON array stored as CLOB
    is_active NUMBER(1) DEFAULT 1,
    is_admin NUMBER(1) DEFAULT 0,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    last_login_at TIMESTAMP,
    updated_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_users_username ON users(username);
CREATE INDEX idx_users_is_active ON users(is_active);

-- Trigger for updated_at timestamp
CREATE OR REPLACE TRIGGER trg_users_updated_at
BEFORE UPDATE ON users
FOR EACH ROW
BEGIN
    :NEW.updated_at := SYSTIMESTAMP;
END;
/

-- Create default admin user (password: admin123 - CHANGE IMMEDIATELY!)
-- Password hash format: PBKDF2-HMAC-SHA256 with 100,000 iterations
BEGIN
    INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions)
    VALUES (
        'admin',
        '$pbkdf2$100000$03c5e322f1241db72d2b7be0c8c0154b$3dc0c2016c69d98e81c41a2d9533b1808e07d465994fa57221aaf6122c6367c0',
        'admin@example.com',
        'System Administrator',
        1,
        '["admin", "upload:read", "upload:write", "cert:read", "cert:export", "pa:verify", "sync:read"]'
    );
    COMMIT;
EXCEPTION
    WHEN DUP_VAL_ON_INDEX THEN
        NULL;  -- Admin user already exists, ignore
END;
/

-- =============================================================================
-- Authentication Audit Log
-- =============================================================================

-- Authentication events (login, logout, token refresh)
CREATE TABLE auth_audit_log (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    user_id VARCHAR2(36),
    username VARCHAR2(255),
    event_type VARCHAR2(50) NOT NULL,
    ip_address VARCHAR2(45),
    user_agent CLOB,
    success NUMBER(1) DEFAULT 1,
    error_message CLOB,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_auth_audit_user FOREIGN KEY (user_id) REFERENCES users(id)
);

CREATE INDEX idx_auth_audit_user_id ON auth_audit_log(user_id);
CREATE INDEX idx_auth_audit_created_at ON auth_audit_log(created_at);
CREATE INDEX idx_auth_audit_event_type ON auth_audit_log(event_type);

-- =============================================================================
-- Operation Audit Log
-- =============================================================================

-- Business operation audit trail (upload, export, validation, etc.)
CREATE TABLE operation_audit_log (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    user_id VARCHAR2(36),
    username VARCHAR2(255) NOT NULL,
    operation_type VARCHAR2(50) NOT NULL,
    operation_subtype VARCHAR2(100),
    resource_id VARCHAR2(255),
    resource_type VARCHAR2(50),
    ip_address VARCHAR2(45),
    user_agent CLOB,
    request_method VARCHAR2(10),
    request_path CLOB,
    success NUMBER(1) DEFAULT 1,
    status_code NUMBER(10),
    error_message CLOB,
    metadata CLOB,  -- JSON stored as CLOB
    duration_ms NUMBER(10),
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_op_audit_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX idx_op_audit_user_id ON operation_audit_log(user_id);
CREATE INDEX idx_op_audit_op_type ON operation_audit_log(operation_type);
CREATE INDEX idx_op_audit_resource_type ON operation_audit_log(resource_type);
CREATE INDEX idx_op_audit_created_at ON operation_audit_log(created_at);
CREATE INDEX idx_op_audit_success ON operation_audit_log(success);

-- =============================================================================
-- Commit changes
-- =============================================================================

COMMIT;

-- Display completion message
BEGIN
    DBMS_OUTPUT.PUT_LINE('=============================================================================');
    DBMS_OUTPUT.PUT_LINE('Security schema created successfully');
    DBMS_OUTPUT.PUT_LINE('Tables: 3 (users, auth_audit_log, operation_audit_log)');
    DBMS_OUTPUT.PUT_LINE('Default admin user: username=admin, password=admin123');
    DBMS_OUTPUT.PUT_LINE('*** CHANGE DEFAULT ADMIN PASSWORD IMMEDIATELY! ***');
    DBMS_OUTPUT.PUT_LINE('=============================================================================');
END;
/

EXIT;

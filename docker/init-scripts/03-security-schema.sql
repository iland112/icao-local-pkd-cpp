-- =============================================================================
-- ICAO Local PKD - Security Schema
-- =============================================================================
-- Version: 2.0.0
-- Created: 2026-01-25
-- Description: Authentication, authorization, and audit logging
-- =============================================================================

-- =============================================================================
-- User Authentication and Authorization
-- =============================================================================

-- User accounts with RBAC permissions
CREATE TABLE IF NOT EXISTS users (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    username VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    email VARCHAR(255),
    full_name VARCHAR(255),
    permissions JSONB DEFAULT '[]'::jsonb,
    is_active BOOLEAN DEFAULT true,
    is_admin BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login_at TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_users_username ON users(username);
CREATE INDEX idx_users_is_active ON users(is_active);

-- Create default admin user (password: admin123 - CHANGE IMMEDIATELY!)
-- Password hash format: PBKDF2-HMAC-SHA256 with 100,000 iterations
INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions)
VALUES (
    'admin',
    '$pbkdf2$100000$03c5e322f1241db72d2b7be0c8c0154b$3dc0c2016c69d98e81c41a2d9533b1808e07d465994fa57221aaf6122c6367c0',
    'admin@example.com',
    'System Administrator',
    true,
    '["admin", "upload:read", "upload:write", "cert:read", "cert:export", "pa:verify", "sync:read"]'::jsonb
)
ON CONFLICT (username) DO NOTHING;

-- =============================================================================
-- Authentication Audit Log
-- =============================================================================

-- Authentication events (login, logout, token refresh)
CREATE TABLE IF NOT EXISTS auth_audit_log (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID REFERENCES users(id),
    username VARCHAR(255),
    event_type VARCHAR(50) NOT NULL,
    ip_address VARCHAR(45),
    user_agent TEXT,
    success BOOLEAN DEFAULT true,
    error_message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_auth_audit_user_id ON auth_audit_log(user_id);
CREATE INDEX idx_auth_audit_created_at ON auth_audit_log(created_at);
CREATE INDEX idx_auth_audit_event_type ON auth_audit_log(event_type);

-- =============================================================================
-- Operation Audit Log
-- =============================================================================

-- Business operation audit trail (upload, export, validation, etc.)
CREATE TABLE IF NOT EXISTS operation_audit_log (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID REFERENCES users(id) ON DELETE SET NULL,
    username VARCHAR(255) NOT NULL,
    operation_type VARCHAR(50) NOT NULL,
    resource_type VARCHAR(50),
    resource_id VARCHAR(255),
    action VARCHAR(50) NOT NULL,
    status VARCHAR(20) NOT NULL,
    details JSONB,
    ip_address VARCHAR(45),
    user_agent TEXT,
    error_message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_op_audit_user_id ON operation_audit_log(user_id);
CREATE INDEX idx_op_audit_operation_type ON operation_audit_log(operation_type);
CREATE INDEX idx_op_audit_resource_type ON operation_audit_log(resource_type);
CREATE INDEX idx_op_audit_created_at ON operation_audit_log(created_at);
CREATE INDEX idx_op_audit_status ON operation_audit_log(status);

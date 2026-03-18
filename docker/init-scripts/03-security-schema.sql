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
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    last_login_at TIMESTAMP WITH TIME ZONE,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_users_username ON users(username);
CREATE INDEX idx_users_is_active ON users(is_active);

-- NOTE: Admin user is created at application startup via ADMIN_INITIAL_PASSWORD environment variable.
-- No hardcoded credentials in SQL scripts. See service_container.cpp ensureAdminUser().

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
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
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
    operation_subtype VARCHAR(100),
    resource_id VARCHAR(255),
    resource_type VARCHAR(50),
    ip_address VARCHAR(45),
    user_agent TEXT,
    request_method VARCHAR(10),
    request_path TEXT,
    success BOOLEAN DEFAULT true,
    status_code INTEGER,
    error_message TEXT,
    error_code VARCHAR(50),
    metadata JSONB,
    duration_ms INTEGER,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_op_audit_user_id ON operation_audit_log(user_id);
CREATE INDEX idx_op_audit_operation_type ON operation_audit_log(operation_type);
CREATE INDEX idx_op_audit_resource_type ON operation_audit_log(resource_type);
CREATE INDEX idx_op_audit_created_at ON operation_audit_log(created_at);
CREATE INDEX idx_op_audit_success ON operation_audit_log(success);
CREATE INDEX idx_op_audit_type_created ON operation_audit_log(operation_type, created_at DESC);

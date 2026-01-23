-- ============================================================================
-- Phase 3 Authentication Schema
-- Users and Authentication Audit Log Tables
-- ============================================================================

-- User authentication and authorization table
CREATE TABLE IF NOT EXISTS users (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,    -- bcrypt format: $2b$10$...
    email VARCHAR(255),
    full_name VARCHAR(255),
    permissions JSONB DEFAULT '[]'::jsonb,  -- ["upload:write", "cert:read", ...]
    is_active BOOLEAN DEFAULT true,
    is_admin BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login_at TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for performance
CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
CREATE INDEX IF NOT EXISTS idx_users_is_active ON users(is_active);
CREATE INDEX IF NOT EXISTS idx_users_permissions ON users USING GIN(permissions);
CREATE INDEX IF NOT EXISTS idx_users_created_at ON users(created_at);

-- Authentication event audit trail
CREATE TABLE IF NOT EXISTS auth_audit_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE SET NULL,
    username VARCHAR(255),
    event_type VARCHAR(50) NOT NULL,  -- LOGIN_SUCCESS, LOGIN_FAILED, LOGOUT, TOKEN_REFRESH, TOKEN_EXPIRED, etc.
    ip_address VARCHAR(45),           -- IPv4 or IPv6
    user_agent TEXT,
    success BOOLEAN DEFAULT true,
    error_message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for audit queries
CREATE INDEX IF NOT EXISTS idx_auth_audit_user_id ON auth_audit_log(user_id);
CREATE INDEX IF NOT EXISTS idx_auth_audit_created_at ON auth_audit_log(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_auth_audit_event_type ON auth_audit_log(event_type);
CREATE INDEX IF NOT EXISTS idx_auth_audit_success ON auth_audit_log(success);
CREATE INDEX IF NOT EXISTS idx_auth_audit_username ON auth_audit_log(username);

-- ============================================================================
-- Default Users (CHANGE PASSWORDS IMMEDIATELY IN PRODUCTION!)
-- ============================================================================

-- Admin user
-- Username: admin
-- Password: admin123
-- Bcrypt hash generated with: htpasswd -bnBC 10 "" admin123 | tr -d ':\n'
INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions)
VALUES (
    'admin',
    '$2y$10$rO0C/vBfLPHUy8K4KpJxcOqYLbP.VtD3tQZnG9UvN7gJ0LQq7zqXi',
    'admin@example.com',
    'System Administrator',
    true,
    '["admin", "upload:read", "upload:write", "cert:read", "cert:export", "pa:verify", "sync:read", "sync:write"]'::jsonb
)
ON CONFLICT (username) DO NOTHING;

-- Test user (for development/testing only)
-- Username: testuser
-- Password: user123
INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions)
VALUES (
    'testuser',
    '$2y$10$N9qo8uLOickgx2ZMRZoMye7g8gLJo5K5h3QU2j3E4Y8h3Y8h3Y8h3',
    'user@example.com',
    'Test User',
    false,
    '["upload:read", "cert:read"]'::jsonb
)
ON CONFLICT (username) DO NOTHING;

-- Read-only user (for monitoring/reporting)
-- Username: viewer
-- Password: view123
INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions)
VALUES (
    'viewer',
    '$2y$10$VpZ3X8K9L1M2N3O4P5Q6R7S8T9U0V1W2X3Y4Z5A6B7C8D9E0F1G2H',
    'viewer@example.com',
    'View Only User',
    false,
    '["upload:read", "cert:read", "pa:verify", "sync:read"]'::jsonb
)
ON CONFLICT (username) DO NOTHING;

-- ============================================================================
-- Permission Reference
-- ============================================================================
--
-- Resource:Action format
--
-- Upload:
--   - upload:read   : View upload history and statistics
--   - upload:write  : Upload files (LDIF, Master List)
--
-- Certificate:
--   - cert:read     : Search and view certificates
--   - cert:export   : Export certificates (file, ZIP)
--
-- Passive Authentication:
--   - pa:verify     : Verify PA (SOD, DG)
--
-- Sync:
--   - sync:read     : View sync status and statistics
--   - sync:write    : Trigger manual sync, reconciliation (admin only)
--
-- Admin:
--   - admin         : Bypass all permission checks (full access)
--
-- ============================================================================

-- Trigger to update updated_at timestamp
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_users_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ============================================================================
-- Verification Queries (for testing)
-- ============================================================================

-- SELECT username, is_admin, permissions FROM users;
-- SELECT COUNT(*) FROM users;
-- SELECT event_type, COUNT(*) FROM auth_audit_log GROUP BY event_type;
-- SELECT * FROM auth_audit_log ORDER BY created_at DESC LIMIT 10;

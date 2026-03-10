-- ============================================================================
-- API Client Management Schema (PostgreSQL)
-- v2.21.0: External client agent API key authentication
-- ============================================================================

-- API Client table
CREATE TABLE IF NOT EXISTS api_clients (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    client_name VARCHAR(255) NOT NULL,
    api_key_hash VARCHAR(64) NOT NULL,
    api_key_prefix VARCHAR(16) NOT NULL,
    description TEXT,

    -- Access control
    permissions JSONB DEFAULT '[]'::jsonb,
    allowed_endpoints JSONB DEFAULT '[]'::jsonb,
    allowed_ips JSONB DEFAULT '[]'::jsonb,

    -- Rate limiting (per-client)
    rate_limit_per_minute INTEGER DEFAULT 60,
    rate_limit_per_hour INTEGER DEFAULT 1000,
    rate_limit_per_day INTEGER DEFAULT 10000,

    -- Status
    is_active BOOLEAN DEFAULT true,
    expires_at TIMESTAMP WITH TIME ZONE,
    last_used_at TIMESTAMP WITH TIME ZONE,
    total_requests BIGINT DEFAULT 0,

    -- Audit
    created_by UUID REFERENCES users(id) ON DELETE SET NULL,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_api_clients_key_hash ON api_clients(api_key_hash);
CREATE INDEX IF NOT EXISTS idx_api_clients_prefix ON api_clients(api_key_prefix);
CREATE INDEX IF NOT EXISTS idx_api_clients_active ON api_clients(is_active);

-- API Client usage log table
CREATE TABLE IF NOT EXISTS api_client_usage_log (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    client_id UUID REFERENCES api_clients(id) ON DELETE SET NULL,
    client_name VARCHAR(255),
    endpoint VARCHAR(512),
    method VARCHAR(10),
    status_code INTEGER,
    response_time_ms INTEGER,
    ip_address VARCHAR(45),
    user_agent TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_usage_log_client_time ON api_client_usage_log(client_id, created_at);
CREATE INDEX IF NOT EXISTS idx_usage_log_created ON api_client_usage_log(created_at);

-- ============================================================================
-- API Client Request table (Request-Approval workflow)
-- v2.31.0: External users submit API access requests, admins approve/reject
-- ============================================================================
CREATE TABLE IF NOT EXISTS api_client_requests (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),

    -- Requester information (filled by external user)
    requester_name VARCHAR(255) NOT NULL,
    requester_org VARCHAR(255) NOT NULL,
    requester_contact_phone VARCHAR(50),
    requester_contact_email VARCHAR(255) NOT NULL,
    request_reason TEXT NOT NULL,

    -- Desired API client configuration
    client_name VARCHAR(255) NOT NULL,
    description TEXT,
    device_type VARCHAR(20) DEFAULT 'SERVER',
    permissions JSONB DEFAULT '[]'::jsonb,
    allowed_ips JSONB DEFAULT '[]'::jsonb,

    -- Approval workflow
    status VARCHAR(20) DEFAULT 'PENDING' NOT NULL,
    reviewed_by UUID REFERENCES users(id) ON DELETE SET NULL,
    reviewed_at TIMESTAMP WITH TIME ZONE,
    review_comment TEXT,
    approved_client_id UUID REFERENCES api_clients(id) ON DELETE SET NULL,

    -- Audit
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_api_client_requests_status ON api_client_requests(status);
CREATE INDEX IF NOT EXISTS idx_api_client_requests_created ON api_client_requests(created_at);

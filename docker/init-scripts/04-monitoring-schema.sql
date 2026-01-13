-- =============================================================================
-- ICAO Local PKD - Monitoring Service Schema
-- =============================================================================
-- Version: 1.0
-- Created: 2026-01-13
-- Description: System metrics and service health tracking
-- =============================================================================

-- =============================================================================
-- System Metrics Table
-- =============================================================================

CREATE TABLE IF NOT EXISTS system_metrics (
    id SERIAL PRIMARY KEY,
    timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    -- CPU
    cpu_usage_percent FLOAT,
    cpu_load_1min FLOAT,
    cpu_load_5min FLOAT,
    cpu_load_15min FLOAT,

    -- Memory
    memory_total_mb BIGINT,
    memory_used_mb BIGINT,
    memory_free_mb BIGINT,
    memory_usage_percent FLOAT,

    -- Disk
    disk_total_gb BIGINT,
    disk_used_gb BIGINT,
    disk_free_gb BIGINT,
    disk_usage_percent FLOAT,

    -- Network
    net_bytes_sent BIGINT,
    net_bytes_recv BIGINT,
    net_packets_sent BIGINT,
    net_packets_recv BIGINT
);

CREATE INDEX IF NOT EXISTS idx_system_metrics_timestamp ON system_metrics(timestamp DESC);

-- =============================================================================
-- Service Health Table
-- =============================================================================

CREATE TABLE IF NOT EXISTS service_health (
    id SERIAL PRIMARY KEY,
    service_name VARCHAR(50) NOT NULL,
    status VARCHAR(20) NOT NULL,           -- UP, DEGRADED, DOWN, UNKNOWN
    response_time_ms INTEGER,
    error_message TEXT,
    checked_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_service_health_service_name ON service_health(service_name);
CREATE INDEX IF NOT EXISTS idx_service_health_checked_at ON service_health(checked_at DESC);
CREATE INDEX IF NOT EXISTS idx_service_health_status ON service_health(status);

-- =============================================================================
-- Alerts Table
-- =============================================================================

CREATE TABLE IF NOT EXISTS alerts (
    id SERIAL PRIMARY KEY,
    alert_type VARCHAR(50) NOT NULL,      -- CPU_HIGH, MEMORY_HIGH, DISK_HIGH, SERVICE_DOWN
    severity VARCHAR(20) NOT NULL,         -- INFO, WARNING, ERROR, CRITICAL
    message TEXT NOT NULL,
    metric_value FLOAT,
    threshold FLOAT,
    service_name VARCHAR(50),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    acknowledged BOOLEAN DEFAULT FALSE,
    acknowledged_at TIMESTAMP WITH TIME ZONE,
    acknowledged_by VARCHAR(100)
);

CREATE INDEX IF NOT EXISTS idx_alerts_created_at ON alerts(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_alerts_acknowledged ON alerts(acknowledged);
CREATE INDEX IF NOT EXISTS idx_alerts_severity ON alerts(severity);
CREATE INDEX IF NOT EXISTS idx_alerts_type ON alerts(alert_type);

-- =============================================================================
-- Comments
-- =============================================================================

COMMENT ON TABLE system_metrics IS 'System resource metrics collected periodically';
COMMENT ON COLUMN system_metrics.timestamp IS 'Collection timestamp';

COMMENT ON TABLE service_health IS 'Service health check results';
COMMENT ON COLUMN service_health.service_name IS 'Service identifier (pkd-management, pa-service, etc.)';
COMMENT ON COLUMN service_health.status IS 'Health status: UP, DEGRADED, DOWN, UNKNOWN';

COMMENT ON TABLE alerts IS 'System alerts and warnings';
COMMENT ON COLUMN alerts.alert_type IS 'Alert category';
COMMENT ON COLUMN alerts.severity IS 'Alert severity level';

-- =============================================================================
-- Data Retention Policy (cleanup old data)
-- =============================================================================

-- Function to clean up old metrics (keep 7 days)
CREATE OR REPLACE FUNCTION cleanup_old_metrics()
RETURNS void AS $$
BEGIN
    DELETE FROM system_metrics WHERE timestamp < NOW() - INTERVAL '7 days';
    DELETE FROM service_health WHERE checked_at < NOW() - INTERVAL '7 days';
    DELETE FROM alerts WHERE created_at < NOW() - INTERVAL '30 days' AND acknowledged = TRUE;
END;
$$ LANGUAGE plpgsql;

-- Note: This function should be called periodically (e.g., daily cron job or monitoring service scheduler)

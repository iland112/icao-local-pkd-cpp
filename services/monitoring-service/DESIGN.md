# System Monitoring Service - Design Document

**Version**: 1.0.0
**Service Port**: 8084
**Created**: 2026-01-13

---

## Overview

실시간 시스템 리소스 모니터링 및 마이크로서비스 상태 체크를 위한 독립적인 모니터링 서비스입니다.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│              Monitoring Service (:8084)                      │
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐  │
│  │ System Stats  │  │ Service Health│  │  Log Analyzer │  │
│  │  (CPU/Mem/    │  │   Checker     │  │               │  │
│  │   Disk/Net)   │  └───────────────┘  └───────────────┘  │
│  └───────────────┘                                          │
│         │                    │                    │          │
│         ▼                    ▼                    ▼          │
│  ┌──────────────────────────────────────────────────────┐  │
│  │          PostgreSQL (metrics storage)                │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                        │
                        ▼
                  API Gateway (:8080)
                        │
                        ▼
                 React Frontend
```

---

## Core Features

### 1. System Resource Monitoring

| Resource | Metrics | Update Interval |
|----------|---------|-----------------|
| **CPU** | Usage %, Load average | 5초 |
| **Memory** | Total, Used, Free, Available (%) | 5초 |
| **Disk** | Total, Used, Free (%), I/O stats | 10초 |
| **Network** | Bytes sent/received, Packets | 5초 |

**Implementation**:
- Linux: `/proc/stat`, `/proc/meminfo`, `/proc/diskstats`, `/proc/net/dev`
- Parse files directly (no external dependencies)
- Store in ring buffer (최근 1시간 데이터)
- PostgreSQL에 1분마다 aggregate 저장

### 2. Service Health Monitoring

| Service | Endpoint | Check Interval |
|---------|----------|----------------|
| PKD Management | `http://pkd-management:8081/api/health` | 30초 |
| PA Service | `http://pa-service:8082/api/pa/health` | 30초 |
| Sync Service | `http://sync-service:8083/api/sync/health` | 30초 |
| PostgreSQL | Direct connection | 30초 |
| OpenLDAP | LDAP bind | 30초 |
| HAProxy | `http://haproxy:8404/stats` | 30초 |

**Health Status**:
- `UP`: 정상 응답 (HTTP 200, DB connected)
- `DEGRADED`: 부분 장애 (느린 응답, 일부 기능 불가)
- `DOWN`: 서비스 중단 (연결 불가, timeout)

### 3. Log Collection & Analysis

**Log Sources**:
- `/app/logs/pkd-management.log`
- `/app/logs/pa-service.log`
- `/app/logs/sync-service.log`

**Analysis**:
- ERROR/WARN 레벨 카운트 (실시간)
- 패턴 매칭 (DB connection failure, LDAP error)
- 최근 10개 오류 메시지 저장

### 4. Alert System

**Alert Rules**:
| Metric | Condition | Action |
|--------|-----------|--------|
| CPU Usage | > 90% for 5 minutes | Log warning |
| Memory | > 90% for 2 minutes | Log warning |
| Disk | > 85% | Log warning |
| Service Down | Any service down for 1 minute | Log error |

**Alert Storage**:
```sql
CREATE TABLE alerts (
    id SERIAL PRIMARY KEY,
    alert_type VARCHAR(50),  -- CPU_HIGH, MEMORY_HIGH, SERVICE_DOWN
    severity VARCHAR(20),    -- INFO, WARNING, ERROR, CRITICAL
    message TEXT,
    metric_value FLOAT,
    threshold FLOAT,
    created_at TIMESTAMP DEFAULT NOW(),
    acknowledged BOOLEAN DEFAULT FALSE
);
```

---

## API Endpoints

### System Stats

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/monitoring/health` | Service health |
| GET | `/api/monitoring/system/cpu` | CPU usage stats |
| GET | `/api/monitoring/system/memory` | Memory usage stats |
| GET | `/api/monitoring/system/disk` | Disk usage stats |
| GET | `/api/monitoring/system/network` | Network stats |
| GET | `/api/monitoring/system/overview` | All system metrics |

### Service Health

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/monitoring/services` | All services status |
| GET | `/api/monitoring/services/{name}` | Specific service status |
| GET | `/api/monitoring/services/history` | Health check history |

### Logs & Alerts

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/monitoring/logs/recent` | Recent ERROR/WARN logs |
| GET | `/api/monitoring/logs/stats` | Log level statistics |
| GET | `/api/monitoring/alerts` | Active alerts |
| GET | `/api/monitoring/alerts/history` | Alert history |
| POST | `/api/monitoring/alerts/{id}/acknowledge` | Acknowledge alert |

---

## Data Models

### system_metrics Table

```sql
CREATE TABLE system_metrics (
    id SERIAL PRIMARY KEY,
    timestamp TIMESTAMP DEFAULT NOW(),

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
```

### service_health Table

```sql
CREATE TABLE service_health (
    id SERIAL PRIMARY KEY,
    service_name VARCHAR(50),  -- pkd-management, pa-service, etc.
    status VARCHAR(20),        -- UP, DEGRADED, DOWN
    response_time_ms INTEGER,
    error_message TEXT,
    checked_at TIMESTAMP DEFAULT NOW()
);
```

### log_events Table

```sql
CREATE TABLE log_events (
    id SERIAL PRIMARY KEY,
    service_name VARCHAR(50),
    log_level VARCHAR(10),  -- ERROR, WARN, INFO
    message TEXT,
    timestamp TIMESTAMP,
    detected_at TIMESTAMP DEFAULT NOW()
);
```

---

## Technology Stack

| Component | Technology |
|-----------|------------|
| Language | C++20 |
| Framework | Drogon 1.9+ |
| Database | PostgreSQL 15 |
| JSON | jsoncpp (Drogon built-in) |
| Logging | spdlog |
| System Info | `/proc` filesystem (Linux native) |
| HTTP Client | libcurl (for health checks) |

---

## Performance Considerations

1. **Memory Usage**:
   - Ring buffer: 최대 720개 (1시간, 5초 간격)
   - 각 메트릭 ~200 bytes → 총 ~144KB

2. **DB Write Frequency**:
   - System metrics: 1분마다 (86,400 rows/day)
   - Service health: 30초마다 (172,800 rows/day)
   - Auto-cleanup: 7일 이상 데이터 삭제

3. **Thread Model**:
   - Main thread: HTTP server (Drogon 2 threads)
   - Background thread 1: System metrics collector
   - Background thread 2: Service health checker
   - Background thread 3: Log analyzer

---

## Deployment

### Docker Compose

```yaml
monitoring-service:
  build: ./services/monitoring-service
  container_name: icao-monitoring
  ports:
    - "8084:8084"
  environment:
    - SERVER_PORT=8084
    - DB_HOST=postgres
    - DB_PORT=5432
    - DB_NAME=pkd
    - DB_USER=pkd
    - DB_PASSWORD=pkd123
  volumes:
    - ../services/pkd-management/logs:/app/logs/pkd:ro
    - ../services/pa-service/logs:/app/logs/pa:ro
    - ../services/sync-service/logs:/app/logs/sync:ro
  depends_on:
    - postgres
  restart: unless-stopped
```

---

## Frontend Dashboard

### Layout

```
┌────────────────────────────────────────────────────────────┐
│  System Overview (4 cards)                                  │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐      │
│  │ CPU 45% │  │ MEM 65% │  │ DISK 48%│  │ NET ↑↓  │      │
│  └─────────┘  └─────────┘  └─────────┘  └─────────┘      │
├────────────────────────────────────────────────────────────┤
│  Service Status (6 cards)                                   │
│  ┌───────────┐ ┌───────────┐ ┌───────────┐               │
│  │ PKD Mgmt  │ │ PA Service│ │ Sync Svc  │               │
│  │ ● UP      │ │ ● UP      │ │ ● UP      │               │
│  └───────────┘ └───────────┘ └───────────┘               │
├────────────────────────────────────────────────────────────┤
│  Real-time Charts (Line charts)                            │
│  CPU Usage (1 hour), Memory Usage (1 hour)                │
├────────────────────────────────────────────────────────────┤
│  Active Alerts Table                                        │
│  Recent Errors/Warnings                                     │
└────────────────────────────────────────────────────────────┘
```

---

## Future Enhancements (Phase 2)

1. **Email/Slack Notifications**: 임계값 초과 시 알림
2. **Grafana Integration**: 메트릭 시각화
3. **Distributed Tracing**: OpenTelemetry 통합
4. **Log Aggregation**: ELK Stack 연동
5. **Predictive Alerts**: ML 기반 이상 탐지

---

## References

- Linux `/proc` filesystem: https://man7.org/linux/man-pages/man5/proc.5.html
- Drogon Framework: https://github.com/drogonframework/drogon
- System monitoring best practices: ITIL v4

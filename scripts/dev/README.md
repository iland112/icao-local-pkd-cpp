# PA Service Development Environment

**Branch**: `feature/pa-service-repository-pattern`
**Purpose**: Isolated pa-service development for Repository Pattern refactoring

---

## Overview

This development environment allows you to work on pa-service refactoring while keeping production services (main branch) running and untouched.

### Architecture

```
Production (main branch)                Dev (feature branch)
├── postgres (port 5432)                ← Shared
├── openldap1/2 (port 389)              ← Shared
├── pkd-management (port 8081)          ← Shared
├── pkd-relay (port 8083)               ← Shared
├── pa-service (port 8082)
└── api-gateway (port 8080)

                                        ├── pa-service-dev (port 8092) ✨ NEW
```

**Key Points**:
- ✅ pa-service-dev runs on port **8092** (production uses 8082)
- ✅ Uses production DB/LDAP (existing 31,212 certificates)
- ✅ Audit logs tagged with `[DEV]` prefix
- ✅ Production environment remains untouched

---

## Quick Start

### 1. Start Production Services (if not running)

```bash
cd docker
docker-compose up -d
```

### 2. Start PA Service Dev

```bash
./scripts/dev/start-pa-dev.sh
```

**Output**:
```
✅ PA Service Dev is running!

Access points:
  - Health Check: http://localhost:8092/api/health
  - PA Verify:    http://localhost:8092/api/pa/verify
  - API Docs:     http://localhost:8092/api/docs
```

### 3. Verify Dev Service

```bash
# Health check
curl http://localhost:8092/api/health

# Compare with production
curl http://localhost:8082/api/health  # Production (via API Gateway: 8080)
curl http://localhost:8092/api/health  # Development
```

---

## Development Workflow

### After Code Changes

```bash
# Rebuild and restart dev service
./scripts/dev/rebuild-pa-dev.sh

# Force rebuild (clear cache)
./scripts/dev/rebuild-pa-dev.sh --no-cache
```

### View Logs

```bash
# Tail logs (follow mode)
./scripts/dev/logs-pa-dev.sh

# Last 200 lines
./scripts/dev/logs-pa-dev.sh 200
```

### Stop Dev Service

```bash
./scripts/dev/stop-pa-dev.sh
```

**Note**: This only stops pa-service-dev. Production services remain running.

---

## Testing Against Dev Service

### Direct API Calls

```bash
# Use port 8092 for dev
curl -X POST http://localhost:8092/api/pa/verify \
  -H "Content-Type: application/json" \
  -d @test-data/pa-request.json
```

### Frontend Development

Update frontend `.env.development`:
```bash
REACT_APP_API_BASE_URL=http://localhost:8092
```

Then frontend will call dev pa-service instead of production.

---

## Safety Features

### 1. Audit Log Tagging

All dev operations are tagged with `[DEV]` in audit logs:

```sql
SELECT * FROM operation_audit_log
WHERE metadata->>'environment' = 'development';
```

### 2. Separate Log Directory

Dev logs are stored in `.docker-data/pa-dev-logs/` (separate from production).

### 3. Network Isolation

Dev service uses production network but with different container name, so no port conflicts.

---

## Troubleshooting

### Dev service won't start

**Check if production postgres is running**:
```bash
docker ps | grep postgres
```

If not:
```bash
cd docker && docker-compose up -d postgres openldap1
```

### Port 8092 already in use

**Find conflicting process**:
```bash
lsof -i :8092
```

**Kill it or change dev port** in `docker-compose.dev.yml`.

### Build errors

**Clear Docker build cache**:
```bash
./scripts/dev/rebuild-pa-dev.sh --no-cache
```

### View container status

```bash
cd docker
docker-compose -f docker-compose.dev.yml ps
```

---

## Environment Variables

Dev service uses same `.env` file as production:

```bash
# docker/.env
DB_PASSWORD=your_db_password
LDAP_BIND_PASSWORD=your_ldap_password
```

Additional dev-specific variables in `docker-compose.dev.yml`:
- `ENVIRONMENT=development`
- `LOG_LEVEL=debug`
- `AUDIT_TAG=[DEV]`

---

## Cleanup

### Remove dev container only

```bash
./scripts/dev/stop-pa-dev.sh
```

### Remove dev logs

```bash
rm -rf .docker-data/pa-dev-logs
```

### Full cleanup (dev + production)

```bash
cd docker
docker-compose down
docker-compose -f docker-compose.dev.yml down
```

---

## Integration with Main Workflow

### Before merging to main

1. **Stop dev service**:
   ```bash
   ./scripts/dev/stop-pa-dev.sh
   ```

2. **Test with production build**:
   ```bash
   cd docker
   docker-compose build --no-cache pa-service
   docker-compose up -d --force-recreate pa-service
   ```

3. **Verify production compatibility**:
   ```bash
   curl http://localhost:8080/api/pa/verify
   ```

4. **Merge to main** if all tests pass.

---

## Script Reference

| Script | Purpose |
|--------|---------|
| `start-pa-dev.sh` | Build and start pa-service-dev |
| `rebuild-pa-dev.sh` | Rebuild after code changes |
| `logs-pa-dev.sh` | Tail dev service logs |
| `stop-pa-dev.sh` | Stop dev service |

All scripts are in `scripts/dev/` directory.

---

## Production vs Dev Comparison

| Aspect | Production (main) | Dev (feature branch) |
|--------|-------------------|---------------------|
| Branch | main | feature/pa-service-repository-pattern |
| Port | 8082 (via 8080 gateway) | 8092 (direct) |
| Container | icao-local-pkd-pa-service | icao-local-pkd-pa-service-dev |
| Database | postgres (production data) | postgres (shared) |
| LDAP | openldap1/2 (production data) | openldap1/2 (shared) |
| Logs | .docker-data/logs | .docker-data/pa-dev-logs |
| Audit Tag | (none) | [DEV] |
| Build | Stable (main branch) | Refactoring (feature branch) |

---

## Next Steps

After setting up dev environment:
1. ✅ Start dev service: `./scripts/dev/start-pa-dev.sh`
2. ✅ Verify health: `curl http://localhost:8092/api/health`
3. 🚀 Begin Phase 1: Repository Layer Implementation

See [PA_SERVICE_REPOSITORY_PATTERN_PLAN.md](../../docs/PA_SERVICE_REPOSITORY_PATTERN_PLAN.md) for refactoring plan.

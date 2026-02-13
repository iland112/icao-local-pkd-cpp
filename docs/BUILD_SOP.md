# Build Standard Operating Procedure (SOP)

**Version**: 2.0
**Last Updated**: 2026-02-13
**Purpose**: Prevent build verification mistakes and ensure accurate deployments

---

## Critical Lessons Learned

**Problem**: Docker build caching can reuse old build layers even when source code changes, resulting in:
- Version string updated but binary containing old code
- Features not working despite "successful" build
- Wasted time rebuilding after failed deployments

---

## Build Procedure

### Step 1: Code Modification

1. Make source code changes
2. **ALWAYS** update the version string in `services/<service>/src/main.cpp`

### Step 2: Build

**Quick rebuild (development):**
```bash
# Using rebuild scripts
./scripts/build/rebuild-pkd-relay.sh
./scripts/build/rebuild-frontend.sh

# Or via docker compose
docker compose -f docker/docker-compose.yaml build <service-name>
docker compose -f docker/docker-compose.yaml up -d <service-name>
```

**Force fresh build (when cache issues suspected):**
```bash
./scripts/build/rebuild-pkd-relay.sh --no-cache

# Or directly
docker compose -f docker/docker-compose.yaml build --no-cache <service-name>
```

### Step 3: Verify Build BEFORE Deployment

```bash
# Check container is running
docker compose -f docker/docker-compose.yaml ps <service-name>

# Check version in logs
docker logs icao-local-pkd-<service> --tail 10 | grep -i version

# Check image creation time (should be recent)
docker inspect icao-local-pkd-<service> --format='{{.Created}}'
```

### Step 4: Test Functionality

After deployment, **always** test the specific feature you modified. Don't rely solely on version strings.

---

## When to Use --no-cache

| Scenario | Cache OK? | Notes |
|----------|-----------|-------|
| Minor code change | Yes | Docker cache usually works |
| CMakeLists.txt change | **No** | Must rebuild |
| vcpkg.json change | **No** | Must rebuild dependencies |
| New shared library | **No** | Must rebuild |
| Dockerfile change | **No** | Must rebuild |
| Version mismatch | **No** | Force fresh build |
| Config-only changes | Yes | nginx.conf, docker-compose |
| Frontend changes | Yes | Vite build is fast |

---

## Service-Specific Build Commands

| Service | Rebuild Script | Container Name |
|---------|---------------|----------------|
| PKD Management | `./scripts/build/rebuild-pkd-relay.sh` (generic) | `icao-local-pkd-management` |
| PA Service | `docker compose build pa-service` | `icao-local-pkd-pa-service` |
| PKD Relay | `./scripts/build/rebuild-pkd-relay.sh` | `icao-local-pkd-relay` |
| Monitoring | `docker compose build monitoring` | `icao-local-pkd-monitoring` |
| Frontend | `./scripts/build/rebuild-frontend.sh` | `icao-local-pkd-frontend` |

---

## Troubleshooting

### Function not found in binary

**Cause**: Docker used cached build layers
**Fix**: Rebuild with `--no-cache`, verify again

### Image is old despite rebuild

**Cause**: Docker used cached final stage
**Fix**: Check build output for "Using cache" messages, rebuild with `--no-cache`

### Version in logs doesn't match

**Cause**: Running old container, not the newly built image
**Fix**: `docker compose up -d --force-recreate <service-name>`

---

## Remember

1. **Version changes alone don't guarantee new code is compiled**
2. **Docker cache is aggressive** - it will reuse layers when possible
3. **Always verify builds** before claiming they're complete
4. **Test functionality** after deployment, not just version strings
5. **When in doubt, --no-cache**

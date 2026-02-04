# Shared Database Library - Changelog

## v1.0.0 (2026-02-04)

### Created
- Thread-safe PostgreSQL connection pool implementation
- RAII-based DbConnection wrapper for automatic resource management
- Configurable pool sizing (min/max connections)
- Connection health checking and recycling
- 5-second connection acquisition timeout

### Features
- **Thread Safety**: Each request gets independent connection from pool
- **Performance**: Connection reuse reduces overhead, min connections always ready
- **Resource Management**: RAII pattern ensures automatic connection release
- **Stability**: Max connection limit prevents database overload

### Integration
- Used by pkd-relay-service (v2.4.2)
- Replaces single PGconn* shared across threads
- Fixes "timeout of 60000ms exceeded" errors caused by race conditions

### Configuration
- Default pool size: min=5, max=20
- Default acquire timeout: 5 seconds
- Configurable via constructor parameters

### Directory Structure
```
shared/lib/database/
├── db_connection_pool.h           # Header file
├── db_connection_pool.cpp         # Implementation
├── CMakeLists.txt                 # Build configuration
├── icao-database-config.cmake.in  # CMake package config
├── README.md                      # Usage documentation
└── CHANGELOG.md                   # This file
```

### See Also
- [README.md](README.md) - Complete usage guide and migration instructions
- [../../CMakeLists.txt](../../CMakeLists.txt) - Parent CMake configuration

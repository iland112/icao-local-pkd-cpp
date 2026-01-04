#!/bin/bash
#
# Luckfox ICAO Local PKD - Common Configuration
# This file is sourced by other scripts
#

# Default version
VERSION="${VERSION:-cpp}"

# Directory and compose file paths
JVM_DIR="/home/luckfox/icao-local-pkd"
CPP_DIR="/home/luckfox/icao-local-pkd-cpp-v2"

JVM_COMPOSE="$JVM_DIR/docker-compose.yml"
CPP_COMPOSE="$CPP_DIR/docker-compose-luckfox.yaml"

# Host IP for external access
EXTERNAL_HOST="192.168.100.11"
# Host for internal API calls (localhost for host network mode)
HOST="localhost"

# Parse version option
parse_version() {
    if [ "$1" = "jvm" ] || [ "$1" = "JVM" ]; then
        VERSION="jvm"
        shift
    elif [ "$1" = "cpp" ] || [ "$1" = "CPP" ]; then
        VERSION="cpp"
        shift
    fi

    # Return remaining arguments
    echo "$@"
}

# Get compose file and directory based on version
get_compose_file() {
    if [ "$VERSION" = "jvm" ]; then
        echo "$JVM_COMPOSE"
    else
        echo "$CPP_COMPOSE"
    fi
}

get_project_dir() {
    if [ "$VERSION" = "jvm" ]; then
        echo "$JVM_DIR"
    else
        echo "$CPP_DIR"
    fi
}

# Print version info
print_version_info() {
    if [ "$VERSION" = "jvm" ]; then
        echo "Version: JVM (Java/Kotlin)"
        echo "Directory: $JVM_DIR"
    else
        echo "Version: CPP (C++/Drogon)"
        echo "Directory: $CPP_DIR"
    fi
}

# Print access URLs based on version
print_access_urls() {
    echo ""
    echo "=== Access URLs ==="
    if [ "$VERSION" = "jvm" ]; then
        echo "Frontend:    http://$EXTERNAL_HOST:3000"
        echo "API:         http://$EXTERNAL_HOST:8080/api"
    else
        echo "Frontend:    http://$EXTERNAL_HOST:3000"
        echo "PKD API:     http://$EXTERNAL_HOST:8081/api"
        echo "PA API:      http://$EXTERNAL_HOST:8082/api"
        echo "Sync API:    http://$EXTERNAL_HOST:8083/api"
    fi
}

# Usage helper
print_usage() {
    local script_name="$1"
    local extra_args="$2"
    echo "Usage: $script_name [jvm|cpp] $extra_args"
    echo ""
    echo "Options:"
    echo "  jvm    - Use JVM (Java/Kotlin) version"
    echo "  cpp    - Use CPP (C++/Drogon) version (default)"
}

#!/bin/bash
# Convenience wrapper for scripts/docker/health.sh
exec "$(dirname "$0")/scripts/docker/health.sh" "$@"

#!/bin/bash
# Convenience wrapper for scripts/podman/health.sh
exec "$(dirname "$0")/scripts/podman/health.sh" "$@"

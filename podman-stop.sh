#!/bin/bash
# Convenience wrapper for scripts/podman/stop.sh
exec "$(dirname "$0")/scripts/podman/stop.sh" "$@"

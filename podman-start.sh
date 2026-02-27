#!/bin/bash
# Convenience wrapper for scripts/podman/start.sh
exec "$(dirname "$0")/scripts/podman/start.sh" "$@"

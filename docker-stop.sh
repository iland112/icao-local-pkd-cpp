#!/bin/bash
# Convenience wrapper for scripts/docker/stop.sh
exec "$(dirname "$0")/scripts/docker/stop.sh" "$@"

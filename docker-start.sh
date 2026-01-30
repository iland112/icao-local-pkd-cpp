#!/bin/bash
# Convenience wrapper for scripts/docker/start.sh
exec "$(dirname "$0")/scripts/docker/start.sh" "$@"

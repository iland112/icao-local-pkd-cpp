#!/bin/bash
# Convenience wrapper for scripts/docker/clean-and-init.sh
exec "$(dirname "$0")/scripts/docker/clean-and-init.sh" "$@"

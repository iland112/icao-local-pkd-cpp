#!/bin/bash
# Convenience wrapper for scripts/podman/clean-and-init.sh
exec "$(dirname "$0")/scripts/podman/clean-and-init.sh" "$@"

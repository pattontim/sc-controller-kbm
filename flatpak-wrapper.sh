#!/bin/bash
# SC-Daemon Flatpak Wrapper
# Sets up environment variables and starts the daemon

set -e

# Set environment paths for Flatpak sandbox
export SCC_SHARED="/app/share"
export SCC_DRIVERS="/app/lib/scc/drivers"
export LD_LIBRARY_PATH="/app/lib:${LD_LIBRARY_PATH}"

# Ensure driver directory exists
mkdir -p "$SCC_DRIVERS"

# Execute the daemon with all arguments
exec /app/bin/scc-daemon "$@"

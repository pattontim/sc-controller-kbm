#!/bin/bash
# SC Controller Flatpak Bundle Creation Script
# This script exports the locally-installed scc-daemon flatpak into a portable
# .flatpak bundle file that can be transferred and installed on other machines.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${HOME}/flatpak-repo"
BUNDLE_FILE="${HOME}/scc-daemon.flatpak"
APP_ID="com.github.kozec.scc-daemon"
BRANCH="master"

echo "=== SC Controller Flatpak Bundle Creator ==="
echo ""
echo "This script will create a portable flatpak bundle."
echo "Requirements:"
echo "  - scc-daemon flatpak must be installed locally"
echo "  - Sufficient disk space for the bundle (~5 MB)"
echo ""

# Check if flatpak app is installed
if ! flatpak info "${APP_ID}" &>/dev/null; then
    echo "Error: ${APP_ID} is not installed locally."
    echo "Please build and install the flatpak first:"
    echo "  flatpak-builder --user --force-clean --install ~/.var/app/${APP_ID} com.github.kozec.scc-daemon.yml"
    exit 1
fi

# Create repository directory
echo "[1/3] Creating repository directory..."
mkdir -p "${REPO_DIR}"

# Export app to repository
echo "[2/3] Exporting app to repository..."
flatpak build-export "${REPO_DIR}/repo" ~/.var/app/"${APP_ID}"

# Create bundle
echo "[3/3] Creating flatpak bundle..."
flatpak build-bundle "${REPO_DIR}/repo" "${BUNDLE_FILE}" "${APP_ID}" "${BRANCH}"

echo ""
echo "=== Success ==="
echo "Bundle created: ${BUNDLE_FILE}"
echo "Bundle size: $(du -h "${BUNDLE_FILE}" | cut -f1)"
echo ""
echo "To install on another machine:"
echo "  1. Transfer the bundle: scp ${BUNDLE_FILE} user@remote:~/"
echo "  2. Install on remote: flatpak install ~/scc-daemon.flatpak"
echo ""

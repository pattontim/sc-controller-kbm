#!/bin/bash
# Copy sc-controller-kbm to remote steamdeck-1 machine
# Excludes build artifacts and flatpak-builder

set -e

SOURCE="/home/deck/to_stay/Learning/Programming/sc-controller-kbm"
REMOTE_HOST="steamdeck-1"
REMOTE_USER="deck"
REMOTE_PATH="/home/deck/Downloads/sc-controller"

echo "Syncing sc-controller-kbm to ${REMOTE_HOST}:${REMOTE_PATH}..."

rsync -av \
  --delete \
  --exclude=".flatpak-builder" \
  --exclude="build" \
  --exclude="builddir" \
  --exclude=".git" \
  --exclude="__pycache__" \
  --exclude="*.o" \
  --exclude="*.a" \
  --exclude="*.so" \
  ${SOURCE}/ \
  ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}/

echo "✓ Sync complete!"
echo ""
echo "Next steps on ${REMOTE_HOST}:"
echo "  cd ${REMOTE_PATH}"
echo "  meson build"
echo "  ninja -C build scc-daemon"

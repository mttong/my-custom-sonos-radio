#!/usr/bin/env bash
# sync_media.sh — push local media_assets to the VM
# Run this from your local machine whenever you add or change music files.
set -euo pipefail

VM="root@134.122.126.121"
REMOTE_PATH="/root/my-custom-sonos-radio/media_assets/"

BLUE='\033[0;34m'; GREEN='\033[0;32m'; NC='\033[0m'

echo -e "${BLUE}Syncing media_assets to ${VM}...${NC}"

rsync -avz --progress --delete \
    --exclude='.git' \
    --exclude='*.lfsconfig' \
    media_assets/ "${VM}:${REMOTE_PATH}"

echo -e "${GREEN}Done! Media assets synced to VM.${NC}"

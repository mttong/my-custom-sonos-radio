#!/usr/bin/env bash
# stop.sh — gracefully stop the Maggie Sonos integration
set -euo pipefail

BLUE='\033[0;34m'; GREEN='\033[0;32m'; NC='\033[0m'

echo -e "${BLUE}Stopping Maggie Sonos Integration...${NC}"

docker-compose down

# Release ports if anything is still holding them
for port in 4040 8080; do
    if lsof -ti:$port &>/dev/null; then
        echo -e "${BLUE}Releasing port $port...${NC}"
        lsof -ti:$port | xargs kill -9 2>/dev/null || true
    fi
done

echo -e "${GREEN}Stopped.${NC}"

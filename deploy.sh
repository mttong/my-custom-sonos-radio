#!/usr/bin/env bash
# deploy.sh — start the cloud deployment (no ngrok, Caddy handles HTTPS)
set -euo pipefail

BLUE='\033[0;34m'; GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

# Use 'docker compose' plugin if available, fall back to 'docker-compose'
if docker compose version &>/dev/null 2>&1; then
    DC="docker compose"
else
    DC="docker-compose"
fi

if [ ! -f .env.cloud ]; then
    echo -e "${RED}ERROR: .env.cloud not found.${NC}"
    echo "  cp .env.cloud.example .env.cloud  then fill in your domain and credentials"
    exit 1
fi

if [ ! -f tokens.json ]; then
    echo '{}' > tokens.json
fi

echo -e "${BLUE}Pulling latest from main...${NC}"
git pull origin main

echo -e "${BLUE}Building and starting services...${NC}"
$DC -f docker-compose.cloud.yml up --build -d

echo -ne "${BLUE}Waiting for server...${NC} "
for i in $(seq 1 40); do
    if curl -sf http://localhost:8080/health > /dev/null 2>&1; then
        echo -e "${GREEN}ready!${NC}"
        break
    fi
    if [ "$i" -eq 40 ]; then
        echo -e "${RED}timed out. Check logs: $DC -f docker-compose.cloud.yml logs${NC}"
        exit 1
    fi
    sleep 1
done

set -a; source .env.cloud; set +a

echo ""
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo -e "${GREEN}  Server is running!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo ""
echo -e "  SMAPI endpoint:  https://${PUBLIC_DOMAIN}/smapi"
echo -e "  Redirect URI:    https://${PUBLIC_DOMAIN}/oauth/callback"
echo -e "  Authorize:       https://${PUBLIC_DOMAIN}/auth/login"
echo ""
echo -e "${BLUE}Streaming logs (Ctrl+C to stop following, server keeps running)...${NC}"
$DC -f docker-compose.cloud.yml logs -f sonos-server

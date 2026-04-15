#!/usr/bin/env bash
# demo.sh — one-command launch for the Maggie Sonos integration
set -euo pipefail

BLUE='\033[0;34m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'

# Use 'docker compose' plugin if available, fall back to 'docker-compose'
if docker compose version &>/dev/null 2>&1; then
    DC="docker compose"
else
    DC="docker-compose"
fi

echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   Maggie Sonos Integration — Demo     ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}"
echo ""

# ── Pull latest code ───────────────────────────────────────────────────────────
echo -e "${BLUE}Pulling latest from main...${NC}"
git pull origin main

# ── Pre-flight checks ──────────────────────────────────────────────────────────
if [ ! -f .env ]; then
    echo -e "${RED}ERROR: .env not found.${NC}"
    echo "  cp .env.example .env  then fill in NGROK_AUTHTOKEN, NGROK_DOMAIN, NGROK_URL"
    exit 1
fi

# Source .env for display (only exported vars)
set -a; source .env; set +a

if [ -z "${NGROK_AUTHTOKEN:-}" ]; then
    echo -e "${RED}ERROR: NGROK_AUTHTOKEN not set in .env${NC}"
    exit 1
fi
if [ -z "${NGROK_DOMAIN:-}" ]; then
    echo -e "${YELLOW}WARNING: NGROK_DOMAIN not set — URL will change on each restart.${NC}"
    echo "  Claim a free static domain at dashboard.ngrok.com/domains"
fi

# ── Start services ─────────────────────────────────────────────────────────────
echo -e "${BLUE}Building and starting Docker services...${NC}"
$DC up --build -d

# ── Wait for server health ─────────────────────────────────────────────────────
echo -ne "${BLUE}Waiting for server...${NC} "
for i in $(seq 1 40); do
    if curl -sf http://localhost:8080/health > /dev/null 2>&1; then
        echo -e "${GREEN}ready!${NC}"
        break
    fi
    if [ "$i" -eq 40 ]; then
        echo -e "${RED}timed out. Check logs: $DC logs sonos-server${NC}"
        exit 1
    fi
    sleep 1
done

# ── Print status ───────────────────────────────────────────────────────────────
PUBLIC_URL="${NGROK_URL:-https://${NGROK_DOMAIN:-unknown}}"

echo ""
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo -e "${GREEN}  Server is running!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════${NC}"
echo ""
echo -e "  ${BLUE}Web UI:${NC}           http://localhost:8080"
echo -e "  ${BLUE}ngrok dashboard:${NC}  http://localhost:4040"
echo ""
echo -e "  ${YELLOW}SMAPI Endpoint${NC} (paste into Sonos developer portal):"
echo -e "  ${GREEN}${PUBLIC_URL}/smapi${NC}"
echo ""
echo -e "  ${YELLOW}Redirect URI${NC} (paste into Control Integration credentials):"
echo -e "  ${GREEN}${PUBLIC_URL}/oauth/callback${NC}"
echo ""
echo -e "  ${YELLOW}Authorise Control API:${NC}"
echo -e "  ${GREEN}http://localhost:8080/auth/login${NC}"
echo ""
echo -e "  ${YELLOW}Sonos app:${NC}"
echo -e "  Settings → Services & Voice → Add a Service → your service name"
echo ""

# ── Open browser if available ──────────────────────────────────────────────────
if command -v xdg-open &> /dev/null; then
    xdg-open http://localhost:8080 &> /dev/null &
fi

# ── Tail logs ─────────────────────────────────────────────────────────────────
echo -e "${BLUE}Streaming server logs (Ctrl+C to stop)...${NC}"
echo ""
$DC logs -f sonos-server

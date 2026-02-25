#!/usr/bin/env bash
set -euo pipefail

# Tutti deployment script — idempotent provisioning for a fresh Ubuntu server.
# Usage: sudo bash deploy.sh

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${CYAN}[info]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ok]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
fatal() { echo -e "${RED}[error]${NC} $*"; exit 1; }

# ── Preflight ────────────────────────────────────────────────────────────────
[[ $EUID -eq 0 ]] || fatal "This script must be run as root (use sudo)."

if [[ -f /etc/os-release ]]; then
    . /etc/os-release
    if [[ "${ID:-}" != "ubuntu" ]]; then
        warn "Detected $ID — this script is tested on Ubuntu 22.04+."
    elif [[ "${VERSION_ID%%.*}" -lt 22 ]]; then
        fatal "Ubuntu ${VERSION_ID} detected. Requires 22.04 or later."
    fi
else
    warn "Cannot detect OS. Proceeding anyway."
fi

# ── Install Docker + Compose ────────────────────────────────────────────────
if command -v docker &>/dev/null; then
    ok "Docker already installed: $(docker --version)"
else
    info "Installing Docker from official apt repository..."
    apt-get update -qq
    apt-get install -y -qq ca-certificates curl gnupg

    install -m 0755 -d /etc/apt/keyrings
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
        | gpg --dearmor -o /etc/apt/keyrings/docker.gpg
    chmod a+r /etc/apt/keyrings/docker.gpg

    echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "$VERSION_CODENAME") stable" \
        > /etc/apt/sources.list.d/docker.list

    apt-get update -qq
    apt-get install -y -qq docker-ce docker-ce-cli containerd.io docker-compose-plugin
    ok "Docker installed: $(docker --version)"
fi

if ! docker compose version &>/dev/null; then
    fatal "docker compose plugin not found. Install it manually."
fi

# ── Firewall (ufw) ──────────────────────────────────────────────────────────
if command -v ufw &>/dev/null; then
    info "Configuring firewall rules..."
    ufw allow 22/tcp   comment "SSH"          >/dev/null 2>&1 || true
    ufw allow 80/tcp   comment "HTTP"         >/dev/null 2>&1 || true
    ufw allow 443/tcp  comment "HTTPS"        >/dev/null 2>&1 || true
    ufw allow 443/udp  comment "HTTP/3 QUIC"  >/dev/null 2>&1 || true
    ufw allow 4433/udp comment "WebTransport" >/dev/null 2>&1 || true
    ufw --force enable >/dev/null 2>&1 || true
    ok "Firewall configured (22/tcp, 80/tcp, 443/tcp, 443/udp, 4433/udp)"
else
    warn "ufw not found — make sure ports 80, 443 (tcp+udp), and 4433/udp are open."
fi

# ── SSH hardening ──────────────────────────────────────────────────────────
info "Hardening SSH configuration..."

if [[ ! -f /root/.ssh/authorized_keys ]] || [[ ! -s /root/.ssh/authorized_keys ]]; then
    warn "No SSH authorized_keys found — skipping SSH hardening to avoid lockout."
    warn "Add your public key to /root/.ssh/authorized_keys and re-run this script."
else
    SSHD_CHANGED=false

    harden_sshd() {
        local key="$1" value="$2"
        # Check main config and drop-in configs
        if grep -rq "^${key} ${value}$" /etc/ssh/sshd_config /etc/ssh/sshd_config.d/ 2>/dev/null; then
            return
        fi
        # Set in main config
        if grep -q "^#*${key}" /etc/ssh/sshd_config; then
            sed -i "s/^#*${key}.*/${key} ${value}/" /etc/ssh/sshd_config
        else
            echo "${key} ${value}" >> /etc/ssh/sshd_config
        fi
        # Override any drop-in configs that conflict
        for f in /etc/ssh/sshd_config.d/*.conf; do
            [[ -f "$f" ]] || continue
            if grep -q "^${key}" "$f"; then
                sed -i "s/^${key}.*/${key} ${value}/" "$f"
            fi
        done
        SSHD_CHANGED=true
    }

    harden_sshd PermitRootLogin prohibit-password
    harden_sshd PasswordAuthentication no
    harden_sshd ChallengeResponseAuthentication no
    harden_sshd MaxAuthTries 3
    harden_sshd UsePAM yes

    if $SSHD_CHANGED; then
        sshd -t 2>/dev/null && systemctl restart ssh 2>/dev/null || systemctl restart sshd 2>/dev/null || true
        ok "SSH hardened and restarted (key-only auth, no passwords)"
    else
        ok "SSH already hardened"
    fi
fi

# ── fail2ban ───────────────────────────────────────────────────────────────
if command -v fail2ban-client &>/dev/null; then
    ok "fail2ban already installed"
else
    info "Installing fail2ban..."
    apt-get update -qq
    apt-get install -y -qq fail2ban
    ok "fail2ban installed"
fi

JAIL_LOCAL="/etc/fail2ban/jail.local"
if [[ ! -f "$JAIL_LOCAL" ]] || ! grep -q "tutti" "$JAIL_LOCAL" 2>/dev/null; then
    cat > "$JAIL_LOCAL" << 'JAIL'
# Tutti — fail2ban configuration
[DEFAULT]
bantime  = 3600
findtime = 600
maxretry = 5

[sshd]
enabled  = true
port     = ssh
filter   = sshd
logpath  = /var/log/auth.log
JAIL
    systemctl enable fail2ban >/dev/null 2>&1
    systemctl restart fail2ban
    ok "fail2ban configured (ban after 5 failures, 1hr ban)"
else
    ok "fail2ban already configured"
fi

# ── Unattended upgrades ───────────────────────────────────────────────────
if dpkg -l unattended-upgrades &>/dev/null 2>&1; then
    ok "unattended-upgrades already installed"
else
    info "Installing unattended-upgrades..."
    apt-get update -qq
    apt-get install -y -qq unattended-upgrades
    ok "unattended-upgrades installed"
fi

# Enable automatic security updates
AUTO_UPGRADES="/etc/apt/apt.conf.d/20auto-upgrades"
if [[ ! -f "$AUTO_UPGRADES" ]]; then
    cat > "$AUTO_UPGRADES" << 'AUTOUP'
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
AUTOUP
    ok "Automatic security updates enabled"
else
    ok "Automatic security updates already configured"
fi

# ── Set up .env ──────────────────────────────────────────────────────────────
ENV_FILE="$REPO_DIR/.env"

if [[ -f "$ENV_FILE" ]]; then
    info "Existing .env found:"
    cat "$ENV_FILE"
    echo
    read -rp "Keep existing .env? [Y/n] " keep
    if [[ "${keep,,}" == "n" ]]; then
        rm "$ENV_FILE"
    fi
fi

if [[ ! -f "$ENV_FILE" ]]; then
    read -rp "Enter domain name [tutti.rocks]: " domain
    domain="${domain:-tutti.rocks}"

    cat > "$ENV_FILE" <<EOF
DOMAIN=${domain}
WT_PORT=4433
ORIGIN=https://${domain}
EOF
    ok "Created .env for domain: ${domain}"
else
    domain=$(grep -oP '^DOMAIN=\K.*' "$ENV_FILE" || echo "localhost")
fi

# ── DNS verification ────────────────────────────────────────────────────────
info "Verifying DNS for ${domain}..."
resolved_ip=$(dig +short "$domain" A 2>/dev/null | tail -1 || true)
public_ip=$(curl -sf --max-time 5 https://api.ipify.org 2>/dev/null || true)

if [[ -z "$resolved_ip" ]]; then
    warn "Could not resolve ${domain}. Make sure your DNS A record is set."
elif [[ -z "$public_ip" ]]; then
    warn "Could not determine server public IP. DNS resolves to: ${resolved_ip}"
elif [[ "$resolved_ip" != "$public_ip" ]]; then
    warn "DNS mismatch: ${domain} resolves to ${resolved_ip}, but this server is ${public_ip}"
    warn "TLS certificate provisioning will fail until DNS is correct."
else
    ok "DNS verified: ${domain} -> ${public_ip}"
fi

# ── Pull latest code ─────────────────────────────────────────────────────────
cd "$REPO_DIR"
info "Pulling latest changes..."
sudo -u "${SUDO_USER:-$(stat -c '%U' .)}" git pull --ff-only || fatal "git pull failed — resolve manually."
ok "Code up to date: $(git log --oneline -1)"

# ── Launch ───────────────────────────────────────────────────────────────────
info "Building images..."
docker compose build
info "Restarting containers..."
docker compose down --remove-orphans 2>/dev/null || true

# Stop any stray containers (from previous project names) still holding our ports
for port in 80 443; do
    blocking=$(docker ps -q --filter "publish=$port" 2>/dev/null)
    if [[ -n "$blocking" ]]; then
        warn "Stopping containers blocking port $port..."
        echo "$blocking" | xargs docker stop 2>/dev/null || true
    fi
done

docker compose up -d

ok "Containers launched"

# ── Health check ─────────────────────────────────────────────────────────────
info "Waiting for services to become healthy..."
max_attempts=30
attempt=0
while [[ $attempt -lt $max_attempts ]]; do
    server_ip=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$(docker compose ps -q server)" 2>/dev/null)
    if [[ -n "$server_ip" ]] && curl -sf --max-time 2 "http://${server_ip}:8080/api/health" >/dev/null 2>&1; then
        ok "Health check passed"
        break
    fi
    attempt=$((attempt + 1))
    sleep 2
done

if [[ $attempt -eq $max_attempts ]]; then
    warn "Health check timed out after 60s. Check logs: docker compose logs"
fi

# ── Summary ──────────────────────────────────────────────────────────────────
echo
echo -e "${GREEN}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  Tutti is running!                               ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║${NC}  URL:    https://${domain}"
echo -e "${GREEN}║${NC}  Health: https://${domain}/api/health"
echo -e "${GREEN}║${NC}  Logs:   docker compose logs -f"
echo -e "${GREEN}╚══════════════════════════════════════════════════╝${NC}"

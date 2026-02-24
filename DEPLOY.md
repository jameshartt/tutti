# Deploying Tutti

## Prerequisites

- **Server**: Ubuntu 22.04+ (tested on Contabo VPS, Portsmouth UK) with at least 1 vCPU and 1 GB RAM
- **Ports**: 80/tcp, 443/tcp, 443/udp, 4433/udp open (the script configures ufw)
- **DNS**: An A record pointing your domain to the server's public IP
- **Root access**: The provisioning script must run as root
- **SSH key**: Ed25519 key pair for passwordless access (see [SSH Key Setup](#ssh-key-setup) below)

## Quick Start

### 1. Set up SSH key (from your local machine)

```bash
# Generate a key pair (skip if you already have one)
ssh-keygen -t ed25519 -C "tutti-deploy" -f ~/.ssh/tutti_contabo

# Copy the public key to your server (will prompt for password)
ssh-copy-id -i ~/.ssh/tutti_contabo.pub root@YOUR_SERVER_IP

# Add a host alias to ~/.ssh/config
cat >> ~/.ssh/config << 'EOF'
Host tutti-contabo
    HostName YOUR_SERVER_IP
    User root
    IdentityFile ~/.ssh/tutti_contabo
    IdentitiesOnly yes
EOF
chmod 600 ~/.ssh/config
```

### 2. Deploy

```bash
ssh tutti-contabo
git clone https://github.com/jameshartt/tutti.git
cd tutti
sudo bash deploy.sh
```

The script is idempotent — safe to re-run at any time.

## Architecture

```
                  Internet
                     │
        ┌────────────┼────────────┐
        │ 80/tcp   443/tcp   443/udp
        ▼            ▼            ▼
   ┌─────────────────────────────────┐
   │            Caddy                │  Auto-TLS (Let's Encrypt)
   │   HTTP→HTTPS redirect + H2/H3  │
   └──┬──────────┬──────────┬───────┘
      │          │          │
      │ /api/*   │ /ws      │ /*
      ▼          ▼          ▼
   ┌────────────────┐  ┌─────────┐
   │  tutti-server   │  │ client  │
   │  (C++, :8080)   │  │ (Node,  │
   │  WS relay :8081 │  │  :3000) │
   └───────┬─────────┘  └─────────┘
           │
      4433/udp (direct, not proxied)
           │
   ┌───────┴─────────┐
   │  WebTransport    │  QUIC datagrams
   │  (low-latency)   │  for real-time audio
   └──────────────────┘
```

## Manual Deployment

If you prefer not to use the script, follow these steps.

> **Note**: Set up your SSH key first — see [SSH Key Setup](#1-set-up-ssh-key-from-your-local-machine) in Quick Start above.

### 1. Install Docker

```bash
# Official Docker install (Ubuntu)
curl -fsSL https://get.docker.com | sh
```

### 2. Open Firewall Ports

```bash
sudo ufw allow 22/tcp    # SSH
sudo ufw allow 80/tcp    # HTTP
sudo ufw allow 443/tcp   # HTTPS
sudo ufw allow 443/udp   # HTTP/3 (QUIC)
sudo ufw allow 4433/udp  # WebTransport
sudo ufw enable
```

### 3. Configure Environment

```bash
cp .env.example .env
# Edit .env — set DOMAIN to your domain name:
#   DOMAIN=tutti.rocks
#   WT_PORT=4433
#   ORIGIN=https://tutti.rocks
```

### 4. Build and Start

```bash
docker compose up -d --build
```

### 5. Verify

```bash
curl https://your-domain.com/api/health
# → {"status":"ok"}
```

## Security

The deployment script automatically hardens the server. Here's what it does and how to verify.

### SSH Hardening

The script configures `/etc/ssh/sshd_config` (and drop-in files in `sshd_config.d/`) to:

- **Key-only root login** — `PermitRootLogin prohibit-password`
- **No password auth** — `PasswordAuthentication no`
- **Max 3 auth attempts** — `MaxAuthTries 3`

**Safety check**: The script will skip SSH hardening if no `authorized_keys` file exists, to prevent lockout.

Verify password auth is disabled:

```bash
# Should fail with "Permission denied (publickey)"
ssh -o PubkeyAuthentication=no root@YOUR_SERVER_IP
```

### fail2ban

Installed and configured to protect SSH:

- **Ban trigger**: 5 failed attempts within 10 minutes
- **Ban duration**: 1 hour
- **Config**: `/etc/fail2ban/jail.local`

Check status:

```bash
sudo fail2ban-client status sshd
```

### Unattended Upgrades

Automatic security updates are enabled via `unattended-upgrades`. The system checks for and installs security patches daily.

Check status:

```bash
sudo systemctl status unattended-upgrades
```

## Operations

### View Logs

```bash
# All services
docker compose logs -f

# Single service
docker compose logs -f server
docker compose logs -f client
docker compose logs -f caddy
```

### Restart

```bash
docker compose restart
```

### Update (Pull + Rebuild)

```bash
git pull
docker compose up -d --build
```

### Stop

```bash
docker compose down
```

### Full Reset (removes volumes including TLS certs)

```bash
docker compose down -v
```

## Monitoring

### Health Endpoint

```bash
curl https://your-domain.com/api/health
# → {"status":"ok"}
```

### Real-Time Priority Check

The server container has `SYS_NICE` capability and `rtprio: 99`. Verify inside the container:

```bash
docker compose exec server cat /proc/self/status | grep -i cap
```

### Container Status

```bash
docker compose ps
```

## TLS

Caddy automatically provisions TLS certificates from Let's Encrypt. No manual configuration is needed.

- **First request**: Caddy obtains the cert on-demand when the first HTTPS request arrives. This takes a few seconds.
- **Renewal**: Caddy auto-renews certificates before they expire. If the Caddy container is recreated, it reuses certs stored in the `caddy_data` Docker volume.
- **WebTransport certs**: The C++ server reads Caddy's Let's Encrypt certs from the shared `caddy_data` volume. If certs aren't yet available (first boot), it generates a self-signed cert as a temporary fallback.

## Troubleshooting

### DNS not pointing to this server

```bash
dig +short your-domain.com
curl -s https://api.ipify.org
```

Both should return the same IP. DNS propagation can take up to 48 hours but usually completes within minutes.

### Ports blocked

Test from another machine:

```bash
nc -zv your-domain.com 443
nc -zuv your-domain.com 4433
```

Common causes: cloud provider security group, ISP firewall, or ufw not configured.

### WebTransport not connecting

1. Check that port **4433/udp** is open and not blocked by a firewall or NAT.
2. Ensure the server has valid TLS certs (check `docker compose logs server` for cert messages).
3. Browsers fall back to WebRTC (WebSocket signaling) automatically if WebTransport is unavailable.

### TLS certificates not yet ready

On first boot, Caddy needs a few seconds to obtain certificates. If you see certificate errors:

```bash
# Check Caddy logs for ACME progress
docker compose logs caddy | grep -i "certificate\|acme\|tls"

# Force Caddy to retry
docker compose restart caddy
```

### Server container crashing

```bash
# Check server logs for crash reason
docker compose logs server

# Rebuild from scratch
docker compose up -d --build --force-recreate server
```

#!/usr/bin/env bash
# Run as root on the droplet: bash bootstrap.sh /path/to/dodo.pub
set -euo pipefail

if [[ $EUID -ne 0 || $# -ne 1 ]]; then
  echo "Usage: sudo bash bootstrap.sh /path/to/deployment-key.pub" >&2
  exit 1
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
public_key=$(cat -- "$1")
[[ $public_key == ssh-ed25519\ * && $public_key != *$'\n'* ]]

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential liburing-dev python3 sudo

if ! id littlebear >/dev/null 2>&1; then
  useradd --system --user-group --home-dir /var/lib/littlebear --shell /usr/sbin/nologin littlebear
fi
if ! id drawdo-deploy >/dev/null 2>&1; then
  useradd --create-home --user-group --shell /bin/bash drawdo-deploy
fi

install -d -o drawdo-deploy -g drawdo-deploy -m 755 /opt/littlebear
install -d -o drawdo-deploy -g drawdo-deploy -m 755 /opt/littlebear/releases /opt/littlebear/incoming
install -d -o littlebear -g littlebear -m 750 /var/lib/littlebear
install -d -o drawdo-deploy -g drawdo-deploy -m 700 /home/drawdo-deploy/.ssh
authorized_keys=/home/drawdo-deploy/.ssh/authorized_keys
touch "$authorized_keys"
if ! grep -qxF "restrict $public_key" "$authorized_keys"; then
  printf 'restrict %s\n' "$public_key" >> "$authorized_keys"
fi
chown drawdo-deploy:drawdo-deploy "$authorized_keys"
chmod 600 "$authorized_keys"

sudoers=$(mktemp)
trap 'rm -f "$sudoers"' EXIT
cat > "$sudoers" <<'EOF'
drawdo-deploy ALL=(root) NOPASSWD: /usr/bin/systemctl restart littlebear.service, /usr/bin/systemctl stop littlebear.service
EOF
visudo -cf "$sudoers"
install -o root -g root -m 440 "$sudoers" /etc/sudoers.d/drawdo-deploy
install -o root -g root -m 644 "$script_dir/littlebear.service" /etc/systemd/system/littlebear.service
systemctl daemon-reload
systemctl enable littlebear.service
echo "Droplet ready for deployment. The first successful deployment starts Littlebear."

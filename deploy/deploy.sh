#!/usr/bin/env bash
# Invoked over SSH as drawdo-deploy after uploading the matching source archive.
set -euo pipefail

release_id=${1:?Expected a release ID}
if [[ ! $release_id =~ ^[0-9a-f]{40}-[0-9]+-[0-9]+$ ]]; then
  echo "Invalid release ID" >&2
  exit 1
fi

deploy_root=/opt/littlebear
archive="$deploy_root/incoming/$release_id.tar.gz"
release="$deploy_root/releases/$release_id"
next_link="$deploy_root/current.next.$$"

# Also serialize deployments started outside GitHub Actions.
exec 9> "$deploy_root/deploy.lock"
flock -w 120 9
trap 'rm -f "$archive" "$next_link"' EXIT

test -f "$archive"
mkdir "$release"
tar --extract --gzip --file="$archive" --directory="$release" --no-same-owner
make -C "$release/littlebear" CXX=g++
test -x "$release/littlebear/core"
printf '%s\n' "${release_id:0:40}" > "$release/REVISION"

previous=$(readlink -f "$deploy_root/current" || true)
ln -s "$release" "$next_link"
mv -Tf "$next_link" "$deploy_root/current"

check_release() {
  local attempt
  for attempt in {1..10}; do
    echo "Checking Littlebear startup ($attempt/10)..."
    if systemctl is-active --quiet littlebear.service &&
       python3 "$release/deploy/healthcheck.py"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

if sudo -n /usr/bin/systemctl restart littlebear.service && check_release; then
  echo "Littlebear deployed: $release_id"
else
  echo "Startup check failed; restoring the previous release." >&2
  systemctl --no-pager --full status littlebear.service || true
  if [[ -n $previous && -d $previous ]]; then
    ln -s "$previous" "$next_link"
    mv -Tf "$next_link" "$deploy_root/current"
    sudo -n /usr/bin/systemctl restart littlebear.service
  else
    sudo -n /usr/bin/systemctl stop littlebear.service
    rm -f "$deploy_root/current"
  fi
  exit 1
fi

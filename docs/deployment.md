# Deploying Littlebear

The user explicitly authorized deploying the latest server whenever needed
(2026-10-08). Littlebear is a disposable cache: no additional approval or cache
backup is required. Verify startup and protocol compatibility after deployment.

Pushing to `main`, including merging a pull request into `main`, runs
`.github/workflows/deploy.yml`. The workflow can also be run manually on `main`.
It first builds the server and runs component tests with and without address
and undefined-behavior sanitizers on a GitHub runner. Pull requests to `main`
run those checks too; only `main` deploys. Once the checks pass, the workflow
uploads the exact tested Linux binary to `147.182.181.182`, switches the
current release, and restarts the systemd service. The checks job packages the
executable, schema digest and protocol health check; the deploy job downloads
that artifact. No server source or remote compiler is needed.
Deployments are serialized and do not cancel a deployment already in progress.

## Droplet setup

Copy `deploy/bootstrap.sh`, `deploy/littlebear.service`, and the deployment public
key to a temporary directory on the server, then run as root:

```sh
bash bootstrap.sh /path/to/dodo.pub
```

This installs the compiler, liburing and libsodium development packages, creates
`drawdo-deploy` for SSH/builds and `littlebear` for the running service, and
installs the unit. The deployment account can restart or stop only this service
through sudo. Bootstrap enables the service; the first deployment starts it.
Service-unit changes require rerunning bootstrap as root.

Configure these repository Actions secrets:

| Secret | Value |
| --- | --- |
| `DODO_SSH_PRIVATE_KEY` | The unencrypted deployment private key matching the authorized public key |
| `DODO_SSH_KNOWN_HOSTS` | The pinned SSH host-key entry for `147.182.181.182` |

The workflow verifies the pinned host key on every connection. For initial
setup, the host key was recorded on the first SSH connection to this droplet.
If the droplet is replaced, verify and update the host-key secret.

## Files and service

| Path | Purpose |
| --- | --- |
| `/opt/littlebear/releases/<commit>-<run>-<attempt>/` | Tested executable, health check and schema bundle |
| `/opt/littlebear/current` | Symlink to the active release |
| `/opt/littlebear/current/REVISION` | Deployed Git commit |
| `/var/lib/littlebear/` | Runtime cache files, separate from release directories |
| `/etc/systemd/system/littlebear.service` | Service definition |

Build failures leave the running release untouched. Startup-check failures
restore and restart the previous release, or stop the service if there was no
previous release. Releases are retained for inspection and manual rollback;
remove old inactive releases as needed to reclaim disk space.

The startup check sends an LBS5 connection heartbeat to localhost port 7777 and
verifies the correlation IDs and success response. It neither creates namespaces
nor writes records. Storage/sync correctness is covered by the CI integration tests.
Restarting discards the cache; owner clients repopulate it.

The active GitHub workflow already deploys on pushes/merges to `main`; recent
runs were verified successful. The current local workflow changes additionally
pass the tested executable between jobs with pinned artifact actions. These
workflow changes take effect when committed and merged.

The acknowledgment-free sync validation deployed a locally built x86-64 binary using
the same bundle and activation script on 2026-10-08. Active release:
`/opt/littlebear/releases/d891d8b86795aafed8162f71644356c67e040b4e-20261008-5/`.
This is a **working-tree build**, based on that commit; it is not a GitHub Actions
run or a claim that the uncommitted source is on GitHub. `SHA256SUMS` identifies
the executable, health script and schema. The server's installed unit still
invokes `littlebear/core`; deployment now creates a small compatibility launcher
at that path, so both the old unit and the current `littlebear/build/server` unit
work without a privileged unit update.

A first local ARM64 bundle was rejected at startup on the x86-64 server. The
previous release was restored and restarted after systemd's rate-limit window.
Deployment now checks ELF architecture **before** switching the running release.
The corrected x86-64 bundle passed its protocol startup check. No cache backup
was created; cache replacement remains authorized.

As root on the droplet:

```sh
systemctl status littlebear
journalctl -u littlebear -n 100 --no-pager
cat /opt/littlebear/current/REVISION
```

The service listens on port 7777 on all interfaces, preserving the deployment
endpoint. Configure Datafriend with that port (its standalone default is 7778).
This setup does not change host or DigitalOcean firewall rules. The protocol
allows public reads and requires Ed25519 signatures for owner writes.

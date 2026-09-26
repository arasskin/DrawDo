# Deploying Littlebear

Pushing to `main`, including merging a pull request into `main`, runs
`.github/workflows/deploy.yml`. The workflow can also be run manually on `main`.
It uploads the exact checked-out revision to `147.182.181.182`, builds Littlebear
on Ubuntu 24.04, switches the current release, and restarts the systemd service.
Deployments are serialized and do not cancel a deployment already in progress.

## Droplet setup

Copy `deploy/bootstrap.sh`, `deploy/littlebear.service`, and the deployment public
key to a temporary directory on the server, then run as root:

```sh
bash bootstrap.sh /path/to/dodo.pub
```

This installs the compiler and liburing development package, creates
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
| `/opt/littlebear/releases/<commit>-<run>-<attempt>/` | Uploaded source and compiled executable |
| `/opt/littlebear/current` | Symlink to the active release |
| `/opt/littlebear/current/REVISION` | Deployed Git commit |
| `/var/lib/littlebear/` | Runtime cache files, separate from release directories |
| `/etc/systemd/system/littlebear.service` | Service definition |

Build failures leave the running release untouched. Startup-check failures
restore and restart the previous release, or stop the service if there was no
previous release. Releases are retained for inspection and manual rollback;
remove old inactive releases as needed to reclaim disk space.

The current startup check sends `add 0` to localhost port 7777 and verifies the
prototype's echo response. It checks the request loop, not storage or sync
correctness. Update `deploy/healthcheck.py` when the real protocol is implemented.
Restarting currently loses any in-memory state; durable cache recovery is not
part of this deployment setup.

As root on the droplet:

```sh
systemctl status littlebear
journalctl -u littlebear -n 100 --no-pager
cat /opt/littlebear/current/REVISION
```

The prototype listens on port 7777 on all interfaces. This setup does not change
host or DigitalOcean firewall rules. The application remains the existing
unauthenticated parser/echo prototype until the backend checklist is implemented.

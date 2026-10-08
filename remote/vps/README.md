# Soul Link relay maintenance

SSH: `ubuntu@193.70.89.162`. Player endpoint: `gbfbc.com:8443`. Nginx keeps the existing site on ports 80/443. Players connect outbound with verified TLS.

## Service

The Python modules are in `/opt/soullink/`. `soullink.service` runs under the `soullink` system account and starts at boot. Certificate files are in `/etc/soullink/`; the Certbot hook `/etc/letsencrypt/renewal-hooks/deploy/50-soullink` copies renewed certificates and restarts the relay.

```sh
sudo systemctl status soullink
sudo journalctl -u soullink -n 50 --no-pager
sudo systemctl restart soullink
```

Rooms live in memory. Empty rooms expire after ten minutes. The creator reopens a room with the same code; a lone joining player waits for the creator. Restarting the relay briefly disconnects players, who reconnect automatically. Saves are never stored on the VPS.

## Deploying

Copy `network.py`, `companion_core.py`, `public_relay.py` and the deployment files into a staging folder, then run:

```sh
sudo bash deploy.sh /etc/letsencrypt/live/gbfbc.com
```

The script checks Python 3.10+, the certificate/key pair and port availability. On a new server, allow TCP 8443 through the applicable firewall. Keep certificate and DNS-name verification enabled.

The local web companion runs only on player PCs. Its module and static files do not need to be exposed on the VPS. When changing player-facing relay messages, update the shared network/core modules as well.

```powershell
python -X utf8 tests/soul_link/test_live_companion.py
python -X utf8 tests/soul_link/test_vps_live.py gbfbc.com 8443
```

## Emulator downloads

Nginx serves public archives from `/var/www/soullink-downloads/` at `https://gbfbc.com/soullink-downloads/`. Versioned URLs are immutable. `mgba-soullink-0.10.5-2.zip` contains the emulator, DLLs and licenses; its source archive contains the modified MPL 2.0 sources, patches and build instructions. No ROM or player save is published there.

`remote/mgba-autoload.json` pins download and installed-file hashes. A new binary needs new archive URLs, new hashes and a rebuilt player ZIP. The current English UI uses the existing binary; only the default language setting changes.

To remove the relay, stop and disable `soullink`, remove its TCP 8443 rule and its dedicated Certbot hook. Keep certificates and renewal jobs shared with the existing site.

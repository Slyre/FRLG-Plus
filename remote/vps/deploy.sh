#!/usr/bin/env bash
set -euo pipefail
# Run from the extracted server bundle after obtaining the DNS certificate.
if [[ ${EUID} -ne 0 || $# -ne 1 ]]; then
    echo 'Usage : sudo bash deploy.sh /etc/letsencrypt/live/gbfbc.com' >&2
    exit 1
fi
task_source=$(cd -- "$(dirname -- "$0")" && pwd)
task_certificate=$1
python3 - "$task_certificate" <<'PY'
import pathlib, ssl, sys
if sys.version_info < (3, 10):
    raise SystemExit('Python 3.10 ou plus récent est nécessaire.')
root = pathlib.Path(sys.argv[1])
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(root / 'fullchain.pem', root / 'privkey.pem')
PY
if ! systemctl is-active --quiet soullink && [[ -n $(ss -H -ltn 'sport = :8443') ]]; then
    echo 'Le port 8443 est déjà utilisé. Aucune modification effectuée.' >&2
    exit 1
fi
if ! getent passwd soullink >/dev/null; then
    useradd --system --home /opt/soullink --shell /usr/sbin/nologin soullink
fi
install -d -o root -g root -m 755 /opt/soullink
install -d -o root -g soullink -m 750 /etc/soullink
install -o root -g root -m 644 "$task_source/network.py" "$task_source/companion_core.py" "$task_source/public_relay.py" /opt/soullink/
install -o root -g soullink -m 640 "$task_certificate/fullchain.pem" /etc/soullink/fullchain.pem
install -o root -g soullink -m 640 "$task_certificate/privkey.pem" /etc/soullink/privkey.pem
install -o root -g root -m 644 "$task_source/soullink.service" /etc/systemd/system/soullink.service
install -o root -g root -m 755 "$task_source/renew-certificate.sh" /opt/soullink/renew-certificate.sh
printf '%s\n' "$task_certificate" > /etc/soullink/certificate-lineage
chmod 644 /etc/soullink/certificate-lineage
install -d -o root -g root -m 755 /etc/letsencrypt/renewal-hooks/deploy
install -o root -g root -m 755 "$task_source/renew-certificate.sh" /etc/letsencrypt/renewal-hooks/deploy/50-soullink
systemctl daemon-reload
systemctl enable soullink
systemctl restart soullink
systemctl --no-pager --full status soullink
echo 'Relais installé. Vérifier les DNS et autoriser TCP 8443 dans les pare-feu applicables.'

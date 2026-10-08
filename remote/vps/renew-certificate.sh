#!/usr/bin/env bash
set -euo pipefail
# Certbot deploy hook; configure only after checking the actual certificate name.
task_lineage=${RENEWED_LINEAGE:-}
if [[ ${EUID} -ne 0 || ! -f /etc/soullink/certificate-lineage ]]; then
    exit 0
fi
task_expected=$(cat /etc/soullink/certificate-lineage)
if [[ -z $task_lineage || $task_lineage != "$task_expected" ]]; then
    exit 0
fi
python3 - "$task_lineage" <<'PY'
import pathlib, ssl, sys
root = pathlib.Path(sys.argv[1])
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(root / 'fullchain.pem', root / 'privkey.pem')
PY
install -o root -g soullink -m 640 "$task_lineage/fullchain.pem" /etc/soullink/fullchain.pem
install -o root -g soullink -m 640 "$task_lineage/privkey.pem" /etc/soullink/privkey.pem
systemctl restart soullink

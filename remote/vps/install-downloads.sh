#!/bin/bash
# Add an isolated public download path to the existing gbfbc.com HTTPS vhost.
set -euo pipefail
stage="${1:?deployment directory required}"
config=/etc/nginx/sites-available/gbftools.conf
if [ ! -f "$config" ]; then config=/etc/nginx/sites-enabled/gbftools.conf; fi
nginx -t
install -d -m 755 /var/www/soullink-downloads /etc/nginx/snippets /etc/nginx/soullink-backups
for filename in mgba-soullink-0.10.5-1.zip mgba-soullink-0.10.5-1-source.zip; do
    if [ -e "/var/www/soullink-downloads/$filename" ]; then
        cmp "$stage/$filename" "/var/www/soullink-downloads/$filename"
    else
        install -m 644 "$stage/$filename" "/var/www/soullink-downloads/$filename"
    fi
done
backup="/etc/nginx/soullink-backups/gbftools-$(date -u +%Y%m%dT%H%M%SZ).conf"
cp -a "$config" "$backup"
install -m 644 "$stage/soullink-downloads.conf" /etc/nginx/snippets/soullink-downloads.conf
python3 - "$config" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); s=p.read_text()
include='    include /etc/nginx/snippets/soullink-downloads.conf;'
if include not in s:
    marker='    server_name gbfbc.com;'
    if s.count(marker)!=2: raise SystemExit('Unexpected vhost; no configuration changed')
    p.write_text(s.replace(marker,marker+'\n'+include,1))
PY
if ! nginx -t; then
    cp -a "$backup" "$config"
    exit 1
fi
systemctl reload nginx
sha256sum /var/www/soullink-downloads/*.zip

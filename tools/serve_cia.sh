#!/bin/sh
# Shares baba3ds.cia on your Wi-Fi and shows a QR code for FBI: Remote Install -> Scan QR Code.
# Only the .cia is served (from its own folder), only to your network, only while this runs (Ctrl+C stops it).
set -e
cd "$(dirname "$0")/.."
[ -f baba3ds.cia ] || { echo "no baba3ds.cia yet: run tools/make_cia.sh first"; exit 1; }
PORT=${PORT:-8000}
IP=${IP:-$(ipconfig getifaddr en0 || ipconfig getifaddr en1 || true)}
[ -n "$IP" ] || { echo "no Wi-Fi address found; run with IP=<this Mac's address>"; exit 1; }
DIR=build-host/serve
mkdir -p "$DIR"
cp baba3ds.cia "$DIR/"
URL="http://$IP:$PORT/baba3ds.cia"
qrencode -t ANSIUTF8 -m 2 "$URL"
echo "Scan with FBI (Remote Install -> Scan QR Code): $URL"
cd "$DIR"
exec python3 -m http.server "$PORT" --bind "$IP"

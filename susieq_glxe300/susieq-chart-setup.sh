#!/bin/sh
# susieq-chart-setup.sh — ensimmäisen kerran asennus GL-XE300:lla
# Vaatii internet-yhteyden (Leaflet-lataus)
# Ajo: ssh root@192.168.8.1, sitten sh /tmp/susieq-chart-setup.sh

set -e

echo "=== SusieQ karttaplotteri — asennus ==="

# Luo hakemistot
mkdir -p /usr/share/susieq-chart
mkdir -p /mnt/tfcard/susieq-tiles/osm
mkdir -p /mnt/tfcard/susieq-tiles/seamark

echo "Hakemistot luotu."

# Lataa Leaflet 1.9.4
LEAFLET="https://unpkg.com/leaflet@1.9.4/dist"
echo "Ladataan Leaflet.js..."
curl -sSL "$LEAFLET/leaflet.min.js"  -o /usr/share/susieq-chart/leaflet.min.js
curl -sSL "$LEAFLET/leaflet.min.css" -o /usr/share/susieq-chart/leaflet.min.css
echo "Leaflet ladattu."

# Aseta palvelinkomennot suoritettavaksi
if [ -f /usr/bin/susieq-chart-server.py ]; then
    chmod +x /usr/bin/susieq-chart-server.py
fi
if [ -f /usr/bin/susieq-chart-preload.py ]; then
    chmod +x /usr/bin/susieq-chart-preload.py
fi

# Aktivoi ja käynnistä palvelu (jos init.d asennettu)
if [ -f /etc/init.d/susieq-chart ]; then
    chmod +x /etc/init.d/susieq-chart
    /etc/init.d/susieq-chart enable
    /etc/init.d/susieq-chart start
    echo "Palvelu käynnistetty."
fi

echo ""
echo "Valmis! Karttaplotteri: http://192.168.8.1:8080/"
echo "GPS-testi: curl http://localhost:8080/gps"
echo ""
echo "Muista ajaa tile-esikuormitus kun internet kuuluu:"
echo "  python3 /usr/bin/susieq-chart-preload.py"

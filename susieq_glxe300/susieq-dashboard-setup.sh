#!/bin/sh
# susieq-dashboard-setup.sh — ensimmäisen kerran asennus GL-XE300:lla
# Ajo: ssh root@192.168.8.1, sitten sh /tmp/susieq-dashboard-setup.sh

set -e

echo "=== SusieQ dashboard-palvelin — asennus ==="

mkdir -p /usr/share/susieq-dashboard
echo "Hakemisto luotu."

if [ -f /usr/bin/susieq-dashboard-server.py ]; then
    chmod +x /usr/bin/susieq-dashboard-server.py
fi

if [ -f /etc/init.d/susieq-dashboard ]; then
    chmod +x /etc/init.d/susieq-dashboard
    /etc/init.d/susieq-dashboard enable
    /etc/init.d/susieq-dashboard start
    echo "Palvelu käynnistetty."
fi

echo ""
echo "Valmis! Dashboard: http://192.168.8.1:8081/"
echo "Datatesti: curl http://localhost:8081/data"

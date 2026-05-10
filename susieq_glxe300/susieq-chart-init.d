#!/bin/sh /etc/rc.common
# susieq-chart — karttaplotteri-palvelin (OpenWrt procd-palvelu)
# Asennus: /etc/init.d/susieq-chart
# chmod +x /etc/init.d/susieq-chart
# /etc/init.d/susieq-chart enable
# /etc/init.d/susieq-chart start

USE_PROCD=1
START=95
STOP=10

start_service() {
    procd_open_instance
    procd_set_param command python3 /usr/bin/susieq-chart-server.py
    # Käynnistä automaattisesti uudelleen kaatumisen jälkeen
    procd_set_param respawn 3600 5 0
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_close_instance
}

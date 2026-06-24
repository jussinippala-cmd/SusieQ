#!/bin/sh /etc/rc.common
# susieq-dashboard — dashboard-palvelin (OpenWrt procd-palvelu)
# Asennus: /etc/init.d/susieq-dashboard
# chmod +x /etc/init.d/susieq-dashboard
# /etc/init.d/susieq-dashboard enable
# /etc/init.d/susieq-dashboard start

USE_PROCD=1
START=95
STOP=10

start_service() {
    procd_open_instance
    procd_set_param command /usr/bin/python3 /usr/bin/susieq-dashboard-server.py
    # Käynnistä automaattisesti uudelleen kaatumisen jälkeen
    procd_set_param respawn 3600 5 0
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_close_instance
}

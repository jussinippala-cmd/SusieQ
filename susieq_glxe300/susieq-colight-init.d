#!/bin/sh /etc/rc.common
# susieq-colight — CoLight-kytkinpaneelin BLE-sillan komentojono (OpenWrt procd-palvelu)
# Asennus: /etc/init.d/susieq-colight
# chmod +x /etc/init.d/susieq-colight
# /etc/init.d/susieq-colight enable
# /etc/init.d/susieq-colight start

USE_PROCD=1
START=95
STOP=10

start_service() {
    procd_open_instance
    procd_set_param command /usr/bin/susieq-colight.sh
    # Käynnistä automaattisesti uudelleen kaatumisen jälkeen
    procd_set_param respawn 3600 5 0
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_close_instance
}

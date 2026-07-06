#!/bin/sh
#
# susieq-wake.sh — Herättää 4G-radion, tekee ifdown/ifup vain jos radio oli oikeasti nukkumassa
# Asennus: /usr/bin/susieq-wake.sh  (chmod +x)
# Cron:    0 6 * * * /usr/bin/susieq-wake.sh

gl_modem AT AT+CFUN=1

if [ -f /tmp/susieq_slept ]; then
    logger -t susieq-wake "Heratys: ifdown/ifup wwan"
    # Ei &&-ketjua: ifdown:n paluuarvolla ei ole väliä — ifup pitää ajaa
    # joka tapauksessa, muuten wwan jää alas koko päiväksi.
    sleep 45
    ifdown wwan
    sleep 5
    ifup wwan
    rm -f /tmp/susieq_slept
else
    logger -t susieq-wake "Lepotila ohitettu — yhteys jo paalla, ei ifdown/ifup"
fi

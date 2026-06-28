# Modeemin lepotila-toggle — Design Spec
**Päivämäärä:** 2026-06-28
**Status:** Hyväksytty

## Tavoite

Mahdollistaa GL-XE300-modeemin 4G-lepotilan (klo 22–06) kytkeminen päälle/pois susieq.net-ohjauspaneelista. Asetus on pysyvä (jää voimaan kunnes muutetaan).

## Arkkitehtuuri

```
susieq.net (kirjautunut käyttäjä)
  └── toggle ON/OFF (Ohjauspaneeli-välilehti)
      └── sb.from('modem_settings').update({ sleep_enabled: false })
                                          ↓
                                    Supabase
                                    modem_settings (id=1, sleep_enabled=bool)
                                          ↑
GL-XE300 klo 22:00 (susieq-sleep.sh)
  ├── 1. tarkasta /tmp/susieq_state → away? → ohita
  ├── 2. tarkasta /etc/susieq_no_sleep → manuaalinen yliajo? → ohita
  ├── 3. curl Supabase → sleep_enabled=false? → ohita
  └── 4. kaikki ok → AT+CFUN=4 + touch /tmp/susieq_slept
```

## Komponentit

### 1. Supabase — modem_settings-taulukko

Uusi taulukko, yksi rivi (id=1):

```sql
CREATE TABLE modem_settings (
  id int PRIMARY KEY DEFAULT 1,
  sleep_enabled boolean NOT NULL DEFAULT true,
  updated_at timestamptz DEFAULT now()
);
INSERT INTO modem_settings VALUES (1, true, now());

ALTER TABLE modem_settings ENABLE ROW LEVEL SECURITY;
CREATE POLICY "anon voi lukea"    ON modem_settings FOR SELECT TO anon        USING (true);
CREATE POLICY "auth voi päivittää" ON modem_settings FOR UPDATE TO authenticated USING (true);
```

`updated_at` päivitetään frontend-puolella UPDATE-kutsussa: `{ sleep_enabled: val, updated_at: new Date().toISOString() }` — ei tarvita triggeriä.

### 2. susieq-remote — Toggle UI

**Sijainti:** Ohjauspaneeli-välilehti (`data-panel="paneeli"`), switch-gridin yläpuolella omana korttiaan.

**Rakenne:**
```html
<div class="modem-sleep-card" id="modem-sleep-card">
  <div class="modem-sleep-label">Modeemin lepotila 22–06</div>
  <button class="modem-sleep-toggle" id="modem-sleep-toggle">
    <span class="modem-sleep-state">KÄYTÖSSÄ</span>
  </button>
  <div class="modem-sleep-updated" id="modem-sleep-updated"></div>
</div>
```

**Toiminta:**
- Sivun lataus: lukee `modem_settings` anon-avaimella, näyttää tilan
- Klikkaus: vaatii kirjautumisen; kirjoittaa `sleep_enabled = !current` + `updated_at = now()` authenticated-avaimella
- Visuaalinen tila: `KÄYTÖSSÄ` (brass-väri, on) / `POIS` (muted, off)
- Ei kirjautunut: toggle näkyy mutta on disabled, tooltip "Kirjaudu muuttaaksesi"

**Tyyli:** Sama dark/brass-teema kuin muukin UI. Erillinen kortti switch-gridin yläpuolella, ei osana switch-grid-ruudukkoa.

### 3. susieq-sleep.sh — Supabase-tarkistus

Lisätään kolmanneksi tarkistukseksi ennen `AT+CFUN=4`:

```sh
# Tarkista Supabase-asetus (fallback: nukutaan jos ei vastausta)
SLEEP_ENABLED=$(curl -s --connect-timeout 5 --max-time 10 \
  "${SUPABASE_URL}/rest/v1/modem_settings?id=eq.1&select=sleep_enabled" \
  -H "apikey: ${SUPABASE_SERVICE_KEY}" | \
  python3 -c "import sys,json; d=json.load(sys.stdin); print(d[0]['sleep_enabled'])" 2>/dev/null)
if [ "$SLEEP_ENABLED" = "False" ]; then
    logger -t susieq-sleep "Ohitettu: lepotila poistettu susieq.net-asetuksista"
    exit 0
fi
```

**Fallback:** Jos curl tai python3 epäonnistuu (Supabase ei vastaa, JSON-virhe), `SLEEP_ENABLED` jää tyhjäksi → ei osu `False`-ehtoon → modeemi nukkuu. Turvallisin oletus: ei jää vahingossa hereillä jos yhteys poikki.

## Tiedostomuutokset

| Tiedosto | Muutos |
|---|---|
| Supabase | Uusi taulukko `modem_settings` + RLS-politiikat |
| `susieq-remote/index.html` | Toggle-kortti Ohjauspaneeli-välilehdelle |
| `susieq-remote/app.js` | `loadModemSettings()` + `toggleModemSleep()` funktiot |
| `susieq_glxe300/susieq-sleep.sh` | Supabase-tarkistus ennen CFUN=4 |

## Prioriteetit ja rajoitteet

- Toggle vaatii kirjautumisen kirjoittaakseen — lukeminen on julkista
- Supabase-kutsu klo 22:00 on nopea (yksi REST-pyyntö, ~200 ms) — ei hidasta merkittävästi
- Olemassa olevat ohitusmekanismit (`away`-tila, `/etc/susieq_no_sleep`) säilyvät ja ajavat Supabase-tarkistuksen edelle
- Modeemille ei tarvita muutoksia crontabiin

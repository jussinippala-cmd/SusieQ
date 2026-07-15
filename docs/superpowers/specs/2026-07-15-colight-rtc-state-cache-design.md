# CoLight-tilakehyksen säilytys RTC-muistissa boottien yli — design

**Päivämäärä:** 2026-07-15
**Tila:** hyväksytty (käyttäjä 2026-07-15)

## Ongelma

Cockpit-ESP32:n ohjelmistobootti (esim. HTTP-liveness-watchdog, kuten 2026-07-15 klo 18:57) tyhjentää CoLight-paneelin tilavälimuistin. Paneeli lähettää tilakehyksen (`f9` + 6 tavua) **vain fyysisen kytkinmuutoksen yhteydessä** — ei koskaan connectin/subscriben yhteydessä eikä pyynnöstä. Bootin jälkeen `/colight/state` palauttaa `unknown_state`, modeemin daemon ei kirjoita `switch_state`-rivejä, ja susieq.netin Ohjauspaneeli näyttää "yhteys katkennut" kunnes joku käy fyysisesti painamassa paneelin kytkintä veneellä.

Todettu 2026-07-15: BLE-reconnect toimii bootin jälkeen täydellisesti (paneeli löytyi 2 s, subscribe 3,5 s) — **ainoa puuttuva asia on kehys**. Kehys on deterministinen: paneelin tila muuttuu vain fyysisestä painalluksesta (→ notifikaatio päivittää välimuistin) tai omasta kirjoituksestamme (→ kirjoituspolku päivittää välimuistin), joten bootin hetkellä välimuistin kehys **on** paneelin todellinen tila.

## Ratkaisu

Viimeisin tilakehys kopioidaan `RTC_NOINIT_ATTR`-muistiin aina kun välimuisti päivittyy, ja palautetaan sieltä bootissa kun palautus on turvallista. Sama malli kuin `main.cpp`:n `boot_count`/`restart_note` (magic-sentinel + reset-syyn tarkistus).

### Hylätyt vaihtoehdot

- **NVS-flash:** säilyisi virtakatkon yli — mutta paneeli ja ESP32 jakavat saman pääkytkimen, joten virtakatkossa myös paneeli nollautuu ja vanhan tilan palautus olisi virhe. Lisäksi flash-kuluminen jokaisesta painalluksesta.
- **Palautus Supabasesta modeemin kautta:** uusi endpoint + daemon-muutos + verkkoriippuvuus bootissa; ylimitoitettu kun 6 tavua riittää.

## Tietorakenne (`colight.cpp`)

```cpp
#define COLIGHT_RTC_MAGIC 0xC0119870
RTC_NOINIT_ATTR static uint8_t  colight_rtc_frame[6];
RTC_NOINIT_ATTR static uint32_t colight_rtc_magic;
RTC_NOINIT_ATTR static uint32_t colight_rtc_check;   // tarkistussumma kehyksestä
```

Tarkistussumma ja palautuspäätös eristetään pure-funktioiksi uuteen tiedostopariin `colight_rtc.h`/`.cpp` (ei Arduino/BLE-riippuvuuksia), jotta ne voidaan yksikkötestata natiivisti samaan tapaan kuin `colight_protocol`:

```cpp
uint32_t colight_rtc_checksum(const uint8_t frame[6]);       // FNV-1a 32-bit kehyksen 6 tavusta
bool colight_rtc_frame_valid(uint32_t magic, uint32_t check, const uint8_t frame[6]);
```

`ColightCache`-rakenteeseen ja `ColightResult`-rakenteeseen lisätään `bool restored` (oletus false).

## Tallennuspolku

RTC-kopio (memcpy 6 tavua + magic + check) tehdään täsmälleen niissä kahdessa kohdassa, joissa `colight_cache.frame` jo nyt päivittyy, saman mutex-lukituksen sisällä:

1. `colight_notify_handler` — aidon `f9`-kehyksen saapuessa. Samalla `colight_cache.restored = false` (aito kehys korvaa palautetun).
2. `colight_send_command` — onnistuneen kirjoituksen jälkeen (sama kohta jossa `colight_cache.frame` päivitetään).

Ei uusia lukkoja, ei BLE-kutsuja, ei viivettä — RTC_NOINIT on tavallista muistia kirjoittaa.

## Palautuspolku

`colight_init()`:ssä ennen `colight_task`-taskin luontia (yksisäikeinen vaihe, ei kilpailutilannetta):

```
jos esp_reset_reason() ∈ {ESP_RST_SW, ESP_RST_TASK_WDT, ESP_RST_INT_WDT, ESP_RST_WDT, ESP_RST_PANIC}
   ja colight_rtc_frame_valid(magic, check, frame):
      colight_cache.frame    = colight_rtc_frame
      colight_cache.valid    = true
      colight_cache.restored = true
      colight_log("boot: state restored from RTC")
muuten:
      colight_rtc_magic = 0        // poweron/brownout/roska → nollaa eksplisiittisesti
```

- **Poweron/brownout:** RTC-sisältö on määrittelemätön → magic+check hylkäävät sen käytännössä aina, ja reset-syyehto hylkää sen aina. Käyttäytyminen on täsmälleen nykyinen (`unknown_state` kunnes fyysinen painallus). Tämä kattaa myös pääkytkimen virtakierron, jossa paneeli itse nollautuu.
- **`last_updated_ms`** jätetään palautuksessa arvoon 0 — kenttä kertoo jatkossakin vain tämän bootin aikaisista päivityksistä.

## Käyttäytymismuutokset rajapinnoissa

- `/colight/state` ja `/colight` POST: palautetun kehyksen kanssa `success:true` kuten aidonkin — **etäohjaus toimii heti bootin jälkeen** (käyttäjän hyväksymä valinta 2026-07-15; riski-ikkuna on vain boottikatkon ~15 s, jonka aikana fyysinen painallus tekisi palautetusta kehyksestä vanhentuneen).
- JSON-vastauksiin lisäkenttä `"restored":true` kun tila on RTC-palautettu eikä aitoa kehystä ole vielä nähty. Modeemin `susieq-colight.sh` lukee vain `success`/`state`/`connected`-kentät → lisäkenttä ei vaikuta siihen; susieq.net saa tiedon halutessaan myöhemmin.
- `colight-debug`-lokiin palautusrivi.

## Mihin EI kosketa

BLE-yhteys- ja reconnect-logiikka, backoff, mutex-rakenne (`colight_mutex`/`colight_log_mutex`), hardware-WDT, HTTP-liveness-watchdog, Victron-pause/resume, `colight_protocol.*`, modeemin skriptit ja Supabase-skeema pysyvät kaikki täysin ennallaan. Muutos on välimuistin alustus + kaksi memcpy-kohtaa + JSON-kenttä.

## Virheenkäsittely

| Tilanne | Käyttäytyminen |
|---|---|
| RTC-roska (satunnainen bitti oikein magicissa) | check-summa hylkää; nykykäyttäytyminen |
| Poweron/brownout | reset-syy hylkää; magic nollataan; nykykäyttäytyminen |
| Fyysinen painallus boottikatkon aikana | palautettu kehys vanhentunut → seuraava web-komento kirjoittaa vanhentuneen snapshotin; hyväksytty jäännösriski (ikkuna ~15 s), korjaantuu seuraavasta fyysisestä painalluksesta tai notifikaatiosta |
| Mutex-timeout tallennuskohdissa | RTC-kopio jää tekemättä samalla kun kehyskin jää päivittymättä — RTC ei voi olla välimuistia jäljessä |

## Testaus

1. **Native-yksikkötestit** (`test_colight_rtc`): validi kehys → checksum täsmää; yhden bitin muutos kehyksessä/checkissä/magicissa → hylkäys; nollakehys validilla magicilla+checkillä → hyväksytään (kaikki pois on laillinen tila).
2. **Olemassa olevat testit:** `pio test -e native` (colight_protocol 9/9) pysyy vihreänä; `pio run -e esp32dev` kääntyy.
3. **Kenttäverifiointi veneellä (OTA:n jälkeen):**
   - Aja web-komento (esim. kanava 10 päälle) → varmista tila sivustolla.
   - Boottaa ESP32 ohjelmallisesti (esim. OTA-reflash tai liveness-testi).
   - Varmista `/colight/state` palauttaa saman tilan `restored:true` ja susieq.netin Ohjauspaneeli herää ~2 min sisällä ilman fyysistä painallusta.
   - Varmista kytkimen etäohjaus toimii heti bootin jälkeen ja fyysinen painallus pudottaa `restored`-lipun.

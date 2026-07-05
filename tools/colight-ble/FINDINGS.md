# CoLight BLE -selvityksen löydökset

**Päivämäärä:** 2026-06-20
**Laite:** COLIGHT 12/8 Gang Switch Panel, BLE-nimi `MG-P1C-12P`
**Tausta/prosessi:** [`docs/superpowers/specs/2026-06-14-colight-ble-selvitys-design.md`](../../docs/superpowers/specs/2026-06-14-colight-ble-selvitys-design.md)

## GATT-rakenne

Kaksi palvelua, ei tunnettuja candidate-UUID:eja (FFE0/FFD5-perheet eivät löytyneet):

| Palvelu | Characteristic | Properties | Rooli |
|---|---|---|---|
| `0003cbbb-0000-1000-8000-00805f9beff0` | `...beff1` | write-without-response | tuntematon (testattu, ei vaikutusta) |
| | `...beff2` | write-without-response | tuntematon (testattu, ei vaikutusta) |
| | `...beff3` | write-without-response | tuntematon (testattu, ei vaikutusta) |
| | `...beff4` | write-without-response | tuntematon (testattu, ei vaikutusta) |
| | `...beffa` | write-without-response + notify | **tilaraportointi** (vahvistettu) + testattu kirjoituksena (ei vaikutusta) |
| `9e5d1e47-5c13-43a0-8635-82ad38a1386f` | `e3dd50bf-f7a7-4e99-838e-570a086c666b` | indicate + notify + write | vastaa kirjoituksiin `00`-tavulla (ACK?), ei havaittua kytkinvaikutusta |
| | `92e86c7a-d961-4091-b74f-2409e72efe36` | write | tuntematon (testattu, ei vaikutusta) |
| | `347f7608-2e2d-47eb-913b-75d4edc4de3b` | read | laitetunniste: `D20B_MGP1_V1025` |

## Tilaraportointiformaatti (vahvistettu, täysin selvitetty)

`...beffa` lähettää useita kehystyyppejä jatkuvasti (~2-5 s välein), erottuvat ensimmäisestä tavusta (`f1`, `f2`, `f3`, `f4`, `f9`). Näistä **`f9`-kehys** raportoi kytkintilan muutokset:

Lepoarvo (idle):
```
f9 22 00 00 00 00 00 00 19 04 04 04 04 04 04 04 04 04 04 04 04
```

Kun fyysistä kytkintä käännetään, tavu-indeksit 1–6 (6 tavua) saavat hetkellisesti `+0x10` tai `+0x01` (palautuu ~1-2 s kuluttua takaisin nollaan). 6 tavua × 2 nibble-bittiä = 12 kanavaa:

| Kanava | Tavu (index) | Nibble |
|---|---|---|
| 1 | 1 | 0x10 |
| 2 | 1 | 0x01 |
| 3 | 2 | 0x10 |
| 4 | 2 | 0x01 |
| 5 | 3 | 0x10 |
| 6 | 3 | 0x01 |
| 7 | 4 | 0x10 |
| 8 | 4 | 0x01 |
| 9 | 5 | 0x10 |
| 10 | 5 | 0x01 |
| 11 | 6 | 0x10 |
| 12 | 6 | 0x01 |

Vahvistettu empiirisesti kääntämällä kaikki 12 kytkintä yksi kerrallaan (kytkin 9 erikseen, koska siihen ei ole mitään kytketty — käytettiin vahvistamaan ennustettu tavu5/0x10-paikka).

**Tämä riittää susieq-remoten "tilannekuva"-osuuden toteutukseen** — ei vaadi write-puolen ratkaisua.

## Yhteysrajoitustesti

Ei tehty erikseen suunnitellulla tavalla (puhelimen CoLight-sovellus + Mac samanaikaisesti), mutta sivuhavaintona: **kaksi samanaikaista `BleakClient`-yhteyttä Macilta samaan laitteeseen toimi ongelmitta** (yksi pitkäkestoinen `monitor`-prosessi + erilliset lyhyet `write`-yhteydet rinnakkain). Viittaa siihen, että laite/CoreBluetooth sallii useamman keskusyksikön, mutta tätä ei ole vahvistettu puhelinsovelluksen kanssa.

## Write-komennot — RATKAISTU 2026-07-05

Aiemmat sokkona-arvailut (alla, historiallinen dokumentaatio) epäonnistuivat koska oikea komento käyttää täysin eri kehysmuotoa. Ratkaisu löytyi kaappaamalla oikean CoLight-sovelluksen liikenne Android-puhelimen (Samsung Galaxy A32) `adb bugreport`-komennolla — **ei dumpsys bluetooth_manager -yhteenveto** (se jättää ACL/GATT-datan aina pois yksityisyyssyistä riippumatta snoop-tilasta), vaan `adb bugreport bugreport.zip` sisältää oikean täyden `FS/data/log/bt/btsnoop_hci.log`-tiedoston suoraan zipissä, standardimuodossa (ei btsnooz-purkua tarvita, suoraan tsharkiin).

Prosessi: `adb shell settings put global bluetooth_btsnoop_default_mode full` → kierrätä Bluetooth (`adb shell svc bluetooth disable && adb shell svc bluetooth enable`) → tee toiminto CoLight-sovelluksella → `adb bugreport bugreport.zip` → pura `FS/data/log/bt/btsnoop_hci.log` → `tshark -r btsnoop_hci.log -Y "btatt.opcode==0x52"`.

**Kirjoitus-characteristic:** `0003cbbb-0000-1000-8000-00805f9beffa` (sama kuin tilaraportointi, GATT handle 0x000b), write-without-response (opcode 0x52).

**Kehysmuoto:** `f5 G1 G2 G3 G4 G5 G6` (7 tavua), missä G1..G6 vastaavat suoraan tilaraportoinnin tavu-indeksejä 1-6 (ks. yllä oleva taulukko) — **paitsi G1:n lepoarvo on `0x22`** (ei `0x00` kuten G2-G6). Kanavan N kytkeminen päälle = lisää `+0x10` (pariton kanava) tai `+0x01` (parillinen kanava) kyseisen ryhmän tavuun. Kaikkien kanavien pois päältä = pelkkä lepoarvo `f5220000000000`.

**Live-vahvistettu** (Macilta suoraan `colight_ble.py write`-komennolla, käyttäjä näki fyysisen vaikutuksen): kanava 10 (INTERIOR) päälle/pois molemmat toimivat.

**Täysi kanavataulukko** (johdettu kaavasta + kaapatusta liikenteestä kun kaikki 12 painiketta painettiin sovelluksesta läpi):

| Kanava | Painike | Komento (ON) | Vahvistustapa |
|---|---|---|---|
| 1 | LOWER | `f5320000000000` | kaapattu (käyttäjä painoi fyysisesti, "rommikaappi kiinni") |
| 2 | RAISE | `f5230000000000` | kaapattu |
| 3 | HEATER | `f5221000000000` | kaapattu |
| 4 | WATER | `f5220100000000` | kaapattu |
| 5 | DECK LIGHTS | `f5220010000000` | kaapattu |
| 6 | STEREO | `f5220001000000` | kaapattu |
| 7 | OUTLETS | `f5220000100000` | kaapattu |
| 8 | RADIO | `f5220000010000` | kaapattu |
| 9 | (tyhjä) | `f5220000001000` | **johdettu kaavasta, ei kaapattu eikä testattu** |
| 10 | INTERIOR | `f5220000000100` | kaapattu JA live-vahvistettu fyysisesti (Mac→paneeli) |
| 11 | NAV LIGHTS | `f5220000000010` | kaapattu |
| 12 | ANCHOR LIGHT | `f5220000000001` | kaapattu |
| kaikki | POIS | `f5220000000000` | kaapattu (baseline-kehys jokaisen ON-testin jälkeen) |

**VAHVISTETTU 2026-07-05 (live-testi Macilta) — kirjoitus on koko tilan snapshot, EI delta:** kirjoitettiin DECK LIGHTS (ch5) päälle (`f5220010000000`), sitten STEREO (ch6) päälle (`f5220001000000`) — DECK LIGHTS **sammui**, koska sen bitti ei ollut mukana toisessa kirjoituksessa. Yhden kanavan ohjaus on siis aina rakennettava lukemalla ensin nykyinen kokonaistila (f9-notifikaatiosta) ja säilyttämällä kaikkien muiden kanavien bitit muuttumattomina — muuten komento nollaa/ylikirjoittaa kaikki muut samanaikaisesti päällä olevat kanavat. Tämä on pakollinen arkkitehtuurivaatimus mille tahansa ohjaustoteutukselle (myös susieq-remoten web-ohjaukselle).

### Historia: aiemmat epäonnistuneet arvailut (2026-06-20, ennen läpimurtoa)

Testattu kanava 10 (sisävalo), fyysinen kytkin pidetty ON-asennossa koko ajan. Yhtäkään seuraavista ei havaittu vaikuttavan valoon:

| Characteristic | Payloadit testattu |
|---|---|
| `...beffa` | täysi tilakehys kanavan 10 "on"-pulssille (`f922000000010019040404040404040404040404`) |
| `e3dd50bf-...` | sama täysi kehys; `0a01` |
| `...beff1` | `01`, `00`, `0001`, `0a01` |
| `...beff2` | `01`, `00`, `0001`, `0a01` |
| `...beff3` | `01`, `00`, `0001`, `0a01` |
| `...beff4` | `01`, `00`, `0001`, `000100`, `0a01` |
| `92e86c7a-...` | `01`, `00`, `0001`, `0a01` |

`e3dd50bf-...` vastasi kaikkiin kirjoituksiin yhden tavun `00`-indikaatiolla — todennäköisesti yleinen ACK riippumatta sisällöstä, ei vahvistus oikeasta komentomuodosta. Syy epäonnistumiseen: oikea komento käyttää `f5`-alkuista kehystä, jota ei arvattu.

## Kytkentäkartta

Täytetty 2026-06-21 paneelin painikemerkintöjen perusteella (kuva paneelista). Paneelin fyysinen järjestys (vasemmalta oikealle, ylhäältä alas) täsmää suoraan BLE-kanavanumerointiin — vahvistettu, koska kanava 10 = INTERIOR ja kanava 9 = tyhjä/merkitsemätön painike, mikä vastaa aiempaa empiiristä löydöstä ("kytkin 9, ei mitään kytkettynä").

| Kanava | Painike | Huom |
|---|---|---|
| 1 | LOWER | (purje/ankkuri winch alas?) |
| 2 | RAISE | (purje/ankkuri winch ylös?) |
| 3 | HEATER | |
| 4 | WATER | (vesipumppu?) |
| 5 | DECK LIGHTS | |
| 6 | STEREO | |
| 7 | OUTLETS | (pistokkeet) |
| 8 | RADIO | |
| 9 | (merkitsemätön/tyhjä) | ei kytketty mihinkään (vahvistettu) |
| 10 | INTERIOR | sisävalot (vahvistettu) |
| 11 | NAV LIGHTS | |
| 12 | ANCHOR LIGHT | |

Paneelin vasemmassa reunassa on kolme erillistä pientä kuvaketta (virta, taustavalo/kirkkaus, RGB) — nämä ohjaavat panelin omia asetuksia (käynnistys, näppäinten taustavalon kirkkaus, RGB-tunnelmavalo), eivät erillisiä kanavia. Eivät sisälly 12 kanavan numerointiin.

## Työkalu

`tools/colight-ble/colight_ble.py` täydennetty `write`-alikomennolla (`write <osoite> <characteristic> <hex> [--listen N]`) tätä selvitystä varten. Testit `test_colight_ble.py`.

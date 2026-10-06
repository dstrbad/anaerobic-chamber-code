# Sustav upravljanja anaerobnom komorom

Firmware, kolektor podataka i web nadzorna ploča za eksperimentalni prototip anaerobne komore u biološkim istraživanjima. Arduino Mega 2560 čita plinske senzore (O2, H2, H2S, O3, CO2), tlak i temperaturu, vodi cikluse pročišćavanja i grijanja te provodi sve sigurnosne blokade. ESP32 prosljeđuje podatke preko MQTT-a Python kolektoru koji piše CSV datoteke i nadzornoj ploči u pregledniku s Plotly grafikonima uživo.

[English version](README.md)

> **Sigurnosna napomena.** Komora nema ventile za rasterećenje tlaka. Softver je zadnja linija obrane prije hardverskih termalnih prekidača. Pročitajte odjeljak [Sigurnosni model](#sigurnosni-model) prije izmjena upravljačke logike aktuatora.

## Arhitektura

Senzori šalju podatke na Megu, Mega ih šalje kao JSON po serijskoj vezi (115200 baud, 1 Hz) na ESP32, ESP32 ih objavljuje na Mosquitto broker, a s brokera ih čitaju Python kolektor (trajni CSV) i web nadzorna ploča (grafikoni uživo). Naredbe idu obrnutim smjerom: nadzorna ploča, MQTT, ESP32, serijska veza, Mega, gdje ih sigurnosni modul provjerava prije izvršavanja. Dijagram je u engleskoj verziji.

## Projektne odluke

**Mega i ESP32, ne samo ESP32.** Mega vodi hardversku kontrolu u stvarnom vremenu s determinističkim vremenima. WiFi stog na ESP32 ima pozadinske zadatke (ponovno spajanje, DHCP, TLS) koji mogu blokirati izvršavanje stotinama milisekundi, što je neprihvatljivo kad tlačni nadzornik mora ugasiti pumpu unutar sekunde ili dvije. Prekid WiFi veze nikad ne utječe na sigurnost komore; Mega radi normalno i kad je ESP32 isključen.

**Tri ortogonalna FSM-a umjesto jednog.** Operacije (ciklusi pročišćavanja), termalna kontrola (grijači) i korisničko sučelje (zaslon i izbornici) su neovisne domene. Jedan kombinirani FSM imao bi više od stotinu stanja. S tri stroja od četiri do šest stanja, "pročišćavanje uz grijanje" je jednostavno `OpsState::PURGE_SMALL` zajedno s `ThermalState::HEAT_CATALYST`.

**Vremenski vođeno uzorkovanje.** Hardverski timeri na ATmega2560 jamče frekvencije uzorkovanja bez obzira na opterećenje glavne petlje: tlak 10 Hz (Timer1), plinski senzori 1 Hz (Timer3), zaslon 20 Hz (Timer4). ISR-ovi samo postavljaju zastavice, a I2C čitanja se događaju u glavnoj petlji.

**Četiri sigurnosna sloja.** Jedan sigurnosni modul u glavnoj petlji može zakazati zajedno s petljom (zaključana I2C sabirnica, beskonačna petlja). Četiri neovisna sloja znače da nijedan pojedinačni kvar ne može ostaviti aktuator uključenim.

**JSON po linijama preko serijske veze.** Pri 1 Hz i oko 300 bajtova po okviru propusnost nije problem. JSON je samoopisujući, nova polja se dodaju bez usklađivanja verzija firmwarea, oštećeni okviri padaju na parsiranju i odbacuju se, a veza se dijagnosticira običnim serijskim monitorom.

**Zasebni Python kolektor.** MQTT ne čuva povijest; `retain` sprema samo zadnju poruku po temi. Kolektor radi kao pozadinski servis i bilježi svaku poruku u CSV neovisno o pregledniku.

**Plotly.js i čisti HTML/JS.** Plotly nativno podržava vremenske osi, zoom, hover i više Y-osi te učinkovito ažuriranje uživo. Nadzorna ploča je jedna HTML stranica bez procesa izgradnje.

## Hardver

| Komponenta | Uloga | Sučelje |
|------------|-------|---------|
| Arduino Mega 2560 | Glavni kontroler | USB |
| Arduino Nano ESP32 | WiFi/MQTT most | Serial1 prema Megi, 115200 baud |
| TCA9548A | I2C multiplekser | I2C 0x70 |
| DFRobot SEN0465 / SEN0473 / SEN0467 / SEN0472 | O2 / H2 / H2S / O3 | I2C preko mux kanala 7, adrese 0x74 do 0x77 |
| DFRobot SEN0514 (ENS160) | Kvaliteta zraka, eCO2 | I2C preko mux kanala 7, 0x53 |
| BMP280 x2 | Tlak glavne i transferne komore | I2C preko mux kanala 2 i 4 |
| DS18B20 x3 | T katalizatora, grijača komore, komore | 1-Wire na D32 |
| SSR, relej, 2x IRLZ44N | Pumpa (D22), ventil (D23), grijači (D44, D45 PWM) | GPIO |
| SSD1306 OLED, joystick, tipkalo, zujalica | Korisničko sučelje | I2C 0x3D, A4/A5, A8, D33 |
| MicroSD DFR0229 | CSV zapis | SPI, CS na D53 |

## Sigurnosni model

Bilo koji od četiri sloja dovoljan je da spriječi nekontrolirani rad aktuatora. Tko mijenja upravljačku logiku aktuatora mora razumjeti sva četiri.

**Sloj 0, hardverski termalni prekidači.** Fizički prekidači na oba grijača isključuju napajanje bez softvera.

**Sloj 1, watchdog timer.** WDT na ATmega2560 postavljen je na 1 s i resetira se na kraju svake iteracije `loop()`. Ako petlja zastane, MCU se tvrdo resetira, svi GPIO pinovi se vraćaju u LOW i svi aktuatori se gase.

**Sloj 2, Timer5 ISR na 50 Hz.** Radi neovisno o glavnoj petlji. Ne čita senzore (nema I2C u ISR-u), nego provjerava spremljene vrijednosti i izravno upravlja pinovima preko registara portova: tvrdi termalni limiti (katalizator 60 °C, komora 45 °C), tvrdi limit pretlaka, istek aktuatora (glavna petlja mora svaku iteraciju pozvati `refreshTimeout()`, inače ISR gasi aktuator, npr. nakon 90 s za pumpu) i provjera živosti FSM-ova (bez otkucaja 750 ms gase se svi aktuatori).

**Sloj 3, modul `safety.cpp` u glavnoj petlji.** Jedini sloj s punim kontekstom; postavlja kodove grešaka i pokreće prijelaze FSM-a. Nadzornik tlačnog odziva (pumpa ili ventil uključeni dulje od 1,5 s bez promjene tlaka), pretlak (alarm 50 hPa iznad referentnog, isklop ventila na 100 hPa), pregrijavanje, istek DS18B20 (nevaljano očitanje dulje od 5 s uz aktivni grijač), O2 vrata (pročišćavanje velike komore blokirano ako je O2 iznad 3 % zbog opasnosti od eksplozije H2 na paladijskom katalizatoru) i globalni prekid dugim pritiskom tipkala.

## Početak rada

Potrebni su [PlatformIO CLI](https://platformio.org/install/cli) ili VS Code ekstenzija, [Mosquitto](https://mosquitto.org/download/) ili Docker, Python 3.8 ili noviji te obje ploče na USB-u.

1. **Mapiranje hardvera.** `make discovery` programira skicu koja skenira I2C sabirnicu i sve mux kanale, ispisuje ROM ID-eve DS18B20 sondi, čita oba BMP280 i ispisuje analogne ulaze. Prepišite ROM ID-eve u `firmware/mega/src/config.h` i pridružite ih sondama grijući svaku zasebno.
2. **Mega.** `make flash-mega`, zatim `make monitor-mega`.
3. **ESP32.** Kopirajte `firmware/esp32/src/secrets.example.h` u `secrets.h`, upišite SSID, lozinku i IP brokera, zatim `make flash-esp32`. Datoteka `secrets.h` je u `.gitignore`; pristupni podaci nikad ne idu u `config.h`.
4. **Broker, kolektor i nadzorna ploča.** `make docker-up` podiže nginx na 8080, Mosquitto na 1883 i 9001 te kolektor s API-jem predložaka na 8000. Otvorite <http://localhost:8080>; s `?demo` u URL-u ploča radi na lažnim podacima bez hardvera. Bez Dockera: dodajte `listener 9001`, `protocol websockets` i `allow_anonymous true` u `mosquitto.conf`, pa `make collector-install && make collector` i `make web`.

CSV datoteke nastaju u `collector/data/continuous/` (tjedno) i `collector/data/runs/` (jedna datoteka po ciklusu pročišćavanja u punoj rezoluciji 1 Hz). Na SD kartici Mege datoteke se zovu `LOG0000.CSV`, `LOG0001.CSV` itd. i rotiraju se nakon 64 KB; aktivni mod (zadano uključen) piše redak svakih 10 s kroz četiri sata, pasivni jedan redak na sat.

## Struktura repozitorija

```
platformio.ini              PlatformIO okruženja: mega, esp32, discovery
Makefile                    Izgradnja, programiranje, monitor, Docker, MQTT pomoćnici
docker-compose.yml          web + mqtt + collector
firmware/mega/src/          Firmware za Arduino Mega 2560 (FSM, sigurnost, senzori, aktuatori, zaslon, SD)
firmware/esp32/src/         WiFi/MQTT most; config.h uključuje secrets.h (predložak: secrets.example.h)
helpers/                    hardware_discovery.cpp (okruženje discovery), relay_test.cpp
collector/                  Python MQTT u CSV kolektor i HTTP API za predloške
web/                        Nadzorna ploča bez procesa izgradnje
```

## Konfiguracija

Mega: sve je u `firmware/mega/src/config.h`. Zadane vrijednosti: 7 ciklusa malog pročišćavanja po 400 hPa, 7 ciklusa velikog po 50 hPa, katalizator 50 °C (isklop 60 °C), komora 37 °C (isklop 45 °C), istek DS18B20 5 s, prozor tlačnog nadzornika 1,5 s, zagrijavanje plinskih senzora 600 s.

ESP32: pristupni podaci i adresa brokera u `secrets.h`; port, prefiks tema (zadano `anaerobic/chamber1/`) i ID klijenta u `config.h`.

Kolektor: `MQTT_BROKER`, `MQTT_PORT`, `MQTT_TOPIC`, `DATA_DIR`, `TEMPLATES_PATH` i `TEMPLATES_PORT` čitaju se iz okoline, zadano localhost. Docker Compose postavlja `MQTT_BROKER=mqtt`.

## MQTT teme

Sve teme su pod prefiksom `anaerobic/chamber1/`. Senzori: `sensor/o2`, `sensor/h2`, `sensor/h2s`, `sensor/o3`, `sensor/co2`, `sensor/pressure_big`, `sensor/pressure_small`, `sensor/temp_catalyst`, `sensor/temp_heater`, `sensor/temp_chamber`. Status: `status/state` (`INIT`, `IDLE`, `PURGE_S`, `PURGE_B`, `FAULT`), `status/thermal` (`OFF`, `CAT`, `CHM`, `BOTH`, `FAULT`), `status/fault` (kod greške ili `NONE`), `status/purge_progress`, `status/uptime`, `status/warmup`. Aktuatori: `actuator/pump`, `actuator/solenoid`, `actuator/catalyst_pwm`, `actuator/chamber_pwm`. Naredbe (bez retain): `command/purge` (`small` ili `big`), `command/heat` (`{"target": "catalyst", "sp": 50}`), `command/abort`. Tema `bulk` nosi cijeli okvir u JSON-u. Puna tablica je u engleskoj verziji.

## Stanje i ograničenja

Ovo je radni prototip za jednu komoru u jednom laboratoriju, ne proizvod. Mosquitto dopušta anonimni pristup, a API predložaka ima otvoren CORS; to je u redu na radnoj stanici i pogrešno za bilo što dostupno izvana. Adrese senzora, ROM ID-evi i pinovi vrijede za ovaj konkretan sklop i na drugom hardveru ih treba ponovno mapirati discovery skicom.

## Licenca

MIT, vidi [LICENSE](LICENSE).

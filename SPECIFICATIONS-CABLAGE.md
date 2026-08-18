# Plan de câblage

Ce document propose un plan de câblage pour les liaisons matérielles listées dans `SPECIFICATIONS.md` § « Description du materiel ». Les références de pins RPi Zero utilisent la numérotation **BCM** (celle utilisée par les libs GPIO), pas les numéros physiques du connecteur — le tableau récapitulatif en fin de document donne aussi les numéros physiques.

⚠️ Plusieurs points restent à vérifier physiquement avant de souder quoi que ce soit — voir la section « Points de vigilance » en fin de document. Ce plan pose une proposition cohérente, pas un schéma validé sur le matériel réel (contrairement aux specs Zigbee/UART qui, elles, ont été testées).

## Vue d'ensemble des liaisons

| Liaison | Type | Détail |
|---|---|---|
| RPi Zero ↔ ESP32-C6-ZERO | UART | Lien applicatif décrit dans [SPECIFICATIONS-UART.md](SPECIFICATIONS-UART.md) |
| RPi Zero ↔ écran Waveshare 3,5" | SPI + GPIO | Écran + tactile, montés en HAT sur le connecteur 40 broches |
| RPi Zero ↔ GY-BME280-3.3 | I2C | Température/pression ambiante mesurée côté RPi (indépendante des têtes Zigbee) |
| RPi Zero → Panasonic AQY212GH → chaudière | GPIO (sortie tout-ou-rien) | Emule le contact sec du thermostat d'ambiance de la chaudière |
| Alimentation | 5V / 3,3V | Voir « Alimentation électrique » |

## Alimentation électrique

* **Alimentation unique 5V** (bloc secteur USB, ≥ 3A recommandé) alimentant la RPi Zero par son port micro-USB (ou les pins `5V`/`GND` du GPIO).
* **ESP32-C6-ZERO** : alimenté en 5V depuis la broche `5V` du GPIO de la RPi (pin physique 2 ou 4) plutôt que par son propre USB-C, pour n'avoir qu'un seul point d'alimentation à superviser — la RPi Zero seule consomme peu, et la radio Zigbee de l'ESP32-C6 ne pique qu'à quelques centaines de mA en émission, ce qui reste dans le budget d'un bloc 5V/3A. **Point de vigilance** : garder le câble USB-C de l'ESP32 branché uniquement pour le flash/debug (pas relié à une autre alimentation en même temps que le 5V RPi, pour éviter de faire cohabiter deux sources 5V sur la même carte).
* **Masse commune obligatoire** entre RPi et ESP32-C6, même si l'alimentation de l'ESP32 venait à être faite séparément (nécessaire pour que la liaison UART ait une référence de tension commune).
* **GY-BME280-3.3** : alimenté en **3,3V uniquement** (broche `3V3` du GPIO RPi, pin physique 1 ou 17) — cette variante « -3.3 » du module GY-BME280 n'a **pas** de régulateur de tension embarqué contrairement aux variantes 5V-tolérantes plus courantes ; la brancher sur le rail 5V la détruirait probablement.
* **AQY212GH** : côté commande (LED d'entrée), alimenté directement par un GPIO 3,3V de la RPi à travers une résistance série (voir plus bas) ; côté charge (contact de sortie), alimenté par le circuit basse tension de la chaudière lui-même, électriquement isolé du reste du montage — c'est tout l'intérêt d'un relais à photocoupleur (PhotoMOS) ici.

## RPi Zero ↔ ESP32-C6-ZERO (UART)

Reprend les paramètres de transport définis dans [SPECIFICATIONS-UART.md](SPECIFICATIONS-UART.md) (115200 bauds, 8N1). Les deux cartes fonctionnent nativement en logique 3,3V : liaison directe, **pas besoin de level shifter**.

| RPi Zero (BCM) | Fonction | ESP32-C6-ZERO | Fonction |
|---|---|---|---|
| GPIO14 (TXD0) | Sortie RPi → Entrée ESP32 | `IO_RX` *(à confirmer sur le pinout exact de la carte)* | Entrée UART |
| GPIO15 (RXD0) | Entrée RPi ← Sortie ESP32 | `IO_TX` *(à confirmer)* | Sortie UART |
| GND | Masse commune | GND | Masse commune |

* Croisement TX↔RX classique : le TX d'une carte va sur le RX de l'autre.
* Côté ESP32-C6, ne **pas** utiliser l'UART0 par défaut (celle multiplexée avec l'USB natif du chip, utilisée pour le flash et les logs `idf.py monitor`) — dédier un second contrôleur UART à des GPIO libres, en évitant les broches de strapping (`GPIO4`, `GPIO5`, `GPIO8`, `GPIO9`, `GPIO15` sur ESP32-C6). Ainsi le port USB-C reste disponible pour le monitoring en développement sans interférer avec le lien RPi.
* Côté RPi Zero, l'UART matériel (`/dev/ttyAMA0`/`/dev/serial0`) est par défaut soit assigné au Bluetooth (sur les modèles avec BT), soit exposé comme console série de login — dans les deux cas il faut le libérer via `raspi-config` (désactiver le login série, éventuellement `dtoverlay=disable-bt`) avant de pouvoir l'utiliser pour ce lien applicatif.

## RPi Zero ↔ écran tactile Waveshare 3,5"

Écran de type « HAT » : se monte directement sur l'intégralité du connecteur 40 broches de la RPi Zero (pas de câblage fil à fil). Utilise le bus SPI0 pour l'affichage et le contrôleur tactile, plus quelques GPIO dédiés :

| Fonction | GPIO (BCM) typique |
|---|---|
| SPI0 SCLK / MOSI / MISO | GPIO11 / GPIO10 / GPIO9 |
| LCD CS | GPIO8 (CE0) |
| Tactile CS | GPIO7 (CE1) |
| LCD DC (Data/Command) | GPIO25 |
| LCD RESET | GPIO24 |
| Rétroéclairage (PWM) | GPIO18 |
| Tactile IRQ | GPIO17 |

⚠️ **Point de vigilance majeur** : ces valeurs correspondent à l'affectation habituelle des écrans Waveshare 3,5" RPi LCD — **à confirmer sur la documentation du modèle exact utilisé** avant câblage. Surtout : vérifier si la carte possède un **connecteur de passage (pass-through header)** au-dessus de l'écran, exposant les broches non utilisées par celui-ci. Si ce n'est pas le cas, les liaisons I2C (BME280) et GPIO (relais) décrites plus bas ne sont **pas accessibles** une fois l'écran monté, et il faudra soit :
* utiliser un câble nappe d'extension GPIO + carte de répartition (breadboard/breakout) intercalée entre la RPi et l'écran, soit
* souder directement sur les pastilles traversantes de la carte écran (faces des connecteurs), si le modèle le permet.

## RPi Zero ↔ GY-BME280-3.3 (I2C)

Bus I2C matériel de la RPi (`i2c-1`), avec pull-ups déjà présents sur la RPi (~1,8kΩ) — pas de résistances additionnelles nécessaires.

| RPi Zero (BCM) | Broche physique | GY-BME280-3.3 |
|---|---|---|
| `3V3` | 1 | `VCC` |
| GND | 6 (ou autre GND) | `GND` |
| GPIO2 (SDA1) | 3 | `SDA` |
| GPIO3 (SCL1) | 5 | `SCL` |

* Adresse I2C par défaut du BME280 : `0x76` ou `0x77` selon l'état de la broche `SDO` du module (à vérifier/mesurer une fois câblé, avec `i2cdetect -y 1`).
* Activer l'interface I2C via `raspi-config` si ce n'est pas déjà fait.

## RPi Zero → relais Panasonic AQY212GH → chaudière

L'AQY212GH est un relais statique à photocoupleur (PhotoMOS), boîtier SOP4 : 2 broches d'entrée (LED de commande), 2 broches de sortie (contact isolé galvaniquement, sans polarité). Il ne sert **que** de contact sec pour émuler le thermostat d'ambiance de la chaudière — il ne pilote pas directement le secteur.

### Côté commande (entrée LED)

| RPi Zero (BCM) | Fonction | AQY212GH |
|---|---|---|
| GPIO (à choisir, ex. GPIO26) | Sortie GPIO → résistance série | Pin 1 (Anode, `IN+`) |
| GND | Masse | Pin 2 (Cathode, `IN-`) |

* Résistance série calculée pour rester dans la plage de courant de seuil de la LED d'entrée (typiquement 2–5mA selon le datasheet Panasonic — **à confirmer sur la fiche technique précise du composant en stock**) : `R = (3,3V − Vf_LED) / I_cible`, soit avec `Vf_LED ≈ 1,2V` et `I_cible ≈ 3mA` → `R ≈ 700Ω`, arrondi à une valeur standard **≈ 680Ω à 1kΩ**.
* Pilotage direct depuis le GPIO sans transistor : le courant requis (quelques mA) est largement dans la capacité de sortie d'un GPIO RPi (~16mA max par broche).

### Côté charge (contact de sortie)

| AQY212GH | Chaudière ELM Leblanc ACLEIS BAS NOX NGLM 24-7XN |
|---|---|
| Pin 3 (Load) | Borne « TA » (thermostat d'ambiance) |
| Pin 4 (Load) | Borne « TA » (retour) |

* Le contact se ferme (GPIO à l'état actif) = appel de chauffe ; ouvert = pas de demande.
* **Tension du circuit « TA » confirmée : 24V** — largement dans la plage supportée par l'AQY212GH (jusqu'à 350V/100mA en charge), aucune contrainte de dimensionnement supplémentaire côté relais.
* Isolation galvanique native du PhotoMOS : la masse du montage RPi/ESP32 (3,3V logique) reste totalement séparée du circuit basse tension 24V de la chaudière — ne pas relier les deux masses.

## Table récapitulative des GPIO RPi Zero utilisés

| BCM | Physique | Usage |
|---|---|---|
| 2 | 3 | I2C SDA (BME280) |
| 3 | 5 | I2C SCL (BME280) |
| 4 | 7 | libre |
| 7 | 26 | Écran — tactile CS |
| 8 | 24 | Écran — LCD CS |
| 9 | 21 | Écran — SPI MISO |
| 10 | 19 | Écran — SPI MOSI |
| 11 | 23 | Écran — SPI SCLK |
| 14 | 8 | UART TXD → ESP32 |
| 15 | 10 | UART RXD ← ESP32 |
| 17 | 11 | Écran — tactile IRQ |
| 18 | 12 | Écran — rétroéclairage PWM |
| 24 | 18 | Écran — LCD RESET |
| 25 | 22 | Écran — LCD DC |
| 26 | 37 | Commande relais AQY212GH |

Toutes les broches ci-dessus sont physiquement occupées par l'écran (posé en HAT) sauf si celui-ci dispose d'un connecteur de passage — voir avertissement plus haut.

## Points de vigilance avant câblage

* **Écran Waveshare** : confirmer le modèle exact (existence d'un pass-through header) et son affectation GPIO/SPI réelle (peut varier entre « RPi LCD (A) », « (B) », etc.).
* **ESP32-C6-ZERO** : confirmer le pinout exact (silkscreen) de la carte utilisée pour choisir les GPIO UART définitifs, en évitant les broches de strapping.
* **RPi Zero** : libérer `/dev/ttyAMA0` du Bluetooth/console série avant de câbler l'UART vers l'ESP32.
* **AQY212GH** : confirmer sur le datasheet précis le courant de seuil (`IFT`) de la LED d'entrée pour dimensionner la résistance série exacte, et l'ordre exact des 4 broches sur le boîtier utilisé.
* **Chaudière ELM Leblanc ACLEIS BAS NOX NGLM 24-7XN** : tension du bornier « TA » confirmée à 24V (voir « Côté charge » ci-dessus) — reste à confirmer la nature exacte du contact (sec ou référencé) et le repérage précis des bornes sur le modèle en notice avant raccordement.
* **GY-BME280-3.3** : confirmer sur le module en stock qu'il s'agit bien de la variante sans régulateur (certains revendeurs étiquettent différemment) avant de l'alimenter en 3,3V.

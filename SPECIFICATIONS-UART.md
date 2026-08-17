## Protocol UART
Le protocol UART va permettre une communication bidirectionnelle entre la RPI et l'ESP32 pour piloter et remonter l'information des têtes thermostatiques. Le protocole applicatif (JSON + signature) et les types de messages ci-dessous s'appuient sur ce que le prototype (`/prototype`, voir `prototype/SPECIFICATIONS.md`) a validé côté radio Zigbee/Tuya avec une vraie tête KETOTEK KTF0177 : identification du device, DataPoints confirmés (mode, consigne, température mesurée, verrouillage enfant) et commandes gateway existantes ou restant à implémenter.

### Transport
* Liaison série 115200 bauds, 8N1, sans contrôle de flux matériel (à revoir si la distance/le câblage l'imposent)
* Un message = un objet JSON sérialisé sur une seule ligne, terminée par `\n` (NDJSON) : pas de préfixe de taille binaire, format simple à logguer/débugger tel quel
* Encodage UTF-8

### Enveloppe des messages
Les messages seront structurés en json avec un format : 
``` json
{
    "data": {
        "type": "...",
        "...": "..."
    },
    "sign" : "xxxx"
}
```
* ```sign``` contient le hash du message ```data``` (sérialisation JSON compacte, clés triées) pour pouvoir s'assurer qu'il n'y a pas eu d'erreur de transmission. Proposition : CRC16-CCITT en hexadécimal — assez robuste pour détecter une erreur de transmission sur une liaison UART point-à-point courte distance, et beaucoup plus léger à calculer sur l'ESP32-C6 qu'un hash cryptographique (disproportionné pour un lien local non exposé).
* ```data.type``` identifie le type de message (voir tables ci-dessous).
* Chaque tête thermostatique est référencée par son `short_addr` Zigbee (adresse réseau 16 bits, ex. `"0x3d6e"`) — c'est déjà l'identifiant utilisé côté gateway dans le prototype (`g_paired_devices[]`).

### Sens ESP32 vers RPI

| `type` | Déclenchement | Contenu de `data` |
|---|---|---|
| `pairing_request` | Signal Zigbee `ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE` (un device rejoint le réseau) | `short_addr`, `ieee_addr` (MAC), `manufacturer` (ex. `_TZE200_p3dbf6qs`), `model` (ex. `TS0601`) — lus via la lecture ZCL du cluster Basic (« magic packet », validée sur le KTF0177 réel) |
| `status_report` | Réception d'un rapport Tuya décodé (DataPoints) | `short_addr` et, selon les DP effectivement reçus dans la trame : `system_mode` (`auto`/`heat`/`off`, DP2), `running_state` (`heat`/`idle`, DP3), `heating_setpoint` (°C, DP4), `local_temp` (°C, DP5), `child_lock` (bool, DP7). Un rapport peut ne contenir qu'un sous-ensemble de ces champs. |
| `command_ack` | Accusé de réception d'une commande envoyée par la RPI | `command_id` (repris de la commande d'origine), `status` (`ok`/`error`), `error` (message, si `status = error`) |
| `device_left` | Départ spontané d'un device du réseau Zigbee (hors révocation demandée par la RPI, ex. reset usine du device) | `short_addr` |
| `devices_list` | Réponse à la commande `list_devices` : état courant tel que vu par l'ESP32 (peut différer de la liste connue de la RPI si des devices ont rejoint/quitté depuis) | `devices` : tableau de `{ short_addr, ieee_addr, manufacturer, model }` |
| `known_devices_request` | Émis une fois au démarrage de l'ESP32, avant toute ouverture du réseau Zigbee | *(vide)* |

### Sens RPI vers ESP32

| `type` | Effet côté gateway | Contenu de `data` |
|---|---|---|
| `set_heating_setpoint` | Écrit la consigne de chauffe (DP4) sur la tête | `short_addr`, `value` (°C, ex. `21.0`) |
| `set_system_mode` | Écrit le mode (DP2) sur la tête | `short_addr`, `value` (`auto`/`heat`/`off`) |
| `set_child_lock` | Écrit le verrouillage enfant (DP7) sur la tête | `short_addr`, `value` (bool) |
| `permit_join` | Ouvre la fenêtre d'appairage Zigbee (le réseau est **fermé par défaut**, y compris au démarrage — voir « Appairage » ci-dessous) | `duration_sec` (ex. `180`) |
| `list_devices` | Retourne l'état courant des devices appairés côté ESP32 | *(vide)* |
| `remove_device` | Révoque l'appairage d'un device : quitte le réseau Zigbee (ZDO Leave) et le retire de la table en mémoire | `short_addr` |
| `known_devices_list` | Réponse à `known_devices_request` : liste des devices déjà connus, source de vérité côté RPI | `devices` : tableau de `{ short_addr, ieee_addr, manufacturer, model }` |

Chaque commande envoyée par la RPI porte un `command_id` (identifiant unique généré côté RPI, ex. compteur ou UUID court) afin de pouvoir corréler la `command_ack` correspondante.

### Notes d'implémentation
* Les champs de `status_report`/`set_*` reprennent uniquement les DataPoints **confirmés** sur le vrai KETOTEK KTF0177 dans le prototype (DP2/DP4/DP5/DP7 — voir `prototype/SPECIFICATIONS.md` § « DataPoints confirmés »). Les autres DP déjà décodés en lecture seule côté gateway (planning hebdomadaire DP28-34, protection hors-gel DP36, anti-entartrage DP39, calibration DP47, etc.) ne sont pas encore exposés dans ce protocole UART ; extension possible une fois un besoin identifié côté RPI/UI.
* Le réseau Zigbee est **fermé par défaut**, y compris au démarrage de l'ESP32 : aucun nouveau device ne peut rejoindre tant que la RPI n'a pas envoyé `permit_join`. C'est un changement volontaire par rapport au prototype, où `esp_zb_bdb_open_network(180)` était appelé automatiquement à chaque boot.
* `list_devices`, `remove_device` et `permit_join` correspondent aux commandes CLI encore non implémentées côté gateway dans le prototype (`TODO(THERMOSTAT_ENABLE_CLI)`) — il faudra les développer sur l'ESP32 avant de pouvoir les piloter depuis la RPI. `permit_join` peut s'appuyer directement sur `esp_zb_bdb_open_network()`, déjà utilisé au démarrage de la gateway dans le prototype.
* La RPI est la source de vérité de la liste des devices connus (persistée côté RPI, pas sur l'ESP32). Au démarrage, l'ESP32 envoie `known_devices_request` et attend la réponse `known_devices_list` de la RPI pour reconstruire sa table en mémoire avant d'interagir avec le réseau Zigbee — ceci remplace le besoin de persistance NVS locale des devices appairés côté ESP32.
* Pas de polling périodique prévu depuis la RPI pour lire l'état des têtes : comme observé sur le vrai KTF0177, les rapports Tuya sont spontanés, à la discrétion du firmware de la tête (pas de cadence fixe garantie, contrairement au simulateur du prototype qui rapporte toutes les 30s). La RPI doit donc consommer les `status_report` au fil de l'eau plutôt qu'attendre une réponse synchrone à une requête de lecture.
* Pas de commande Tuya « data query » fiable côté device réel (voir `prototype/SPECIFICATIONS.md` § « Erreur historique sur `0x11` ») : impossible de forcer une tête à renvoyer son état à la demande, y compris via ce protocole UART — seuls les rapports spontanés font foi.
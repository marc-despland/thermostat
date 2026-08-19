# Installation et déploiement — Raspberry Pi Zero

Ce document décrit comment préparer la Raspberry Pi Zero et mettre en place le flux de travail de développement : le code de l'application applicative de l'écran (voir [SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md), [SPECIFICATIONS-PIZERO.md](SPECIFICATIONS-PIZERO.md), [SPECIFICATIONS-PROGRAM.md](SPECIFICATIONS-PROGRAM.md)) est écrit et édité sur le Mac (dans ce repo, via VSCode/Claude Code), puis déployé et testé sur la Pi Zero physique par SSH — pas d'édition directe sur le Pi, dont les ressources (mono/quad-cœur ARM limité, peu de RAM) sont trop faibles pour faire tourner confortablement un serveur VSCode ou Claude Code.

## 1. Préparation de la carte SD

* Flasher **Raspberry Pi OS Lite** (pas de bureau nécessaire, l'application pilote l'écran tactile Waveshare directement).
* Avec Raspberry Pi Imager, utiliser les options avancées (⚙️) pour préconfigurer en une fois, avant le premier boot :
  * activation SSH (authentification par clé de préférence, voir §2),
  * nom d'hôte (ex. `thermostat`),
  * utilisateur/mot de passe,
  * Wi-Fi si le Pi n'est pas en Ethernet.
* Sans Imager : déposer un fichier vide nommé `ssh` à la racine de la partition boot pour activer SSH au premier démarrage.

## 2. Accès SSH sans mot de passe depuis le Mac

```bash
ssh-keygen -t ed25519 -C "mac-thermostat"   # si pas déjà de clé
ssh-copy-id pi@thermostat.local
```

Puis dans `~/.ssh/config` sur le Mac :

```
Host pizero
    HostName thermostat.local
    User pi
    IdentityFile ~/.ssh/id_ed25519
```

Ensuite `ssh pizero` suffit pour se connecter — c'est cet alias qui est utilisé dans les commandes ci-dessous, et celui que Claude Code emploiera via l'outil Bash.

## 3. Configuration réseau

* Par défaut, mDNS donne accès au Pi via `raspberrypi.local` ; le renommer via `sudo raspi-config` → *System Options* → *Hostname* (ex. `thermostat.local`) pour éviter les conflits si plusieurs Pi sont sur le réseau.
* Alternative plus robuste : réserver une IP fixe pour l'adresse MAC du Pi dans le routeur/box, et l'utiliser comme `HostName` dans `~/.ssh/config` à la place du nom mDNS.

## 4. Organisation du code applicatif dans le repo

Le code de l'application Pi Zero vit dans ce repo, dans le dossier `/thermostat` (voir l'arborescence du code dans [SPECIFICATIONS.md](SPECIFICATIONS.md)), à côté de `/controller-zigbee` (ESP32) ; `/prototype` est l'ancien code de validation matérielle, gardé comme référence, non actif. Rien ne change dans le flux de travail habituel : édition avec VSCode/Claude Code sur le Mac, commits git normaux.

## 5. Script de déploiement

`thermostat/deploy.sh` — synchronise le code vers le Pi et redémarre le service applicatif :

```bash
#!/usr/bin/env bash
set -euo pipefail

rsync -avz --delete \
  --exclude '.venv' --exclude '__pycache__' --exclude '*.pyc' \
  ./thermostat/ pizero:/home/pi/thermostat/

ssh pizero 'sudo systemctl restart thermostat'
```

```bash
chmod +x thermostat/deploy.sh
```

## 6. Service systemd

Pour que l'application démarre au boot du Pi et puisse être redémarrée simplement après chaque déploiement, créer sur le Pi `/etc/systemd/system/thermostat.service` :

```ini
[Unit]
Description=Thermostat - application écran tactile
After=network.target

[Service]
WorkingDirectory=/home/pi/thermostat
ExecStart=/usr/bin/python3 /home/pi/thermostat/main.py
Restart=on-failure
User=pi

[Install]
WantedBy=multi-user.target
```

Puis :

```bash
ssh pizero 'sudo systemctl daemon-reload && sudo systemctl enable --now thermostat'
```

(Adapter `ExecStart` selon le langage/runtime réellement choisi pour l'application.)

## 7. Consultation des logs à distance

```bash
ssh pizero 'journalctl -u thermostat -n 100 --no-pager'
ssh pizero 'journalctl -u thermostat -f'   # suivi en direct
```

## 8. Vérification visuelle de l'écran tactile

Claude Code n'a pas d'yeux sur l'écran physique du Pi. Pour vérifier visuellement un écran (cf. [SPECIFICATION-PIZERO-SCREEN.md](SPECIFICATION-PIZERO-SCREEN.md)) après un déploiement, capturer une image de l'affichage et la rapatrier sur le Mac :

```bash
ssh pizero 'DISPLAY=:0 scrot -o /tmp/screen.png'
scp pizero:/tmp/screen.png /tmp/screen.png
```

(`scrot` à installer sur le Pi : `sudo apt install scrot` ; adapter la commande de capture si l'application tourne en Wayland plutôt qu'en X11.) Le fichier `/tmp/screen.png` peut ensuite être ouvert directement par Claude Code pour être vu et commenté.

## 9. Permissions Bash de Claude Code

Les commandes `ssh`/`rsync`/`scp` déclenchent une confirmation de permission la première fois qu'elles sont exécutées dans une session. Une fois le flux de déploiement validé, ces commandes peuvent être ajoutées à la liste d'autorisations du projet (`.claude/settings.local.json`) pour ne plus être reconfirmées à chaque fois.

## Résumé du cycle de développement

1. Éditer le code dans `/thermostat` sur le Mac (VSCode / Claude Code).
2. `./thermostat/deploy.sh` — synchronise et redémarre le service sur le Pi.
3. `ssh pizero 'journalctl -u thermostat -f'` — vérifier qu'il démarre sans erreur.
4. Capture d'écran (§8) si le changement touche l'IHM.
5. Commit git une fois validé.

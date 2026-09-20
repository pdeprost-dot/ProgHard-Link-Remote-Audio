# ProgHard Link Remote Audio

Audio du microphone PDM de la **Seeed Studio XIAO ESP32-S3 Sense**, avec antenne Wi-Fi externe. La page Web et le serveur HTTP sont embarqués dans l’ESP : l’écoute en LAN fonctionne directement par l’adresse IP de la carte, sans Internet, VPS ni tunnel. ProgHard Link donne accès à la même page à distance. Ce dépôt ne contient et ne modifie pas le framework ProgHard Link. Aucun code caméra.

## Matériel et format

- XIAO ESP32-S3 Sense, ESP32-S3 double cœur, 8 Mo de flash, 8 Mo de PSRAM ; microphone PDM sur GPIO 42 (horloge) et 41 (données), conformément à la [documentation Seeed](https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/).
- Antenne Wi-Fi externe : essentielle sur l’installation testée. Avant son ajout, RSSI faible, débit PCM limité à 132–163 kbit/s pour 256 requis et réponses G.711 parfois retardées de 2–9 s. Ces résultats ne prouvent pas une limitation de ProgHard Link.
- Acquisition PCM mono 16 bits à 16 kHz ; retrait de la composante continue, gain numérique fixe ×8 avec saturation, sous-échantillonnage à 8 kHz, G.711 μ-law mono 8 bits. Débit nominal : 64 kbit/s. Le navigateur décode avec Web Audio.

## Compilation et flash

Environnement vérifié : Arduino ESP32 core 3.3.11, bibliothèque ESPway/ProgHard Link 0.4.13 du dépôt voisin et `arduino-cli`. Depuis ce dossier :

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,USBMode=hwcdc --libraries '..\ProgHard-Link\firmware\esp8266\libraries' --output-dir build RemoteAudio
arduino-cli upload -p COM12 --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,USBMode=hwcdc --input-dir build RemoteAudio
```

Adapter le port et le chemin de bibliothèque. Le flash USB ordinaire a conservé la configuration Wi-Fi et l’identité PHL de la carte testée ; un effacement complet pourrait les supprimer.

## Utilisation et sécurité

En LAN, ouvrir `http://<IP-de-la-XIAO>/audio` (adresse de l’appareil testé : `http://192.168.50.198/audio`). En accès distant, ouvrir `/audio` sur l’URL du dispositif dans ProgHard Link. L’interface affiche connexion, état du microphone, format, débit, buffer et RSSI. Elle ne charge aucun CDN ou autre ressource Internet.

Le micro est **OFF au démarrage**. Le bouton Démarrer envoie `POST /audio/start` ; Arrêter envoie `POST /audio/stop`. Sans lecture pendant 10 secondes, une tâche indépendante du réseau, du serveur HTTP et du tunnel l’éteint. La page vérifie l’état réel par `/audio/status`. Sur le LAN, le framework ne fournit pas d’authentification à ces routes : réserver l’accès aux personnes de confiance.

Diagnostics : `/audio/level` renvoie peak, moyenne et RMS PCM **avant μ-law** et remet la fenêtre de mesure à zéro ; `/audio/status` expose notamment RSSI, octets servis, pertes de file, lectures courtes, échantillons écrêtés et mémoire libre. `/audio/bench` sert 2048 octets synthétiques sans démarrer le micro. Le script `tools/probe_audio.py` est un test LAN court.

## Mesures sur matériel

- Avant version 1.0, avec antenne externe : flux LAN 124 s à 62,8 kbit/s, sans erreur ni perte ; écoute distante Chrome/PHL rapportée par l’utilisateur pendant au moins 10 min à environ 59,4 kbit/s, buffer environ 806 ms, sans interruption notable. Une observation passive de 122 s a montré RSSI −58 à −63 dBm et tunnel connecté ; une pointe de file a perdu 3072 octets sans interruption audible signalée.
- Données synthétiques LAN, 20 s par palier : 64,0 / 128,0 / 256,0 / 510,9 kbit/s reçus pour 64 / 128 / 256 / 512 visés, sans réponse corrompue ni erreur.
- Firmware 1.0, tone Windows 800 Hz capté par le micro : PCM brut ~376 RMS alternatif, pic brut 1967 ; G.711 après gain ×8 ~2996 RMS décodés, pic décodé 5116 ; aucun nouvel échantillon écrêté pendant le tone. Après le tone, bruit PCM ~15 RMS alternatif. Un échantillon a été écrêté au démarrage pendant le transitoire. La voix normale à distance connue reste à mesurer pour valider le confort perçu.
- Firmware 1.0 : écoute LAN 600 s, 4 799 872 octets reçus, environ 64,0 kbit/s, aucune erreur HTTP, nouvelle perte de file ou lecture courte. Latence médiane 28–30 ms, maximum 400 ms. Heap libre 198,5–198,7 Ko pendant l’écoute ; RSSI −54 à −62 dBm ; microphone OFF après arrêt.
- Firmware 1.0 via PHL/Chrome : son audible sans saturation perçue selon l’utilisateur ; capture à 60,1 kbit/s, buffer 790 ms, RSSI −67 dBm. La carte a servi 5 192 448 octets avant arrêt, soit environ 10,8 min au débit G.711 nominal. Zéro perte de file et zéro lecture courte ; heap libre environ 195–201 Ko pendant le flux ; micro OFF après arrêt. 99 échantillons écrêtés sur la session, sans saturation audible rapportée.
- Cinq cycles start/stop : OFF à chaque arrêt ; watchdog : OFF après 13 s sans lecture. Test temporaire sur carte : coupure Wi-Fi 15 s, LAN revenu sans reboot, puis tunnel PHL reconnecté (`remote=yes` à environ 121 s d’uptime), micro resté OFF. Image temporaire exclue du dépôt ; firmware 1.0 définitif reflashé et tunnel reconnecté.

La distance de la voix durant l’écoute n’a pas été contrôlée ; un calibrage subjectif plus précis du gain reste possible. Le benchmark PHL synthétique n’a pas été refait : le flux audio 1.0 tient plus de dix minutes. Les anciennes pauses avec Wi-Fi faible sont compatibles avec une cause radio ; aucune limite de débit propre à PHL n’est démontrée.

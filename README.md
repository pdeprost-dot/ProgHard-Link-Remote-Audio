# ProgHard Link Remote Audio

Laboratoire autonome d'écoute du microphone de la Seeed Studio XIAO ESP32-S3 Sense. La caméra n'est pas utilisée. ProgHard Link est une dépendance externe, jamais modifiée par ce dépôt.

## Matériel et logiciel

- XIAO ESP32-S3 Sense avec microphone PDM sur GPIO 42 (horloge) et GPIO 41 (données).
- ESP32-S3 révision 0.2, flash 8 Mo et PSRAM intégrée 8 Mo, vérifiés sur COM12 avec `esptool flash-id` / `chip-id`.
- Arduino ESP32 core 3.3.11 et bibliothèque ProgHard Link 0.4.13 (dépôt local `../ProgHard-Link/firmware/esp8266/libraries/ESPway`).

Voir la [documentation officielle du microphone Seeed](https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/).

## Architecture

L'application implémente `ESPwayApplication`. Sa page et ses routes utilisent le dispatcher HTTP du framework, commun au LAN et au tunnel. Aucun serveur ni tunnel secondaire n'est créé. Le microphone est arrêté au démarrage, puis activé par `POST /audio/start` et désactivé par `POST /audio/stop` ou après trois secondes sans lecture. `GET /audio/chunk` renvoie environ 100 ms de PCM signé mono 16 bits à 16 kHz (**256 kbit/s**, soit 32 ko/s). Le navigateur lit des blocs successifs avec Web Audio. `GET /audio/status` expose ON/OFF, blocs lus, lectures courtes et mémoire libre.

Cette méthode HTTP est simple et compatible avec le tunnel existant, mais son débit et sa latence réels doivent être mesurés. Elle ne promet pas une diffusion multi-utilisateur : une seule session d'écoute est prévue.

## Compilation et flash

Depuis ce dossier, avec Arduino CLI et le core ESP32 installés :

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,USBMode=hwcdc --libraries '..\ProgHard-Link\firmware\esp8266\libraries' --output-dir build RemoteAudio
arduino-cli upload -p COM12 --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,USBMode=hwcdc --input-dir build RemoteAudio
```

Adapter le chemin de la bibliothèque et le port si nécessaire. Un flash USB standard conserve normalement la configuration du framework si la table de partitions est compatible. Éviter l'effacement complet si l'identité du périphérique est déjà liée au serveur.

## Utilisation

Configurer le Wi-Fi et ProgHard Link via les pages du framework. Ouvrir `/audio` sur l'IP LAN. Cliquer sur **Démarrer l'écoute**, puis **Arrêter**. La page affiche l'état et le débit reçu. Pour l'accès distant, ouvrir le même chemin `/audio` sur l'URL du périphérique depuis le Device Manager ProgHard Link.

## Vérification

| Étape | État |
|---|---|
| Identification puce, flash et PSRAM | Confirmée avec `esptool` |
| Compilation Arduino | En cours |
| Acquisition et variation des échantillons | À tester sur carte |
| Écoute LAN et stabilité | À tester |
| Écoute via ProgHard Link | À tester |

## Limites connues

Le contrat public `WebResponse` assemble chaque corps en mémoire avant envoi et le tunnel l'encode en base64. Cela convient à de petits blocs PCM mais ajoute allocations et surcoût. Il n'expose actuellement pas de streaming HTTP continu ni de WebSocket applicatif à travers la même route. L'accès LAN aux routes applicatives n'a pas d'authentification utilisateur fournie par le framework ; restreindre le LAN aux personnes de confiance.

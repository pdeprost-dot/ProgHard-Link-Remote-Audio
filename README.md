# ProgHard Link Remote Audio

Laboratoire autonome pour le microphone de la Seeed Studio XIAO ESP32-S3 Sense. La caméra n'est pas utilisée. ProgHard Link reste une dépendance externe.

## Matériel vérifié

L'utilisateur a confirmé la XIAO ESP32-S3 Sense. Sur COM12, esptool a identifié un ESP32-S3 révision 0.2, 8 Mo de flash et 8 Mo de PSRAM. Le microphone est PDM : GPIO 42 pour l'horloge et GPIO 41 pour les données, selon la [documentation officielle Seeed](https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/).

L'environnement testé est Arduino ESP32 core 3.3.11 et ProgHard Link Arduino 0.4.13 dans le dépôt voisin ProgHard-Link. La bibliothèque n'est pas copiée dans ce projet.

## Architecture

RemoteAudio implémente ESPwayApplication et expose la page /audio ainsi que /audio/start, /audio/stop, /audio/chunk et /audio/status via le dispatcher HTTP du framework. Ces routes sont communes au LAN et au tunnel. Aucun second serveur ou tunnel n'est créé.

Le microphone est éteint au démarrage. Une action explicite sur le bouton Démarrer envoie POST /audio/start. Arrêter envoie POST /audio/stop. Sans lecture pendant dix secondes, une tâche watchdog indépendante de la boucle HTTP et du tunnel éteint le microphone. La page interroge régulièrement l'état matériel pour afficher ON ou OFF même si un autre client agit.

Le firmware acquiert le PDM à 16 kHz sur 16 bits, prend un échantillon sur deux, puis encode en G.711 μ-law mono à 8 kHz. Le débit nominal transmis est 64 kbit/s. Le navigateur décode chaque bloc et le joue avec Web Audio. Le codec a été introduit après mesure de l'insuffisance du PCM brut à 256 kbit/s.

## Compilation et flash

Depuis ce dossier, avec Arduino CLI installé :

    arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,USBMode=hwcdc --libraries '..\ProgHard-Link\firmware\esp8266\libraries' --output-dir build RemoteAudio
    arduino-cli upload -p COM12 --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,USBMode=hwcdc --input-dir build RemoteAudio

Adapter le port et le chemin de bibliothèque au PC. Le flash USB a conservé la configuration Wi-Fi et l'identité ProgHard Link présentes sur cette carte ; un effacement complet pourrait les supprimer.

## Utilisation

Après configuration du Wi-Fi et de ProgHard Link, ouvrir l'adresse LAN de la carte suivie de /audio. Sur l'appareil testé, l'adresse était http://192.168.50.198/audio. Pour un accès distant, ouvrir la même page /audio depuis le Device Manager ProgHard Link.

**État après ajout de l’antenne externe :** l’utilisateur a écouté le flux distant dans Chrome via ProgHard Link pendant au moins 10 minutes, sans interruption notable, à environ 59,4 kbit/s et avec environ 806 ms de buffer. Le LAN fonctionne aussi. Ces observations remplacent la conclusion antérieure sur l’instabilité du flux.

Les diagnostics firmware 0.1.2 ajoutent `/audio/level` (statistiques PCM 16 bits avant conversion μ-law, remises à zéro à chaque lecture), `/audio/bench` (2048 octets synthétiques par réponse), et le RSSI et le nombre d’octets audio servis dans `/audio/status`. Le benchmark ne démarre pas le microphone. Les routes utilisent le même serveur LAN et le même chemin applicatif PHL ; la page reste embarquée dans l’ESP et utilisable sans Internet.

Le script tools/probe_audio.py mesure les blocs depuis le LAN et peut recevoir une URL de base en argument. Il active le microphone puis l'éteint en fin de test.

## Anciennes mesures avant antenne externe (firmware 0.1.1)

| Étape | Résultat |
| --- | --- |
| Matériel | Modèle confirmé par l'utilisateur ; puce, flash et PSRAM vérifiés par esptool |
| Démarrage | Wi-Fi LAN connecté ; tunnel espway-tunnel/2 connecté et métadonnées vérifiées dans le journal série |
| Compilation | Réussie avec Arduino CLI et la bibliothèque externe |
| Acquisition | Blocs réels de microphone reçus ; échantillons variables, mais réaction à un son contrôlé non vérifiée |
| PCM brut 16 kHz | 132 à 163 kbit/s utiles au LAN contre 256 kbit/s nécessaires ; pertes de buffer avec faible Wi-Fi |
| G.711 μ-law 8 kHz | Plusieurs requêtes ont pris 2 à 9 secondes avec faible Wi-Fi |
| Sécurité arrêt | Test USB avec LAN inaccessible : ON puis OFF en environ dix secondes, confirmé par USB. Image normale : POST ON, puis statut OFF après quatorze secondes sans lecture. |
| Navigateur LAN | Page compilée et servie, écoute auditive non validée |
| Tunnel distant | Connexion du tunnel vérifiée ; page et flux distants non validés avec une session utilisateur |
| Stabilité plusieurs minutes | Non validée avant antenne |

## Requalification après antenne externe, 20 septembre 2026

- Test distant utilisateur : au moins 10 min d’écoute audible et stable via Chrome/PHL, environ 59,4 kbit/s, buffer environ 806 ms, aucune interruption notable rapportée. La mesure est issue du navigateur utilisateur, pas d’une instrumentation distante automatisée.
- Observation passive pendant 122 s de ce flux : RSSI de −58 à −63 dBm, tunnel connecté à chaque relevé, 0 lecture courte, heap libre de 191 à 202 Ko, aucune reprise du boot observée. Le compteur de pertes a augmenté de 3072 octets lors d’une pointe de file ; cela représente environ 384 ms de G.711. Aucun décrochage audible n’a été signalé par l’utilisateur.
- Flux LAN avec firmware 0.1.2 : 124 s, 976000 octets utiles, 62,8 kbit/s moyens, 818 réponses, aucune erreur HTTP, aucune lecture courte, aucune perte de file. Latence par réponse médiane 59 ms, maximum 238 ms. Heap libre en fin de test 198620 octets ; RSSI −66 dBm. Le micro a été arrêté après le test.
- PCM avant μ-law, sans parole contrôlée : niveau continu moyen proche de +1355 à +1360 unités ; RMS brut proche de 1355 à 1360 ; RMS après retrait de la moyenne de 13 à 20 unités sur les fenêtres calmes. Une première fenêtre transitoire a atteint un pic de 11342 et 271 RMS alternatif. Le niveau de voix normale à distance connue n’a pas été mesuré. Aucun gain fixe ou AGC n’est donc retenu à ce stade.
- Données synthétiques LAN, 20 s par palier : 64,0 / 128,0 / 256,0 / 510,9 kbit/s reçus pour 64 / 128 / 256 / 512 visés. Aucune réponse corrompue ni erreur. Latence maximale par réponse 405 / 245 / 99 / 172 ms respectivement ; heap libre stable autour de 202,7 Ko. Le benchmark synthétique distant PHL n’a pas été mesuré.

Les anciennes mesures PCM de 132–163 kbit/s pour 256 kbit/s nécessaires, et les anciennes réponses G.711 retardées de 2 à 9 s, datent de la liaison Wi-Fi faible avant antenne externe. L’amélioration simultanée du RSSI et du flux est compatible avec une cause Wi-Fi, mais ne démontre pas à elle seule la causalité. Une limitation de débit du tunnel PHL n’est pas établie. L’accès LAN reste autonome lorsque Internet ou PHL est indisponible ; le firmware et le serveur HTTP restent sur l’ESP.

Le benchmark distant synthétique, le niveau PCM avec voix contrôlée et une mesure quantitative du jitter et des pertes dans le navigateur distant restent à effectuer. Les routes applicatives LAN n’ont pas d’authentification utilisateur fournie par le framework ; réserver ce LAN aux personnes de confiance.

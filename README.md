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

**État expérimental : l'écoute continue n'est pas encore validée.** Les mesures ci-dessous montrent des pauses HTTP de plusieurs secondes. Le bouton et les routes permettent de diagnostiquer l'acquisition, mais cette version ne doit pas être présentée comme une solution d'écoute fiable.

Le script tools/probe_audio.py mesure les blocs depuis le LAN et peut recevoir une URL de base en argument. Il active le microphone puis l'éteint en fin de test.

## Résultats mesurés le 20 septembre 2026

| Étape | Résultat |
| --- | --- |
| Matériel | Modèle confirmé par l'utilisateur ; puce, flash et PSRAM vérifiés par esptool |
| Démarrage | Wi-Fi LAN connecté ; tunnel espway-tunnel/2 connecté et métadonnées vérifiées dans le journal série |
| Compilation | Réussie avec Arduino CLI et la bibliothèque externe |
| Acquisition | Blocs réels de microphone reçus ; échantillons variables, mais réaction à un son contrôlé non vérifiée |
| PCM brut 16 kHz | 132 à 163 kbit/s utiles au LAN contre 256 kbit/s nécessaires ; pertes de buffer |
| G.711 μ-law 8 kHz | Blocs LAN reçus, mais plusieurs requêtes ont pris 2 à 9 secondes ; pertes de buffer, lecture continue non validée |
| Sécurité arrêt | Test USB avec LAN inaccessible : ON puis OFF en environ dix secondes, confirmé par USB. Image normale : POST ON, puis statut OFF après quatorze secondes sans lecture. |
| Navigateur LAN | Page compilée et servie, écoute auditive non validée |
| Tunnel distant | Connexion du tunnel vérifiée ; page et flux distants non validés avec une session utilisateur |
| Stabilité plusieurs minutes | Non validée |

## Limites observées

Le contrat public WebResponse assemble chaque réponse en mémoire. Le tunnel encode ensuite le corps en base64 et ferme le flux de requête ; il ne fournit pas de flux HTTP continu ou de WebSocket applicatif. Pour obtenir de l'audio, cette application doit donc multiplier les réponses HTTP courtes. Sur l'appareil et le réseau testés, leur latence est trop irrégulière pour maintenir une lecture continue, même à 64 kbit/s. C'est une limite générique potentielle pour les applications à flux continu ; elle est documentée ici, sans modification de ProgHard Link.

Les routes applicatives LAN ne disposent pas d'une authentification utilisateur fournie par le framework. Un client du LAN peut activer le microphone par POST. La page affiche l'état réel et le délai d'inactivité éteint le micro, mais l'accès LAN doit rester réservé aux personnes de confiance.

L'essai n'a pas encore établi la latence ou la stabilité du tunnel distant, ni une variation contrôlée du niveau sonore. Aucune mesure de plusieurs minutes n'est revendiquée.

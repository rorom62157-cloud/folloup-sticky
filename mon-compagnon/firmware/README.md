# Firmware de diagnostic

`diagnostic/diagnostic.ino` est un diagnostic USB série autonome, **pas l’application Mon compagnon**. Il vise uniquement Waveshare ESP32-S3-ePaper-3.97 (SKU33552), avec Arduino-ESP32 **3.3.8** et ses bibliothèques intégrées Arduino/Wire. Aucun téléchargement de bibliothèque n’est nécessaire.

## Comportement

- Affiche modèle/révision CPU, flash détectée, PSRAM disponible et mémoire libre ; attend au plus 3 secondes la connexion série.
- Signale les appuis et relâchements BOOT0, rocker haut4, clic5 et bas6, avec anti-rebond de 30 ms. Une commande série `i` répète l’identification ; un message de vie rappelle le mode diagnostic toutes les 15 secondes.
- Sonde une seule fois les adresses I2C 0x34, 0x18, 0x51, 0x6A, 0x6B et 0x70 sur SDA41/SCL42 à 100 kHz. Chaque sonde transmet uniquement START/adresse/STOP, sans registre, commande ou donnée. Un ACK prouve seulement une présence ; un NACK ne prouve pas l’absence du composant, notamment s’il dort ou n’est pas alimenté.
- Ne configure ni PMIC, rails, charge batterie, RTC ou capteurs. N’initialise ni écran ni audio et n’active aucun réseau. La microSD reste démontée : aucun formatage, écriture, parcours ou montage FAT n’est tenté, faute de garantie de montage strictement en lecture seule dans cette voie Arduino. Les fichiers existants restent hors du périmètre du test.

Le programme ne change pas les réglages du PMIC. Ses comportements matériels existants restent donc actifs : PWR peut encore couper l’alimentation lors d’un maintien. PWR n’est pas un bouton GPIO direct ; ses événements exigent un pilote PMIC et ne sont pas diagnostiqués ici. Relâcher BOOT lors d’un reset/démarrage pour éviter le mode téléchargement ROM. La position physique des boutons est à vérifier avec leur sérigraphie et les événements série.

## Compilation uniquement

Depuis la racine `mon-compagnon/`, avec le core déjà installé :

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' compile \
  --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi' \
  --output-dir /tmp/mon-compagnon-diagnostic-build \
  firmware/diagnostic
```

La cible générique ESP32-S3 utilise le port USB natif en mode Hardware CDC/JTAG avec CDC au démarrage. La PSRAM du module N16R8 doit être en mode OPI ; une valeur PSRAM nulle peut simplement signaler une configuration incorrecte. Les options et le partitionnement d’un futur firmware applicatif doivent être validés séparément. Ce diagnostic ne fournit pas de procédure de flash : vérifier préalablement la sauvegarde/récupération du firmware existant, l’image, le port et le partitionnement avec le responsable de l’intégration.

Après un éventuel flash autorisé séparément, ouvrir le moniteur série à 115200 bauds. L’écran peut conserver l’image du firmware précédent puisque ce programme ne le rafraîchit pas. La compilation ne valide pas le matériel ; aucune observation matérielle n’est revendiquée dans ce livrable. Ne pas interpréter une sonde ACK comme un test audio, écran, capteur ou stockage réussi.

## Provenance

- [Documentation constructeur et caractéristiques](https://docs.waveshare.com/ESP32-S3-ePaper-3.97) : ESP32-S3, 16 Mo flash, 8 Mo PSRAM, écran 800×480, ES8311, RTC PCF85063, SHTC3 et QMI8658.
- [Schéma constructeur](https://files.waveshare.com/wiki/ESP32-S3-ePaper-3.97/ESP32-S3_e-Paper-3.97-schematic.pdf).
- [Exemple ESP-IDF constructeur, composants](https://github.com/waveshareteam/ESP32-S3-ePaper-3.97/tree/main/ESP-IDF/08_ESP32-S3_e-Paper-3.97/components) : `button_bsp/button_bsp.c` pour GPIO0/4/5/6 actifs bas ; `i2c_bsp/i2c_bsp.h` pour SDA41/SCL42 et adresses. Le code utilise AXP2101 à 0x34 tandis que la documentation nomme TG28 ; la sonde ne tranche pas l’identité.
- [Configuration de référence utilisateur, commit 1682b7e7d7ff9c6b23ab7e903c44b241f095b402](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/board/include/waveshare_board_config.h) : adresse QMI8658 0x6B ; l’exemple constructeur mentionne aussi 0x6A, d’où les deux sondes. Le diagnostic est écrit indépendamment et ne copie pas les pilotes de référence.

Vérifications de ce livrable : inspection statique des appels ; compilation et essai matériel laissés à l’intégration. Aucun flash, reset, effacement, accès série ou commit n’est exécuté par l’auteur de ce diagnostic.

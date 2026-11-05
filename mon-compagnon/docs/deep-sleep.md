# Veille profonde ESP32-S3 — adaptateur réel

`firmware/device/deep_sleep.*` utilise réellement `esp_deep_sleep_start()`.
L'armement efface toutes les sources avec `ESP_SLEEP_WAKEUP_ALL`, vide aussi
l'ancien masque EXT1, puis active seulement GPIO4/5/6 en `ANY_LOW`.
Aucun timer, GPIO38/IRQ PMIC ou GPIO0/BOOT n'est ajouté comme source de réveil.
Le helper ne commande ni PMIC, ni radios, ni stockage, ni formatage, ni NVS.

## Contrat d'intégration

Le propriétaire applicatif sérialise la transition : empêcher de nouvelles
opérations, terminer l'écran et les écritures notes/WAV, vérifier l'audio au
repos, arrêter le serveur HTTP et le Wi-Fi, et désactiver l'amplificateur GPIO39.
Attendre le relâchement stable des trois contacts de la molette avant
`armDeepSleep(error)`, vérifier encore `wakeKeysReleased()` immédiatement avant
`enterDeepSleep()`. Toute erreur d'armement interdit l'entrée en sommeil.
`wakeKeysReleased()` observe les niveaux instantanés ; la stabilisation et son
délai borné appartiennent à l'appelant. Un appui qui arrive après la dernière
vérification peut légitimement provoquer un réveil immédiat.

Au démarrage, appeler une fois `wokeFromDeepSleep()` avant `beginBoard()`.
Si vrai, faire `rtc_gpio_deinit()` sur GPIO4, GPIO5 et GPIO6 avant leur
configuration numérique. Après initialisation, l'application peut incrémenter
son compteur persistant pour ce seul résultat EXT1. Le helper ne persiste rien.
La fonction de cause est pure et n'incrémente aucun compteur. Un reset USB ou
un démarrage ordinaire ne doit pas devenir un réveil profond observé.

## Justification et sources vérifiées

Le [guide Espressif ESP-IDF 5.5.2 pour ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/system/sleep_modes.html#external-wakeup-ext1)
confirme EXT1 `ANY_LOW`, les RTC GPIO0–21 et le changement de multiplexage
à l'entrée en sommeil. Il demande `rtc_gpio_deinit()` avant réutilisation
numérique après réveil. Le helper garde `RTC_PERIPH` alimenté et configure
`rtc_gpio_pullup_en()` / `rtc_gpio_pulldown_dis()` pour maintenir les niveaux
de repos hauts ; cela favorise la fiabilité plutôt que le courant minimal.

Dans le SDK installé sous
`~/Library/Arduino15/packages/esp32/tools/esp32s3-libs/3.3.6/`, vérification de
`include/esp_common/include/esp_idf_version.h` : 5.5.2 ;
`include/esp_hw_support/include/esp_sleep.h` : enum S3 `ANY_LOW`, API moderne
`esp_sleep_enable_ext1_wakeup_io()` et effacement de masque avec
`esp_sleep_disable_ext1_wakeup_io(0)`. L'API moderne ajoute des broches :
l'effacement explicite empêche de réintroduire une ancienne source EXT1.
Les en-têtes locaux et le [code Espressif au tag v5.5.2](https://github.com/espressif/esp-idf/blob/v5.5.2/components/esp_hw_support/sleep_modes.c)
constituent les références techniques de cette adaptation.

## Vérification et limites

Le 4 novembre 2026, compilation de cette unité seule avec les arguments réels
du build ESP32-S3 Arduino 3.3.6, PSRAM OPI, SDK qio_opi, compilateur
`esp-x32/2511/bin/xtensa-esp32s3-elf-g++` : succès sous C++17 avec
`-Wall -Wextra -Werror` (suppression du `-w` Arduino). Il s'agit d'une vérification
de traduction, pas d'une preuve de liaison complète ni d'un test matériel.

Lors de la remise isolée de ce helper, aucun flash ni sommeil/réveil réel n’avait été exécuté. L’intégration 0.4 est désormais installée et vérifiée (voir status.md). Restent à
observer sur la carte : maintien de l'écran, arrêt des opérations, réveil par
chacun des trois contacts, absence de réveil programmé, relâchement sans boucle
de réveil et restauration des commandes après redémarrage. Aucun niveau de
consommation ni gain d'autonomie n'est revendiqué.

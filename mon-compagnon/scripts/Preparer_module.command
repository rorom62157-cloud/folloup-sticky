#!/bin/bash
# Diagnostic update by default. Full erase requires an explicit option. Never touches SD.
set -euo pipefail
umask 077
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool="$HOME/Library/Arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool"
core="$HOME/Library/Arduino15/packages/esp32/hardware/esp32/3.3.8"
image="$root/artifacts/diagnostic/diagnostic.ino"
update_only=true
case "${1:-}" in
  '') ;;
  --update-only) update_only=true ;;
  --erase-and-install) update_only=false ;;
  *) echo 'Usage : Preparer_module.command [--update-only | --erase-and-install]'; exit 2 ;;
esac
[ "$#" -le 1 ] || exit 2

finish() {
  result=$?
  if [ "$result" -ne 0 ]; then
    echo 'Arrêt sur erreur. Ne pas considérer le module comme prêt.'
    echo 'Pour reprendre sans effacement complet : relancer avec --update-only.'
  fi
  if [ -t 0 ]; then read -r -p 'Entrée pour fermer cette fenêtre… ' _ || true; fi
}
trap finish EXIT

if "$update_only"; then
  echo 'Mise à jour du diagnostic, sans effacement complet préalable.'
else
  echo 'Préparation ESP32-S3 : effacement interne SANS sauvegarde, puis diagnostic.'
fi
echo 'Fermer les moniteurs série Arduino. La microSD ne sera ni montée ni formatée.'
[ -x "$tool" ] || { echo 'esptool 5.2.0 absent.'; exit 1; }
[ -f "$core/tools/partitions/boot_app0.bin" ] || exit 1

# Bind to the port requested by the user; a moved board needs re-identification.
shopt -s nullglob
ports=(/dev/cu.usbmodem*)
[ "${#ports[@]}" -eq 1 ] || { echo 'Brancher uniquement la carte cible : aucun port USB unique détecté.'; exit 1; }
port="${ports[0]}"
[ "$port" = /dev/cu.usbmodem14501 ] || { echo 'Port différent de la cible validée : arrêt avant écriture.'; exit 1; }
echo "Port cible : $port"

# Build and inspect BEFORE the first destructive action. No network installation.
sh "$root/scripts/build-diagnostic.sh"
"$tool" --chip esp32s3 image-info "$image.bootloader.bin"
"$tool" --chip esp32s3 image-info "$image.bin"
[ "$(wc -c < "$image.bin")" -le 1310720 ] || { echo 'Application trop grande pour app0.'; exit 1; }
python3 "$root/scripts/check-diagnostic-preflight.py" partitions "$image.partitions.bin"

# Explicit chip selection rejects a different chip. The flash-size gate is exact;
# unfamiliar output fails closed. No --force or eFuse modifications are used.
chip_info=$("$tool" --chip esp32s3 --port "$port" read-mac)
printf '%s\n' "$chip_info"
printf '%s\n' "$chip_info" | python3 "$root/scripts/check-diagnostic-preflight.py" target
flash_info=$("$tool" --chip esp32s3 --port "$port" flash-id)
printf '%s\n' "$flash_info"
if ! printf '%s\n' "$flash_info" | LC_ALL=C grep -Eq 'Detected flash size: 16MB[[:space:]]*$'; then
  echo 'Flash 16 MiB non confirmée : aucun effacement.'
  exit 1
fi
security_info=$("$tool" --chip esp32s3 --port "$port" get-security-info)
printf '%s\n' "$security_info"
printf '%s\n' "$security_info" | python3 "$root/scripts/check-diagnostic-preflight.py" security

if ! "$update_only"; then
  echo 'EFFACEMENT de la mémoire interne, sans sauvegarde.'
  "$tool" --chip esp32s3 --port "$port" erase-flash
fi
echo 'Installation du diagnostic (pas de l’application complète).'
"$tool" --chip esp32s3 --port "$port" write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0 "$image.bootloader.bin" \
  0x8000 "$image.partitions.bin" \
  0xe000 "$core/tools/partitions/boot_app0.bin" \
  0x10000 "$image.bin"
"$tool" --chip esp32s3 --port "$port" verify-flash \
  0x0 "$image.bootloader.bin" \
  0x8000 "$image.partitions.bin" \
  0xe000 "$core/tools/partitions/boot_app0.bin" \
  0x10000 "$image.bin"
echo 'Diagnostic installé et vérifié. Moniteur série à 115200 : i = identité ; p = I2C.'
echo 'L’écran peut conserver son ancienne image : ce diagnostic ne le rafraîchit pas.'

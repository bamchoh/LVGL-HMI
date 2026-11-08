#!/bin/bash
# Apply the 480x320 HDMI LCD + ADS7846 touch settings to Raspberry Pi OS (Bookworm/Trixie).
# Run as:  sudo ~/hml/lcd/apply_lcd.sh          (legacy: firmware drives HDMI, /dev/fb0 = simplefb 480x320)
#          sudo ~/hml/lcd/apply_lcd.sh --fkms   (old Yocto-style vc4-fkms-v3d; broken on kernel 6.18:
#                                               "Cannot find any crtc or sizes" and fb0 disappears)
#          sudo ~/hml/lcd/apply_lcd.sh --kms    (keep vc4-kms-v3d, force the mode from cmdline.txt)
#          sudo ~/hml/lcd/apply_lcd.sh --revert (restore the backups)
set -euo pipefail
BOOT=/boot/firmware
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }

if [ "${1:-}" = "--revert" ]; then
  cp -v "$BOOT/config.txt.hml-bak" "$BOOT/config.txt"
  cp -v "$BOOT/cmdline.txt.hml-bak" "$BOOT/cmdline.txt"
  systemctl enable getty@tty1.service   # quiet_boot.sh disabled it
  echo "restored; reboot to apply"; exit 0
fi

# backups (only the first time, so --revert always returns to the original)
[ -f "$BOOT/config.txt.hml-bak" ]  || cp -v "$BOOT/config.txt"  "$BOOT/config.txt.hml-bak"
[ -f "$BOOT/cmdline.txt.hml-bak" ] || cp -v "$BOOT/cmdline.txt" "$BOOT/cmdline.txt.hml-bak"

# start from the original config.txt, drop the stock KMS line (we add our own below)
sed '/^dtoverlay=vc4-kms-v3d/d' "$BOOT/config.txt.hml-bak" > "$BOOT/config.txt"
CMDLINE=$(cat "$BOOT/cmdline.txt.hml-bak")

if [ "${1:-}" = "--kms" ]; then
  # KMS variant: the kernel drives HDMI, custom mode via video= (M = CVT timings, D = force digital)
  sed -e 's/^dtoverlay=vc4-fkms-v3d/dtoverlay=vc4-kms-v3d/' \
      -e '/^hdmi_/d' -e '/^framebuffer_/d' -e '/^gpu_mem/d' "$HERE/config-lcd.txt" >> "$BOOT/config.txt"
  CMDLINE="$CMDLINE video=HDMI-A-1:480x320M@60D"
elif [ "${1:-}" = "--fkms" ]; then
  cat "$HERE/config-lcd.txt" >> "$BOOT/config.txt"
else
  # legacy: no vc4 overlay at all. The firmware sets the mode from hdmi_* and hands the kernel a
  # simple-framebuffer (/dev/fb0, 480x320 RGB565). No DRM/GPU, which the fbdev backend does not need.
  sed -e '/^dtoverlay=vc4-fkms-v3d/d' "$HERE/config-lcd.txt" >> "$BOOT/config.txt"
fi

# hide the blinking console cursor and the boot logo on the LCD
case "$CMDLINE" in *vt.global_cursor_default*) ;; *) CMDLINE="$CMDLINE vt.global_cursor_default=0 logo.nologo";; esac
printf '%s\n' "$CMDLINE" > "$BOOT/cmdline.txt"

# boot / shutdown messages off the LCD (console on tty3, no login prompt on tty1)
[ -f "$HERE/quiet_boot.sh" ] && bash "$HERE/quiet_boot.sh" > /dev/null

echo "---- $BOOT/config.txt (tail)"; tail -n 25 "$BOOT/config.txt"
echo "---- $BOOT/cmdline.txt"; cat "$BOOT/cmdline.txt"
echo
echo "done. reboot now with:  sudo reboot"

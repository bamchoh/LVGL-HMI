#!/bin/bash
# Keep the boot / shutdown console text off the HMI screen (Raspberry Pi OS Bookworm/Trixie).
#   - kernel + systemd messages go to tty3 (never shown) instead of tty1 (the LCD)
#   - "quiet loglevel=3": fewer kernel messages, faster boot
#   - no login prompt on tty1 (getty@tty1 disabled; log in with SSH)
# The serial console (console=serial0,...) is left as it is.
# Run as:  sudo ~/hml/tools/rpi/quiet_boot.sh            (then reboot)
#          sudo ~/hml/tools/rpi/quiet_boot.sh --revert   (messages and login prompt back on tty1)
# Safe to run more than once. apply_lcd.sh runs it too, because it rebuilds cmdline.txt.
set -euo pipefail
CMDLINE_FILE=/boot/firmware/cmdline.txt
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }

# cmdline.txt is a single line of space separated options
read -r -a opts < "$CMDLINE_FILE"
out=()
if [ "${1:-}" = "--revert" ]; then
  for o in "${opts[@]}"; do
    case "$o" in
      console=tty3) out+=("console=tty1") ;;
      quiet|loglevel=3) ;;
      *) out+=("$o") ;;
    esac
  done
  getty=enable
else
  for o in "${opts[@]}"; do
    case "$o" in
      console=tty1) out+=("console=tty3") ;;
      quiet|loglevel=*) ;;   # re-added below, exactly once
      *) out+=("$o") ;;
    esac
  done
  out+=(quiet loglevel=3)
  case " ${out[*]} " in *" vt.global_cursor_default="*) ;; *) out+=(vt.global_cursor_default=0) ;; esac
  case " ${out[*]} " in *" logo.nologo "*) ;; *) out+=(logo.nologo) ;; esac
  # hml.service already conflicts with getty@tty1, but getty starts first at boot and the login
  # prompt flashes on the LCD before the HMI comes up
  getty=disable
fi
# write cmdline.txt first: it is the part that hides the kernel messages
printf '%s\n' "${out[*]}" > "$CMDLINE_FILE"
systemctl "$getty" getty@tty1.service || echo "warning: systemctl $getty getty@tty1 failed"

echo "---- $CMDLINE_FILE"; cat "$CMDLINE_FILE"
echo
echo "done. reboot to apply:  sudo reboot"

#!/bin/sh
# ArchiveTV for the Miyoo Mini family (Onion OS, spruceOS, sprigUI): browse, download and watch Internet Archive videos.
APPDIR=$(cd "$(dirname "$0")" && pwd)
cd "$APPDIR" || exit 1
LOG="$APPDIR/log.txt"
ROTATE=1      # 1 = draw rotated 180 degrees (right for Onion). Set 0 if the picture is upside down.
VERIFY=1      # 1 = check HTTPS certificates, 0 = don't (only if loading fails because of the clock)

if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 524288 ]; then mv "$LOG" "$LOG.old"; fi
echo "--- $(date)" >> "$LOG"
# FFmpeg 2.4 (LGPL) ships in libs/, so the same folder runs on Onion OS and spruceOS / sprigUI
export LD_LIBRARY_PATH="$APPDIR/libs:/mnt/SDCARD/miyoo/lib:/config/lib:/customer/lib:$LD_LIBRARY_PATH"
# Without a clock battery the date can be very old, and certificates can't be checked then
if [ "$(date +%Y)" -lt 2025 ]; then VERIFY=0; fi
echo "System year: $(date +%Y) - TLS verification: $VERIFY" >> "$LOG"
export VERIFY
chmod +x "$APPDIR/archivetv" 2>/dev/null

GOVF=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
GOV=$(cat "$GOVF" 2>/dev/null)
echo performance > "$GOVF" 2>/dev/null
touch /tmp/stay_awake    # Onion: no auto-sleep while watching

"$APPDIR/archivetv" "$APPDIR" "$ROTATE" >> "$LOG" 2>&1
echo "exit code $?" >> "$LOG"

rm -f /tmp/stay_awake
[ -n "$GOV" ] && echo "$GOV" > "$GOVF" 2>/dev/null
sync
exit 0

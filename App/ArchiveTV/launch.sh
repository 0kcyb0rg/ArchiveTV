#!/bin/sh
# ArchiveTV for the Miyoo Mini family (Onion OS, spruceOS, sprigUI): browse, download and watch Internet Archive videos.
{   # the braces make the shell read this whole file before running it, so an update may replace it
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
chmod +x "$APPDIR/archivetv" "$APPDIR/bin/curl" 2>/dev/null

GOVF=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
GOV=$(cat "$GOVF" 2>/dev/null)
echo performance > "$GOVF" 2>/dev/null
touch /tmp/stay_awake    # Onion: no auto-sleep while watching

"$APPDIR/archivetv" "$APPDIR" "$ROTATE" >> "$LOG" 2>&1
CODE=$?
echo "exit code $CODE" >> "$LOG"

rm -f /tmp/stay_awake
[ -n "$GOV" ] && echo "$GOV" > "$GOVF" 2>/dev/null

# An update the app downloaded (Settings > Update ArchiveTV): every file in update/files was checked by the app
# before it wrote update/ready. Move them into place now that the app has closed.
U="$APPDIR/update"
if [ -f "$U/ready" ] && [ -f "$U/list" ]; then
    echo "installing update $(cat "$U/ready")" >> "$LOG"
    OK=1
    while IFS= read -r F; do
        [ -n "$F" ] || continue
        case "$F" in /*|*..*) continue ;; esac
        if [ -f "$U/files/$F" ]; then
            mkdir -p "$(dirname "$APPDIR/$F")"
            mv -f "$U/files/$F" "$APPDIR/$F" || { echo "update: could not move $F" >> "$LOG"; OK=0; }
            [ "$F" = music/music.ogg ] && rm -f "$APPDIR/music/music.raw"    # unpacked again from the new song
        else
            echo "update: $F is missing" >> "$LOG"; OK=0
        fi
    done < "$U/list"
    chmod +x "$APPDIR/archivetv" "$APPDIR/bin/curl" "$APPDIR/launch.sh" 2>/dev/null
    if [ "$OK" = 1 ]; then rm -rf "$U"; echo "update installed" >> "$LOG"; else rm -f "$U/ready" "$U/list"; fi
fi
sync
# 42 = the app asked to be started again (to finish an update): the new launch.sh takes over
if [ "$CODE" = 42 ]; then exec /bin/sh "$APPDIR/launch.sh"; fi
exit 0
}

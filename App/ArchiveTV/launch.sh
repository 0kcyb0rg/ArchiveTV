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

# ---- updates (Settings > Update ArchiveTV) ----
# The app downloads and checks every file into update/files, then writes update/list and update/ready.
# Here, before the app starts, they are moved into place; the files they replace go to update/backup and
# update/ready becomes update/trial. A new version that starts properly deletes update/ (see upd_start_check
# in src/atv.c). If update/trial is still there when the app has closed, the new version never got going:
# the old files are put back and the old version starts instead.
U="$APPDIR/update"
install_update() {
    echo "installing update $(cat "$U/ready")" >> "$LOG"
    OK=1
    while IFS= read -r F; do
        [ -n "$F" ] || continue
        case "$F" in /*|*..*) continue ;; esac
        [ -f "$U/files/$F" ] || continue      # moved in already, by an install that was cut short
        mkdir -p "$(dirname "$APPDIR/$F")" "$(dirname "$U/backup/$F")"
        if [ -f "$APPDIR/$F" ] && [ ! -f "$U/backup/$F" ]; then mv -f "$APPDIR/$F" "$U/backup/$F" || OK=0; fi
        mv -f "$U/files/$F" "$APPDIR/$F" || { echo "update: could not move $F" >> "$LOG"; OK=0; }
        [ "$F" = music/music.ogg ] && rm -f "$APPDIR/music/music.raw"    # unpacked again from the new song
    done < "$U/list"
    chmod +x "$APPDIR/archivetv" "$APPDIR/bin/curl" "$APPDIR/launch.sh" 2>/dev/null
    mv -f "$U/ready" "$U/trial"
    sync
    [ "$OK" = 1 ] || { echo "update: install failed" >> "$LOG"; roll_back; }
}
roll_back() {
    echo "update: putting the old version back" >> "$LOG"
    while IFS= read -r F; do
        [ -n "$F" ] || continue
        case "$F" in /*|*..*) continue ;; esac
        if [ -f "$U/backup/$F" ]; then mv -f "$U/backup/$F" "$APPDIR/$F"; else rm -f "$APPDIR/$F"; fi    # no backup = a new file
    done < "$U/list"
    chmod +x "$APPDIR/archivetv" "$APPDIR/bin/curl" "$APPDIR/launch.sh" 2>/dev/null
    head -n 1 "$U/trial" > "$APPDIR/update-failed.txt" 2>/dev/null    # the app won't offer this version again unasked
    rm -rf "$U"
    sync
}
if [ -f "$U/ready" ] && [ -f "$U/list" ]; then install_update; fi

"$APPDIR/archivetv" "$APPDIR" "$ROTATE" >> "$LOG" 2>&1
CODE=$?
echo "exit code $CODE" >> "$LOG"

rm -f /tmp/stay_awake
[ -n "$GOV" ] && echo "$GOV" > "$GOVF" 2>/dev/null
if [ -f "$U/trial" ]; then echo "update: the new version did not start" >> "$LOG"; roll_back; CODE=42; fi
sync
# 42 = start again: the app asked for it to install an update, or an update was just undone
if [ "$CODE" = 42 ]; then exec /bin/sh "$APPDIR/launch.sh"; fi
exit 0
}

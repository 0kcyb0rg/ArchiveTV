#!/bin/bash
# ArchiveTV for Linux handhelds (Anbernic RG35XX Plus / H / SP / 2024, RG40XX, TrimUI Smart Pro / Brick, ...):
# browse, download and watch Internet Archive videos. Put this file and the archivetv folder in your ports folder.
# PortMaster sets up the buttons when it's installed; without it the app tries on its own.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then controlfolder="$XDG_DATA_HOME/PortMaster"
else controlfolder="/roms/ports/PortMaster"; fi

if [ -f "$controlfolder/control.txt" ]; then
    source "$controlfolder/control.txt"
    [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
    get_controls
    export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi

GAMEDIR="$(cd "$(dirname "$0")" && pwd)/archivetv"
cd "$GAMEDIR" || exit 1
LOG="$GAMEDIR/log.txt"
if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 524288 ]; then mv "$LOG" "$LOG.old"; fi
echo "--- $(date)" >> "$LOG"
VERIFY=1                                    # check HTTPS certificates; not possible while the clock is wrong
if [ "$(date +%Y)" -lt 2025 ]; then VERIFY=0; fi
export VERIFY
export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
chmod +x "$GAMEDIR/archivetv" 2>/dev/null
echo "device: $(cat /proc/device-tree/model 2>/dev/null | tr -d '\0'), $(uname -m), CFW ${CFW_NAME:-?}, PortMaster ${controlfolder}" >> "$LOG"

"$GAMEDIR/archivetv" "$GAMEDIR" 0 >> "$LOG" 2>&1
echo "exit code $?" >> "$LOG"
sync
command -v pm_finish >/dev/null 2>&1 && pm_finish
exit 0

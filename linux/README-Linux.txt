ArchiveTV for Linux handhelds (BETA) - by 0kCyb0rg (okcyborg.com)
================================================================
The Miyoo Mini app, built for 64-bit ARM Linux handhelds:
  Anbernic RG35XX Plus / H / SP / 2024 / Pro, RG28XX, RG34XX, RG40XX H / V, RG CubeXX,
  TrimUI Smart Pro and Brick - on muOS, KNULLI or the stock / CrossMix firmware.
It needs Wi-Fi to browse and download (videos already on the card play without it).
This is a beta: it hasn't been run on these devices by the author yet. Reports welcome!

INSTALL
  Copy ArchiveTV.sh and the archivetv folder into your ports folder, next to your other ports:
    muOS:     /mnt/mmc/ports   (or the ports folder on SD2)   - then it shows under Ports
    KNULLI:   /userdata/roms/ports                           - Ports collection
    TrimUI stock / CrossMix: Roms/PORTS                      - needs PortMaster installed
  Start "ArchiveTV" from Ports. With PortMaster installed the buttons are set up for you.

BUTTONS (the same as on the Miyoo)
  A choose   B back   X / Y as shown at the bottom   L/R tabs   START play   SELECT: picture size
  MENU / HOME: quit menu. The left stick works as the D-pad.

IF SOMETHING'S WRONG
  archivetv/log.txt says what happened: which controller was found, the screen size, errors.
  Please send it along with your device and firmware: github.com/mroxfordyazuka/ArchiveTV/issues

Updates for this version come as new releases on GitHub (Settings > Update is for the Miyoo).
Uses the system's curl for downloads. FFmpeg 2.4 (LGPL 2.1) is in archivetv/libs; see archivetv/licenses.

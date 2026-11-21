ARCHIVETV for the Miyoo Mini family - Onion OS
Browse Internet Archive videos, watch them while they download, keep them on the card.
By @0kCyb0rg - okcyborg.com

DEVICES (one build for all of them)
  Miyoo Mini v1-v3, Mini Plus   640x480
  Miyoo Mini v4, Mini Flip      752x560 (full screen, thanks to the "full_resolution" file)
  Wi-Fi models (Mini Plus, Mini Flip) can browse and download.
  Models without Wi-Fi play videos you copy into App/ArchiveTV/videos from a computer.
  Needs Onion OS 4.2 or newer (it uses the FFmpeg and curl that come with Onion).
  If the picture is upside down on your model, set ROTATE=0 in launch.sh.

INSTALL
  Copy the App folder from this zip to the root of the SD card (merge folders).
  It ends up as /mnt/SDCARD/App/ArchiveTV. Open "ArchiveTV" from Apps.

HOME TABS (L1 / R1 to switch)
  Search  - search videos or collections; recent searches are kept
  Guide   - hand-picked collections by category (edit guide.txt)
  Saved   - your bookmarks: shows, collections and searches (Y anywhere saves)
  Lists   - your playlists
  Files   - the download queue, paused downloads, then the videos on your card
  (gear)  - Settings: music, menu sounds, scanlines, theme, vsync, About

BUTTONS
  D-pad move      LEFT/RIGHT page      A open / menu      B back
  START  do it now: play a video, run a search, open a saved show
  Y      save to the Saved tab          SELECT  change theme
  X      tap: delete / sort / remove (whatever the footer says)
         hold: scroll a long title to its end
  MENU   turn ArchiveTV off (asks first)
  Themes: Night (default), Cute, Game Boy, Plain.
  The arrow badge in the footer (e.g. 45%) means a download is running.

A VIDEO (press A on a file)
  Watch now              starts playing once a little is downloaded (the bar shows where),
                         keeps downloading while you watch
  Add to download queue  downloads in the background while you browse (Files tab)
  Add to playlist...     pick a playlist or make a new one
  Green = recommended (480p or lower MP4). Yellow = HD, may stutter.
  Some files (some .ogv and .mp4) can't start early; ArchiveTV offers to queue those.

FILES TAB
  Queue items: Watch now, Pause / Resume download, Delete download.
  Paused downloads keep what they have and pick up where they stopped.
  Videos: Play, Add to playlist, Delete. START plays right away.

PLAYING
  A / START pause        LEFT/RIGHT -/+10 s     DOWN/UP or L1/R1 -/+1 min
  L2/R2 -/+5 min         SELECT start over      Y show/hide the time bar
  X screen off/on        B / MENU stop          It remembers where you stopped.

FILES IN THIS FOLDER
  videos/       downloaded videos       playlists/   one .txt per playlist
  channels.txt  your saved things       guide.txt    the Guide tab
  queue.txt     the download queue      settings.txt your settings
  overlays/     scanlines               music/  sfx/ menu music and sounds
                (the first run unpacks music.ogg to music.raw, about 26 MB)
  log.txt       what happened (send this when something breaks)

CREDITS
  Made by @0kCyb0rg - okcyborg.com
  Music: "Select a Level" by @0kCyb0rg
  Menu sounds by @0kCyb0rg
  Font: m5x7 by Daniel Linssen (managore.itch.io/m5x7)
  Video playback: FFmpeg (ffmpeg.org), as shipped with Onion OS
  Downloads: curl (curl.se), as shipped with Onion OS
  CA certificates: Mozilla, via curl (MPL 2.0, see licenses/)
  Videos: Internet Archive - please donate at archive.org/donate

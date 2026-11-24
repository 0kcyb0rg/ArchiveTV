ARCHIVETV for the Miyoo Mini family - Onion OS
Browse Internet Archive videos, watch them while they download, keep them on the card.
By @0kCyb0rg - okcyborg.com

DEVICES (one build for all of them)
  Miyoo Mini v1-v3, Mini Plus   640x480
  Miyoo Mini v4, Mini Flip      752x560 (full screen, thanks to the "full_resolution" file)
  Wi-Fi models (Mini Plus, Mini Flip) can browse and download.
  Models without Wi-Fi play videos you copy into App/ArchiveTV/videos from a computer.
  Works on Onion OS 4.2+, and (beta, untested) on spruceOS and sprigUI.
  It brings its own FFmpeg (libs/), and its own curl (bin/) for systems that have none.
  If the picture is upside down on your model, set ROTATE=0 in launch.sh.

INSTALL
  Copy the App folder from this zip to the root of the SD card (merge folders).
  It ends up as /mnt/SDCARD/App/ArchiveTV. Open "ArchiveTV" from Apps.
  Same folder for Onion OS, spruceOS and sprigUI.

HOME TABS (L1 / R1 or L2 / R2 to switch)
  Search  - search videos or collections; recent searches are kept
  Guide   - hand-picked collections by category (edit guide.txt)
  Saved   - your bookmarks: shows, collections and searches (Y anywhere saves)
  Lists   - your playlists
  Files   - the download queue, paused downloads, then the videos on your card
  (gear)  - Settings: music, menu sounds, scanlines, theme, vsync,
            group episodes, turn off after downloads, thumbnails, subtitles,
            Update, About

BUTTONS
  D-pad move      LEFT/RIGHT page      A open / menu      B back
  START  do it now: play a video, run a search, open a saved show
  Y      save to the Saved tab          SELECT  change theme
  X      tap: delete / sort / remove (whatever the footer says)
         hold: scroll a long title to its end
  MENU   turn ArchiveTV off (asks first)
  Themes: Night (default), Cute, Game Boy, Plain.
  The arrow badge in the footer (e.g. 45%) means a download is running.

SHOWS AND SERIES
  Episodes of one show are folded into a single row ("Sailor Moon  46 eps");
  open it to see them in episode order. Turn this off in Settings.
  In a show, X sorts its files: best quality first, or A to Z (episode order).
  Search results: X sorts by most popular, newest, oldest or A to Z.

TURN OFF AFTER DOWNLOADS (Settings, off by default)
  Queue up a bunch of videos and when the last one is done, ArchiveTV counts
  down 30 seconds and powers the handheld off. B keeps it on.

THUMBNAILS (Settings, on by default)
  A little picture of the selected show, item or episode shows at the right of
  the list (Internet Archive's own preview, or a still from the video). Your
  downloaded videos get a frame from the video itself, no Wi-Fi needed.
  Pictures are kept in thumbs/ so they show at once next time.

UPDATES (Settings > Update ArchiveTV, needs Wi-Fi)
  At start ArchiveTV quietly checks for a new version and says so at the bottom
  of the screen. Update downloads only the files that changed (B stops; it picks
  up where it left off), checks every one, then restarts into the new version.
  Videos, playlists, saved shows, the queue and settings are never touched.
  It won't update if the clock is wrong (certificates can't be checked then).

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
  Videos: Play, Add to playlist, Playlist of "<show>", Delete. START plays.
  Playlist of "<show>" puts every downloaded episode of that show into a
  playlist named after it, in episode order.
  Y marks videos (+). Then A: add all the marked ones to a playlist, or delete them.

PLAYING
  A / START pause        LEFT/RIGHT -/+10 s     DOWN/UP or L1/R1 -/+1 min
  L2/R2 -/+5 min         SELECT fit/zoom/stretch Y subtitles on/off/track
  X screen off/on        B / MENU stop          It remembers where you stopped.

SUBTITLES (Settings, on by default)
  Shown when the video has text subtitles of its own (SRT, ASS/SSA, MP4 text)
  or Internet Archive has a matching .srt / .vtt file next to it, which is
  downloaded along with the video. Y switches between them and off.
  Picture subtitles (DVD / Blu-ray rips) and non-Latin scripts aren't shown.
  For your own videos: put "<video name>.srt" next to the video.

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
  Video playback: FFmpeg 2.4 (ffmpeg.org), LGPL, see licenses/
  Downloads: curl (curl.se); bin/curl is a static curl + OpenSSL build, see licenses/
  CA certificates: Mozilla, via curl (MPL 2.0, see licenses/)
  Videos: Internet Archive - please donate at archive.org/donate

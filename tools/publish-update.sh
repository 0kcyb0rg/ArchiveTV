#!/bin/sh
# Publish a Miyoo version to every ArchiveTV 1.4+ install (Settings > Update ArchiveTV).
#   tools/publish-update.sh <full App/ArchiveTV folder>      (with the built archivetv, libs/ and bin/curl)
# 1. commits that folder to the miyoo-builds branch as App/ArchiveTV
# 2. writes update.txt pointing at that commit (so later pushes never change what an older list points to)
# 3. commits update.txt and pushes the branch. From that push on, apps see the new version at their next start.
set -e
PKG=$(cd "$1" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$PKG/archivetv" ] && [ -d "$PKG/libs" ] || { echo "not a full App/ArchiveTV folder: $PKG"; exit 1; }
W=$(mktemp -d)
cd "$ROOT"
git fetch -q origin miyoo-builds 2>/dev/null || true
if git rev-parse -q --verify origin/miyoo-builds >/dev/null; then
    git worktree add -q -B miyoo-builds "$W" origin/miyoo-builds
else
    git worktree add -q --detach "$W" && (cd "$W" && git checkout -q --orphan miyoo-builds && git rm -rqf . )
fi
cd "$W"
rm -rf App && mkdir -p App/ArchiveTV
cp -R "$PKG"/. App/ArchiveTV/
rm -rf App/ArchiveTV/update App/ArchiveTV/videos App/ArchiveTV/playlists App/ArchiveTV/log.txt* App/ArchiveTV/music/music.raw
find App -name '.DS_Store' -o -name '._*' | xargs rm -f
printf 'Builds of the Miyoo app, for its updater. Source: the main branch.\n' > README.md
git add -A && git commit -qm "Miyoo build $(grep APP_VERSION "$ROOT/src/version.h" | cut -d'"' -f2)" || true
python3 "$ROOT/tools/make_update.py" App/ArchiveTV "$(git rev-parse HEAD)" update.txt
git add update.txt && git commit -qm "update.txt: $(sed -n 2p update.txt)"
git push -q origin miyoo-builds
cd "$ROOT" && git worktree remove --force "$W"
echo "published"

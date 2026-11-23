#!/usr/bin/env python3
"""Write update.txt, the list the Miyoo app's own updater reads (see "updating the app itself" in src/atv.c).

    python3 tools/make_update.py <package dir> <commit> <out file>

<package dir> is a full App/ArchiveTV folder (with archivetv, libs/, bin/curl), as committed on the
miyoo-builds branch at <commit>. tools/publish-update.sh does the whole release; use that."""
import os, re, sys, zlib

app, commit, out = sys.argv[1], sys.argv[2], sys.argv[3]
version = re.search(r'#define APP_VERSION "([^"]+)"',
                    open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'version.h')).read()).group(1)
# the user's own files (and channels.txt, which they edit by saving shows) are never part of an update
skip = {'settings.txt', 'queue.txt', 'searches.txt', 'channels.txt', 'log.txt', 'log.txt.old', '.DS_Store'}
lines = ['archivetv-update 1', 'version ' + version,
         'base https://raw.githubusercontent.com/mroxfordyazuka/ArchiveTV/%s/App/ArchiveTV/' % commit]
total = 0
for folder, dirs, names in sorted(os.walk(app)):
    dirs[:] = sorted(d for d in dirs if d not in ('update', 'videos', 'playlists'))
    for name in sorted(names):
        rel = os.path.relpath(os.path.join(folder, name), app).replace(os.sep, '/')
        if name in skip or name.startswith('._') or rel == 'music/music.raw':
            continue
        if not re.fullmatch(r'[A-Za-z0-9._/-]+', rel) or '..' in rel:
            sys.exit('a file name the updater will not accept: ' + rel)
        data = open(os.path.join(folder, name), 'rb').read()
        lines.append('file %08x %d %s' % (zlib.crc32(data) & 0xffffffff, len(data), rel))
        total += len(data)
if len(lines) - 3 > 80:
    sys.exit('more than 80 files: raise UPD_MAX in src/atv.c first')
open(out, 'w').write('\n'.join(lines) + '\n')
print('update.txt: version %s, %d files, %.1f MB, from commit %s' % (version, len(lines) - 3, total / 1048576, commit))

#!/usr/bin/env python3
"""Make bsd/kern/on7put_payload.h for the kernel's file installer (xnu 0194, boot-arg on7put=1).

  on7put-payload.py OUT.h TAG SPEC...

  SPEC = DEST=SRC,MODE[,before=FILE][,backup=PATH]
    DEST    absolute path in the iOS root
    SRC     local file whose bytes go there
    MODE    octal (0755)
    before  the file DEST must be now (size + FNV-1a 64) or it is left alone
    backup  where the current DEST is copied first (only if not there yet)

Example (mali_test, ios-root-files artifact of root-tweak.yml with mali_test=true):
  on7put-payload.py on7put_payload.h malitest-43634883047 \\
    /usr/local/bin/mali_test=files/mali_test,0755 \\
    /System/Library/xpc/launchd.plist=files/launchd.plist.after,0644,before=files/launchd.plist.before,backup=/System/Library/xpc/launchd.plist.pre-malitest

The header can hold Apple files (launchd.plist): it belongs to a local build only, never to git.
"""
import sys


def fnv(b):
    h = 0xcbf29ce484222325
    for c in b:
        h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return h


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    out, tag, specs = sys.argv[1], sys.argv[2], sys.argv[3:]
    lines = ['/* made by tools/on7put-payload.py: local build only, never commit (may hold Apple files) */']
    entries = []
    for i, spec in enumerate(specs):
        dest, rest = spec.split('=', 1)
        parts = rest.split(',')
        src, mode = parts[0], int(parts[1], 8)
        opts = dict(p.split('=', 1) for p in parts[2:])
        assert dest.startswith('/'), dest
        data = open(src, 'rb').read()
        lines.append('static const unsigned char on7put_d%d[%d] = {' % (i, max(len(data), 1)))
        for at in range(0, len(data), 24):
            lines.append('\t' + ','.join('0x%02x' % c for c in data[at:at + 24]) + ',')
        lines.append('};')
        before = 0, 0, 0
        if 'before' in opts:
            b = open(opts['before'], 'rb').read()
            before = 1, fnv(b), len(b)
        backup = cstr(opts['backup']) if 'backup' in opts else 'NULL'
        entries.append('\t{ %s, 0%o, on7put_d%d, %d, %d, 0x%016xULL, %d, %s },' % (
            cstr(dest), mode, i, len(data), before[0], before[1], before[2], backup))
        print('%s <- %s: %d bytes, fnv %016x, mode %o%s%s' % (
            dest, src, len(data), fnv(data), mode,
            ', before %d bytes %016x' % (before[2], before[1]) if before[0] else '',
            ', backup ' + opts['backup'] if 'backup' in opts else ''))
    lines.append('static const struct on7put_file on7put_files[] = {')
    lines += entries
    lines.append('};')
    lines.append('#define ON7PUT_COUNT %d' % len(entries))
    lines.append('#define ON7PUT_TAG %s' % cstr(tag))
    with open(out, 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()

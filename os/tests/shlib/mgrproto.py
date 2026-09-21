# mgrproto.py -- compare one MGR client's escape streams in a console
# transcript, cut between the command's echo and the END marker's.  Bytes,
# not lines: the stream has no final newline.
#
#	mgrproto.py <transcript> <client> [<args>]
# prints "same <n>", "differ <n> <m>" or "missing".
import sys

b = open(sys.argv[1], 'rb').read()
c = sys.argv[2]
a = sys.argv[3] if len(sys.argv) > 3 else ''
out = []
for k in ('s', 'h'):
    i = b.find(('# /bin/%s.%s%s\r\n' % (k, c, a)).encode())
    j = b.find(('# echo END.%s.%s' % (k, c)).encode(), i) if i >= 0 else -1
    if i < 0 or j <= i:
        print('missing')
        raise SystemExit(0)
    # Drop the kernel's `shlib:' messages from the stream.
    d = b[i:j].split(b'\r\n', 1)[1]
    d = b''.join(x for x in d.split(b'\r\n')
                 if not x.startswith(b'shlib: '))
    out.append(d)
if out[0] != out[1]:
    print('differ %d %d' % (len(out[0]), len(out[1])))
else:
    print('same %d' % len(out[0]))

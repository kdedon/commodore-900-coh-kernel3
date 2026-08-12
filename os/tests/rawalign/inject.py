#!/usr/bin/env python3
"""inject.py <image> <partition-start-block> <hostfile> <guest-path> ...

Replace the contents of guest files inside one Coherent partition of a disk
image, in place.  Same filesystem knowledge as hostbuild/inject-kernel.py --
this one takes a partition offset and walks directories, so it can reach
/drv/notty as well as /coherent.  Old blocks are leaked, which is fine for a
scratch boot image.
"""
import sys

BSIZE = 512
INODE_SIZE = 64


def u16(b, o): return b[o] | b[o + 1] << 8
def u32(b, o): return (b[o] | b[o + 1] << 8) << 16 | (b[o + 2] | b[o + 3] << 8)
def p16(v): return bytes((v & 0xFF, (v >> 8) & 0xFF))


def p32(v):
    hi, lo = (v >> 16) & 0xFFFF, v & 0xFFFF
    return bytes((hi & 0xFF, hi >> 8, lo & 0xFF, lo >> 8))


def l3get(b, o): return b[o] << 16 | b[o + 1] | b[o + 2] << 8
def l3put(v): return bytes(((v >> 16) & 0xFF, v & 0xFF, (v >> 8) & 0xFF))


class FS:
    def __init__(self, d, base):
        self.d = d
        self.base = base * BSIZE
        sb = self.blk(1)
        self.isize = u16(sb, 0)
        self.fsize = u32(sb, 2)

    def blk(self, n):
        o = self.base + n * BSIZE
        return self.d[o:o + BSIZE]

    def wblk(self, n, data):
        o = self.base + n * BSIZE
        self.d[o:o + len(data)] = data

    def inode_off(self, ino):
        return self.base + 2 * BSIZE + (ino - 1) * INODE_SIZE

    def alloc_block(self):
        sb = bytearray(self.blk(1))
        nfree = u16(sb, 6)
        if nfree == 0:
            raise RuntimeError("free list exhausted")
        nfree -= 1
        bno = u32(sb, 8 + 4 * nfree)
        if nfree == 0:
            if bno == 0:
                raise RuntimeError("out of disk space")
            nxt = self.blk(bno)
            nfree = u16(nxt, 0)
            sb[8:8 + 4 * 64] = nxt[2:2 + 4 * 64]
        sb[6:8] = p16(nfree)
        self.wblk(1, sb)
        if bno == 0 or bno >= self.fsize:
            raise RuntimeError("bad free block %d" % bno)
        self.wblk(bno, bytes(BSIZE))
        return bno

    def dirents(self, ino):
        ioff = self.inode_off(ino)
        size = u32(self.d, ioff + 8)
        addrs = [l3get(self.d, ioff + 12 + 3 * i) for i in range(13)]
        pos = 0
        for a in addrs[:10]:
            if a == 0 or pos >= size:
                break
            blk = self.blk(a)
            for e in range(0, BSIZE, 16):
                if pos + e >= size:
                    break
                n = u16(blk, e)
                nm = bytes(blk[e + 2:e + 16]).split(b'\0')[0].decode('latin1')
                if n:
                    yield n, nm
            pos += BSIZE

    def lookup(self, path):
        ino = 2
        for part in path.strip('/').split('/'):
            for n, nm in self.dirents(ino):
                if nm == part:
                    ino = n
                    break
            else:
                raise RuntimeError("%s: no %s" % (path, part))
        return ino

    def replace(self, path, data):
        ino = self.lookup(path)
        ioff = self.inode_off(ino)
        old = u32(self.d, ioff + 8)
        nblk = (len(data) + BSIZE - 1) // BSIZE
        blocks = [self.alloc_block() for _ in range(nblk)]
        for i, b in enumerate(blocks):
            self.wblk(b, data[i * BSIZE:(i + 1) * BSIZE])
        addrs = [0] * 13
        addrs[:min(nblk, 10)] = blocks[:10]
        rest = blocks[10:]
        if rest:
            ind = self.alloc_block()
            addrs[10] = ind
            ib = bytearray(BSIZE)
            for i, b in enumerate(rest[:128]):
                ib[4 * i:4 * i + 4] = p32(b)
            self.wblk(ind, ib)
            rest = rest[128:]
        if rest:
            dbl = self.alloc_block()
            addrs[11] = dbl
            db = bytearray(BSIZE)
            for j in range(0, len(rest), 128):
                l1 = self.alloc_block()
                db[4 * (j // 128):4 * (j // 128) + 4] = p32(l1)
                ib = bytearray(BSIZE)
                for i, b in enumerate(rest[j:j + 128]):
                    ib[4 * i:4 * i + 4] = p32(b)
                self.wblk(l1, ib)
            self.wblk(dbl, db)
        for i in range(13):
            self.d[ioff + 12 + 3 * i:ioff + 15 + 3 * i] = l3put(addrs[i])
        self.d[ioff + 8:ioff + 12] = p32(len(data))
        print("  %s: inode %d, %d -> %d bytes (%d blocks)"
              % (path, ino, old, len(data), nblk))


def main():
    img, base = sys.argv[1], int(sys.argv[2])
    d = bytearray(open(img, 'rb').read())
    fs = FS(d, base)
    args = sys.argv[3:]
    for i in range(0, len(args), 2):
        fs.replace(args[i + 1], open(args[i], 'rb').read())
    open(img, 'wb').write(fs.d)
    print("written:", img)


main()

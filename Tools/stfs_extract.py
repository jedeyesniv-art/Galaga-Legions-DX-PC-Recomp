"""Extract files from an Xbox 360 STFS package (LIVE/CON/PIRS), read-only packages.

usage: python stfs_extract.py <package> <out_dir> [--list]

Follows Xenia's block layout (stfs_container_device.cc). Files whose blocks are not
consecutive are followed through the level-0 hash tables.
"""
import os
import struct
import sys

BLOCK = 0x1000
LEVELS = (0xAA, 0xAA * 0xAA, 0xAA * 0xAA * 0xAA)


def u24le(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)


class Stfs:
    def __init__(self, path):
        self.f = open(path, 'rb')
        hdr = self.f.read(0x971A)
        if hdr[:4] not in (b'LIVE', b'CON ', b'PIRS'):
            raise SystemExit('not an STFS package')
        header_size = struct.unpack_from('>I', hdr, 0x340)[0]
        self.base = (header_size + 0xFFF) & ~0xFFF
        vd = hdr[0x379:0x379 + 0x24]
        self.read_only = vd[2] & 1
        self.per_table = 1 if self.read_only else 2
        self.ft_count = struct.unpack_from('<H', vd, 3)[0]
        self.ft_block = u24le(vd, 5)
        self.title_id = struct.unpack_from('>I', hdr, 0x360)[0]

    def backing(self, index):
        block = index
        for base in LEVELS:
            block += ((index + base) // base) * self.per_table
            if index < base:
                break
        return block

    def hash_block(self, index):
        # Level-0 hash table holding the entry of data block `index`.
        group = index // LEVELS[0]
        block = group * (LEVELS[0] + self.per_table)
        if index >= LEVELS[0]:
            block += ((index // LEVELS[1]) + 1) * self.per_table
            if index >= LEVELS[1]:
                block += self.per_table
        return block

    def read_block(self, index):
        self.f.seek(self.base + self.backing(index) * BLOCK)
        return self.f.read(BLOCK)

    def next_block(self, index):
        self.f.seek(self.base + self.hash_block(index) * BLOCK + (index % LEVELS[0]) * 0x18)
        e = self.f.read(0x18)
        return (e[0x15] << 16) | (e[0x16] << 8) | e[0x17]

    def chain(self, start, count, consecutive):
        out, b = [], start
        for _ in range(count):
            out.append(b)
            b = b + 1 if consecutive else self.next_block(b)
        return out

    def entries(self):
        data = b''.join(self.read_block(b) for b in self.chain(self.ft_block, self.ft_count, False))
        result = []
        for o in range(0, len(data), 0x40):
            e = data[o:o + 0x40]
            flags = e[0x28]
            if flags & 0x3F == 0:
                continue
            name = e[:flags & 0x3F].decode('ascii', 'replace')
            result.append(dict(
                name=name, dir=bool(flags & 0x80), consecutive=bool(flags & 0x40),
                blocks=u24le(e, 0x29), start=u24le(e, 0x2F),
                parent=struct.unpack_from('>h', e, 0x32)[0], size=struct.unpack_from('>I', e, 0x34)[0]))
        return result


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    pkg, out = Stfs(sys.argv[1]), sys.argv[2]
    ents = pkg.entries()

    def path_of(i):
        parts = []
        while i != -1 and i < len(ents):
            parts.append(ents[i]['name'])
            i = ents[i]['parent']
        return '/'.join(reversed(parts))

    print(f'title id {pkg.title_id:08X}, {len(ents)} entries')
    for i, e in enumerate(ents):
        p = path_of(i)
        if '--list' in sys.argv:
            print(f"{'<dir>' if e['dir'] else e['size']:>10}  {p}")
            continue
        dest = os.path.join(out, *p.split('/'))
        if e['dir']:
            os.makedirs(dest, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        remaining = e['size']
        with open(dest, 'wb') as f:
            for b in pkg.chain(e['start'], e['blocks'], e['consecutive']):
                chunk = pkg.read_block(b)[:min(BLOCK, remaining)]
                f.write(chunk)
                remaining -= len(chunk)
    if '--list' not in sys.argv:
        print('extracted to', out)


if __name__ == '__main__':
    main()

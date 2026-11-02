"""Read-only probes over ARENA2 for the dungeon editor design doc.
Usage: python3 -I dfprobe.py <ARENA2 dir>
Writes df.pkl (parsed blocks, model bounds, dungeon grids) to the current directory
for follow-up queries. Read-only over ARENA2; numbers in docs/DESIGN.md come from it."""
import struct, sys, os, pickle, collections

A2 = sys.argv[1]

def bsa(path):
    b = open(path, 'rb').read()
    count, dtype = struct.unpack_from('<hH', b, 0)
    recs = []
    pos = 4
    if dtype == 0x100:
        d = len(b) - 18 * count
        for i in range(count):
            name = b[d:d+14].split(b'\0')[0].decode('latin1')
            size = struct.unpack_from('<i', b, d + 14)[0]
            recs.append((name, pos, size)); pos += size; d += 18
    else:
        d = len(b) - 8 * count
        for i in range(count):
            rid, size = struct.unpack_from('<Ii', b, d)
            recs.append((rid, pos, size)); pos += size; d += 8
    return b, recs

# ---------------- RDB ----------------
def read_rdb(b, off):
    unk1, w, h, rootOff, unk2 = struct.unpack_from('<5I', b, off)
    refs = []
    p = off + 20
    for i in range(750):
        mid = b[p:p+5].split(b'\0')[0].decode('latin1')
        desc = b[p+5:p+8].split(b'\0')[0].decode('latin1')
        refs.append((mid, desc)); p += 8
    roots = []
    rp = off + rootOff
    for i in range(w * h):
        roots.append(struct.unpack_from('<i', b, rp + 4 * i)[0])
    objs = []  # (root, pos, x, y, z, type, res)
    for ri, ro in enumerate(roots):
        if ro < 0: continue
        pos = ro
        while True:
            nxt, prv, x, y, z = struct.unpack_from('<iiiii', b, off + pos)
            t = b[off + pos + 20]
            resOff = struct.unpack_from('<I', b, off + pos + 21)[0]
            r = off + resOff
            res = {}
            if t == 1:
                xr, yr, zr, mi = struct.unpack_from('<iiiH', b, r)
                trig = struct.unpack_from('<I', b, r + 14)[0]
                snd = b[r + 18]
                actOff = struct.unpack_from('<i', b, r + 19)[0]
                res = dict(xr=xr, yr=yr, zr=zr, mi=mi, trig=trig, snd=snd)
                if actOff > 0:
                    a = off + actOff
                    axis = b[a]; dur, mag = struct.unpack_from('<HH', b, a + 1)
                    nxto = struct.unpack_from('<i', b, a + 5)[0]; fl = b[a + 9]
                    res['act'] = dict(axis=axis, dur=dur, mag=mag, next=nxto, flags=fl)
            elif t == 3:
                tb, fl = struct.unpack_from('<HH', b, r)
                mag, snd = b[r + 4], b[r + 5]
                nxto = struct.unpack_from('<i', b, r + 6)[0]
                act = b[r + 10]
                res = dict(arc=tb >> 7, rec=tb & 0x7f, flags=fl, mag=mag, snd=snd, next=nxto, action=act)
            elif t == 2:
                u1, u2, rad = struct.unpack_from('<IIH', b, r)
                res = dict(radius=rad)
            objs.append((ri, pos, x, y, z, t, res))
            if nxt < 0: break
            pos = nxt
    return dict(w=w, h=h, refs=refs, objs=objs)

bb, brecs = bsa(os.path.join(A2, 'BLOCKS.BSA'))
rdbs = {}
for name, pos, size in brecs:
    if name.endswith('.RDB'):
        try:
            rdbs[name] = read_rdb(bb, pos)
        except Exception as e:
            print('RDB FAIL', name, e)

# ---------------- ARCH3D bounds ----------------
ab, arecs = bsa(os.path.join(A2, 'ARCH3D.BSA'))
bounds = {}
for rid, pos, size in arecs:
    ver = ab[pos:pos+4]
    pc = struct.unpack_from('<i', ab, pos + 4)[0]
    plc = struct.unpack_from('<i', ab, pos + 8)[0]
    plo = struct.unpack_from('<i', ab, pos + 48)[0]
    if pc <= 0: continue
    xs = []; ys = []; zs = []
    for i in range(pc):
        x, y, z = struct.unpack_from('<iii', ab, pos + plo + 12 * i)
        xs.append(x); ys.append(y); zs.append(z)
    bounds[rid] = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs), plc, ver)

# ---------------- MAPS dungeons ----------------
mb, mrecs = bsa(os.path.join(A2, 'MAPS.BSA'))
LET = ['N', 'W', 'L', 'S', 'B', 'M']
dungeons = []
for name, pos, size in mrecs:
    if not name.startswith('MAPDITEM') or size == 0: continue
    region = name
    cnt = struct.unpack_from('<I', mb, pos)[0]
    for i in range(cnt):
        offv = struct.unpack_from('<I', mb, pos + 4 + 8 * i)[0]
        r = pos + 4 + cnt * 8 + offv
        dc = struct.unpack_from('<I', mb, r)[0]; r += 4 + dc * 6
        locId = struct.unpack_from('<H', mb, r + 33)[0]
        lname = mb[r + 71:r + 71 + 32].split(b'\0')[0].decode('latin1')
        r += 45 + 26 + 32 + 9
        bc = struct.unpack_from('<H', mb, r + 10)[0]; r += 17
        blocks = []
        for k in range(bc):
            x, z = struct.unpack_from('<bb', mb, r)
            bf = struct.unpack_from('<H', mb, r + 2)[0]; r += 4
            blocks.append((x, z, '%s%07d.RDB' % (LET[bf >> 11], bf & 0x3ff), bool(bf & 0x400)))
        dungeons.append(dict(region=region, name=lname, locId=locId, blocks=blocks))

pickle.dump(dict(rdbs=rdbs, bounds=bounds, dungeons=dungeons),
            open("df.pkl", "wb"))
print('rdb blocks', len(rdbs), 'arch3d', len(bounds), 'dungeons', len(dungeons))

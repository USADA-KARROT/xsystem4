#!/usr/bin/env python3
"""Generate small, entirely synthetic APEG movies for the apeg-video probe.

The encoder below writes the bitstream syntax src/apeg.c decodes, with its
own copy of the format's code tables. The pictures are described here only as
macroblock and block parameters; apeg_video_fixture.inc computes the expected
samples from the same parameters with a separate model, never from the
decoder. No game data: the SOND chunk is the probe's own synthetic Vorbis.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile

# ---- code tables: codeword -> value ----
TYPE_P = {'0': 10, '10': 2, '110': 8, '11100': 1, '11101': 26, '11110': 18,
          '111110': 17, '111111': 24}
TYPE_B = {'00': 14, '01': 12, '100': 6, '101': 4, '1100': 10, '1101': 8,
          '11100': 1, '11101': 30, '111100': 28, '111101': 22, '1111100': 20,
          '1111101': 26, '1111110': 24, '1111111': 17}
CBP = {'000': 60, '0010': 4, '0011': 8, '0100': 16, '0101': 32, '01100': 1,
       '01101': 2, '01110': 12, '01111': 20, '10000': 28, '10001': 40,
       '10010': 44, '10011': 48, '10100': 52, '10101': 56, '10110': 61,
       '10111': 62, '110000': 3, '110001': 24, '110010': 36, '110011': 63,
       '1101000': 5, '1101001': 6, '1101010': 9, '1101011': 10, '1101100': 17,
       '1101101': 18, '1101110': 33, '1101111': 34}
MOTION = {'0': 0, '10': 1, '110': 2, '1110': 3, '111100': 4, '1111010': 5,
          '1111011': 6, '1111100': 7, '11111010': 8, '11111011': 9,
          '11111100': 10, '111111010': 11, '111111011': 12, '111111100': 13,
          '111111101': 14, '111111110': 15, '111111111': 16}
SKIP = {'0': 0, '100': 1, '101': 2, '1100': 3, '1101': 4, '11100': 5,
        '11101': 6, '1111000': 7, '1111001': 8}
SKIP_ESCAPE = '11111111'          # adds 35
DC_LUMA = {'00': 1, '01': 2, '100': 0, '101': 3, '110': 4, '1110': 5,
           '11110': 6, '111110': 7, '111111': 8}
DC_CHROMA = {'00': 0, '01': 1, '10': 2, '110': 3, '1110': 4, '11110': 5,
             '111110': 6, '1111110': 7, '1111111': 8}
COEF = {'100': (1, 1), '1010': (0, 2), '1011': (2, 1), '11000': (0, 3),
        '1111111100000': (0, 12), '111111100000': (0, 8)}
COEF_EOB = '00'
COEF_FIRST_ONE = '01'             # (0, 1) when not the first coefficient of a non-intra block
COEF_ESCAPE = '110110'
CBP_INVALID = '11111111'

def inverse(t):
    return {v: k for k, v in t.items()}

TYPE_P_I, TYPE_B_I, CBP_I, MOTION_I = map(inverse, (TYPE_P, TYPE_B, CBP, MOTION))
SKIP_I, DC_LUMA_I, DC_CHROMA_I, COEF_I = map(inverse, (SKIP, DC_LUMA, DC_CHROMA, COEF))


class Bits:
    def __init__(self):
        self.s = []

    def put(self, code):
        assert set(code) <= {'0', '1'}
        self.s.append(code)

    def num(self, v, n):
        if n:
            assert 0 <= v < (1 << n)
            self.s.append(format(v, f'0{n}b'))

    def tobytes(self):
        b = ''.join(self.s)
        b += '0' * (-len(b) % 8)
        return bytes(int(b[i:i + 8], 2) for i in range(0, len(b), 8))


def put_dc(w, luma, diff):
    n = abs(diff).bit_length()
    assert n <= 8
    w.put((DC_LUMA_I if luma else DC_CHROMA_I)[n])
    if n:
        w.num(diff if diff > 0 else diff + (1 << n) - 1, n)


def put_coef(w, run, level, first):
    if first and run == 0 and abs(level) == 1:
        w.put('0' + ('1' if level < 0 else '0'))
    elif not first and run == 0 and abs(level) == 1:
        w.put(COEF_FIRST_ONE + ('1' if level < 0 else '0'))
    elif (run, abs(level)) in COEF_I:
        w.put(COEF_I[(run, abs(level))] + ('1' if level < 0 else '0'))
    else:                         # escape: 6 bits run, 8 or 16 bits level
        w.put(COEF_ESCAPE)
        w.num(run, 6)
        if 0 < level < 128:
            w.num(level, 8)
        elif -128 < level < 0:
            w.num(level + 256, 8)
        elif level > 0:
            w.num(0, 8); w.num(level, 8)
        else:
            w.num(0x80, 8); w.num(level + 256, 8)


def put_block(w, coefs, first_inter):
    """coefs: [(run, level)] in bitstream order, then end of block."""
    for i, (run, level) in enumerate(coefs):
        put_coef(w, run, level, first_inter and i == 0)
    w.put(COEF_EOB)


def put_mv(w, delta, r):
    if delta == 0:
        w.put(MOTION_I[0]); return
    d = abs(delta)
    code = ((d - 1) >> r) + 1
    assert code <= 16
    w.put(MOTION_I[code])
    w.num(1 if delta < 0 else 0, 1)
    w.num((d - 1) & ((1 << r) - 1), r)


def put_skip(w, n):
    while n >= 35:
        w.put(SKIP_ESCAPE); n -= 35
    w.put(SKIP_I[n])


def intra_blocks(w, dcp, values, ac):
    """values: 4 Y, Cb, Cr targets (DC-only block value = DC predictor). ac: {block: coefs}."""
    for b in range(6):
        c = 0 if b < 4 else b - 3
        put_dc(w, b < 4, values[b] - dcp[c])
        dcp[c] = values[b]
        put_block(w, ac.get(b, []), False)


def i_picture(ts, mbs):
    """mbs: per macroblock {'values': [...6], 'qs': None|int, 'ac': {}}."""
    w = Bits(); dcp = [128, 128, 128]
    for m in mbs:
        if m.get('qs') is not None:
            w.put('1'); w.num(m['qs'], 5)
        else:
            w.put('0')
        intra_blocks(w, dcp, m['values'], m.get('ac', {}))
    return struct.pack('<I', ts) + w.tobytes()


def inter_picture(kind, ts, header, r, mbs, nmb):
    """kind 'P' or 'B'. r = (r_fwd, r_bwd). mbs: coded macroblocks in order:
    {'type', 'fwd': (x, y), 'bwd': (x, y), 'cbp', 'qs', 'skip', 'values' (intra),
     'blocks': {block: coefs} (non-intra)}. Vectors are the targets in half
    samples; the encoder writes them as differences from the predictors."""
    w = Bits(); dcp = [128, 128, 128]; pf = [0, 0]; pb = [0, 0]; mb = 0
    for m in mbs:
        t = m['type']
        w.put((TYPE_P_I if kind == 'P' else TYPE_B_I)[t])
        if t & 8:
            for k in range(2):
                put_mv(w, m['fwd'][k] - pf[k], r[0]); pf[k] = m['fwd'][k]
        elif kind == 'P':
            pf = [0, 0]; pb = [0, 0]
        if kind == 'B' and t & 4:
            for k in range(2):
                put_mv(w, m['bwd'][k] - pb[k], r[1]); pb[k] = m['bwd'][k]
        if kind == 'B' and not t & 0xc:
            pf = [0, 0]; pb = [0, 0]
        if t & 2:
            w.put(CBP_I[m['cbp']])
        if t & 16:
            w.num(m['qs'], 5)
        skip = m.get('skip', 0)
        put_skip(w, skip)
        if t & 1:
            intra_blocks(w, dcp, m['values'], {})
        else:
            dcp = [128, 128, 128]
            for b in range(6):
                if m.get('cbp', 0) & (32 >> b):
                    put_block(w, m['blocks'][b], True)
        if skip:
            dcp = [128, 128, 128]
            if kind == 'P' or mb + 1 + skip >= nmb:
                pf = [0, 0]; pb = [0, 0]
        mb += 1 + skip
    assert mb == nmb, (kind, ts, mb)
    return struct.pack('<I', ts) + bytes([header]) + w.tobytes()


def container(w, h, duration, chunks, version=5, qi=None, qn=None):
    """chunks: [(4cc, payload)]. The index lists every IPIC (time, chunk offset)."""
    qi = qi or [8] + [16] * 63
    qn = qn or [16] * 64
    ipics = [c for c in chunks if c[0] == b'IPIC']
    data = 0x98 + 8 * len(ipics)
    offsets = []; pos = data
    for cc, payload in chunks:
        offsets.append(pos); pos += 8 + len(payload)
    index = b''.join(struct.pack('<II', struct.unpack_from('<I', p)[0], off)
                     for (cc, p), off in zip(chunks, offsets) if cc == b'IPIC')
    head = (b'APEG' + struct.pack('<IIHH', data - 8, version, w, h) + bytes(qi) + bytes(qn)
            + struct.pack('<II', duration, len(ipics)))
    assert len(head) == 0x98
    body = b''.join(cc + struct.pack('<I', len(p)) + p for cc, p in chunks)
    return head + index + body


# ---- the probe's pictures (48x32: 3x2 macroblocks) ----
W, H, NMB = 48, 32, 6

def y_value(m, b):                # I0 luma block values
    return 40 + 20 * m + 9 * b

I0_MBS = [{'values': [y_value(m, b) for b in range(4)] + [100 + 7 * m, 150 - 9 * m],
           'qs': 3 if m == 2 else None} for m in range(NMB)]
# I4: DC only except macroblock 4 block Y1 (horizontal and vertical first
# harmonics and, by escape, horizontal second harmonic) at quantiser scale 2.
I4_AC = [(0, 12), (0, -8), (2, -30)]
I4_MBS = [{'values': [200 - 15 * m - 5 * b for b in range(4)] + [60 + 11 * m, 190 - 13 * m],
           'qs': (2 if m == 4 else 1) if m >= 4 else None,
           'ac': {1: I4_AC} if m == 4 else {}} for m in range(NMB)]

P1_MBS = [
    {'type': 1, 'values': [200, 200, 200, 200, 60, 70]},                         # intra
    {'type': 8, 'fwd': (-16, 0)},                                                # whole-sample vector
    {'type': 8, 'fwd': (-3, 1), 'skip': 1},                                      # half x and y; MB3 skipped (copy)
    {'type': 18, 'cbp': 33, 'qs': 16, 'blocks': {0: [(0, 1)], 5: [(0, -1)]}},    # no vector, DC residuals
    {'type': 26, 'fwd': (0, -2), 'cbp': 4, 'qs': 8, 'blocks': {3: [(0, -1)]}},   # vector + residual
]
B2_MBS = [
    {'type': 30, 'fwd': (1, 1), 'bwd': (2, 1), 'cbp': 2, 'qs': 16,               # both + residual,
     'blocks': {4: [(0, 1)]}, 'skip': 1},                                        # MB1 repeats it
    {'type': 8, 'fwd': (-2, 0)},                                                 # forward only
    {'type': 4, 'bwd': (0, -1)},                                                 # backward only
    {'type': 1, 'values': [99, 88, 77, 66, 33, 222]},                            # intra
    {'type': 12, 'fwd': (0, -2), 'bwd': (-1, 0)},                                # both, no residual
]
B3_MBS = [{'type': 12, 'fwd': (2, 0), 'bwd': (0, 2), 'skip': 5}]                 # skip to the end: vectors reset
# open-gop.apeg's second GOP starts with I600 (DC only).
I6_MBS = [{'values': [30 + 25 * m + 6 * b for b in range(4)] + [170 - 8 * m, 80 + 12 * m]} for m in range(NMB)]
P_HEADER = 0x17                   # full_pel 0, r_size 1, then 0111
B2_HEADER = 0x01                  # forward r_size 0, backward r_size 1
B3_HEADER = 0x00


def generate(parent, ogg_path):
    out = Path(tempfile.mkdtemp(prefix='apeg-fixtures-', dir=parent))
    ogg = Path(ogg_path).read_bytes()
    pics = [(b'IPIC', i_picture(0, I0_MBS)),
            (b'PPIC', inter_picture('P', 300, P_HEADER, (1, 0), P1_MBS, NMB)),
            (b'BPIC', inter_picture('B', 100, B2_HEADER, (0, 1), B2_MBS, NMB)),
            (b'BPIC', inter_picture('B', 200, B3_HEADER, (0, 0), B3_MBS, NMB)),
            (b'IPIC', i_picture(400, I4_MBS))]
    good = container(W, H, 900, [(b'TMNL', b'\x00' * 4), (b'SOND', ogg)] + pics)
    files = {'video.apeg': good}
    # The same pictures with a header length (250) shorter than the sound:
    # when the sound ends the clock goes back from about 1.5 s to 250.
    files['clockback.apeg'] = container(W, H, 250, [(b'TMNL', b'\x00' * 4), (b'SOND', ogg)] + pics)

    # Two GOPs, the second one open: decode order I0 P300 B100 B200 I600 B400
    # B500 P900 B700 B800. B400 and B500 follow I600 but are shown before it;
    # their past reference is P300 of the first GOP. B400/B700 use B2's
    # macroblocks, B500/B800 B3's, P900 P1's, each with its own references.
    gop = pics[:4] + [(b'IPIC', i_picture(600, I6_MBS)),
                      (b'BPIC', inter_picture('B', 400, B2_HEADER, (0, 1), B2_MBS, NMB)),
                      (b'BPIC', inter_picture('B', 500, B3_HEADER, (0, 0), B3_MBS, NMB)),
                      (b'PPIC', inter_picture('P', 900, P_HEADER, (1, 0), P1_MBS, NMB)),
                      (b'BPIC', inter_picture('B', 700, B2_HEADER, (0, 1), B2_MBS, NMB)),
                      (b'BPIC', inter_picture('B', 800, B3_HEADER, (0, 0), B3_MBS, NMB))]
    files['open-gop.apeg'] = container(W, H, 1000, gop)

    # Odd size (40x24 shown of 48x32) with samples outside the clamping ranges.
    odd = [{'values': [-20, 100, 250, 100, 5 if m % 2 else 250, 250 if m % 2 else 5]} for m in range(NMB)]
    files['odd.apeg'] = container(40, 24, 100, [(b'IPIC', i_picture(0, odd))])

    # Containers the decoder must reject.
    def patched(off, fmt, value, src=good):
        b = bytearray(src); struct.pack_into(fmt, b, off, value); return bytes(b)
    files['bad-magic.apeg'] = b'APEX' + good[4:]
    files['version-4.apeg'] = patched(8, '<I', 4)
    files['version-0.apeg'] = patched(8, '<I', 0)
    files['short-header.apeg'] = good[:0x60]
    files['data-offset.apeg'] = patched(4, '<I', len(good) + 100)
    files['chunk-truncated.apeg'] = good[:-5]
    files['zero-width.apeg'] = patched(12, '<H', 0)
    # Containers that open with a warning: a bad I picture index (it is not
    # needed to find the pictures) and stray bytes after the last chunk.
    files['index-count.apeg'] = patched(0x94, '<I', 1000)
    files['index-offset.apeg'] = patched(0x98 + 4, '<I', len(good) + 10)
    files['trailing-bytes.apeg'] = good + b'\x01\x02\x03\x04\x05'

    # Valid containers with pictures that cannot be decoded.
    p_only = inter_picture('P', 0, P_HEADER, (1, 0), P1_MBS, NMB)
    files['p-first.apeg'] = container(W, H, 100, [(b'PPIC', p_only)])
    i0 = i_picture(0, I0_MBS)
    files['i-truncated.apeg'] = container(W, H, 100, [(b'IPIC', i0[:len(i0) // 2])])
    w = Bits(); w.put(TYPE_P_I[2]); w.put(CBP_INVALID)
    files['bad-cbp.apeg'] = container(W, H, 100, [(b'IPIC', i0), (b'PPIC', struct.pack('<IB', 100, P_HEADER) + w.tobytes())])
    w = Bits(); w.put('0'); put_dc(w, True, 0); put_coef(w, 63, 5, False); w.put(COEF_EOB)
    files['coef-63.apeg'] = container(W, H, 100, [(b'IPIC', struct.pack('<I', 0) + w.tobytes())])
    w = Bits(); w.put(TYPE_P_I[8]); put_mv(w, 0, 1); put_mv(w, 0, 1); put_skip(w, 35)
    files['skip-past.apeg'] = container(W, H, 100, [(b'IPIC', i0), (b'PPIC', struct.pack('<IB', 100, P_HEADER) + w.tobytes())])
    # A broken I, a P depending on it, then a good I: decoding resumes at the I.
    files['broken-chain.apeg'] = container(W, H, 300, [(b'IPIC', i0[:len(i0) // 2]),
                                                     (b'PPIC', inter_picture('P', 100, P_HEADER, (1, 0), P1_MBS, NMB)),
                                                     (b'IPIC', i_picture(200, I4_MBS))])
    for name, data in files.items():
        (out / name).write_bytes(data)
    manifest = {'generator_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'scope': 'Synthetic APEG pictures (parameters in this file) and the probe\'s own Vorbis; no game data',
                'sound': {'source': Path(ogg_path).name, 'sha256': hashlib.sha256(ogg).hexdigest(),
                          'size': len(ogg)},
                'files': {n: {'sha256': hashlib.sha256(d).hexdigest(), 'size': len(d)}
                          for n, d in sorted(files.items())}}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return out


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('parent', type=Path)
    ap.add_argument('ogg', type=Path)
    a = ap.parse_args()
    print(generate(a.parent.resolve(), a.ogg))

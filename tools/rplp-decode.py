#!/usr/bin/env python3
"""Decode WhateverXe's -wex-rplp-observe properties and diff them against a Linux capture.

On macOS (booted with -wex-rplp-observe):
    ioreg -lw0 -r -k RPLPObserveVersion > rplp-ioreg.txt
Then, anywhere with Python 3:
    python3 tools/rplp-decode.py rplp-ioreg.txt [--linux rpl-capture-XXXX(.tar.gz)] [--json out.json]

Struct layouts mirror WhateverXe/kern_raptorlake_p.hpp and kern_alderlake_n.hpp (branch rplp-r1).
Fuses are hardware constants and must match Linux exactly. Display registers only match a Linux
capture taken with i915 unbound (GOP state); with i915 loaded they show i915's own modeset.
"""
import argparse, json, re, struct, sys, tarfile
from pathlib import Path

GT_STATUS = ['NotAdmitted', 'ExistingOwner', 'AckTimeout', 'Unavailable', 'ProfileMismatch', 'ReleaseFailed', 'Complete']
DISPLAY_STATUS = ['NotAdmitted', 'Complete', 'Unavailable', 'PowerChanged']
EDP_STATUS = ['NotAdmitted', 'Complete', 'Unavailable', 'PowerChanged']
FUSES = [(0x9138, 'slice enable'), (0x913C, 'DSS enable'), (0x9134, 'EU disable'), (0x9118, 'mirror fuse 3 (L3)'), (0x9140, 'media disable')]
NAMES = {
    0x45404: 'PWR_WELL_CTL2', 0x45400: 'PWR_WELL_CTL1 (BIOS)', 0x60400: 'TRANS_DDI_FUNC_CTL A', 0x64000: 'DDI_BUF_CTL A',
    0x60540: 'DP_TP_CTL A', 0x60544: 'DP_TP_STATUS A', 0x60030: 'DATA_M1 A', 0x60034: 'DATA_N1 A', 0x60040: 'LINK_M1 A',
    0x60044: 'LINK_N1 A', 0x60410: 'MSA_MISC A', 0x60800: 'PSR_CTL A', 0x60840: 'PSR_STATUS A', 0x60900: 'PSR2_CTL A',
    0x60940: 'PSR2_STATUS A', 0x68180: 'PS_1A_CTRL', 0x68280: 'PS_2A_CTRL', 0xC7200: 'PP_STATUS', 0xC7204: 'PP_CONTROL',
    0xC7208: 'PP_ON_DELAYS', 0xC720C: 'PP_OFF_DELAYS', 0xC8250: 'BLC_PWM_CTL1', 0xC8254: 'BLC_PWM_FREQ1',
    0xC8258: 'BLC_PWM_DUTY1', 0x45504: 'DC_STATE_EN', 0x64010: 'DP_AUX_CH_CTL A', 0x46010: 'DPLL0_ENABLE',
    0x46014: 'DPLL1_ENABLE', 0x164280: 'DPCLKA_CFGCR0', 0x164284: 'DPLL0_CFGCR0', 0x164288: 'DPLL0_CFGCR1',
    0x16428C: 'DPLL1_CFGCR0', 0x164290: 'DPLL1_CFGCR1', 0x70008: 'TRANSCONF A', 0x70180: 'PLANE_CTL 1A',
    0x70188: 'PLANE_STRIDE 1A', 0x70190: 'PLANE_SIZE 1A', 0x7019C: 'PLANE_SURF 1A', 0x60000: 'HTOTAL A', 0x6000C: 'VTOTAL A',
}

def words(blob, n=None):
    if len(blob) % 4: raise ValueError('blob length %d is not a multiple of 4' % len(blob))
    w = list(struct.unpack('<%dI' % (len(blob) // 4), blob))
    if n is not None and len(w) != n: raise ValueError('expected %d words, got %d' % (n, len(w)))
    return w

def qwords(blob):
    return list(struct.unpack('<%dQ' % (len(blob) // 8), blob))

def parse_ioreg(text):
    props = {}
    for m in re.finditer(r'"(RPLP\w+)"\s*=\s*(<[0-9a-fA-F]*>|"[^"]*"|0x[0-9a-fA-F]+|\d+|Yes|No)', text):
        key, value = m.group(1), m.group(2)
        if key in props: continue
        if value.startswith('<'): props[key] = bytes.fromhex(value[1:-1])
        elif value.startswith('"'): props[key] = value[1:-1]
        elif value in ('Yes', 'No'): props[key] = value == 'Yes'
        else: props[key] = int(value, 0)
    return props

def registers(w, start, count):
    return [(w[start + 2 * i], w[start + 2 * i + 1]) for i in range(count)]

def decode(props):
    out = {'identity': props.get('RPLPPhysicalIdentity'), 'revision': props.get('RPLPPhysicalRevision'),
           'gtStep': props.get('RPLPGTStep'), 'displayStep': props.get('RPLPDisplayStep'), 'registers': {}}
    if 'RPLPPhysicalMemoryLayout' in props:
        q = qwords(props['RPLPPhysicalMemoryLayout'])
        out['memory'] = dict(zip(['version', 'ggc', 'bdsmLow', 'bdsmHigh', 'apertureBytes', 'valid', 'base', 'bytes', 'gttBytes'], q))
    if 'RPLPStolenReservation' in props:
        q = qwords(props['RPLPStolenReservation'])
        out['reservation'] = dict(zip(['version', 'known', 'register', 'usable'], q))
    if 'RPLPGTReadout' in props:
        w = words(props['RPLPGTReadout'], 67)
        gt = {'status': GT_STATUS[w[1]] if w[1] < len(GT_STATUS) else w[1], 'writes': w[2], 'requestedMask': w[3],
              'releasedMask': w[4], 'releaseFailedMask': w[5], 'requestBefore': w[6:8], 'ackBefore': w[8:10],
              'ackHeld': w[10:12], 'requestAfter': w[12:14], 'ackAfter': w[14:16],
              'fuses': {hex(off): w[16 + i] for i, (off, _) in enumerate(FUSES)},
              'resources': dict(zip(['slices', 'dss', 'nativeSubslices', 'eus', 'l3BankPairs', 'vdbox', 'vebox'], w[60:67]))}
        for i, (off, _) in enumerate(FUSES): out['registers'][off] = w[16 + i]
        for off, val in registers(w, 28, w[27]): out['registers'][off] = val
        out['gt'] = gt
    if 'RPLPDisplay13Boot' in props:
        w = words(props['RPLPDisplay13Boot'], 262)
        out['display'] = {'status': DISPLAY_STATUS[w[1]] if w[1] < len(DISPLAY_STATUS) else w[1], 'count': w[2],
                          'poweredPipes': w[3], 'activePipes': w[4], 'hdmiBPipes': w[5]}
        for off, val in registers(w, 6, min(w[2], 128)): out['registers'].setdefault(off, val)
    if 'RPLPEDPBoot' in props:
        w = words(props['RPLPEDPBoot'], 133)
        out['edp'] = {'status': EDP_STATUS[w[1]] if w[1] < len(EDP_STATUS) else w[1], 'count': w[2],
                      'dpPipes': w[3], 'ddiAPipes': w[4]}
        for off, val in registers(w, 5, min(w[2], 64)): out['registers'].setdefault(off, val)
    return out

def linux_registers(path):
    path, regs, files = Path(path), {}, {}
    wanted = lambda name: name.endswith('.txt') or 'sseu_status' in name   # debugfs files have no extension
    if path.is_dir():
        for f in sorted(path.rglob('*')):
            name = f.relative_to(path).as_posix()
            if f.is_file() and wanted(name): files[name] = f.read_text(errors='replace')
    else:
        with tarfile.open(path) as tar:
            for m in tar.getmembers():
                if m.isfile() and wanted(m.name):
                    files[m.name] = tar.extractfile(m).read().decode(errors='replace')
    bound = next((t.strip() for n, t in files.items() if n.endswith('regs/i915-bound.txt')), 'unknown')
    sseu = {}
    for name, text in files.items():
        if '/regs/' in '/' + name:
            for m in re.finditer(r'\(0x([0-9a-fA-F]+)\):\s*0x([0-9a-fA-F]+)', text):
                regs.setdefault(int(m.group(1), 16), int(m.group(2), 16))
        if 'sseu_status' in name:
            for key in ('Available EU Total', 'Available Subslice Total', 'Available Slice Total'):
                m = re.search(re.escape(key) + r':\s*(\d+)', text)
                if m: sseu.setdefault(key, int(m.group(1)))
    return regs, bound, sseu

def report(d, linux=None):
    print('identity %s rev %s  GT %s / display %s' % (hex(d['identity'] or 0), d['revision'], d['gtStep'], d['displayStep']))
    if 'memory' in d:
        m = d['memory']
        print('GGC %#06x  BDSM %#010x:%#010x  aperture %d MiB  stolen %d MiB valid=%d' % (
            m['ggc'], m['bdsmHigh'], m['bdsmLow'], m['apertureBytes'] >> 20, m['bytes'] >> 20, m['valid']))
    if 'reservation' in d:
        r = d['reservation']
        print('stolen reservation register %#x known=%d usable %d MiB' % (r['register'], r['known'], r['usable'] >> 20))
    if 'gt' in d:
        g = d['gt']
        print('GT readout %s  writes=%d requested=%x released=%x failed=%x' % (
            g['status'], g['writes'], g['requestedMask'], g['releasedMask'], g['releaseFailedMask']))
        for off, name in FUSES: print('  fuse %-7s %-20s %#010x' % (hex(off), name, d['registers'].get(off, 0)))
        print('  resources', g['resources'])
    for key in ('display', 'edp'):
        if key in d: print(key, d[key])
    eDP = [o for o in sorted(d['registers']) if o in NAMES]
    for o in eDP: print('  %-8s %-22s %#010x' % (hex(o), NAMES[o], d['registers'][o]))
    if linux is None: return 0
    regs, bound, sseu = linux
    print('\nLinux capture: %d registers, i915 driver: %s' % (len(regs), bound))
    bad = 0
    for off, name in FUSES:
        if off not in regs: print('  fuse %s missing from Linux capture' % hex(off)); continue
        ok = regs[off] == d['registers'].get(off)
        bad += not ok
        print('  fuse %-7s macOS %#010x  Linux %#010x  %s' % (hex(off), d['registers'].get(off, 0), regs[off], 'MATCH' if ok else 'MISMATCH'))
    res = d.get('gt', {}).get('resources', {})
    if 'Available EU Total' in sseu:
        ok = sseu['Available EU Total'] == res.get('eus')
        bad += not ok
        print('  EU total macOS %s Linux %s %s' % (res.get('eus'), sseu['Available EU Total'], 'MATCH' if ok else 'MISMATCH'))
    if 'Available Subslice Total' in sseu:
        ok = sseu['Available Subslice Total'] == res.get('dss')
        bad += not ok
        print('  DSS total macOS %s Linux %s %s' % (res.get('dss'), sseu['Available Subslice Total'], 'MATCH' if ok else 'MISMATCH'))
    shared = sorted(o for o in d['registers'] if o in regs and o not in dict(FUSES))
    diff = [o for o in shared if regs[o] != d['registers'][o]]
    print('  display/other registers: %d compared, %d differ%s' % (len(shared), len(diff),
          '' if 'no driver' in bound else ' (expected: Linux shows i915 state, macOS shows GOP state)'))
    for o in diff[:40]: print('    %-8s %-22s macOS %#010x  Linux %#010x' % (hex(o), NAMES.get(o, ''), d['registers'][o], regs[o]))
    print('R1 fuse/SSEU verdict:', 'PASS' if not bad else 'FAIL (%d mismatches)' % bad)
    return 1 if bad else 0

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('ioreg', type=Path)
    p.add_argument('--linux', type=Path, help='capture directory or rpl-capture-*.tar.gz')
    p.add_argument('--json', type=Path, help='write decoded values as JSON')
    a = p.parse_args()
    props = parse_ioreg(a.ioreg.read_text(errors='replace'))
    if 'RPLPObserveVersion' not in props: sys.exit('no RPLP properties found; was the kernel booted with -wex-rplp-observe?')
    d = decode(props)
    if a.json:
        a.json.write_text(json.dumps({**d, 'registers': {hex(k): v for k, v in sorted(d['registers'].items())}}, indent=1))
    sys.exit(report(d, linux_registers(a.linux) if a.linux else None))

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Generates a nasty test clickpack + the same macro in every supported format, runs the
wafflebot-test harness on each, and checks the rendered audio against an independent
numpy re-implementation of the expected result."""
import json, math, os, struct, subprocess, sys, wave
import msgpack
import numpy as np

HARNESS = sys.argv[1]
OUT = sys.argv[2]
os.makedirs(OUT, exist_ok=True)
FPS = 240

# (frame, player2, down)
EVENTS = [(240,0,1),(250,0,0),(480,0,1),(484,0,0),(486,0,1),(490,0,0),
          (600,1,1),(610,1,0),(1200,0,1),(1210,0,0)]

# ------------------------------------------------------------------ clickpack
def burst(amp, freq, rate, ms=25):
    n = int(rate*ms/1000)
    t = np.arange(n)
    return (amp*np.cos(2*math.pi*freq*t/rate)*np.exp(-t/(0.005*rate)))

def write_wav(path, data, rate, channels):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pcm = np.clip(np.round(data*32767), -32768, 32767).astype('<i2')
    if channels == 2 and pcm.ndim == 1:
        pcm = np.stack([pcm, pcm], axis=1)
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(pcm.tobytes())

PACK = os.path.join(OUT, 'pack')
SOUNDS = {}   # name -> (float data (n,) mono or (n,2), rate)
def add(rel, fname, amp, freq, rate, ch):
    d = burst(amp, freq, rate)
    path = os.path.join(PACK, rel, fname)
    write_wav(path, d, rate, ch)
    SOUNDS[rel] = (d, rate, ch)

add('clicks','a.wav',0.5,1000,44100,2)
write_wav(os.path.join(PACK,'clicks','b.wav'), burst(0.5,1000,44100), 44100, 2)   # identical twin
add('releases','a.wav',0.25,700,44100,2)
write_wav(os.path.join(PACK,'releases','b.wav'), burst(0.25,700,44100), 44100, 2)
add('softclicks','a.wav',0.4,1500,48000,1)          # different rate + mono
add('hardclicks','a.wav',0.8,500,22050,1)           # different rate + mono
add('player2/clicks','a.wav',0.6,900,44100,1)       # mono
# junk that must not break anything
open(os.path.join(PACK,'clicks','desktop.ini'),'w').write('[.ShellClassInfo]\n')
open(os.path.join(PACK,'clicks','Thumbs.db'),'wb').write(b'\x00\x01\x02'*100)
open(os.path.join(PACK,'releases','readme.txt'),'w').write('hello')

# ------------------------------------------------------------------ expected render
def load_sound(rel, rate_out):
    d, rate, ch = SOUNDS[rel]
    q = np.round(d*32767)/32768.0      # what the 16 bit file really contains
    if rate != rate_out:
        n = len(q); m = int(math.ceil(n/(rate/rate_out)))
        q = np.interp(np.arange(m)*(rate/rate_out), np.arange(n), q)
    return q

def expected(events, rate=44100):
    # type of each event according to the default thresholds used by the harness
    kinds = [('clicks','releases')]*0
    sel = {   # frame -> (pool name)
        (240,0,1):'clicks',(250,0,0):'releases',(480,0,1):'clicks',(484,0,0):'releases',
        (486,0,1):'softclicks',(490,0,0):'releases',(600,1,1):'player2/clicks',(610,1,0):'releases',
        (1200,0,1):'hardclicks',(1210,0,0):'releases'}
    placed = []
    end = 0
    for e in events:
        s = load_sound(sel[e], rate)
        start = int(math.floor(e[0]/FPS*rate + 0.5))
        placed.append((start, s)); end = max(end, start+len(s))
    total = end + rate
    buf = np.zeros(total)
    for start, s in placed:
        buf[start:start+len(s)] += s
    return buf

EXP = expected(EVENTS)

# ------------------------------------------------------------------ macro writers
M = os.path.join(OUT, 'macros'); os.makedirs(M, exist_ok=True)
def gdr_dict(bot, invert=False, with_fps=True):
    d = {"bot":{"name":bot,"version":"1"},"author":"t","description":"","duration":5.0,
         "gameVersion":2.2,"version":1.0,"seed":0,"coins":0,"ldm":False,
         "level":{"id":1,"name":"x"},
         "inputs":[{"frame":f,"btn":1,"2p":bool(p2) ^ invert,"down":bool(dn)} for f,p2,dn in EVENTS]}
    if with_fps: d["framerate"]=240.0
    return d

def varint(v):
    out = bytearray()
    while True:
        b = v & 0x7f; v >>= 7
        if v: out.append(b|0x80)
        else: out.append(b); break
    return bytes(out)
def vstr(s): b=s.encode(); return varint(len(b))+b

def gdr2_bytes():
    w = bytearray(b'GDR'); w += varint(2); w += vstr('')
    w += vstr('auth')+vstr('desc')+struct.pack('>f',5.0)+varint(2200)+struct.pack('>d',240.0)
    w += varint(0)+varint(0)+bytes([0])+bytes([0])+vstr('bot')+varint(1)+varint(1)+vstr('lvl')
    w += varint(0)            # extension section
    w += varint(0)            # deaths
    p1 = [e for e in EVENTS if not e[1]]; p2 = [e for e in EVENTS if e[1]]
    w += varint(len(EVENTS)) + varint(len(p1))
    for group in (p1, p2):
        prev = 0
        for f,_,dn in group:
            w += varint(((f-prev)<<1)|dn); prev = f
    return bytes(w)

def tasbot_dict():
    ev = []
    for f,p2,dn in EVENTS:
        c = 1 if dn else 2
        ev.append({"frame":f,"player_1":{"click":0 if p2 else c,"x_position":1.0},
                   "player_2":{"click":c if p2 else 0}})
    return {"fps":240.0,"macro":ev}

def mhr_json():
    ev = [{"frame":3,"type":"something-not-a-click"}]
    for f,p2,dn in EVENTS:
        e = {"frame":f,"down":bool(dn),"x":1.0}
        if p2: e["p2"] = True
        ev.append(e)
    return {"meta":{"fps":240.0},"events":ev}

def mhr_bin():
    b = bytearray(b'HACK') + bytes(8) + struct.pack('<I',240) + bytes(12) + struct.pack('<I',len(EVENTS))
    assert len(b) == 32
    for f,p2,dn in EVENTS:
        b += bytes(2) + bytes([1 if dn else 0, 1 if p2 else 0]) + struct.pack('<I',f) + bytes(24)
    return bytes(b)

def xd_text():
    return "240\n" + "".join(f"{f}|{dn}|1|{0 if p2 else 1}|0|0|0\n" for f,p2,dn in EVENTS)

def zbf():
    b = struct.pack('<ff',1/240,1.0)
    for f,p2,dn in EVENTS:
        b += struct.pack('<i',f)+bytes([0x31 if dn else 0x30, 0x30 if p2 else 0x31])
    return b

def txt():
    return "240\n" + "".join(f"{f} {dn} 1 {0 if p2 else 1}\n" for f,p2,dn in EVENTS)

def slc():
    b = struct.pack('<dI',240.0,len(EVENTS))
    for f,p2,dn in EVENTS:
        b += struct.pack('<I',(f<<4)|(p2<<3)|(1<<1)|dn)
    return b + struct.pack('<Q',0)

CASES = {
 'gdr_json_other.json':        lambda: json.dumps(gdr_dict('Eclipse')).encode(),
 'gdr_json_xdbot.gdr.json':    lambda: json.dumps(gdr_dict('xdBot', invert=True)).encode(),
 'mh_replay.json':             lambda: json.dumps(gdr_dict('MH_REPLAY', with_fps=False)).encode(),
 'gdr_msgpack.gdr':            lambda: msgpack.packb(gdr_dict('GDH')),
 'gdr2.gdr2':                  gdr2_bytes,
 'tasbot.json':                lambda: json.dumps(tasbot_dict()).encode(),
 'mhr.mhr.json':               lambda: json.dumps(mhr_json()).encode(),
 'mhr_binary.mhr':             mhr_bin,
 'xdbot.xd':                   lambda: xd_text().encode(),
 'zbot.zbf':                   zbf,
 'plaintext.txt':              lambda: txt().encode(),
 'silicate.slc':               slc,
}

fails = 0
for name, make in CASES.items():
    path = os.path.join(M, name); open(path,'wb').write(make())
    outwav = os.path.join(OUT, name + '.wav')
    if os.path.exists(outwav): os.remove(outwav)
    r = subprocess.run([HARNESS, PACK, path, outwav], capture_output=True, text=True)
    status = 'OK '
    detail = ''
    if r.returncode != 0 or not os.path.exists(outwav):
        status, detail = 'FAIL', r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr
    else:
        with wave.open(outwav) as w:
            n, ch, rate, sw = w.getnframes(), w.getnchannels(), w.getframerate(), w.getsampwidth()
            raw = np.frombuffer(w.readframes(n), dtype='<i2').reshape(-1, ch)
        got = raw.astype(np.float64)/32768.0
        if (ch, rate, sw) != (2, 44100, 2):
            status, detail = 'FAIL', f'wrong wav format ch={ch} rate={rate} sw={sw}'
        elif n != len(EXP):
            status, detail = 'FAIL', f'length {n} != expected {len(EXP)}'
        else:
            err = max(np.abs(got[:,0]-EXP).max(), np.abs(got[:,1]-EXP).max())
            peak = np.abs(got).max()
            if err > 3/32768 or peak < 0.5:
                status, detail = 'FAIL', f'max error {err:.5f}, peak {peak:.3f}'
            else:
                detail = f'{os.path.getsize(outwav)//1024} KB, max err {err*32768:.1f} LSB'
    if status.strip() == 'FAIL': fails += 1
    print(f'{status} {name:28s} {detail}')
print('\nFAILED:', fails)
sys.exit(1 if fails else 0)

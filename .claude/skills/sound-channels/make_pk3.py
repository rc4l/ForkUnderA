# Builds the sound-channel test pk3: one tone lump per sound (sounds sharing a lump alias to one name and one $limit), and one emitter actor per channel rule.
# usage: py -I make_pk3.py OUT.pk3 [--numeric]   (--numeric writes the flags as numbers, for a binary that predates their DECORATE names)
import io, math, sys, wave, zipfile

SR = 11025
# The DECORATE flag names the emitters use, at uzdoom's values.
FLAGS = {'CHAN_OVERLAP': 8192, 'CHAN_NOSTOP': 4096, 'CHAN_LOOP': 256, 'CHAN_PICKUP': 19,
         'CHANF_OVERLAP': 8192, 'CHANF_NOSTOP': 4096, 'CHANF_LOOP': 256, 'CHANF_UI': 32, 'CHANF_LOOPING': 4352}


def tone(freq):
    b = io.BytesIO()
    with wave.open(b, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(SR)
        w.writeframes(bytes(128 + int(60 * math.sin(2 * math.pi * freq * i / SR)) for i in range(SR)))
    return b.getvalue()


def play(s, chan, loop=True):
    return f'A_PlaySound("fua/{s}", {chan}, 1, {"true" if loop else "false"})'


def start(s, flags):
    return f'A_StartSound("fua/{s}", CHAN_BODY, {flags})'


# Each emitter's calls, all in one tic after it spawns.
CASES = {
    'FuaOvAuto': [play(f'ova{i}', 'CHAN_AUTO') for i in range(1, 11)],
    'FuaOvReplace': [play('ovr1', 'CHAN_BODY'), play('ovr2', 'CHAN_BODY')],
    'FuaOvOverlap': [play('ovo1', 'CHAN_BODY|CHAN_OVERLAP'), play('ovo2', 'CHAN_BODY|CHAN_OVERLAP')],
    'FuaOvReplaceAll': [play('ovp1', 'CHAN_BODY|CHAN_OVERLAP'), play('ovp2', 'CHAN_BODY|CHAN_OVERLAP'), play('ovp3', 'CHAN_BODY')],
    'FuaOvNoRestack': [start('ovn1', 'CHANF_OVERLAP|CHANF_LOOP'), start('ovn1', 'CHANF_OVERLAP|CHANF_LOOP')],
    'FuaOvOneShot': [play('ovq1', 'CHAN_BODY'), play('ovq2', 'CHAN_BODY|CHAN_OVERLAP', loop=False)],
    'FuaOvCut': [play('ovc1', 'CHAN_BODY'), play('ovc2', 'CHAN_BODY', loop=False)],
    'FuaOvNoStop': [play('ovx1', 'CHAN_BODY'), play('ovx1', 'CHAN_BODY|CHAN_NOSTOP', loop=False)],
    'FuaOvNoStopOther': [start('ovy1', 'CHANF_LOOP'), start('ovy2', 'CHANF_NOSTOP|CHANF_OVERLAP|CHANF_LOOP')],
    'FuaOvLoopFlag': [play('ovl1', 'CHAN_BODY|CHAN_LOOP', loop=False)],
    'FuaOvPickup': [play('ovk1', 'CHAN_PICKUP')],
    'FuaOvLooping': [start('ovm1', 'CHANF_LOOPING'), start('ovm1', 'CHANF_LOOPING')],
    'FuaOvUI': [start('ovu1', 'CHANF_UI|CHANF_LOOP')],
    # Checked on its own (check.mjs --only FuaOvStart): it stops its slot 3 seconds after spawning.
    'FuaOvStart': [start('ovs1', 'CHANF_OVERLAP|CHANF_LOOP'), start('ovs2', 'CHANF_OVERLAP|CHANF_LOOP')],
}


def main():
    out, numeric = sys.argv[1], '--numeric' in sys.argv[2:]
    sounds = sorted({c.split('"fua/')[1].split('"')[0] for calls in CASES.values() for c in calls})
    lump = {s: 'FUAOV%03d' % i for i, s in enumerate(sounds)}
    decorate = ''
    for name, calls in CASES.items():
        body = '\n'.join(f'\t\tTNT1 A 0 {c}' for c in calls)
        tail = '\t\tTNT1 A 105\n\t\tTNT1 A -1 A_StopSound(CHAN_BODY)\n' if name == 'FuaOvStart' else '\t\tTNT1 A -1\n'
        decorate += (f'actor {name}\n{{\n\t+NOINTERACTION\n\t+NOBLOCKMAP\n\t+NOGRAVITY\n\tRenderStyle None\n\tStates\n\t{{\n'
                     f'\tSpawn:\n\t\tTNT1 A 1\n{body}\n{tail}\t\tStop\n\t}}\n}}\n')
    if numeric:
        for flag in sorted(FLAGS, key=len, reverse=True):
            decorate = decorate.replace(flag, str(FLAGS[flag]))
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for i, s in enumerate(sounds):
            z.writestr(f'sounds/{lump[s]}.wav', tone(300 + 23 * i))
        z.writestr('SNDINFO', ''.join(f'fua/{s} {lump[s]}\n' for s in sounds))
        z.writestr('DECORATE', decorate)
    print(f'{out}: {len(sounds)} sounds, {len(CASES)} emitters{" (numeric flags)" if numeric else ""}')


main()

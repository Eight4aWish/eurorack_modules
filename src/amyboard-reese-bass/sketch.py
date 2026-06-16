# AMYboard Sketch
# DESCRIPTION: Reese bass with OLED menu: wave, detune, octave per osc + filter/amp envelopes. Filter uses dict-form filter_freq with eg1 key, bp1 drives EG1 for cutoff sweep. Rotary encoder navigates/edits parameters, button selects/confirms. All oscs chained to MIDI note (ch1) or 1V/oct CV1 + gate CV2.

import amy, amyboard, midi, math, sequencer

sequencer.tempo(120)

SEESAW = 0x36
BTN_PIN = 24
SYNTH = 1
NUM_VOICES = 1

WAVE_NAMES = ['SINE', 'PULSE', 'SAW_DN', 'SAW_UP', 'TRI', 'NOISE']
WAVE_VALS  = [amy.SINE, amy.PULSE, amy.SAW_DOWN, amy.SAW_UP, amy.TRIANGLE, amy.NOISE]

NUM_OSCS = 5
osc_wave   = [2, 2, 3, 2, 3]
osc_detune = [0, 7, -7, 14, -14]
osc_octave = [0, 0, 0, 1, -1]

amp_env = {'a': 10, 'd': 200, 's': 0.7, 'r': 400}

flt_cutoff = 800
flt_res    = 2.5
flt_env    = {'a': 20, 'd': 300, 's': 0.2, 'r': 500}
flt_depth  = 3000

PAGE_NAMES   = ['OSC 1', 'OSC 2', 'OSC 3', 'OSC 4', 'OSC 5', 'AMP ENV', 'FILTER']
NUM_PAGES    = len(PAGE_NAMES)
OSC_ITEMS    = ['WAVE', 'DETUNE', 'OCTAVE']
AMPENV_ITEMS = ['ATTACK', 'DECAY', 'SUSTAIN', 'RELEASE']
FILT_ITEMS   = ['CUTOFF', 'RESON', 'ATK', 'DECAY', 'SUS', 'REL', 'DEPTH']


def page_items(page):
    if page < NUM_OSCS:
        return OSC_ITEMS
    elif page == NUM_OSCS:
        return AMPENV_ITEMS
    else:
        return FILT_ITEMS


current_page = 0
current_item = 0
editing      = False

try:
    amyboard.init_buttons(pins=(BTN_PIN,), seesaw_dev=SEESAW)
    enc_offset = amyboard.read_encoder(seesaw_dev=SEESAW)
    HAS_HW = True
except Exception:
    enc_offset = 0
    HAS_HW = False

nav_enc_base  = enc_offset
edit_enc_base = enc_offset
prev_btn      = False

try:
    amyboard.init_display()
    disp = amyboard.display
    HAS_DISP = True
except Exception:
    HAS_DISP = False

cv_gate_active  = False
cv_current_note = 69
CV_GATE_THRESHOLD = 1.0
CV1_BASE_NOTE = 60


def cv_volts_to_midi(volts):
    n = int(round(CV1_BASE_NOTE + volts * 12.0))
    return max(0, min(127, n))


def fmt_val(page, item_idx):
    if page < NUM_OSCS:
        oi = page
        if item_idx == 0:
            return WAVE_NAMES[osc_wave[oi]]
        elif item_idx == 1:
            c = osc_detune[oi]
            sign = '+' if c >= 0 else ''
            return sign + str(c) + 'c'
        else:
            o = osc_octave[oi]
            sign = '+' if o >= 0 else ''
            return sign + str(o) + 'o'
    elif page == NUM_OSCS:
        keys = ['a', 'd', 's', 'r']
        k = keys[item_idx]
        v = amp_env[k]
        if k == 's':
            return '%.2f' % v
        else:
            return str(v) + 'ms'
    else:
        keys = ['cutoff', 'res', 'a', 'd', 's', 'r', 'depth']
        k = keys[item_idx]
        if k == 'cutoff':
            return str(flt_cutoff) + 'Hz'
        elif k == 'res':
            return '%.1f' % flt_res
        elif k == 's':
            return '%.2f' % flt_env['s']
        elif k == 'depth':
            return str(flt_depth) + 'Hz'
        elif k in ('a', 'd', 'r'):
            return str(flt_env[k]) + 'ms'
        else:
            return '?'


def draw():
    if not HAS_DISP:
        return
    disp.fill(0)
    items = page_items(current_page)
    hdr = PAGE_NAMES[current_page]
    if editing:
        hdr = hdr + '[E]'
    if len(hdr) > 16:
        hdr = hdr[:16]
    disp.text(hdr, 0, 0, 255)
    start = max(0, current_item - 1)
    end   = min(start + 3, len(items))
    for row, idx in enumerate(range(start, end)):
        y      = 16 + row * 16
        prefix = '>' if idx == current_item else ' '
        label  = items[idx]
        val    = fmt_val(current_page, idx)
        line   = prefix + label + ':' + val
        if len(line) > 16:
            line = line[:16]
        disp.text(line, 0, y, 255)
    amyboard.display_refresh()


def osc_freq_str(osc_idx):
    oct_ratio = math.pow(2.0, float(osc_octave[osc_idx]))
    det_ratio = math.pow(2.0, osc_detune[osc_idx] / 1200.0)
    base_hz   = 440.0 * oct_ratio * det_ratio
    return '%g,1' % base_hz


def build_amp_bp():
    a  = amp_env['a']
    dv = amp_env['d']
    s  = amp_env['s']
    r  = amp_env['r']
    return '%d,1,%d,%g,%d,0' % (a, dv, s, r)


def build_flt_bp():
    a  = flt_env['a']
    dv = flt_env['d']
    s  = flt_env['s']
    r  = flt_env['r']
    return '%d,1,%d,%g,%d,0' % (a, dv, s, r)


OSC_PAN = [0.1, 0.3, 0.5, 0.7, 0.9]


def apply_synth():
    amy.send(synth=SYNTH, num_voices=0)
    amy.send(synth=SYNTH, num_voices=NUM_VOICES, oscs_per_voice=NUM_OSCS)
    amy.send(synth=SYNTH, grab_midi_notes=0)

    amp_bp = build_amp_bp()
    flt_bp = build_flt_bp()
    ff = {'const': flt_cutoff, 'eg1': flt_depth}

    amy.send(synth=SYNTH, osc=0,
             wave=WAVE_VALS[osc_wave[0]],
             freq=osc_freq_str(0),
             pan=OSC_PAN[0],
             filter_type=amy.FILTER_LPF24,
             filter_freq=ff,
             resonance=flt_res,
             bp0=amp_bp,
             bp1=flt_bp,
             chained_osc=1)

    amy.send(synth=SYNTH, osc=1,
             wave=WAVE_VALS[osc_wave[1]],
             freq=osc_freq_str(1),
             pan=OSC_PAN[1],
             bp0=amp_bp,
             chained_osc=2)

    amy.send(synth=SYNTH, osc=2,
             wave=WAVE_VALS[osc_wave[2]],
             freq=osc_freq_str(2),
             pan=OSC_PAN[2],
             bp0=amp_bp,
             chained_osc=3)

    amy.send(synth=SYNTH, osc=3,
             wave=WAVE_VALS[osc_wave[3]],
             freq=osc_freq_str(3),
             pan=OSC_PAN[3],
             bp0=amp_bp,
             chained_osc=4)

    amy.send(synth=SYNTH, osc=4,
             wave=WAVE_VALS[osc_wave[4]],
             freq=osc_freq_str(4),
             pan=OSC_PAN[4],
             bp0=amp_bp)

    draw()


def change_value(page, item_idx, delta):
    global flt_cutoff, flt_res, flt_depth
    if page < NUM_OSCS:
        oi = page
        if item_idx == 0:
            osc_wave[oi] = (osc_wave[oi] + delta) % len(WAVE_NAMES)
        elif item_idx == 1:
            osc_detune[oi] = max(-1200, min(1200, osc_detune[oi] + delta * 5))
        else:
            osc_octave[oi] = max(-2, min(2, osc_octave[oi] + delta))
    elif page == NUM_OSCS:
        keys = ['a', 'd', 's', 'r']
        k = keys[item_idx]
        if k == 's':
            amp_env[k] = max(0.0, min(1.0, amp_env[k] + delta * 0.05))
        else:
            amp_env[k] = max(1, min(8000, amp_env[k] + delta * 20))
    else:
        keys = ['cutoff', 'res', 'a', 'd', 's', 'r', 'depth']
        k = keys[item_idx]
        if k == 'cutoff':
            flt_cutoff = max(20, min(18000, flt_cutoff + delta * 50))
        elif k == 'res':
            flt_res = max(0.0, min(8.0, flt_res + delta * 0.1))
        elif k == 's':
            flt_env['s'] = max(0.0, min(1.0, flt_env['s'] + delta * 0.05))
        elif k == 'depth':
            flt_depth = max(0, min(18000, flt_depth + delta * 100))
        elif k in ('a', 'd', 'r'):
            flt_env[k] = max(1, min(8000, flt_env[k] + delta * 20))
    apply_synth()
    if cv_gate_active:
        amy.send(synth=SYNTH, note=cv_current_note, vel=0.8)


def midi_cb(m):
    if not m or len(m) < 3:
        return
    status = m[0] & 0xF0
    ch     = m[0] & 0x0F
    note   = m[1]
    vel    = m[2]
    if ch != 0:
        return
    if status == 0x90 and vel > 0:
        amy.send(synth=SYNTH, note=note, vel=vel / 127.0)
    elif status == 0x80 or (status == 0x90 and vel == 0):
        amy.send(synth=SYNTH, note=note, vel=0)


midi.add_callback(midi_cb)
apply_synth()


def loop():
    global current_page, current_item, editing
    global nav_enc_base, edit_enc_base, prev_btn
    global cv_gate_active, cv_current_note

    # CV 1V/Oct pitch on CV1, gate on CV2
    try:
        cv1 = amyboard.cv_in(0)
        cv2 = amyboard.cv_in(1)
        gate_high = cv2 >= CV_GATE_THRESHOLD
        new_note  = cv_volts_to_midi(cv1)
        if gate_high and not cv_gate_active:
            cv_gate_active  = True
            cv_current_note = new_note
            amy.send(synth=SYNTH, note=cv_current_note, vel=0.8)
        elif gate_high and new_note != cv_current_note:
            amy.send(synth=SYNTH, note=cv_current_note, vel=0)
            cv_current_note = new_note
            amy.send(synth=SYNTH, note=cv_current_note, vel=0.8)
        elif not gate_high and cv_gate_active:
            cv_gate_active = False
            amy.send(synth=SYNTH, note=cv_current_note, vel=0)
    except Exception:
        pass

    if not HAS_HW:
        return

    # Read encoder + button
    try:
        raw_enc = amyboard.read_encoder(seesaw_dev=SEESAW)
        btn     = amyboard.read_buttons(pins=(BTN_PIN,), seesaw_dev=SEESAW)[0]
    except Exception:
        return

    btn_pressed = prev_btn and not btn
    prev_btn = btn

    items = page_items(current_page)

    if editing:
        delta = raw_enc - edit_enc_base
        if delta != 0:
            edit_enc_base = raw_enc
            change_value(current_page, current_item, delta)
            draw()
        if btn_pressed:
            editing = False
            nav_enc_base = raw_enc
            draw()
    else:
        delta = raw_enc - nav_enc_base
        if delta != 0:
            nav_enc_base = raw_enc
            new_item = current_item + delta
            if new_item < 0:
                current_page = (current_page - 1) % NUM_PAGES
                current_item = len(page_items(current_page)) - 1
            elif new_item >= len(items):
                current_page = (current_page + 1) % NUM_PAGES
                current_item = 0
            else:
                current_item = new_item
            draw()
        if btn_pressed:
            editing = True
            edit_enc_base = raw_enc
            draw()
#!/usr/bin/env python3
"""
netlist_to_layout.py — Parse an MKI x ES EDU netlist markdown document
and generate a skeleton layout JSON for the n8synth breadboard visualizer.

Usage:
    python3 netlist_to_layout.py <netlist.md> [-o output.json]

The output JSON contains all components, IC pin assignments, net connections,
and stage groupings extracted from the netlist. Row/column placement fields
are set to null — these must be filled in manually or in a future session.

The script also generates a .js wrapper alongside the .json for use with
the visualizer's file:// loading.
"""

import re
import json
import sys
import os
from collections import OrderedDict


# ============================================================
# BOM PARSING
# ============================================================

def parse_resistor_bom(lines):
    """Parse the resistors BOM table."""
    resistors = {}
    for line in lines:
        m = re.match(r'\|\s*(\d+)\s*\|\s*\**(\S+?)\**\s*\|\s*\**(.+?)\**\s*\|\s*\**(.+?)\**\s*\|', line)
        if m:
            qty, value, designators, role = m.groups()
            value = value.strip('*')
            for des in re.split(r'[,\s]+', designators):
                des = des.strip().strip('*')
                if des and des[0] == 'R':
                    resistors[des] = {'type': 'R', 'value': value, 'role': role.strip().strip('*')}
    return resistors


def parse_capacitor_bom(lines):
    """Parse the capacitors BOM table."""
    caps = {}
    for line in lines:
        m = re.match(r'\|\s*(\d+)\s*\|\s*\**(\S+?)\**\s*\|\s*\**(\S+?)\**\s*\|\s*\**(.+?)\**\s*\|\s*\**(.+?)\**\s*\|', line)
        if m:
            qty, value, cap_type, designators, role = m.groups()
            value = value.strip('*')
            for des in re.split(r'[,\s]+', designators):
                des = des.strip().strip('*')
                if des and des[0] == 'C':
                    caps[des] = {'type': 'C', 'value': value, 'subtype': cap_type.strip('*'), 'role': role.strip().strip('*')}
    return caps


def parse_semiconductor_bom(lines):
    """Parse semiconductors BOM table."""
    semis = {}
    for line in lines:
        m = re.match(r'\|\s*(\d+)\s*\|\s*(.+?)\s*\|\s*(.+?)\s*\|\s*(.+?)\s*\|', line)
        if m:
            qty, stype, designators, role = m.groups()
            stype = stype.strip()
            for des in re.split(r'[,\s]+', designators):
                des = des.strip()
                if not des or des == '|' or des.startswith('---'):
                    continue
                # Expand ranges like VD3-VD9
                range_m = re.match(r'(\w+?)(\d+)-\1(\d+)', des)
                if range_m:
                    prefix, start, end = range_m.groups()
                    for i in range(int(start), int(end) + 1):
                        d = f'{prefix}{i}'
                        semis[d] = classify_semi(d, stype, role.strip())
                else:
                    semis[des] = classify_semi(des, stype, role.strip())
    return semis


def classify_semi(des, stype, role):
    """Classify a semiconductor by its designator and type string."""
    # ICs first — DA1, DA2, U1 etc. (must come before diode check since DA starts with D)
    if des.startswith('DA') or des.startswith('U'):
        return {'type': 'IC', 'value': stype, 'role': role}
    elif des.startswith('VD') or des.startswith('D'):
        return {'type': 'D', 'value': stype.split()[0] if stype else '1N4148', 'role': role}
    elif des.startswith('VT') or des.startswith('Q'):
        subtype = 'PNP' if 'PNP' in stype else 'NPN'
        value = re.match(r'(\w+)', stype).group(1) if stype else '?'
        return {'type': 'Q', 'subtype': subtype, 'value': value, 'role': role}
    return {'type': '?', 'value': stype, 'role': role}


def parse_potentiometer_bom(lines):
    """Parse potentiometers BOM table."""
    pots = {}
    for line in lines:
        m = re.match(r'\|\s*(\d+)\s*\|\s*(\S+)\s*\|\s*(.+?)\s*\|\s*(.+?)\s*\|\s*(.+?)\s*\|\s*(.+?)\s*\|', line)
        if m:
            qty, value, taper, designators, label, wiring = m.groups()
            for des in re.split(r'[,\s]+', designators):
                des = des.strip()
                if des and des[0] == 'P':
                    pots[des] = {'type': 'P', 'value': value, 'taper': taper.strip(),
                                 'label': label.strip(), 'wiring': wiring.strip()}
    return pots


def parse_connector_bom(lines):
    """Parse connectors BOM table."""
    conns = {}
    for line in lines:
        m = re.match(r'\|\s*(\d+)\s*\|\s*(.+?)\s*\|\s*(\S+)\s*\|\s*(.+?)\s*\|', line)
        if m:
            qty, ctype, des, label = m.groups()
            des = des.strip()
            if des and des[0] in ('X', 'J'):
                conns[des] = {'type': 'J', 'value': ctype.strip(), 'label': label.strip()}
    return conns


# ============================================================
# FUNCTIONAL BLOCK PARSING
# ============================================================

def parse_blocks(text):
    """Parse NETLIST BY FUNCTIONAL BLOCK section into structured blocks."""
    blocks = []
    # Split on ### BLOCK headers
    block_pattern = re.compile(
        r'### BLOCK (\d+):\s*(.+?)$\s*'
        r'\*\*Source:\s*(.+?)\*\*\s*'
        r'(.*?)'  # description text
        r'(\|.+?\|.*?)(?=\n\n\*\*|\n---|\Z)',  # table
        re.MULTILINE | re.DOTALL
    )

    for m in block_pattern.finditer(text):
        block_num = int(m.group(1))
        block_name = m.group(2).strip()
        source = m.group(3).strip()
        desc = m.group(4).strip()
        table_text = m.group(5).strip()

        # Parse table rows
        components = []
        for row in table_text.split('\n'):
            row = row.strip()
            if not row or row.startswith('|---') or row.startswith('| Component'):
                continue
            cells = [c.strip() for c in row.split('|')[1:-1]]
            if len(cells) >= 3:
                comp_str = cells[0]
                value = cells[1] if cells[1] != '—' else ''
                pins = [c for c in cells[2:] if c != '—']

                # Extract component designator
                des_match = re.match(r'(\w+)', comp_str)
                if des_match:
                    des = des_match.group(1)
                    components.append({
                        'designator': des,
                        'raw': comp_str,
                        'value': value,
                        'pins': pins
                    })

        blocks.append({
            'num': block_num,
            'name': block_name,
            'source': source,
            'desc': desc,
            'components': components
        })

    return blocks


# ============================================================
# OP-AMP PIN ASSIGNMENT PARSING
# ============================================================

def parse_opamp_pins(text):
    """Parse OP-AMP PIN ASSIGNMENTS section."""
    ics = {}
    # Find each IC table
    ic_pattern = re.compile(
        r'### (\w+)\s*\((.+?)\):\s*(.+?)$\s*'
        r'(\|.+?\|.*?)(?=\n###|\n---|\n\n##|\Z)',
        re.MULTILINE | re.DOTALL
    )

    for m in ic_pattern.finditer(text):
        ic_id = m.group(1)
        ic_value = m.group(2).strip().split()[0]  # e.g. "TL072"
        ic_label = m.group(3).strip()
        table = m.group(4)

        pins = []
        for row in table.split('\n'):
            pin_m = re.match(r'\|\s*(\d+)\s*\|\s*(.+?)\s*\|\s*(\w+)\s*\|', row)
            if pin_m:
                pin_num = int(pin_m.group(1))
                func = pin_m.group(2).strip()
                net = pin_m.group(3).strip()
                pins.append({'n': pin_num, 'func': func, 'net': net})

        ics[ic_id] = {
            'id': ic_id,
            'value': ic_value,
            'label': ic_label,
            'pins': pins
        }

    return ics


# ============================================================
# BLOCK-TO-STAGE MAPPING
# ============================================================

def map_blocks_to_stages(blocks, ics):
    """
    Map functional blocks to build stages.
    Returns a list of stages, each with component designators.

    Default heuristic: one block = one stage, in signal-flow order.
    Power/ICs get prepended as stage 1.
    Blocks that are closely related get merged.
    """
    stages = []

    # Stage 1: ICs + Power (always first)
    ic_designators = list(ics.keys())
    stages.append({
        'id': 1,
        'name': 'ICs + Power',
        'desc': f'Install {", ".join(ic_designators)} and decoupling caps. Wire IC power pins to rails.',
        'test': f'Verify +12V/-12V on IC power pins.',
        'color': '#888',
        'components': ic_designators,  # ICs themselves
        'blocks': []
    })

    # Map remaining blocks to stages
    stage_id = 2
    for block in blocks:
        # Skip power block (13) and unconfirmed (13)
        if 'power' in block['name'].lower() or 'unconfirmed' in block['name'].lower():
            continue

        comp_designators = [c['designator'] for c in block['components']
                           if not c['designator'].startswith('DA')]  # ICs handled in stage 1

        if not comp_designators:
            continue

        stages.append({
            'id': stage_id,
            'name': block['name'],
            'desc': block['desc'][:200] if block['desc'] else '',
            'test': '',
            'color': STAGE_COLORS[(stage_id - 1) % len(STAGE_COLORS)],
            'components': comp_designators,
            'blocks': [block['num']]
        })
        stage_id += 1

    return stages


STAGE_COLORS = [
    '#888', '#e67e22', '#e74c3c', '#2ecc71', '#3498db',
    '#9b59b6', '#1abc9c', '#f39c12', '#e91e63', '#00bcd4'
]


# ============================================================
# OUTPUT GENERATION
# ============================================================

def build_component_stage_map(stages):
    """Build designator -> stage_id lookup."""
    comp_stage = {}
    for stage in stages:
        for des in stage.get('components', []):
            comp_stage[des] = stage['id']
    return comp_stage


def generate_layout_json(title, bom, ics, blocks, stages):
    """Generate the layout JSON structure with null placements."""
    comp_stage = build_component_stage_map(stages)

    # Build IC entries
    ic_list = []
    for ic_id, ic_data in ics.items():
        # Map pins: for TL072 rotated 180, pin 5 at top-left
        pin_list = []
        for pin in ic_data['pins']:
            # Column: odd pins on left (e), even pins on right (f) for standard
            # With 180 rotation: pin 5,6,7,8 on left, pin 4,3,2,1 on right
            c = 'e' if pin['n'] >= 5 else 'f'
            pin_list.append({
                'n': pin['n'],
                'r': None,  # to be filled in
                'c': c,
                'net': pin['net']
            })
        ic_list.append({
            'id': ic_id,
            'value': ic_data['value'],
            'label': ic_data['label'],
            'stage': comp_stage.get(ic_id, 1),
            'rotation': 180,
            'pins': pin_list
        })

    # Build two-pin components (R, C, D)
    two_pins = []
    for des, info in sorted(bom.items()):
        if info['type'] not in ('R', 'C', 'D'):
            continue
        stage = comp_stage.get(des, None)
        entry = {
            'id': des,
            'type': info['type'],
            'value': info['value'],
            'r1': None, 'c1': None,
            'r2': None, 'c2': None,
            'stage': stage
        }
        if 'role' in info:
            entry['_role'] = info['role']
        two_pins.append(entry)

    # Build transistor entries
    transistors = []
    for des, info in sorted(bom.items()):
        if info['type'] != 'Q':
            continue
        stage = comp_stage.get(des, None)
        transistors.append({
            'id': des,
            'subtype': info.get('subtype', 'NPN'),
            'value': info['value'],
            'eR': None, 'eC': None,
            'bR': None, 'bC': None,
            'cR': None, 'cC': None,
            'stage': stage
        })

    # Build stage list (without internal component lists)
    stage_list = []
    for s in stages:
        stage_list.append({
            'id': s['id'],
            'name': s['name'],
            'desc': s['desc'],
            'test': s['test'],
            'color': s['color']
        })

    # Net connections extracted from blocks (for reference)
    net_connections = {}
    for block in blocks:
        for comp in block['components']:
            des = comp['designator']
            for pin_str in comp['pins']:
                # Extract net name from pin string like "HP_OUT (cathode)"
                net_m = re.match(r'(\w+)', pin_str)
                if net_m:
                    net = net_m.group(1)
                    if net not in ('pin', 'wiper'):
                        if des not in net_connections:
                            net_connections[des] = []
                        net_connections[des].append(net)

    layout = OrderedDict()
    layout['title'] = title
    layout['source'] = ''
    layout['board'] = 'n8synth 6HP'
    layout['revision'] = '0.1'
    layout['notes'] = 'Auto-generated skeleton — row/column placements need to be filled in.'
    layout['stages'] = stage_list
    layout['ics'] = ic_list
    layout['twoPins'] = two_pins
    layout['transistors'] = transistors
    layout['jumpers'] = []
    layout['powerWires'] = []
    layout['jpsWires'] = []
    layout['netLabels'] = []

    # Add net connections as a reference section (not used by viewer, useful for placement)
    layout['_netConnections'] = net_connections
    layout['_blockComponentMap'] = {
        f'Block {b["num"]}: {b["name"]}': [c['designator'] for c in b['components']]
        for b in blocks
    }

    return layout


# ============================================================
# MAIN PARSER
# ============================================================

def parse_netlist_md(filepath):
    """Parse a full netlist markdown file."""
    with open(filepath) as f:
        text = f.read()

    # Extract title
    title_m = re.match(r'#\s*(.+?)(?:\s*—\s*(.+))?$', text, re.MULTILINE)
    title = title_m.group(1).strip() if title_m else os.path.basename(filepath)

    # Find BOM sections
    bom = {}

    # Resistors
    r_section = re.search(r'### Resistors.*?\n((?:\|.+\n)+)', text)
    if r_section:
        bom.update(parse_resistor_bom(r_section.group(1).split('\n')))

    # Capacitors
    c_section = re.search(r'### Capacitors.*?\n((?:\|.+\n)+)', text)
    if c_section:
        bom.update(parse_capacitor_bom(c_section.group(1).split('\n')))

    # Semiconductors
    s_section = re.search(r'### Semiconductors.*?\n((?:\|.+\n)+)', text)
    if s_section:
        bom.update(parse_semiconductor_bom(s_section.group(1).split('\n')))

    # Potentiometers
    p_section = re.search(r'### Potentiometers.*?\n((?:\|.+\n)+)', text)
    if p_section:
        bom.update(parse_potentiometer_bom(p_section.group(1).split('\n')))

    # Connectors
    j_section = re.search(r'### Connectors.*?\n((?:\|.+\n)+)', text)
    if j_section:
        bom.update(parse_connector_bom(j_section.group(1).split('\n')))

    # Parse functional blocks
    blocks_text = text[text.find('## NETLIST BY FUNCTIONAL BLOCK'):] if '## NETLIST BY FUNCTIONAL BLOCK' in text else ''
    blocks = parse_blocks(blocks_text)

    # Parse op-amp pin assignments
    opamp_text = text[text.find('## OP-AMP PIN ASSIGNMENTS'):] if '## OP-AMP PIN ASSIGNMENTS' in text else ''
    ics = parse_opamp_pins(opamp_text)

    # Map blocks to stages
    stages = map_blocks_to_stages(blocks, ics)

    # Assign decoupling caps to stage 1 (ICs + Power)
    for des, info in bom.items():
        if info['type'] == 'C' and 'decoupl' in info.get('role', '').lower():
            if des not in build_component_stage_map(stages):
                stages[0]['components'].append(des)

    # Generate layout
    layout = generate_layout_json(title, bom, ics, blocks, stages)
    layout['source'] = filepath

    return layout


def write_output(layout, output_path):
    """Write JSON and JS wrapper."""
    # Write JSON
    with open(output_path, 'w') as f:
        json.dump(layout, f, indent=2)
    print(f'  JSON: {output_path} ({os.path.getsize(output_path)} bytes)')

    # Write JS wrapper
    js_path = output_path.replace('.json', '.js')
    name = layout.get('title', 'Untitled')
    with open(js_path, 'w') as f:
        f.write(f'registerLayout({json.dumps(name)}, ')
        json.dump(layout, f, indent=2)
        f.write(');')
    print(f'  JS:   {js_path} ({os.path.getsize(js_path)} bytes)')


# ============================================================
# REPORTING
# ============================================================

def print_report(layout):
    """Print a summary of what was parsed."""
    print(f'\nTitle: {layout["title"]}')
    print(f'Stages: {len(layout["stages"])}')
    for s in layout['stages']:
        print(f'  {s["id"]}. {s["name"]}')

    print(f'\nICs: {len(layout["ics"])}')
    for ic in layout['ics']:
        pins_placed = sum(1 for p in ic['pins'] if p['r'] is not None)
        print(f'  {ic["id"]} ({ic["value"]}) — {len(ic["pins"])} pins, {pins_placed} placed')

    print(f'\nTwo-pin components: {len(layout["twoPins"])}')
    by_type = {}
    for c in layout['twoPins']:
        by_type.setdefault(c['type'], []).append(c['id'])
    for t in sorted(by_type):
        names = {'R': 'Resistors', 'C': 'Capacitors', 'D': 'Diodes'}
        print(f'  {names.get(t, t)}: {len(by_type[t])} — {", ".join(sorted(by_type[t]))}')

    print(f'\nTransistors: {len(layout["transistors"])}')
    for t in layout['transistors']:
        print(f'  {t["id"]} ({t["value"]} {t["subtype"]})')

    staged = sum(1 for c in layout['twoPins'] if c['stage'] is not None)
    unstaged = sum(1 for c in layout['twoPins'] if c['stage'] is None)
    placed = sum(1 for c in layout['twoPins'] if c['r1'] is not None)
    print(f'\nPlacement status:')
    print(f'  Staged: {staged}, unstaged: {unstaged}')
    print(f'  Placed: {placed}, unplaced: {len(layout["twoPins"]) - placed}')

    if layout.get('_netConnections'):
        print(f'\nNet connections extracted for {len(layout["_netConnections"])} components')

    if layout.get('_blockComponentMap'):
        print(f'\nBlock → component mapping:')
        for block, comps in layout['_blockComponentMap'].items():
            print(f'  {block}: {", ".join(comps)}')


# ============================================================
# CLI
# ============================================================

def main():
    if len(sys.argv) < 2:
        print('Usage: python3 netlist_to_layout.py <netlist.md> [-o output.json]')
        print()
        print('Parses an MKI x ES EDU netlist markdown and generates a skeleton')
        print('layout JSON for the n8synth breadboard visualizer.')
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = None

    if '-o' in sys.argv:
        idx = sys.argv.index('-o')
        if idx + 1 < len(sys.argv):
            output_path = sys.argv[idx + 1]

    if not output_path:
        base = os.path.splitext(os.path.basename(input_path))[0]
        base = base.replace('_netlist_from_text', '').replace('_netlist', '')
        output_path = os.path.join('tools', 'visualizer', 'layouts', f'{base}.json')

    print(f'Parsing: {input_path}')
    layout = parse_netlist_md(input_path)

    print(f'\nWriting output:')
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    write_output(layout, output_path)

    print_report(layout)

    print(f'\nNext steps:')
    print(f'  1. Fill in row/column placements (r1,c1,r2,c2) for each component')
    print(f'  2. Add IC pin row assignments')
    print(f'  3. Add jumper wires, power wires, JPS wires')
    print(f'  4. Add net labels')
    print(f'  5. Write test descriptions for each stage')
    print(f'  6. Add <script src="layouts/{os.path.basename(output_path).replace(".json",".js")}"></script> to index.html')


if __name__ == '__main__':
    main()

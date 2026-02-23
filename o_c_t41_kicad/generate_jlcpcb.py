#!/usr/bin/env python3
"""
Parse KiCad PCB files and generate JLCPCB-compatible BOM and CPL (Component Placement List) CSV files.
For O_C T4.1 Rev 3 - Ornament and Crime with Teensy 4.1
"""

import re
import csv
import os

def parse_kicad_pcb(filepath):
    """Parse a KiCad PCB file and extract component information."""
    with open(filepath, 'r') as f:
        content = f.read()
    
    components = []
    
    # Find all top-level footprint blocks
    # We need to properly handle nested parentheses
    i = 0
    while True:
        # Find next footprint at the correct nesting level
        idx = content.find('\n\t(footprint ', i)
        if idx == -1:
            break
        
        # Find the matching closing paren by counting parens
        start = idx + 1  # skip the newline
        depth = 0
        pos = start
        while pos < len(content):
            if content[pos] == '(':
                depth += 1
            elif content[pos] == ')':
                depth -= 1
                if depth == 0:
                    break
            pos += 1
        
        block = content[start:pos+1]
        i = pos + 1
        
        # Extract footprint name
        fp_match = re.search(r'\(footprint\s+"([^"]+)"', block)
        if not fp_match:
            continue
        footprint = fp_match.group(1)
        
        # Extract layer
        layer_match = re.search(r'\(layer\s+"([^"]+)"\)', block)
        layer = layer_match.group(1) if layer_match else ""
        
        # Extract position (at x y [rotation])
        at_match = re.search(r'^\s+\(at\s+([\d.\-]+)\s+([\d.\-]+)(?:\s+([\d.\-]+))?\)', block, re.MULTILINE)
        if at_match:
            x = float(at_match.group(1))
            y = float(at_match.group(2))
            rotation = float(at_match.group(3)) if at_match.group(3) else 0.0
        else:
            x, y, rotation = 0, 0, 0
        
        # Extract Reference property
        ref_match = re.search(r'\(property\s+"Reference"\s+"([^"]+)"', block)
        reference = ref_match.group(1) if ref_match else ""
        
        # Extract Value property
        val_match = re.search(r'\(property\s+"Value"\s+"([^"]+)"', block)
        value = val_match.group(1) if val_match else ""
        
        # Determine if SMD or THT based on footprint name and pad types
        is_smd = False
        fp_upper = footprint.upper()
        if 'SMD' in fp_upper or 'smd' in footprint.lower():
            is_smd = True
        elif any(pkg in footprint for pkg in ['0805', '0603', '0402', '1206', '1210', '1812',
                                                'SOT-23', 'SOT23', 'SOIC', 'SSOP', 'TSSOP',
                                                'QFP', 'QFN', 'BGA', 'DFN', 'SOD',
                                                'MSOP', 'SC-70', 'HandSolder',
                                                'D_SMB', 'D_SOD', 'DO-214',
                                                'Fuse_1812', 'L_0805', 'L_0603',
                                                'ASPI', 'Abracon']):
            is_smd = True
        elif 'Capacitor_SMD' in footprint or 'Resistor_SMD' in footprint or 'Inductor_SMD' in footprint:
            is_smd = True
        elif 'Diode_SMD' in footprint or 'Package_SO' in footprint or 'Package_TO_SOT' in footprint:
            is_smd = True
        elif 'Diode_Bridge' in footprint:
            # HD01-T is SMD bridge rectifier
            is_smd = True
        elif any(pkg in footprint for pkg in ['_rectpads', 'Metric_Pad']):
            # Custom footprints with SMD pads
            is_smd = True
        
        # Also check if there's "smd" in pad types
        if '(type smd)' in block:
            is_smd = True
        
        # Check for through-hole indicators
        is_tht = False
        if 'THT' in fp_upper or 'Through' in footprint or 'DIP' in footprint:
            is_tht = True
        if 'PinHeader' in footprint or 'PinSocket' in footprint:
            is_tht = True
        if 'SolderJumper' in footprint:
            is_tht = True  # solder jumpers are board features, not assembled
        
        # Combined check: has SMD pads but also check for explicit THT
        if '(type thru_hole)' in block and '(type smd)' not in block:
            is_tht = True
            is_smd = False
        
        components.append({
            'reference': reference,
            'value': value,
            'footprint': footprint,
            'x': x,
            'y': y,
            'rotation': rotation,
            'layer': layer,
            'is_smd': is_smd,
            'is_tht': is_tht,
        })
    
    return components


# Direct mapping from (value_as_in_schematic, ref_prefix) -> (LCSC, description)
# Using exact value strings as found in the KiCad files
DIRECT_LCSC = {
    # ===== CAPACITORS =====
    ('22pF', 'C'): ('C1804', '22pF NP0 0805'),
    ('22pF 5%', 'C'): ('C1804', '22pF NP0 0805'),
    ('47pF 5%', 'C'): ('C1808', '47pF NP0 0805'),
    ('47pF', 'C'): ('C1808', '47pF NP0 0805'),
    ('470pF 5%', 'C'): ('C1742', '470pF NP0 0805'),
    ('470pF', 'C'): ('C1742', '470pF NP0 0805'),
    ('680pF', 'C'): ('C1744', '680pF NP0 0805'),
    ('680pF 5%', 'C'): ('C1744', '680pF NP0 0805'),
    ('1nF', 'C'): ('C1746', '1nF NP0 0805'),
    ('1.0nF 5%', 'C'): ('C1746', '1nF NP0 0805'),
    ('1.0nF', 'C'): ('C1746', '1nF NP0 0805'),
    ('8.2nF 5%', 'C'): ('C541502', '8.2nF NP0 0805 YAGEO CC0805JKNPO9BN822'),
    ('8.2nF', 'C'): ('C541502', '8.2nF NP0 0805 YAGEO CC0805JKNPO9BN822'),
    ('10nF', 'C'): ('C15850', '10nF X7R 0805'),
    ('0.1uF', 'C'): ('C49678', '100nF X7R 0805'),
    ('100nF', 'C'): ('C49678', '100nF X7R 0805'),
    ('1uF', 'C'): ('C28323', '1uF X7R 0805'),
    ('1.0uF', 'C'): ('C28323', '1uF X7R 0805'),
    ('10uF', 'C'): ('C15850', '10uF X5R 0805'),
    ('10uF Tantalum', 'C'): ('C7171', '10uF Tantalum 1206'),
    ('22uF', 'C'): ('C159769', '22uF X5R 25V 1206'),

    # ===== RESISTORS =====
    ('0Ω', 'R'): ('C17477', '0R 0805'),
    ('0R', 'R'): ('C17477', '0R 0805'),
    ('75Ω ¼W', 'R'): ('C25973', '75R 1% 1206'),
    ('75R', 'R'): ('C25973', '75R 1% 1206'),
    ('100Ω', 'R'): ('C17408', '100R 1% 0805'),
    ('100', 'R'): ('C17408', '100R 1% 0805'),
    ('100R', 'R'): ('C17408', '100R 1% 0805'),
    ('100Ω 1%', 'R'): ('C17408', '100R 1% 0805'),
    ('220', 'R'): ('C17557', '220R 1% 0805'),
    ('220R', 'R'): ('C17557', '220R 1% 0805'),
    ('220Ω', 'R'): ('C17557', '220R 1% 0805'),
    ('220Ω 1%', 'R'): ('C17557', '220R 1% 0805'),
    ('220 1%', 'R'): ('C17557', '220R 1% 0805'),
    ('470', 'R'): ('C17710', '470R 1% 0805'),
    ('470R', 'R'): ('C17710', '470R 1% 0805'),
    ('470Ω', 'R'): ('C17710', '470R 1% 0805'),
    ('470 1%', 'R'): ('C17710', '470R 1% 0805'),
    ('1K', 'R'): ('C17513', '1K 1% 0805'),
    ('1k', 'R'): ('C17513', '1K 1% 0805'),
    ('1KΩ', 'R'): ('C17513', '1K 1% 0805'),
    ('1.40K 1%', 'R'): ('C17369', '1.4K 1% 0805'),
    ('1.4K', 'R'): ('C17369', '1.4K 1% 0805'),
    ('1.4K 1%', 'R'): ('C17369', '1.4K 1% 0805'),
    ('2.21K 1%', 'R'): ('C17525', '2.21K 1% 0805'),
    ('2.21K', 'R'): ('C17525', '2.21K 1% 0805'),
    ('3.48K 1%', 'R'): ('C25896', '3.48K 1% 0805'),
    ('3.48K', 'R'): ('C25896', '3.48K 1% 0805'),
    ('3.84K 1%', 'R'): ('C26014', '3.84K 1% 0805'),
    ('3.84K', 'R'): ('C26014', '3.84K 1% 0805'),
    ('4.7K', 'R'): ('C17673', '4.7K 1% 0805'),
    ('4.7K 1%', 'R'): ('C17673', '4.7K 1% 0805'),
    ('4K7', 'R'): ('C17673', '4.7K 1% 0805'),
    ('5.11K 1%', 'R'): ('C25909', '5.11K 1% 0805'),
    ('5.11K', 'R'): ('C25909', '5.11K 1% 0805'),
    ('10K 0.05%', 'R'): ('C17414', '10K 0.05% 0805'),
    ('10K', 'R'): ('C17414', '10K 1% 0805'),
    ('10K 1%', 'R'): ('C17414', '10K 1% 0805'),
    ('11K 1%', 'R'): ('C17585', '11K 1% 0805'),
    ('11K', 'R'): ('C17585', '11K 1% 0805'),
    ('20K 0.05%', 'R'): ('C17556', '20K 0.05% 0805'),
    ('20K', 'R'): ('C17556', '20K 0805'),
    ('22K 1%', 'R'): ('C17560', '22K 1% 0805'),
    ('22K', 'R'): ('C17560', '22K 1% 0805'),
    ('24.9K 0.1%', 'R'): ('C17606', '24.9K 0.1% 0805'),
    ('24.9K', 'R'): ('C17606', '24.9K 0805'),
    ('31.6K 1%', 'R'): ('C17630', '31.6K 1% 0805'),
    ('31.6K', 'R'): ('C17630', '31.6K 0805'),
    ('49.9K 0.1%', 'R'): ('C17684', '49.9K 0.1% 0805'),
    ('49.9K', 'R'): ('C17684', '49.9K 0805'),
    ('75K 0.1%', 'R'): ('C17719', '75K 0.1% 0805'),
    ('75K', 'R'): ('C17719', '75K 0805'),
    ('100K 1%', 'R'): ('C149504', '100K 1% 0805 UNI-ROYAL 0805W8F1003T5E'),
    ('100K', 'R'): ('C149504', '100K 1% 0805 UNI-ROYAL 0805W8F1003T5E'),
    ('100K 0.1%', 'R'): ('C122537', '100K 0.1% 0805 YAGEO RT0805BRD07100KL'),
    ('150K 1%', 'R'): ('C17471', '150K 1% 0805'),
    ('150K', 'R'): ('C17471', '150K 0805'),
    ('1M 1%', 'R'): ('C17514', '1M 1% 0805'),
    ('1M', 'R'): ('C17514', '1M 0805'),

    # ===== DIODES =====
    ('HD01-T', 'D'): ('C52151', 'HD01-T Bridge Rectifier'),
    ('5.1V 3W', 'D'): ('C142361', '1SMB5918BT3G Zener 5.1V 3W'),
    ('5.1V', 'D'): ('C142361', '1SMB5918BT3G Zener 5.1V'),
    ('1N4148W', 'D'): ('C81598', '1N4148W SOD-123'),
    ('BZT52B5V1S', 'D'): ('C78478', 'BZT52B5V1S Zener 5.1V SOD-323'),

    # ===== INDUCTORS =====
    ('600Ω @100MHz', 'L'): ('C1015', 'Ferrite Bead 600R 0805'),
    ('10uH', 'L'): ('C339853', '10uH 1A Inductor'),
    ('1.5uH', 'L'): ('C869612', '1.5uH Inductor 0805'),

    # ===== MOSFETs =====
    ('FDV303N', 'Q'): ('C80498', 'FDV303N N-ch MOSFET SOT-23'),

    # ===== FUSES =====
    ('100mA', 'F'): ('C268780', 'MF-MSMF010-2 PTC 1812'),

    # ===== ICs =====
    ('LM339A', 'U'): ('C74559', 'LM339ADR SOIC-14'),
    ('LM339', 'U'): ('C74559', 'LM339ADR SOIC-14'),
    ('MCP6002', 'U'): ('C116706', 'MCP6002-I/SN SOIC-8'),
    ('OPA2172', 'U'): ('C545167', 'OPA2172IDR SOIC-8'),
    ('GS2262-SR', 'U'): ('C545167', 'OPA2172IDR SOIC-8'),
    ('MCP6022', 'U'): ('C34047', 'MCP6022-I/SN SOIC-8'),
    ('LM833', 'U'): ('C7969', 'LM833DR2G SOIC-8'),
    ('LMR54406F', 'U'): ('C3193129', 'LMR54406FDBVR SOT-23-6'),
    ('LMR54406', 'U'): ('C3193129', 'LMR54406FDBVR SOT-23-6'),
    ('LP2985A-5.0', 'U'): ('C74511', 'LP2985-50DBVR SOT-23-5'),
    ('LP2985', 'U'): ('C74511', 'LP2985-50DBVR SOT-23-5'),
    ('TPS7A2033', 'U'): ('C2862740', 'TPS7A2033PDBVR SOT-23-5'),
    ('LP5907-1.8', 'U'): ('C92498', 'LP5907MFX-1.8/NOPB SOT-23-5'),
    ('LP5907', 'U'): ('C92498', 'LP5907MFX-1.8/NOPB SOT-23-5'),
    ('MCP33131D', 'U'): ('C635129', 'MCP33131D-05-E/MN MSOP-10'),
    ('MCP33131', 'U'): ('C635129', 'MCP33131D-05-E/MN MSOP-10'),
    ('74LV4051', 'U'): ('C547982', '74LV4051PW TSSOP-16'),
    ('DAC8568', 'U'): ('C2680013', 'DAC8568IBPWR TSSOP-16'),
    ('74LV1T125', 'U'): ('C473338', 'SN74LV1T125DBVR SOT-23-5'),
    ('PCM1808', 'U'): ('C55513', 'PCM1808PWR TSSOP-14'),
    ('PCM1754', 'U'): ('C294211', 'PCM1754DBQR TSSOP-16'),
    ('OPA2277', 'U'): ('C24460', 'OPA2277UA SOIC-8'),
}

# Through-hole / excluded components (user will solder these)
THT_REFS = set()
# From bom.txt: J1-J20 (jacks), J21 (OLED), J22-J23 (headers), J26-J27 (headers),
# J28-J37 (sockets/headers), SW1-SW7 (switches/encoders), U12 (Teensy), H11L1 (optocoupler DIP6)
EXCLUDE_PREFIXES = ['J', 'SW', 'TP']  # Jacks, switches, test points
EXCLUDE_REFS = {'U12', 'U27'}  # Teensy 4.1 and optocoupler (DIP through-hole)
DNP_REFS = set()  # R200, R201, R202 are Do Not Populate


def normalize_value(value):
    """Normalize component value strings for matching."""
    v = value.strip()
    # Common normalizations
    v = v.replace('µ', 'u').replace('μ', 'u')
    v = v.replace('Ω', 'R').replace('ohm', 'R').replace('Ohm', 'R')
    return v


def get_package_size(footprint):
    """Extract package size from footprint name."""
    for size in ['0402', '0603', '0805', '1206', '1210', '1812', '2512']:
        if size in footprint:
            return size
    if 'SOT-23' in footprint or 'SOT23' in footprint:
        return 'SOT-23'
    if 'SOIC' in footprint or 'SO-8' in footprint or 'SOP-8' in footprint or 'SOIC8' in footprint:
        return 'SOIC-8'
    if 'SOD-123' in footprint or 'SOD123' in footprint:
        return 'SOD-123'
    if 'SOD-323' in footprint or 'SOD323' in footprint:
        return 'SOD-323'
    if 'MSOP' in footprint:
        return 'MSOP'
    if 'TSSOP' in footprint:
        return 'TSSOP'
    if 'SMB' in footprint or 'DO-214AA' in footprint:
        return 'SMB'
    if 'SOT-353' in footprint:
        return 'SOT-353'
    if 'SOT-753' in footprint:
        return 'SOT-753'
    if 'ASPI' in footprint or 'Abracon' in footprint:
        return 'ASPI-4030'
    if 'Diode_Bridge' in footprint:
        return 'Bridge'
    if 'Fuse_1812' in footprint or '4532' in footprint:
        return '1812'
    return ''


def lookup_lcsc(reference, value, footprint):
    """Look up LCSC part number for a component."""
    prefix = re.match(r'([A-Z]+)', reference)
    prefix = prefix.group(1) if prefix else ''
    
    val = value.strip()
    
    # Try exact match first
    key = (val, prefix)
    if key in DIRECT_LCSC:
        return DIRECT_LCSC[key]
    
    # Try with unicode normalization (Ω -> R, etc.)
    val_norm = normalize_value(val)
    key_norm = (val_norm, prefix)
    if key_norm in DIRECT_LCSC:
        return DIRECT_LCSC[key_norm]
    
    # Try stripping tolerance specs (e.g., "100K 0.1%" -> "100K")
    val_base = re.split(r'\s+\d+\.?\d*%', val)[0].strip()
    key_base = (val_base, prefix)
    if key_base in DIRECT_LCSC:
        return DIRECT_LCSC[key_base]
    
    val_base_norm = re.split(r'\s+\d+\.?\d*%', val_norm)[0].strip()
    key_base_norm = (val_base_norm, prefix)
    if key_base_norm in DIRECT_LCSC:
        return DIRECT_LCSC[key_base_norm]
    
    # For ICs, try matching beginning of value string
    if prefix == 'U':
        for (map_val, map_prefix), (lcsc, desc) in DIRECT_LCSC.items():
            if map_prefix == 'U' and (val.startswith(map_val) or map_val.startswith(val)):
                return lcsc, desc
    
    return '', ''


def should_exclude(ref, value, footprint, is_smd, is_tht):
    """Determine if a component should be excluded from assembly."""
    # Exclude DNP
    if ref in DNP_REFS:
        return True, "DNP"
    # R200-R202 DNP
    if ref in ('R200', 'R201', 'R202'):
        return True, "DNP"
    # Exclude power symbols, mounting holes, etc
    if ref.startswith('#') or ref.startswith('H') or ref == '':
        return True, "Non-component"
    # Exclude specific refs
    if ref in EXCLUDE_REFS:
        return True, "Through-hole (user solder)"
    # Exclude by prefix
    for p in EXCLUDE_PREFIXES:
        if ref.startswith(p) and (len(ref) == len(p) or ref[len(p):].isdigit()):
            return True, f"Through-hole {p} (user solder)"
    # Exclude through-hole parts
    if is_tht and not is_smd:
        return True, "Through-hole"
    return False, ""


def process_board(pcb_file, board_name):
    """Process a single board and generate BOM + CPL data."""
    print(f"\n{'='*60}")
    print(f"Processing: {board_name} ({pcb_file})")
    print(f"{'='*60}")
    
    components = parse_kicad_pcb(pcb_file)
    print(f"Total components found in PCB: {len(components)}")
    
    smd_components = []
    excluded = []
    unmatched = []
    
    for comp in components:
        ref = comp['reference']
        val = comp['value']
        fp = comp['footprint']
        
        excl, reason = should_exclude(ref, val, fp, comp['is_smd'], comp['is_tht'])
        if excl:
            excluded.append((ref, val, fp, reason))
            continue
        
        if not comp['is_smd']:
            excluded.append((ref, val, fp, "Detected as non-SMD"))
            continue
        
        lcsc, desc = lookup_lcsc(ref, val, fp)
        
        smd_components.append({
            'reference': ref,
            'value': val,
            'footprint': fp,
            'x': comp['x'],
            'y': comp['y'],
            'rotation': comp['rotation'],
            'layer': comp['layer'],
            'lcsc': lcsc,
            'lcsc_desc': desc,
        })
        
        if not lcsc:
            unmatched.append((ref, val, fp))
    
    # Sort by reference
    smd_components.sort(key=lambda c: (re.match(r'([A-Z]+)', c['reference']).group(1) if re.match(r'([A-Z]+)', c['reference']) else '', 
                                        int(re.search(r'(\d+)', c['reference']).group(1)) if re.search(r'(\d+)', c['reference']) else 0))
    
    print(f"\nSMD components for assembly: {len(smd_components)}")
    print(f"Excluded components: {len(excluded)}")
    
    if unmatched:
        print(f"\nWARNING: {len(unmatched)} SMD components without LCSC mapping:")
        for ref, val, fp in sorted(unmatched):
            print(f"  {ref}: {val} ({fp})")
    
    print(f"\nExcluded components:")
    for ref, val, fp, reason in sorted(excluded):
        print(f"  {ref}: {val} - {reason}")
    
    return smd_components


def generate_bom_csv(components, output_file):
    """Generate JLCPCB BOM CSV file."""
    # Group components by value + footprint + LCSC
    groups = {}
    for comp in components:
        key = (comp['value'], comp['footprint'], comp['lcsc'])
        if key not in groups:
            groups[key] = []
        groups[key].append(comp['reference'])
    
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part Number'])
        
        for (value, footprint, lcsc), refs in sorted(groups.items()):
            refs_str = ','.join(sorted(refs, key=lambda r: (re.match(r'([A-Z]+)', r).group(1) if re.match(r'([A-Z]+)', r) else '',
                                                             int(re.search(r'(\d+)', r).group(1)) if re.search(r'(\d+)', r) else 0)))
            # Simplify footprint for display
            fp_short = footprint.split(':')[-1] if ':' in footprint else footprint
            writer.writerow([value, refs_str, fp_short, lcsc])
    
    print(f"BOM written to: {output_file}")


def generate_cpl_csv(components, output_file):
    """Generate JLCPCB CPL (Component Placement List) CSV file."""
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['Designator', 'Val', 'Package', 'Mid X', 'Mid Y', 'Rotation', 'Layer'])
        
        for comp in components:
            layer = 'top' if comp['layer'] == 'F.Cu' else 'bottom'
            fp_short = comp['footprint'].split(':')[-1] if ':' in comp['footprint'] else comp['footprint']
            writer.writerow([
                comp['reference'],
                comp['value'],
                fp_short,
                f"{comp['x']}mm",
                f"{-comp['y']}mm",  # KiCad Y is inverted vs JLCPCB
                comp['rotation'],
                layer
            ])
    
    print(f"CPL written to: {output_file}")


def main():
    # Process control (CPU) board
    control_components = process_board('control_rev3.kicad_pcb', 'Control/CPU Board')
    
    # Process analog board
    analog_components = process_board('analog_rev3.kicad_pcb', 'Analog Board')
    
    # Generate output files
    os.makedirs('jlcpcb_output', exist_ok=True)
    
    generate_bom_csv(control_components, 'jlcpcb_output/control_rev3_bom.csv')
    generate_cpl_csv(control_components, 'jlcpcb_output/control_rev3_cpl.csv')
    
    generate_bom_csv(analog_components, 'jlcpcb_output/analog_rev3_bom.csv')
    generate_cpl_csv(analog_components, 'jlcpcb_output/analog_rev3_cpl.csv')
    
    # Summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    
    for name, comps in [('Control/CPU', control_components), ('Analog', analog_components)]:
        total = len(comps)
        matched = sum(1 for c in comps if c['lcsc'])
        unmatched = total - matched
        print(f"\n{name} Board:")
        print(f"  SMD parts for assembly: {total}")
        print(f"  LCSC matched: {matched}")
        print(f"  Need manual LCSC lookup: {unmatched}")
        
        if unmatched:
            print(f"  Unmatched parts:")
            for c in comps:
                if not c['lcsc']:
                    print(f"    {c['reference']}: {c['value']} ({c['footprint']})")


if __name__ == '__main__':
    main()

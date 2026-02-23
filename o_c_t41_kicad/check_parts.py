import re

with open('control_rev3.kicad_pcb', 'r') as f:
    content = f.read()

for ref in ['D1', 'D2', 'D3', 'D4', 'D5', 'D6', 'L1', 'L2', 'L3', 'F1']:
    pattern = r'"Reference"\s+"' + ref + '"'
    m = re.search(pattern, content)
    if m:
        block_start = content.rfind('(footprint ', 0, m.start())
        fp_match = re.search(r'\(footprint\s+"([^"]+)"', content[block_start:block_start+200])
        fp = fp_match.group(1) if fp_match else '?'
        val_pattern = r'"Value"\s+"([^"]+)"'
        val_match = re.search(val_pattern, content[block_start:block_start+2000])
        val = val_match.group(1) if val_match else '?'
        pad_start = content[block_start:block_start+5000]
        has_smd = '(type smd)' in pad_start
        has_tht = '(type thru_hole)' in pad_start
        print(f'{ref}: value={val}, fp={fp}, smd={has_smd}, tht={has_tht}')
    else:
        print(f'{ref}: not found')

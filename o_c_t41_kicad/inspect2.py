import re

with open('control_rev3.kicad_sch', 'r') as f:
    content = f.read()

# Find symbol blocks
for m in re.finditer(r'\(symbol ', content):
    start = m.start()
    chunk = content[start:start+1500]
    lines = chunk.split('\n')
    for i, line in enumerate(lines[:30]):
        print(f'{i}: {line}')
    print('===')
    break

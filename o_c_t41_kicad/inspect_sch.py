import re

with open('control_rev3.kicad_sch', 'r') as f:
    content = f.read()

# Look for symbol instances
count = 0
for m in re.finditer(r'\(symbol \(lib_id', content):
    start = m.start()
    lines = content[start:start+2000].split('\n')
    for i, line in enumerate(lines[:40]):
        print(f'{i}: {line}')
    print('---')
    count += 1
    if count >= 3:
        break

if count == 0:
    print("No '(symbol (lib_id' found. Searching for alternatives...")
    # Try other patterns
    for pattern in [r'\(symbol ', r'"Reference"', r'"Value"', r'\(property']:
        matches = len(re.findall(pattern, content))
        print(f"  Pattern '{pattern}' found {matches} times")
    
    # Show first 200 chars
    print("\nFirst 500 chars of file:")
    print(content[:500])

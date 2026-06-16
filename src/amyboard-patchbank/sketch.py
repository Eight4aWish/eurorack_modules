# AMYboard Sketch
# DESCRIPTION: Patch Bank -- browse + play 24 pad/bass patches with the rotary
# encoder and CV/gate. Tulip auto-runs this "current sketch" at boot: it runs
# top-to-bottom once (setup), then calls the global loop() every frame. The app
# itself lives in /user/patchbank.py.
import sys
if '/user' not in sys.path:
    sys.path.append('/user')

import patchbank

patchbank.setup()
loop = patchbank.loop

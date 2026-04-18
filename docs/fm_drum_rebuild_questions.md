# FM Drum Layout Rebuild — Questions for Schematic Verification

These questions came up while designing the new layouts. Please verify against the production schematic (page 2) when you get a chance.

## Board 1 Questions

1. **R17 (100K) — is it Q1's base resistor or a direct feedback path?**
   The netlist says R17 goes from CAR_SCH_OUT to CAR_INT_INV. But Q1 also connects B=SCH_OUT, C=INT_INV. Is R17 actually the base resistor (SCH_OUT → Q1.B), with Q1.C separately connecting to INT_INV? Or does R17 truly go from SCH_OUT directly to INT_INV (in parallel with Q1)?

2. **Same question for R27 on the modulator side** — base resistor for Q3, or direct feedback?

3. **R20 / R29 (1M) — connected to VCC or trigger bus?**
   The netlist says "VCC" but notes say "trigger pulls base high" and "connects to trigger path." On the production schematic, do R20/R29 connect to UNACC_TRIG, or to VCC with the trigger arriving at Q2/Q4 base through a different path?

4. **MOD_CV input — is there a 33K limiter pair?**
   The carrier has R31 (33K VCC→POT4 top) and R33 (33K POT4 wiper→CARRIER_CV). Does the modulator have an equivalent pair for POT5? Or does POT5 wiper connect directly to MOD_CV?

5. **R53 (100K) — where is it?**
   Listed in BOM but not assigned to any netlist block. Could it be the modulator equivalent of R32 (FM coupling) or something in the tune CV path?

6. **Does MOD_CV need a negative clamp diode like D4?**
   D4 clamps CARRIER_CV to prevent negative voltages. Is there an equivalent diode for MOD_CV?

7. **Q6 impact distortion — exact connections?**
   The netlist says Q6.C connects to "R34/SINE_NODE junction" and Q6.E to "CAR_INT_OUT side of R34." Does Q6 bridge across R34 (shorting it when conducting), or is it wired differently?

## Board 2 Questions

8. **R3 (100K) threshold — from VCC or from COMP_OUT?**
   On the kick, the comparator threshold has R16 (100K) from VCC to COMP_INV and R4 (33K) from COMP_INV to GND. The FM drum netlist has R3 (100K) from VCC to pin 2. Is this correct, or does it come from COMP_OUT (positive feedback)?

9. **Q9 (PNP) collector — to GND or to VEE?**
   The netlist says Q9.C = GND. On the production schematic, is the PNP collector actually to GND, or could it be to VEE?

10. **R45 (10K) XOR pull-down — to GND or VEE?**
    The netlist says GND. The schematic might show VEE for the pull-down to ensure full output swing.

11. **Are there any input protection resistors (R54-R56, 1K) on Board 2?**
    The netlist flags these as "likely PCB protection." Do you see 1K resistors at any jack inputs on the schematic?

12. **HPF feedback R44 — 100K or 200K?**
    Tutorial shows 200K, but BOM only has one 200K (used in VCA). Does the production schematic show 100K or 200K for the HPF feedback?

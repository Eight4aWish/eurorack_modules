# Post-script for [env:nanoesp32-corthex] that injects a serial-number
# filter into the dfu-util UPLOADERFLAGS list. The espressif32 platform's
# default DFU upload uses only the vendor:product ID (2341:0070), which is
# ambiguous when other Arduino-DFU devices are on the bus.
#
# The serial number below identifies a specific physical Nano ESP32. If
# you swap to a replacement board, run `dfu-util --list` and update.

Import("env")

NANO_ESP32_SERIAL = "744DBDA1B538"

flags = list(env.get("UPLOADERFLAGS", []))
new_flags = []
i = 0
while i < len(flags):
    new_flags.append(flags[i])
    if flags[i] == "-d" and i + 1 < len(flags):
        new_flags.append(flags[i + 1])
        new_flags.extend(["-S", NANO_ESP32_SERIAL])
        i += 2
    else:
        i += 1

env.Replace(UPLOADERFLAGS=new_flags)

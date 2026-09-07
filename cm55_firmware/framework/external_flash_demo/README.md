# External flash byte-stream demo

This isolated CM55 framework initializes IPC, waits one second for CM33 to
connect, erases one external QSPI flash sector, programs 16 bytes, reads them
through MemorySPI, and sends a pass/fail event through the existing MicroPython
interface. It does not use CM55 `printf()`, so no second UART is needed. The
first version intentionally sends only control events because the existing
MicroPython callback is not a bulk-data transport. It does not modify the
normal `deepcraft` framework or any existing repository file.

The demo uses `0x60880000`, immediately after the current CM55 trailer. Do not
use this address for a production image until the bootloader and complete flash
layout reserve it formally.

## Build with Docker

From the repository root:

```sh
docker run --rm \\
  -v "$PWD":/workspace \\
  -w /workspace/cm55_firmware \\
  ifxmakers/psoc-embedded-ai-toolchain:deepcraft-0.1.0 \\
  make CONFIG=Debug FRAMEWORK=external_flash_demo APPNAME=external_flash_demo
```

## Flash with Docker

Connect the board's KitProg3 USB interface and run:

```sh
docker run --rm \\
  --device=/dev/bus/usb \\
  -v "$PWD":/workspace \\
  -w /workspace/cm55_firmware \\
  ifxmakers/psoc-embedded-ai-toolchain:deepcraft-0.1.0 \\
  make CONFIG=Debug FRAMEWORK=external_flash_demo APPNAME=external_flash_demo flash
```

## Observe UART output

The CM33 MicroPython console is normally exposed by KitProg3. On Linux, find
the port with `ls /dev/ttyACM* /dev/ttyUSB*` and open it at 115200 baud:

```sh
screen /dev/ttyACM0 115200
```

The MicroPython callback prints a startup event followed by
`CM55 flash demo: PASS`.

The demo intentionally repeats the write on every boot. It is a learning
experiment, not yet a persistent data store or model slot implementation.

## Run from MicroPython

After flashing CM55, open the existing MicroPython console and run:

```python
from machine import IPC
from deepcraft_model import DeepcraftModel
import time

ipc = IPC(src_core=IPC.CM33, target_core=IPC.CM55)
model = DeepcraftModel(ipc, model=DeepcraftModel.MODEL_VA)

def on_event(event, value):
  if event == DeepcraftModel.VA_EVENT_INTENT:
    if value == 0x90:
      print("CM55 flash demo: PASS")
    elif value == 0x91:
      print("CM55 flash demo: FAIL")
    else:
      print("CM55 event value:", value)

model.set_event_cb(on_event)
model.enable_target()
time.sleep_ms(3000)
```

The unknown command bytes are intentionally interpreted as intent events by
the existing demo MicroPython engine. This is only a console proof of the IPC
path, not a production protocol extension.
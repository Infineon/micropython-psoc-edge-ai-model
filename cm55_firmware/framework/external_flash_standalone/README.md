# Standalone CM55 external-flash test

This test removes CM33, MicroPython, IPC, and UART from the experiment. CM55
alone erases, programs, reads, modifies, and rereads 16 bytes in external QSPI
flash.

The board RGB green LED is the result indicator:

- Green blinks three times: a validation passed.
- Blue blinks three times: a validation failed.
- One short green blink at boot: CM55 reached `main()`; no later result means
  the flash operation is stuck or CM55 did not start.

The demo first validates the known pattern ending in `FF`, then programs the
final byte to `EE` and validates it again. This is a valid NOR-flash
modification because it changes bits from 1 to 0 without another erase.

The test uses offset `0x00880000`, mapped address `0x60880000`, immediately
following the current CM55 image/trailer layout. Do not use this address for a
production model slot until the complete flash layout reserves it formally.

## Build with Docker

From the repository root:

```sh
docker run --rm \\
  -v "$PWD":/workspace \\
  -w /workspace/cm55_firmware \\
  ifxmakers/psoc-embedded-ai-toolchain:deepcraft-0.1.0 \\
  make CONFIG=Debug FRAMEWORK=external_flash_standalone APPNAME=external_flash_standalone
```

## Flash with KitProg3

Close `screen` or Thonny first, then run:

```sh
docker run --rm \\
  --device=/dev/bus/usb \\
  -v "$PWD":/workspace \\
  -w /workspace/cm55_firmware \\
  ifxmakers/psoc-embedded-ai-toolchain:deepcraft-0.1.0 \\
  make CONFIG=Debug FRAMEWORK=external_flash_standalone APPNAME=external_flash_standalone flash
```

After flashing, reset or power-cycle the board. No MicroPython script is
needed. The LED is the only test output.

# TFLM (TensorFlow Lite for Microcontrollers) Framework

A small, transport-agnostic framework that runs arbitrary `.tflite` models on the
CM55 core of multi-core PSOC™ Edge devices (CM33 running MicroPython, CM55
running TFLM), with the model supplied at **runtime** from MicroPython instead of
being compiled into the firmware.

## What it does

- Runs a generic **TFLM engine** on CM55: a command-driven state machine
  (load, describe, run, infer) that treats the model as bytes-in / bytes-out.
- Loads the model **in place from external flash** (XIP, zero-copy). MicroPython
  on CM33 writes the `.tflite` into the shared-data flash partition; the CM55
  firmware does not need to be rebuilt to change models.
- Keeps the engine **transport-agnostic**: it only depends on a small
  `transport_interface_t` vtable, so no transport-specific code lives in the
  engine. Currently only the IPC pipe is enabled and tested in this framework.
- Leaves all application logic (input quantization, output decoding, thresholds)
  to the MicroPython script on CM33. CM55 is a passive server.
- Provides an optional model-specific **preprocess hook**. The "hey edge"
  keyword-spotting (KWS) front-end plugs in here and turns raw PCM into MFE
  features.

## Components

| File | Role |
| --- | --- |
| [cm55_firmware/framework/tflm/main.c](../../cm55_firmware/framework/tflm/main.c) | CM55 entry point: wires the IPC transport, creates the FreeRTOS `tflm-task`, sets the model base address (`0x62000000`). |
| [cm55_firmware/framework/tflm/tflm.mk](../../cm55_firmware/framework/tflm/tflm.mk) | Build fragment (GCC_ARM only): sources, TFLM core archive, optional KWS front-end. |
| [cm55_firmware/engine/tflm/tflm_engine.h](../../cm55_firmware/engine/tflm/tflm_engine.h) | Wire protocol (commands, events, error codes) and engine API. |
| [cm55_firmware/engine/tflm/tflm_engine.c](../../cm55_firmware/engine/tflm/tflm_engine.c) | Transport-agnostic state machine, command queue, input/output buffers. |
| [cm55_firmware/adapters/tflm/adapter.h](../../cm55_firmware/adapters/tflm/adapter.h) / [adapter.cpp](../../cm55_firmware/adapters/tflm/adapter.cpp) | Wraps `tflite::MicroInterpreter` in a flat C API (`load`, `get_info`, `invoke`, weak `preprocess`). |
| [cm55_firmware/framework/tflm/edge-impulse-kws/kws_frontend.cpp](../../cm55_firmware/framework/tflm/edge-impulse-kws/kws_frontend.cpp) | KWS preprocess override: PCM to MFE features to int8 (build with `KWS=1`). |
| [cm55_firmware/sources/transport/ipc.c](../../cm55_firmware/sources/transport/ipc.c) | IPC implementation of `transport_interface_t`. |
| [shared/include/ipc_ring.h](../../shared/include/ipc_ring.h) | Two 64 KB lock-free byte rings in shared SRAM (CM33→CM55 and CM55→CM33). |
| `deps/tflm/tflite-micro` | Upstream tflite-micro (git submodule). |
| `deps/tflm/prebuilt/tflm-core.a` | Prebuilt TFLM core archive; used automatically if present. |
| `deps/libedge-impulse-sdk.a` | Prebuilt Edge Impulse SDK + MFE DSP, linked only when `KWS=1`. |

CM33 side (MicroPython port, repo `micropython-psoc-edge_project`) uses the
existing `machine.IPC`, `machine.ExternalFlash` and `machine.PDM_PCM` modules; no
extra C module is needed.

## Supported applications

| Application | CM33 script | Model | Build |
| --- | --- | --- | --- |
| Sine wave regression (hello world) | `ports/psoc-edge/examples/tflm/sine_model_ipc.py` | `hello_world_int8.tflite` (also `hello_world_float.tflite`) | `make FRAMEWORK=tflm` |
| "hey edge" keyword spotting from the live PDM mic | `ports/psoc-edge/examples/tflm/hey_edge_mic_ipc.py` | `keyword_spotting_demo.tflite` (Edge Impulse project 1128757) | `make FRAMEWORK=tflm KWS=1` |

One `KWS=1` image runs **both** applications: the KWS preprocess falls back to
pass-through for any model whose input tensor is not 7920 bytes, so switching
between them only needs a different model file and script, no reflash.

## Wire protocol

Control messages carry a command and a 32-bit value. Bulk bytes travel through
the shared rings, announced by a `DATA_AVAIL(len)` doorbell. 
\
IDs: \
CM33 = EP 1,
client 3 \
CM55 = EP 2, client 5.

| Command (CM33 → CM55) | Value | Valid when | Reply |
| --- | --- | --- | --- |
| `MODEL_LOAD` 0x90 | byte offset of the model in the flash partition | unloaded | `EVT_MODEL_LOADED` 0xB1 |
| `MODEL_INFO` 0x95 | 0 | loaded or running | 64 B bulk: input then output tensor info |
| `MODEL_RUN` 0x92 | 0 | loaded | `EVT_MODEL_RUNNING` 0xB3 |
| `RUN_INFERENCE` 0x94 | 0 | running | bulk: raw output tensor bytes |
| `MODEL_PAUSE` 0x93 / `MODEL_UNLOAD` 0x91 | 0 | running / loaded | `EVT_MODEL_PAUSED` 0xB4 / `EVT_MODEL_UNLOADED` 0xB2 |
| bulk `DATA_AVAIL` 0x84 | length | any | none; bytes accumulate in the engine input buffer until `RUN_INFERENCE` |

Other events: `EVT_READY` 0xB0 (engine booted) and `EVT_ERROR` 0xE0 with
value \
1 = bad state, \
2 = model load failed, \
3 = invoke failed / input overflow.

## Build and deploy

### Prerequisites

- GNU Arm Embedded toolchain (`arm-none-eabi-gcc`). The `tflm` framework only
  supports `TOOLCHAIN=GCC_ARM`; set `GCC_ARM_DIR` if it is not on `PATH`.
- `openocd` (ModusToolbox build) with KitProg3 access to the board.
- Git submodules: `git submodule update --init` (for `deps/psoc-edge`, or
  `make submodules` in `cm55_firmware`).
- Board: KIT_PSE84_AI.

### One-time setup

Generate the pruned tflite-micro tree (needs network access; repeat only after
bumping the submodule):

```bash
cd cm55_firmware/framework/tflm
make -f tflm.mk regen-tflm-tree
```

If `deps/tflm/prebuilt/tflm-core.a` exists it is used automatically. Otherwise
the core is compiled from `tflm_tree`. Pass `TFLM_PREBUILT_CORE_LIB=` (empty) to
force a from-source build.

### Build and flash the CM33 MicroPython firmware

From `micropython-psoc-edge repository`:

```bash
make BOARD=KIT_PSE84_AI
make deploy BOARD=KIT_PSE84_AI
```

Flash CM33 first, then CM55. The CM33 secure image must map the shared-data flash range for CM55 (MPC region), which the MicroPython port provides.

### Build and flash the CM55 firmware

From `cm55 repository`:

```bash
make clean FRAMEWORK=tflm            # needed when switching frameworks (shared build dir)
make FRAMEWORK=tflm                  # sine model only
make FRAMEWORK=tflm KWS=1            # sine + hey_edge KWS
make deploy FRAMEWORK=tflm KWS=1     # build (if needed) and flash via OpenOCD
```

Output: `build/Debug/tflm.elf`, `.hex`, `.bin`. Variables: `CONFIG=Debug|Release`,
`BOARD=KIT_PSE84_AI`.

### Copy the models and scripts

From micropython repository path : `../ports/psoc-edge/examples/tflm`:

```bash
mpremote cp hello_world_int8.tflite :
mpremote cp keyword_spotting_demo.tflite :
```

The scripts open the model by relative path on the device filesystem.

## Using the applications

**Hard-reset the board before each run.** CM55 keeps running across a CM33 soft
reset, so a second `MODEL_LOAD` would be rejected with `BAD_STATE`.

### Sine model

```bash
mpremote run sine_model_ipc.py
```

What it does:

1. Writes `hello_world_int8.tflite` to external flash and verifies it.
2. Initializes IPC, registers client 3, boots CM55 and waits for `EVT_READY`.
3. Sends `MODEL_LOAD`, `MODEL_INFO` (prints input/output type, dims, scale,
   zero point) and `MODEL_RUN`.
4. Sweeps x over [0, 2π) in steps of 0.2. For each x it quantizes the value into
   the input tensor (`q = round(x / scale) + zero_point`, or a float32 for the
   float model), sends it as bulk data, sends `RUN_INFERENCE`, then dequantizes
   the 1-byte result into y ≈ sin(x).

Example output:

```text
x=1.6000  y=0.9949
x=4.6000  y=-1.0944
```

The loop runs forever. Reset the board to stop it, since Ctrl-C is not handled
while the script is running.

To use the float model, change `SOURCE_PATH` to `hello_world_float.tflite`. The
script picks 1 or 4 bytes per value from the tensor type reported by
`MODEL_INFO`.

### "hey edge" keyword spotting

```bash
mpremote run hey_edge_mic_ipc.py
```

Requires the `KWS=1` CM55 image and a PDM microphone.

What it does:

1. Provisions `keyword_spotting_demo.tflite` into flash and boots CM55 as above.
2. Repeats for `RUN_SECONDS` (20 s): captures samples in chunks, streams them to CM55, sends
   `RUN_INFERENCE`, and reads back the result `[hey_edge, noise]`.
3. Dequantizes with the output scale and zero point from `MODEL_INFO`. If
   `p(hey_edge) >= 0.6` (`KWS_THRESHOLD`) it prints a detection with the
   round-trip latency and blinks USER_LED_B (P20_5) three times.
4. Prints a runtime summary (model size, load time, average inference latency, detection count).

Example output:

```text
[listening ~2 s] ...
  >>> 'hey edge' DETECTED [p(hey_edge)=<p> p(noise)=<p>]
Inference time : <n> ms
```

Tunable constants at the top of the script: `KWS_THRESHOLD`, `RUN_SECONDS`,
`EXPECTED_DETECTIONS`, `LED_PIN`.

## Using your own model

1. Convert to a single-input, single-output `.tflite` (int8 or float32).
2. If the model's input is not the raw bytes your script will send, add a strong
   `tflm_adapter_preprocess()` override in its own source file (see
   `kws_frontend.cpp`) and add it to `FRAMEWORK_CXX_SRCS` in `tflm.mk`.
3. Copy the model to the device, write it to flash with `machine.ExternalFlash`,
   and drive the command sequence as in `sine_model_ipc.py`.
4. Use the scale and zero point from `MODEL_INFO` to quantize inputs and decode
   outputs on CM33.

Limits:

- One input and one output tensor (index 0).
- Tensor arena 128 KB, staged input up to 64 KB, output up to 4 KB.
- All builtin TFLM ops are registered, so no per-model op list is needed.
- The model must fit the flash partition and be at the offset passed to
  `MODEL_LOAD`.

## Known limitations

- CM33 MicroPython heap is small (~38 KB free). Open the model file first, and
  stream it to flash in small chunks.
- Capture is windowed (about 2 s, non-overlapping). Audio during inference is not
  captured.
- `MODEL_LOAD` is only valid from the unloaded state, so hard-reset between runs.
- The `KWS=1` build needs `deps/libedge-impulse-sdk.a`. Without `KWS=1`, KWS PCM
  input is rejected (`EVT_ERROR`), which shows up as a timeout on CM33.

## License

MIT © 2026 Infineon Technologies AG. See [LICENSE](../../LICENSE). tflite-micro
is Apache-2.0; the Edge Impulse SDK and the KWS model are subject to their own
licenses.

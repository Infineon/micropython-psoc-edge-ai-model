# TFLM (TensorFlow Lite for Microcontrollers) Framework

A small, transport-agnostic framework that runs arbitrary `.tflite` models on the CM55 core of multi-core PSOC™ Edge devices (CM33 running MicroPython, CM55 running TFLM), with the model supplied at **runtime** from MicroPython instead of being compiled into the firmware.

## What it does

- Runs a generic **TFLM engine** on CM55: a command-driven state machine (load, describe, run, infer) that treats the model as bytes-in / bytes-out.
- Loads the model from the shared-data flash partition (XIP, `0x62000000`). If its weights fit in the free SOCMEM (about 2.5 MB), the adapter copies the model there at `MODEL_LOAD` so inference reads the weights from RAM instead of flash. Larger models keep running in place from flash.
- Keeps the engine **transport-agnostic**: it only depends on a small `transport_interface_t` vtable, so no transport-specific code lives in the engine. Currently only the IPC pipe is enabled and tested in this framework.
- Leaves all application logic (input quantization, output decoding, thresholds)
  to the MicroPython script on CM33. CM55 is a passive server.


## Prerequisites

To run this project you need:

- **Board:** PSOC™ Edge E84 AI Kit (`KIT_PSE84_AI`), connected to your computer with a USB cable on the KitProg3 port.
- **Python 3** with `pip`, to run the `mp-ifx-flash.py` flashing script and `mpremote` (`pip install requests mpremote`). `mp-ifx-flash.py` uses OpenOCD to flash the board and can install it for you.
- **A MicroPython tool to run scripts:** [Thonny](https://thonny.org/) or `mpremote`.

## Quick Start

Run a model on the KIT_PSE84_AI board in four steps, using prebuilt firmware. To build the firmware yourself instead, see [Build from source](#build-from-source).

Both firmware images are flashed with the `mp-ifx-flash.py` script from the [MicroPython PSOC™ Edge installation guide](https://ifx-micropython-psoc-edge.readthedocs.io/en/latest/psoc-edge/installation.html#installing-micropython).
It works on Windows, Linux and macOS. Connect the board over the KitProg3 USB port.

Set up the tools once:

```bash
mkdir mp-install && cd mp-install
curl -s -L https://raw.githubusercontent.com/infineon/micropython-psoc-edge/psoc-edge-main/ports/psoc-edge/tools/mp-ifx-flash.py > mp-ifx-flash.py
pip install requests mpremote
```
### 1. Flash the CM33 firmware (MicroPython)

The CM33 MicroPython firmware with TFLM support (`machine.IPC`,`machine.ExternalFlash` and `machine.PDM_PCM`) is built by the CI of the [`poc-tflm-model-execution`](https://github.com/Infineon/micropython-psoc-edge/tree/poc-tflm-model-execution) branch of micropython-psoc-edge.

1. Open the latest successful run of the [psoc-edge port](https://github.com/Infineon/micropython-psoc-edge/actions/workflows/ports_psoc-edge-ifx.yml) workflow on that branch(for    example [run 38884203266](https://github.com/Infineon/micropython-psoc-edge/actions/runs/38884203266)).
2. Download the `mpy-psoc-edge_KIT_PSE84_AI_<commit>` artifact from the **Artifacts** section at the bottom of the run page and unzip it.
3. Flash it with `mp-ifx-flash.py`:

```bash
# the artifact contains a firmware package (.zip)
python mp-ifx-flash.py from-package -b KIT_PSE84_AI -z firmware.zip

```
### 2. Flash the CM55 firmware (AI engine)

The CM55 firmware is published in the [Releases](https://github.com/Infineon/micropython-psoc-edge-ai-model/releases) of this repository as a firmware package: a `.zip` with the `.hex`, `.cfg` and `.FLM` files, like the CM33 one.

1. Download the latest firmware package `.zip` from the Releases page.
2. Flash it with `mp-ifx-flash.py`:

```bash
python mp-ifx-flash.py from-package -b KIT_PSE84_AI -z path/to/cm55-firmware.zip
```

Then **hard-reset the board** (press the reset button).

### 3. Get the model and write it to flash

1. Get a single-input, single-output `.tflite` model (int8 or float32). Use one of the example models in the [examples/tflm](https://github.com/Infineon/micropython-psoc-edge/tree/poc-tflm-model-execution/ports/psoc-edge/examples/tflm) folder of micropython-psoc-edge, or build or adapt your own with a platform such as [DEEPCRAFT™ Ready Models](https://developer.imagimob.com/deepcraft-ready-models).

2. Copy it to the MicroPython filesystem:

   ```bash
   mpremote cp my_model.tflite :
   ```

3. Write it into the external-flash model partition once. The flash contents persist across resets, so repeat this only when the model changes. It is advised to run it as a separate script.

   ```python
   import gc, os
   from machine import ExternalFlash

   MODEL_FILE = "my_model.tflite"
   MODEL_OFFSET = 0                                
   size = os.stat(MODEL_FILE)[6]

   flash = ExternalFlash()
   flash.init()
   sector = flash.sector_size()
   flash.erase(MODEL_OFFSET, ((size + sector - 1) // sector) * sector)

   with open(MODEL_FILE, "rb") as f:
       written = 0
       while True:
           chunk = f.read(1024)                     
           if not chunk:
               break
           flash.write(MODEL_OFFSET + written, chunk)
           written += len(chunk)
           gc.collect()
   print("wrote", written, "bytes")
   ```

> [!NOTE]
> The sine and "hey edge" example scripts write their own model, so for them you can skip this step and only copy the `.tflite` file.

### 4. Run the application on MicroPython

Hard-reset the board, then run an application script from the host. The example scripts are in the same [examples/tflm](https://github.com/Infineon/micropython-psoc-edge/tree/poc-tflm-model-execution/ports/psoc-edge/examples/tflm) folder; download the one you want first and run it using supported IDE's like Thonny.

The script boots CM55, sends `MODEL_LOAD`, `MODEL_INFO` and `MODEL_RUN`, streams input, and decodes the results.

**Hard-reset the board before every run.** CM55 keeps running across a CM33 soft reset, so a second `MODEL_LOAD` is rejected.

> [!TIP]
> **Out-of-memory error** (`MemoryError: memory allocation failed, allocating N bytes`)?
> The MicroPython heap is small (about 38 KB), and compiling a large `.py` script on the device needs more than is free. Precompile it to `.mpy` on the host instead:
>
> 1. Get `mpy-cross` from the same micropython-psoc-edge branch as your CM33 firmware (the `.mpy` format must match the firmware, otherwise the import fails with an "incompatible .mpy" error). From a clone of that branch:
>
>    ```bash
>    make -C mpy-cross        # builds mpy-cross/build/mpy-cross
>    ```
>
> 2. Compile the script. Add `-march=armv7emsp` if it uses `@micropython.viper` or `@micropython.native`:
>
>    ```bash
>    mpy-cross -march=armv7emsp my_app.py       # writes my_app.mpy
>    ```
>
> 3. Copy the `.mpy` to the device (with Thonny's file view or `mpremote`). Delete any `my_app.py` on the device.
>
> 4. Hard-reset the board and import the module instead of running the file. Type `import my_app` in the Thonny shell.


## Supported applications

The CM33 scripts and models are in the [examples/tflm](https://github.com/Infineon/micropython-psoc-edge/tree/poc-tflm-model-execution/ports/psoc-edge/examples/tflm) folder of micropython-psoc-edge.

| Application | CM33 script | Model
| --- | --- | --- |
| Sine wave regression (hello world) | `ports/psoc-edge/examples/tflm/sine_model_ipc.py` | `hello_world_int8.tflite` (also `hello_world_float.tflite`) |
| "hey edge" keyword spotting from the live PDM mic | `ports/psoc-edge/examples/tflm/hey_edge_mic_ipc.py` | `keyword_spotting_demo.tflite` |
| Cough detection from the live PDM mic | `dc_rm_cough_flash.py`, `dc_rm_cough.py` | `cough-rm-psoc-edge-m55-v2.0.tflite` (float32) |


## Wire protocol

Control messages carry a command and a 32-bit value. Bulk bytes travel through the shared rings, announced by a `DATA_AVAIL(len)` doorbell. 

IDs: \
CM33 = EP 1, client 3
CM55 = EP 2, client 5

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

## Build from source
This section is only needed if you want to build the CM33 or CM55 firmware yourself. To use the prebuilt firmware, follow the [Quick Start](#quick-start).

### Prerequisites

- GNU Arm Embedded toolchain (`arm-none-eabi-gcc`). The `tflm` framework only supports `TOOLCHAIN=GCC_ARM`; set `GCC_ARM_DIR` if it is not on `PATH`.
- `openocd` (ModusToolbox build) with KitProg3 access to the board.
- Git submodules: `git submodule update --init` (for `deps/psoc-edge`, or `make submodules` in `cm55_firmware`).
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

From micropython-psoc-edge branch - [poc-tflm-model-execution](https://github.com/Infineon/micropython-psoc-edge/tree/poc-tflm-model-execution/ports/psoc-edge)

```bash
make BOARD=KIT_PSE84_AI
make deploy BOARD=KIT_PSE84_AI
```

Flash CM33 first, then CM55.

### Build and flash the CM55 firmware

From micropython-psoc-edge-ai-model branch - [poc-runtime-model-loading](https://github.com/Infineon/micropython-psoc-edge-ai-model/tree/poc-runtime-model-loading)

```bash
make FRAMEWORK=tflm KWS=1            # sine + hey_edge KWS
make deploy FRAMEWORK=tflm KWS=1     # build (if needed) and flash via OpenOCD
```

Output: `build/Debug/tflm.elf`, `.hex`, `.bin`. Variables: `CONFIG=Debug|Release`, `BOARD=KIT_PSE84_AI`.

### Copy the models and scripts

Download the models and load them to the filesystem of the device. They are in the [examples/tflm](https://github.com/Infineon/micropython-psoc-edge/tree/poc-tflm-model-execution/ports/psoc-edge/examples/tflm) folder of the micropython-psoc-edge repository. See the file transfer section of [mpy-usage.rst](https://github.com/Infineon/micropython-psoc-edge/blob/poc-tflm-audio-ipc/docs/psoc-edge/mpy-usage.rst).

## Using the applications

**Hard-reset the board before each run.** CM55 keeps running across a CM33 soft reset, so a second `MODEL_LOAD` would be rejected with `BAD_STATE`.

### 1. Sine model 
```bash
mpremote run sine_model_ipc.py
```

#### What it does:

1. Writes `hello_world_int8.tflite` to external flash and verifies it.
2. Initializes IPC, registers client 3, boots CM55 and waits for `EVT_READY`.
3. Sends `MODEL_LOAD`, `MODEL_INFO` (prints input/output type, dims, scale,
   zero point) and `MODEL_RUN`.
4. Sweeps x over [0, 2π) in steps of 0.2. For each x it quantizes the value into the input tensor (`q = round(x / scale) + zero_point`, or a float32 for the float model), sends it as bulk data, sends `RUN_INFERENCE`, then dequantizes the 1-byte result into y ≈ sin(x).

#### Example output:

```text
x=1.6000  y=0.9949
x=4.6000  y=-1.0944
```

The loop runs forever. Reset the board to stop it, since Ctrl-C is not handled
while the script is running.

To use the float model, change `SOURCE_PATH` to `hello_world_float.tflite`. The
script picks 1 or 4 bytes per value from the tensor type reported by
`MODEL_INFO`.

### 2. Keyword Spotting

```bash
mpremote run hey_edge_mic_ipc.py
```

Requires the `KWS=1` CM55 image and a PDM microphone.

#### What it does:

1. Provisions `keyword_spotting_demo.tflite` into flash and boots CM55 as above.
2. Repeats for `RUN_SECONDS` (20 s): captures samples in chunks, streams them to CM55, sends `RUN_INFERENCE`, and reads back the result `[hey_edge, noise]`.
3. Dequantizes with the output scale and zero point from `MODEL_INFO`. If `p(hey_edge) >= 0.6` (`KWS_THRESHOLD`) it prints a detection with the round-trip latency and blinks the LED three times.
4. Prints a runtime summary (model size, load time, average inference latency, detection count).

#### Example output:

```text
[listening ~2 s] ...
  >>> 'hey edge' DETECTED [p(hey_edge)=<p> p(noise)=<p>]
Inference time : <n> ms
```

### 3. Cough detection

Detects coughs from the live PDM microphone with a float32 model(`cough-rm-psoc-edge-m55-v2.0.tflite`: input `[1, 30, 40]`, output `[1, 2]`).

1. Copy the model and `dc_rm_cough_flash.py` to the device, then write the model to flash once:

   ```bash
   mpremote cp cough-rm-psoc-edge-m55-v2.0.tflite :
   mpremote run dc_rm_cough_flash.py
   ```

2. Hard-reset the board. The application script is large, so precompile it to `.mpy` as described in the out-of-memory tip in [Quick Start](#4-run-the-application-on-micropython), copy it, and import it:

   ```bash
   mpy-cross -march=armv7emsp dc_rm_cough.py
   mpremote cp dc_rm_cough.mpy :
   mpremote exec "import dc_rm_cough"
   ```

#### What it does:

1. Initializes IPC, boots CM55, and sends `MODEL_LOAD`, `MODEL_INFO` and `MODEL_RUN`.
2. Reads the PDM microphone at 16 kHz (gain 5 dB, `MIC_GAIN_DB`) and computes the model's input on CM33: a log-mel front-end with 512-sample frames every 10 ms and 40 mel bands, giving a 30 x 40 float32 window (the last 300 ms of audio).
3. Sends each window to CM55 as bulk data and sends `RUN_INFERENCE`. Capture and inference overlap, so the microphone is read continuously.
4. Reads back `[no cough, cough]`. If the cough probability is at least `0.5` (`COUGH_THRESHOLD`) it prints a detection and turns USER_LED_B (P20_5) on for 150 ms.
5. After `RUN_SECONDS` (30 s) it prints a runtime summary.

#### Example output:

```text
  >>> COUGH DETECTED  out[0]=0.1594 out[1]=0.8406 (276 ms)

========== Runtime Summary ==========
Windows           : 102
Cough detections  : 5
Max cough prob    : 0.8744
Avg round trip    : 279 ms
Audio coverage    : 99 % (2999 frames, 30002 ms)
```

> [!NOTE]
> Other audio models that use a MEL pre-processing block should work with the same setup, once the front-end constants in the script (frame size, hop, number of mel bands, frequency range) match the model's training configuration. Models that need other pre-processing blocks are not supported yet and are future scope.

## Using your own model

1. Convert to a single-input, single-output `.tflite` (int8 or float32).
2. If the model's input is not the raw bytes your script will send, add a strong `tflm_adapter_preprocess()` override in its own source file (see `kws_frontend.cpp`) and add it to `FRAMEWORK_CXX_SRCS` in `tflm.mk`.
3. Copy the model to the device, write it to flash with `machine.ExternalFlash`, and drive the command sequence as in `sine_model_ipc.py`.
4. Use the scale and zero point from `MODEL_INFO` to quantize inputs and decode outputs on CM33.

## Known limitations

- CM33 MicroPython heap is small (~38 KB free). Open the model file first, and stream it to flash in small chunks.
- Capture is windowed (about 2 s, non-overlapping). Audio during inference is not captured.
- `MODEL_LOAD` is only valid from the unloaded state, so hard-reset between runs.

## License

MIT © 2026 Infineon Technologies AG. See [LICENSE](../../LICENSE). tflite-micro is Apache-2.0; the Edge Impulse SDK and the KWS model are subject to their own licenses.

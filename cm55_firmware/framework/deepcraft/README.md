# DEEPCRAFT™ Voice Assistant Framework

A single Python script that takes a [DEEPCRAFT™ Voice Assistant](https://deepcraft.infineon.com/solutions/voice-assistant)
model and gets it running on a **PSOC™ Edge KIT_PSE84_AI** board — firmware is
built and flashed in one command.

Build with `make FRAMEWORK=deepcraft` from `cm55_firmware/` (see the top-level
[README.md](../../../README.md) for the generic build commands); this document
covers the DEEPCRAFT-specific deployment tool, setup, and build/flash details.

---

## Setup

From the repository root, switch to the `tools/` folder where the deploy script and its config template live:

```powershell
cd tools
```

Set your LLVM toolchain path in `deepcraft-voice-assistant-model-deploy.ini`:

```ini
[tools]
llvm_dir = C:/llvm/LLVM-ET-Arm-19.1.5-Windows-x86_64
```

This avoids the interactive LLVM setup prompt. OpenOCD is downloaded automatically if not found.

---

## Deploy a model

```powershell
python deepcraft-voice-assistant-model-deploy.py \
    --config-file deepcraft-voice-assistant-model-deploy.ini \
    all path\to\your_model
```

`all` clones the firmware repo, builds with your model, and flashes to the board.
The model can be a folder or a `.zip` exported from the [DEEPCRAFT™ cloud tool](https://deepcraft.infineon.com/solutions/voice-assistant).

Other commands:

| Command | Description |
|---------|-------------|
| `build <MODEL>` | Build only (also clones/updates the repo) |
| `flash` | Flash the last built `.hex` |
| `clean` | Remove build artifacts for a clean rebuild |

### For primary users

You don't need a full checkout of this repo — just download the script itself:

```powershell
curl -s -L -o deepcraft-voice-assistant-model-deploy.py https://raw.githubusercontent.com/Infineon/micropython-psoc-edge-ai-model/main/tools/deepcraft-voice-assistant-model-deploy.py
```

Leave `repo_dir` blank in the config file (the default). The tool then manages its
own private copy of the firmware — cloning it under `va-mpy/` next to the script
on first run and updating it on later runs — so your model builds against a
known-good checkout regardless of what else is on disk.

Once `build` or `all` has finished, flash the board (`all` already does this for you):

```powershell
python deepcraft-voice-assistant-model-deploy.py all <model-name>
```

### For secondary users (firmware / script contributors)

Clone the full repo instead of just the script, so you have `cm55_firmware/` and
`tools/` to edit:

```sh
git clone --recurse-submodules https://github.com/Infineon/micropython-psoc-edge-ai-model.git

cd micropython-psoc-edge-ai-model/tools
```

Then point the tool at this checkout instead of letting it manage a separate copy,
so your local changes are picked up immediately without pushing or re-cloning:

```ini
[project]
repo_dir = ..
```

Or, without touching the config file:

```powershell
python deepcraft-voice-assistant-model-deploy.py \
    --repo-dir .. \
    --config-file deepcraft-voice-assistant-model-deploy.ini \
    build test_gpio_control
```

`--repo-dir` skips the clone/update step entirely and builds `cm55_firmware/` as it
sits in your working tree, so re-running `build` after an edit picks it up right away.

---

## Build Firmware

Choose one of the following workflows.

### Option 1: Clone On The Host

Use this workflow when Git is installed on the host. Clone the firmware and
open its root directory before running Docker.

```sh
git clone --recurse-submodules https://github.com/Infineon/micropython-psoc-edge-ai-model.git

cd micropython-psoc-edge-ai-model
```

From this directory, which contains `cm55_firmware/`, run the following
commands. Docker mounts the checkout at `/workspace`; `-w` selects the
project's build directory inside it.

WSL with docker installed:

```sh
docker run --rm \
	-v "$PWD":/workspace \
	-w /workspace/cm55_firmware \
	ifxmakers/psoc-embedded-ai-toolchain:toolchains-0.1.0 \
	make CONFIG=Debug FRAMEWORK=deepcraft
```

### Option 2: Clone Inside Docker

```sh
mkdir firmware-build

docker run --rm \
	-v "$PWD/firmware-build":/workspace \
	--entrypoint sh \
	ifxmakers/psoc-embedded-ai-toolchain:toolchains-0.1.0 \
	-c 'git clone --recurse-submodules https://github.com/Infineon/micropython-psoc-edge-ai-model.git . &&
	     cd cm55_firmware &&
	     make CONFIG=Debug FRAMEWORK=deepcraft'
```

The checkout and build outputs remain in `firmware-build` after the container
exits.

## Flash Firmware

The image includes Infineon's OpenOCD fork, which supports the KitProg3
interface and PSE84 target configs.

Flashing requires USB access to the board's debug probe, which build commands
do not need. On Linux, pass the USB bus through explicitly:

```sh
docker run --rm \
	--device=/dev/bus/usb \
	-v "$PWD":/workspace \
	-w /workspace/cm55_firmware \
	ifxmakers/psoc-embedded-ai-toolchain:toolchains-0.1.0 \
	make CONFIG=Debug FRAMEWORK=deepcraft flash
```

On WSL, attach the debug probe to the WSL distribution first using
[usbipd-win](https://github.com/dorssel/usbipd-win) before running the command
above. Direct USB passthrough to Windows Docker Desktop containers is not
generally supported.

---

## Documentation

| Guide | Contents |
|-------|----------|
| [Quick Start](https://mpy-va-deploy.readthedocs.io/en/latest/enablement_cm33_mpy_ipc.html#quickstart) | CM33 MicroPython over IPC — requirements, steps, example code |
| [Deployment Tool](https://mpy-va-deploy.readthedocs.io/en/latest/deployment_tool.html) | Installation, commands, options, config file |
| [Requirements](https://mpy-va-deploy.readthedocs.io/en/latest/requirements.html) | Hardware, toolchain, and software prerequisites |
| [MicroPython Interface](https://github.com/Infineon/micropython-deepcraft-model-interface) | CM33-side MicroPython API |

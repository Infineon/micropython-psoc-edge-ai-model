# MicroPython PSOC™ Edge AI Model

This repo hosts dual core based deployment enablements for running embedded AI models on **PSOC™ Edge** boards, built around `cm55_firmware`'s pluggable framework system (`make FRAMEWORK=<name>`).

## Supported Frameworks

| Framework | What it runs | Docs |
|---|---|---|
| `deepcraft` | [DEEPCRAFT™ Voice Assistant](https://deepcraft.infineon.com/solutions/voice-assistant) wake-word + command models | [cm55_firmware/framework/deepcraft/README.md](cm55_firmware/framework/deepcraft/README.md) |
| `tflm` | Arbitrary `.tflite` models loaded at runtime from external flash | [cm55_firmware/framework/tflm/README.md](cm55_firmware/framework/tflm/README.md) |

Each framework lives under `cm55_firmware/framework/<name>/` and is built with `make FRAMEWORK=<name>`; see its own README for setup, deployment, and usage specifics.

---

## Build Firmware

```sh
git clone --recurse-submodules https://github.com/Infineon/micropython-psoc-edge-ai-model.git
cd micropython-psoc-edge-ai-model/cm55_firmware

make submodules                    # one-time: fetch deps/psoc-edge
make FRAMEWORK=<name>              # build (CONFIG=Debug|Release, default Debug)
make FRAMEWORK=<name> deploy       # build (if needed) and flash via OpenOCD
make FRAMEWORK=<name> clean        # remove build artifacts
make help                          # list available frameworks
```

Or in Docker, without installing the toolchain locally:

```sh
docker run --rm \
	-v "$PWD":/workspace \
	-w /workspace/cm55_firmware \
	ifxmakers/psoc-embedded-ai-toolchain:toolchains-0.1.0 \
	make CONFIG=Debug FRAMEWORK=<name>
```

Flashing needs USB access to the board's debug probe (`--device=/dev/bus/usb` on
Linux/WSL; see [usbipd-win](https://github.com/dorssel/usbipd-win) to attach the
probe to WSL). Build-only steps do not need it.

Some frameworks have extra prerequisites or build variables (e.g. `tflm` requires
`TOOLCHAIN=GCC_ARM`) — check that framework's own README, linked above.

---

## Adding a New Framework

1. Create `cm55_firmware/framework/<name>/` with your CM55 entry point
   (`main.c`) and a `<name>.mk` (see `tflm.mk` if your
   framework needs non-default build rules, e.g. a fixed toolchain).
2. Build it with `make FRAMEWORK=<name>` from `cm55_firmware/` — no changes to
   the top-level `Makefile` are needed; it discovers frameworks by directory.
3. Document the framework in `cm55_firmware/framework/<name>/README.md`
   (prerequisites, build/flash commands, wire protocol or usage notes).
4. Add a row to the [Supported Frameworks](#supported-frameworks) table above.

---

## Documentation

See each framework's own README (linked above) for detailed guides.


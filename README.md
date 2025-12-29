# NT Flash Tool

A unified command-line tool for flashing disting NT firmware. Replaces the need for Python/SPSDK installation.

## Features

- Flash from local ZIP firmware packages
- Download and flash specific firmware versions
- Download and flash the latest firmware
- Cross-platform: Linux, macOS, Windows

## Building

### Prerequisites

**Linux:**
```bash
sudo apt-get install libudev-dev
```

**macOS:**
- Xcode Command Line Tools: `xcode-select --install`

**Windows (MinGW-w64):**
- Install MinGW-w64 from https://www.mingw-w64.org/
- Ensure `gcc` and `make` are in your PATH

### Build

```bash
# Clone with submodules
git clone --recursive https://github.com/thorinside/nt-flash.git
cd nt-flash

# Build
make

# Or to build on Windows with MinGW
mingw32-make
```

The build produces a single binary: `nt-flash` (or `nt-flash.exe` on Windows).

### Windows Driver Setup (One-time)

On Windows, you need to install the WinUSB driver for the disting NT:

1. Put disting NT in bootloader mode (Menu > Misc > Enter bootloader mode...)
2. Download and run [Zadig](https://zadig.akeo.ie/)
3. Select "NXP SEMICONDUCTORS NXP USB..." from the device list
4. Select "WinUSB" as the driver
5. Click "Install Driver"

## Usage

### Put disting NT in bootloader mode first

Before flashing, enable bootloader mode on the disting NT:
```
Menu > Misc > Enter bootloader mode...
```

### Flash from local file

```bash
nt-flash /path/to/distingNT_1.12.0.zip
```

### Download and flash specific version

```bash
nt-flash --version 1.12.0
```

### Download and flash latest

```bash
nt-flash --latest
```

### Download from custom URL

```bash
nt-flash --url https://example.com/firmware.zip
```

### List available versions

```bash
nt-flash --list
```

### Options

| Option | Description |
|--------|-------------|
| `-v, --verbose` | Show detailed output |
| `-n, --dry-run` | Validate without flashing |
| `-h, --help` | Show help |

## Firmware Package Format

The tool expects ZIP files containing:
```
distingNT_X.Y.Z.zip
├── MANIFEST.json
├── bootable_images/
│   ├── unsigned_MIMXRT1060_flashloader.bin
│   └── disting_NT.bin
└── ...
```

Official firmware packages from [Expert Sleepers](https://www.expert-sleepers.co.uk/distingNTfirmwareupdates.html) are fully supported.

## How It Works

The flash process involves two stages:

1. **SDP Mode** (USB 0x1FC9:0x0135): Upload flashloader to RAM and execute it
2. **Bootloader Mode** (USB 0x15A2:0x0073): Configure flash, erase, write firmware, reset

## Troubleshooting

### Device not found

- Ensure disting NT is in bootloader mode
- On Linux, you may need udev rules. Create `/etc/udev/rules.d/99-disting.rules`:
  ```
  SUBSYSTEM=="usb", ATTR{idVendor}=="1fc9", ATTR{idProduct}=="0135", MODE="0666"
  SUBSYSTEM=="usb", ATTR{idVendor}=="15a2", ATTR{idProduct}=="0073", MODE="0666"
  ```
  Then: `sudo udevadm control --reload-rules`

### Permission denied (Linux)

Run with sudo or set up udev rules as above.

### Windows: Device not recognized

Install the WinUSB driver using Zadig (see Windows Driver Setup above).

## License

This tool incorporates:
- BLFWK from NXP (BSD-3-Clause)
- miniz (Public Domain)
- cJSON (MIT)

## Credits

- [Expert Sleepers](https://www.expert-sleepers.co.uk/) for the disting NT
- [NXP Semiconductors](https://www.nxp.com/) for BLFWK/SPSDK
- [apexrtos/nxp_blhost_sdphost](https://github.com/apexrtos/nxp_blhost_sdphost) for the portable build setup

# NT Flash Tool - Machine-Readable Output Format

This document describes the machine-readable output format used when running `nt-flash` with the `--machine` flag. This format is designed for integration with other tools like NT Helper.

## Usage

```bash
nt-flash --machine <firmware.zip>
nt-flash --machine --version 1.12.0
```

## Output Format

All machine-readable output is sent to stdout, one message per line. Each line follows one of these formats:

### STATUS Messages

```
STATUS:<STAGE>:<PERCENT>:<MESSAGE>
```

- **STAGE**: A unique identifier for the current operation stage
- **PERCENT**: Overall progress percentage (0-100)
- **MESSAGE**: Human-readable description of the current stage

### PROGRESS Messages

```
PROGRESS:<STAGE>:<PERCENT>:<MESSAGE>
```

Used for granular progress updates within a stage (e.g., during firmware write).

### ERROR Messages

```
ERROR:<MESSAGE>
```

Indicates a fatal error. The process will exit with a non-zero code.

## Stage Identifiers

| Stage | Percent | Description |
|-------|---------|-------------|
| `DOWNLOAD` | 0 | Downloading firmware from URL |
| `LOAD` | 0 | Loading firmware package from ZIP |
| `START` | 0 | Flash process starting |
| `SDP_CONNECT` | 5 | Connecting to SDP bootloader (ROM) |
| `BL_CHECK` | 10 | Checking if already in flashloader mode |
| `BL_FOUND` | 15 | Device already in flashloader mode |
| `SDP_UPLOAD` | 15 | Uploading flashloader to RAM |
| `SDP_JUMP` | 25 | Jumping to flashloader |
| `WAIT_ENUM` | 30 | Waiting for device re-enumeration |
| `BL_CONNECT` | 40 | Connecting to flashloader |
| `CONFIGURE` | 50 | Configuring flash memory |
| `ERASE` | 55 | Erasing flash region |
| `FCB` | 60 | Creating Flash Configuration Block |
| `WRITE` | 65 | Writing firmware (PROGRESS messages follow) |
| `RESET` | 95 | Resetting device |
| `COMPLETE` | 100 | Flash complete |

## Example Output

Successful flash:
```
STATUS:LOAD:0:Loading firmware package
STATUS:START:0:Starting disting NT flash
STATUS:SDP_CONNECT:5:Connecting to SDP bootloader
STATUS:SDP_UPLOAD:15:Uploading flashloader to RAM
PROGRESS:SDP_UPLOAD:25:Segment 1/4
PROGRESS:SDP_UPLOAD:50:Segment 2/4
PROGRESS:SDP_UPLOAD:75:Segment 3/4
PROGRESS:SDP_UPLOAD:100:Segment 4/4
STATUS:SDP_JUMP:25:Starting flashloader
STATUS:WAIT_ENUM:30:Waiting for flashloader to start
STATUS:BL_CONNECT:40:Connecting to flashloader
STATUS:CONFIGURE:50:Configuring flash memory
STATUS:ERASE:55:Erasing flash region
STATUS:FCB:60:Creating Flash Configuration Block
STATUS:WRITE:65:Writing firmware
PROGRESS:WRITE:10:Segment 1/10
PROGRESS:WRITE:20:Segment 2/10
...
PROGRESS:WRITE:100:Segment 10/10
STATUS:RESET:95:Resetting device
STATUS:COMPLETE:100:Flash complete
```

Error case:
```
STATUS:LOAD:0:Loading firmware package
STATUS:START:0:Starting disting NT flash
STATUS:SDP_CONNECT:5:Connecting to SDP bootloader
ERROR:Device not found in SDP mode or flashloader mode
```

## Exit Codes

- **0**: Success
- **1**: Error (see ERROR message for details)

## Parsing Notes

1. Lines can be parsed by splitting on `:` with a limit of 4 parts
2. The percent value is always an integer
3. Messages may contain colons but the first three fields never do
4. All output is line-buffered (flushed after each line)
5. When `--machine` is enabled, only machine-readable output is produced (no human-readable messages)

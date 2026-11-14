# Chapter 9: Flashing and monitoring

This chapter covers `nav upload`, which puts a built image on a board, and `nav monitor`, which reads the board's serial output. Chapter 4 named the `runner` and `probe` modules; chapter 5 documented the `[upload]` table. `nav upload` installs a missing flash tool itself (see "A real run" below); chapter 4 describes the provisioning modules, and chapter 8 covers only the compiler check that `nav build` makes.

This chapter was written from the source and the unit tests, not from flashing a board. No hardware was touched and no serial port was opened.

## What `nav upload` flashes

`nav upload` refuses a library project, checks the board is in the catalog, and looks for the firmware ELF. That is `build/<project name>`, or `build/<variant>/<output name>` with `--variant`. If that file does not exist but the same name with `.elf` appended does, the `.elf` file is used. A missing file stops the command with "No firmware at <path>. Run 'nav build' first." (with `--variant`, the hint reads "Run 'nav build --variant <name>' first."). `--dry-run` skips this check.

What gets written to the chip depends on the runner's `image` field:

| `image` | Step before the flash | File flashed |
|---|---|---|
| `elf` | none | the ELF itself |
| `bin` | `<prefix>-objcopy -O binary <elf> <bin>` | `.bin` next to the ELF |
| `hex` | `<prefix>-objcopy -O ihex -R .eeprom <elf> <hex>` | `.hex` next to the ELF |

The objcopy name follows the board's compiler (`arm-none-eabi-gcc` gives `arm-none-eabi-objcopy`, `avr-gcc` gives `avr-objcopy`, anything else plain `objcopy`). An image name replaces a trailing `.elf`, otherwise it is appended. Any other value is an error.

## Runners

A runner is a tool, an image type, an argument template and optional `defaults`. The baked `data/boards.json` defines three:

| Runner | Tool | Image | Command it builds |
|---|---|---|---|
| `st-flash` | `st-flash` | bin | `st-flash [--serial S] [--connect-under-reset] write {bin} {address}` |
| `openocd` | `openocd` | elf | `openocd -f {interface} [probe -c groups] -f {target} [reset_config group] -c 'program "{elf}" verify reset exit'` |
| `avrdude` | `avrdude` | hex | `avrdude -p {part} -c {programmer} -P {port} -b {baud} -D -U flash:w:{hex}:i` |

Defaults: `openocd` interface `interface/stlink.cfg`; `avrdude` programmer `arduino`, baud `115200`.

**Which openocd definition is authoritative.** The README's openocd snippet omits the conditional probe groups. The baked `data/boards.json`, compiled into the binary, has them, and it is what the code runs. Treat the README snippet as simplified.

### Template variables

`plan_flash` fills a variable map, lowest precedence first: the runner's `defaults`; the board's `runners.<runner>` entry; the built-ins `elf`, `bin`, `hex` and `address` (the board's `flash_address`, only when non-empty); then command-line values. `--port`/`-p` sets `port`, and `--set name=value` sets any variable. Probe selection adds `probe_serial`, `probe_usb_path` and `connect_under_reset` (set to `1`) to the same map.

An argument is a plain string or a group `{"if": "a|b", "args": [...]}`. A group is kept when any of the `|`-separated names has a non-empty value; an empty `if` always holds. `{name}` placeholders are replaced inside every kept string. A placeholder with no value is an error: "runner '<id>' needs a value for <names> on board '<id>'. Pass --set <name>=<value>, or add it under boards.<id>.runners.<runner> in ~/.nav/boards.json". The README lists `{port}` as built-in; in the code it is only filled by `--port` or by auto-detection (below).

### Stale user runners and the "predates probe support" note

A user `~/.nav/boards.json` overlays the baked file. If a user runner redefines `args` without every conditional group the baked runner has, nav keeps the built-in args and prints this on stderr:

`nav: note: runner '<id>' in boards.json predates probe support (its args lack the conditional probe groups); using the built-in args. Delete the runner entry from boards.json, or add the groups, to silence this.`

The comparison is by exact `if` string, so a replacement `openocd` needs all four groups. `avrdude` has none, so it never triggers the note.

## Choosing the runner

`--runner`/`-r` names one. Otherwise `default_runner` takes the board's `runner`, then its `flash_tool`, then the first entry of its `runners`. `nav upload --list` prints the board's runners, default first, with tool and image type. An unknown name gives "unknown runner '<name>' (known: ...)".

## Choosing a probe

nav never picks a probe for you. The request comes from the first non-empty of `--probe`, the `NAV_PROBE` environment variable, then `[upload] probe` in `nav.toml` (`choose_probe_request`; a test checks the order). A value like `3-2` or `1-3.3` is read as a USB path, anything else as a serial; `usb:<path>` and `serial:<sn>` force the reading.

With a request on Linux, nav reads `/sys/bus/usb/devices` (the `NAV_USB_SYSFS` variable points it elsewhere, as the tests do), keeps vendor `0483` devices with an ST-Link product id, and sorts them by USB path. It then:

- fails if nothing matches ("no ST-Link is attached at ..." or "no ST-Link has serial ..."), listing what is attached;
- fails if several attached probes share the serial you gave;
- fills `probe_serial` or `probe_usb_path`, whichever the template uses. A USB path given to `st-flash` becomes its serial if unique, else is refused with a hint to use `--runner openocd`.

With no request, one probe or none changes nothing. More than one is an error: "N ST-Links are attached and no probe was chosen; refusing to pick one." followed by the list and "Choose one with --probe <usb-path|serial>, NAV_PROBE, or [upload] probe in nav.toml ...". A runner that cannot take a probe, such as `avrdude`, is not blocked by attached ST-Links, but a request given to it is an error.

Off Linux the request is passed to the runner unchecked, and `--list-probes` prints "--list-probes is not supported on this platform (it reads Linux sysfs). ..." and fails. `--list-probes` needs no project. `--set probe_serial=...` counts as a choice and bypasses resolution.

`--connect-under-reset` or `[upload] connect_under_reset = true` sets `connect_under_reset=1`: `st-flash` gets `--connect-under-reset`, `openocd` gets `reset_config srst_only connect_assert_srst`. Any other runner stops with "Cannot upload: runner '<id>' has no connect-under-reset form ...".

## Flags and `--dry-run`

Every flag `nav upload` parses:

| Flag | Effect |
|---|---|
| `--runner`, `-r <name>` | pick a runner |
| `--port`, `-p <dev>` | sets `port` |
| `--set <name>=<value>` | sets any template variable; repeatable |
| `--probe <serial\|usb-path>` | choose a probe |
| `--connect-under-reset` | see above |
| `--variant <name>` | flash a variant's image |
| `--dry-run`, `-n` | print the plan and stop |
| `--list` | list the board's runners |
| `--list-probes` | list attached ST-Links |

Anything else, including a value-taking flag with no value, prints the usage line.

`--dry-run` prints `Runner: <id>` and then one line per step, `Converting: <argv>` and `Flashing: <argv>`, quoting any argument with a space or `"` in single quotes. It runs nothing and skips the firmware check, the tool check and the reset. It still needs a project, still resolves probes, and skips serial-port auto-detect, so an `avrdude` dry run without `--port` reports the missing `port`.

A real run first looks for the runner's tool and, if it is missing, installs it: "'<tool>' not found — installing it.", and on failure "Could not install '<tool>'. Install it yourself, or pick another runner (--list)." and exit 1 (`UploadCommand::run`, `install_tools`). This is separate from `nav build`, which checks only the board's compiler (chapter 8). Then it runs each step. A failed flash prints "'<runner>' failed (exit N). Check the probe/cable and board power; see its output above." and returns the tool's exit code.

For a runner that uses `{port}` with no `--port`, nav lists serial ports with the monitor's scanner. One port is used; none gives "No serial port found for the board. Connect it, or pass --port <dev>."; several give a list and ask for `--port`.

## Reset after flash

A board may declare `reset_after_flash` and `reset_command`. After a successful flash, and only when the runner used is the board's default, nav runs the reset silently. On the STM32 boards that is `st-flash reset`. If a probe was chosen, nav inserts `--serial <sn>` into the reset when it knows a unique serial; when it does not, it skips the reset with a warning to press RESET. A failing reset only warns. The code comment says some flashers leave the core halted.

## Boards

From the baked `data/boards.json`:

| Board | Default runner | Flash tool | Flash address | Other runners | Reset after flash |
|---|---|---|---|---|---|
| `nucleo_f401re` | `st-flash` | `st-flash` | `0x08000000` | `openocd` (`target/stm32f4x.cfg`) | yes |
| `navixsmf401re` | `st-flash` | `st-flash` | `0x08000000` | `openocd` (`target/stm32f4x.cfg`) | yes |
| `navixdev` | `st-flash` | `st-flash` | `0x08000000` | `openocd` (`target/stm32f4x.cfg`) | yes |
| `atmega328p` | `avrdude` | `avrdude` | empty (unused by avrdude) | none; `part` is `m328p` | no |

## `nav monitor`

`nav monitor` takes `--port`/`-p` and `--baud`/`-b` and silently ignores other arguments. The default baud is 9600. Only 9600, 19200, 38400, 57600 and 115200 are accepted; any other value warns "Unknown baud requested (<value>). Falling back to 9600." although the status line still prints the requested value.

Without `--port` it scans. One candidate is used; none gives "Auto-detection failed. No hardware detected. Specify manually with --port <path>."; several give a list and ask for `--port`.

On Linux and other POSIX systems the scanner takes names in `/dev` that contain `ttyACM` or `ttyUSB`, sorted. On Windows it tests `COM1` to `COM255` with `QueryDosDevice` and keeps those the system defines. Windows opens it with a `\\.\` prefix so `COM10` and above work. The tests exercise only the POSIX scanner.

The port is opened 8 data bits, no parity, one stop bit, no flow control. Incoming bytes are copied to stdout as they arrive, and Ctrl+C detaches ("Link detached gracefully. Channel closed."). The code only reads; it forwards no keyboard input. An open failure reports "Kernel Access Denied! Permission denied or device already locked." The Windows branch was not exercised.

## Sources

- `src/core/commands/upload.cpp`: `UploadCommand::run`, `list_probes`, `join`.
- `src/core/commands/monitor.cpp`: `MonitorCommand::run`.
- `src/core/runner.cpp`, `include/nav/core/runner.hpp`: `plan_flash`, `default_runner`, `board_runner_names`, `runner_uses`, `objcopy_for`, `expand_placeholders`, `when_holds`.
- `src/core/probe.cpp`, `include/nav/core/probe.hpp`: `choose_probe_request`, `resolve_probe`, `find_stlink_probes`, `describe_probes`, `probe_discovery_supported`.
- `src/core/serial.cpp`, `include/nav/core/serial.hpp`: `find_serial_ports`.
- `src/core/registry.cpp`: `load_runners`, `registry`.
- `data/boards.json`: `runners` and `boards`.
- `README.md`: "Flash runners", "Probe selection".
- `src/cli/main.cpp`, `include/nav/core/command.hpp`: verb registration and help text.
- `tests/test_runner.cpp`, `tests/test_probe.cpp`, `tests/test_serial.cpp`.

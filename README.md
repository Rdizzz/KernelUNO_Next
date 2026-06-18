# KernelUNO v1.6 NEXT

A Unix-like shell and RAM filesystem for Arduino UNO R3 and other microcontrollers. Write files, control GPIO pins, read sensors, and run simple scripts all from the serial terminal.

<img width="769" height="659" alt="554" src="https://github.com/Rdizzz/KernelUNO_Next/blob/main/1.png"/>


# Commands (44):

- `ls`
- `cd`
- `pwd`
- `mkdir`
- `touch`
- `cat`
- `echo`
- `rm`
- `info`
- `cp`
- `mv`
- `wc`
- `ed`
- `pinmode`
- `write`
- `read`
- `aread`
- `pwm`
- `blink`
- `gpio`
- `tone`
- `notone`
- `sh`
- `uptime`
- `uname`
- `dmesg`
- `df`
- `free`
- `whoami`
- `clear`
- `reboot`
- `mount`
- `sync`
- `reset-fs`
- `build-info`
- `fastfetch`
- `sleep`
- `calc`
- `rand`
- `ascii`
- `hex`
- `repeat`
- `guess`
- `help`

## How It Works

The core of KernelUNO_Next is a lightweight virtual filesystem (VFS) and command interpreter running directly on the microcontrollers:

### Supported microconrollers

Arduino UNO R3 - FULL SUPPORT!

Arduino UNO R4 - FULL SUPPORT!

Arduino Due - SUPPORTED but partially

Arduino GIGA R1 - SUPPORTED but partially

Adafruit Metro M0 Express - SUPPORTED but partially

Adafruit Feather 328p - FULL SUPPORTED!

ESP8266 - nope :(

ESP32 family - sorry but nope its broken :(


### Virtual Filesystem (RAM & EEPROM)
- **Capacity:** Maximum of **8** files/directories simultaneously in RAM.
- **Limits:** Up to **24 bytes** per file content and **12 character** filenames.
- **Default Structure:** Automatic `/home` and `/dev` directories created on boot.
- **GPIO Mapping:** `/dev/pin2`, `/dev/pin3`, `/dev/pin4` act as special virtual files. Reading/writing to them directly interacts with the physical hardware pins.
- **Persistence:** Optional EEPROM support. Using the `sync` command (or during a `reboot`), the RAM filesystem is serialized and saved to EEPROM, allowing data to survive power cycles.

###  System Architecture
- **Kernel Log (`dmesg`):** A circular buffer storing the last 4 system events with timestamps for debugging.
- **Shell Scripting (`sh`):** A basic interpreter that can read virtual files and execute commands sequentially (using `;` as a separator).
- **Memory Optimization:** Heavy use of `PROGMEM` (via `F()` macro and `PSTR`) to store command strings and UI text in Flash memory, preserving the limited 2KB SRAM for the filesystem and runtime operations.

### Hardware Interaction
GPIO control uses standard Arduino functions (`pinMode()`, `digitalWrite()`, `digitalRead()`, `analogWrite()`, and `tone()`) mapped to shell commands. Unsupported hardware features (like EEPROM on ARM boards or `tone()` on Due) are automatically disabled at compile-time using preprocessor directives.

### Input Processing
Input is buffered from the Serial connection (UART) and parsed line-by-line. The command parser is case-insensitive and supports basic argument splitting.

## Planned Features

-  I Dont Know What Need

## License

BSD3 - Original by [Arc1011](https://github.com/Arc1011/KernelUNO)

this is a fork https://github.com/Arc1011/KernelUNO but very much better and i grabbed coconix

# uEASE on RP2040

Reimplements Lapis uEASE hardware debugger (firmware v3.21) on Raspberry Pi RP2040, to work with the official toolchain.

## Build

Build with Raspberry Pi Pico VS Code extension.

## Usage

The pin mapping is defined in [hardware.h](./hardware.h). Default configuration is as following:

| uEASE terminal | GPIO index              |
| -------------- | ----------------------- |
| VTref          | 0 (Not yet implemented) |
| Vpp            | 1                       |
| RESET_N        | 2                       |
| TEST           | 3                       |
| Vss            | Any GND pin             |

Refer to the official document on how to connect to the target MCU.

Since the RP2040 hardware can't generate 8v Vpp supply for flash programming, you might need to reimplement the `EnableVPP` and `DisableVPP` functions in [hardware.h](./hardware.h) to match your own hardware. These are by default implemented to operate digital output on the Vpp pin directly.

Certain configurations could be modified in [uEASE.c](./uEASE.c). If you choose to enable `FLASH_ENABLE_TEST_AREA_WRITE`, make sure to change property of the test area in your TRG file to `SC` or `SCT` (it's by default set to `TEST`). Do this only if you know what you're doing.

Note that the limit of general purpose hardware breakpoint count is hard-coded to 1 in DTU8. Apply the following patches to Dtu8.exe(version 7.2.30) to set the limit to 2, which matches the actual hardware limit in most cases.

| File offset | patch    |
| ----------- | -------- |
| 0005B215    | 01 -> 02 |
| 0005BCDC    | 01 -> 02 |
| 00090BF7    | 01 -> 02 |
| 002FC0BF    | 01 -> 02 |
| 0030F3C5    | 01 -> 02 |

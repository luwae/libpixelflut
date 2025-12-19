# libpixelflut

This is a convenience layer for pixelflut clients.
It consists of one header, `pixelflut.h`, and the associated source file.

## Quick Example
See `minimal.c` in this folder.

## Features
- support for all pixelflut commands (except `HELP`)
  - size: `SIZE`
  - get pixel: `PX <x> <y>`
  - put pixel: `PX <x> <y> <rrggbb(aa)>`
- useful error messages
- optional write and read buffering, and batched use of commands

## Building
TODO

## License
TODO

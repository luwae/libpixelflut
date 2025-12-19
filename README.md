# libpixelflut

This is a convenience layer for pixelflut clients.
It consists of one header, `pixelflut.h`, and the associated source file.

## Quick Example
```c
struct pf_conn conn;
pf_connect_raw(ADDRESS, PORT, &conn));
uint16_t width, height;
pf_get_size(&conn, &width, &height);
printf("got SIZE %d %d\n", width, height);

struct pixel px = { 0 };
px.x = 10;
px.y = 20;
// read pixel and invert its color
pf_get(&conn, &px);
px.r = ~px.r;
px.g = ~px.g;
px.b = ~px.b;
pf_put(&conn, &px);
```

See `minimal.c` for a full example.

You are strongly encouraged to do error handling, which is not included here.
Every function returns a `pf_result`. See `pixelflut.h` for more.

## Features
- support for all pixelflut commands (except `HELP`)
  - size: `SIZE`
  - get pixel: `PX <x> <y>`
  - put pixel: `PX <x> <y> <rrggbb(aa)>`
- useful error messages
- optional write and read buffering, and batched use of commands

## Building
Use `make`.

## License
MIT. See `LICENSE`

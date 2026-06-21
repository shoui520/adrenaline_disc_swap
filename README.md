# Adrenaline Disc Swap

PSP multi-disc ISO/CSO switcher for PS Vita Adrenaline / pspemu using the
Inferno/UMD9660 ISO driver path.

This is a standalone source tree. It does not build or link against the
original `disc_change` 2.6 source, objects, PRX, INI parser, renderer, alarm
code, or CSO code.

## Dependencies

- pspdev / PSPSDK toolchain
- PSPSDK CFW headers and stubs installed in the SDK tree:
  - `systemctrl.h`
  - `systemctrl_se.h`
  - `libpspsystemctrl_kernel`
  - `libpspsystemctrl_user`
- PSPSDK `libtinyfont` for the 8x8 menu font
- Runtime: 6.61 Adrenaline with the Inferno/UMD9660 ISO driver loaded

The plugin cannot be official-SDK-only at runtime. Switching the mounted UMD
image requires CFW SystemCtrl calls and Adrenaline/Inferno driver internals.

## Build

```sh
make clean
make
```

Output:

```text
adrenaline_disc_swap.prx
```

The default build includes the remount fix needed by games that validate
`disc0:` metadata after a disc swap.

Optional diagnostic build:

```sh
make clean
make DIAG=1
```

Optional direct `disc0:` remount build:

```sh
make clean
make DIRECT_REMOUNT=1
```

## Controls

- Open menu: `SELECT + L + R`
- Select disc: `UP/DOWN`
- Page: `L/R`
- Confirm: `O`
- Cancel: `X` or `SELECT`

## Notes

- Designed for CSO/ISO files in the active Adrenaline Memory Stick ISO folder.
- The menu filters sibling `Disc N` filenames when possible.
- ABM bubbles may not use the same ISO driver setting as Adrenaline itself; test
  from the Adrenaline PSP XMB when diagnosing driver behavior.

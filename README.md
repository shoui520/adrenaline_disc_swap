# Adrenaline Disc Swap

PSP multi-disc ISO/CSO switcher for PS Vita Adrenaline / pspemu using the
INFERNO/UMD9660 ISO driver path.

Important: Adrenaline Bubble Manager bubbles do not properly use the INFERNO driver (tested with CSO games). To use this disc swapper, you need to launch the game in the PSP XMB using Adrenaline or patch your bubbles with [my ABM fork](https://github.com/shoui520/AdrenalineBubbleManager/releases).
## Install

Download the latest [release](https://github.com/shoui520/adrenaline_disc_swap/releases) (.prx)  

Put the .prx file in `ux0:/pspemu/seplugins/`.  
Edit the file (create it if it does not exist) `ux0:/pspemu/seplugins/game.txt`, and add this line:
```
ms0:/seplugins/adrenaline_disc_swap.prx 1
```

Tip: You may want [mschange](https://github.com/YuriSizuku/psp-MSChange) to emulate removing the Memory Stick, as some multi-disc games also require you to do this which is not possible on the Vita.  

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

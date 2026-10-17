# Dynamic injector for firmware 13.60

Stream8 uses [NineS](https://github.com/buzzer-re/NineS) as a separate ELF-injection dependency. Its source and binaries are not redistributed by this repository.

The tested local build was produced from upstream commit `1ad98c3` after applying:

```sh
git apply /path/to/ps5-stream8/patches/nines-fw1360-dynamic.patch
```

Compile the patched NineS checkout with the PS5 Payload SDK and place the resulting ELF at:

```text
ps5-stream8/payloads/stream8-injector-dynamic-fw1360.elf
```

The patch changes the listening port to `9041`, replaces the firmware-incompatible `allproc` name lookup with `KERN_PROC` enumeration, and queries the target PID on every injection request. The `kinfo_proc` byte offsets in the patch were measured against firmware 13.60 and are not portable to another firmware.

A compatible build uses `-O2 -Wall -Wno-macro-redefined` and compiles all
eight C sources from NineS. A fixed PID must not be added as a fallback:
failure to locate the target should abort the injection.

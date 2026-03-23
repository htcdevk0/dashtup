# Dashtup

`dashtup` is a small interactive installer for the Dash Programming Language.

It embeds the packaged `dash` binary, the full `.dash` home layout, and the official documentation files directly inside the installer binary through build resources.

## Commands

```bash
./dashtup install
./dashtup install bin
./dashtup install std
./dashtup install doc
./dashtup uninstall
./dashtup version
./dashtup license
./dashtup help
```

## What it installs

- `dash` to `/bin/dash`
- stdlib to `~/.dash`
- docs to `~/.dash/doc`

## Features

- Interactive install and uninstall
- Embedded resources in the final installer binary
- Automatic backup for an existing `/bin/dash`
- Restore support during uninstall
- Partial install support for `bin`, `std`, or `doc`
- Better sudo behavior for `~/.dash`
- English-only project layout and output
- Simple `Makefile` build
- Built-in `license` command

## Build

```bash
make
```

## Quick test

```bash
make run
```

## Notes

- Writing to `/bin/dash` usually requires elevated privileges.
- When `dashtup` runs through `sudo`, it still resolves `~/.dash` to the original user's home when possible.
- If a different file already exists at `/bin/dash`, `dashtup` offers to move it to `/bin/dash.dashtup.backup` before installing the packaged Dash binary.
- `install doc` installs `DASH_en.txt` and `DASH_en.md` into `~/.dash/doc`.
- Uninstall removes `~/.dash` and restores the backup binary when one exists.

## Optional environment variables

These do not change the command interface. They only help with local testing.

```bash
DASHTUP_PREFIX=/tmp/dashtup-root
DASHTUP_HOME=/tmp/dashtup-home
DASHTUP_ASSUME_YES=1
```

With those variables set, `dashtup install` writes into the custom paths instead of the system paths.

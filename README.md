# Scarlett2 Firmware Management Tool

## Overview

This command-line tool provides firmware management for Focusrite
interfaces using the Scarlett2 USB protocol, supporting Scarlett 2nd,
3rd, 4th Gen, Clarett USB, and Clarett+ series.

Available operations:
- `reboot` — reboot the device
- `reset-config` — reset to factory configuration
- `erase-firmware` — reset the device to factory firmware
- `update` — update the device's firmware

This is a pre-release, not suitable for the general public yet. Please
contact the author by email if you'd like to assist with testing.

## Requirements

- The updated Scarlett2 kernel module with Scarlett 4th Gen and
  firmware update support, available from
  https://github.com/geoffreybennett/scarlett-gen2/releases/tag/v6.5.11-g4.1

- Device firmware should be placed in `/usr/lib/firmware/scarlett2` or
  a subdirectory `firmware` relative to the binary.

## Downloading Firmware

Before this tool will be very useful, you'll need to download the
firmware from (TBA).

## Demo

```
[g@fedora ~]$ scarlett2
Found 1 supported device:
  card1: Scarlett 3rd Gen Solo (firmware version 1605)

[g@fedora ~]$ scarlett2 erase-firmware
Selected device Scarlett 3rd Gen Solo
Resetting configuration to factory default...
Erase progress: Done!
Erasing upgrade firmware...
Erase progress: Done!
Rebooting interface...

[g@fedora ~]$ scarlett2
Found 1 supported device:
  card1: Scarlett 3rd Gen Solo (firmware 1535, update to 1605 available)

[g@fedora ~]$ scarlett2 update
Selected device Scarlett 3rd Gen Solo
Found firmware version 1605 for Scarlett 3rd Gen Solo:
  /usr/lib/firmware/scarlett2/scarlett2-1235-8211-1605.bin
Updating Scarlett 3rd Gen Solo from firmware version 1535 to 1605
Resetting configuration to factory default...
Erase progress: Done!
Erasing upgrade firmware...
Erase progress: Done!
Firmware write progress: Done!
Rebooting interface...

[g@fedora ~]$ scarlett2
Found 1 supported device:
  card1: Scarlett 3rd Gen Solo (firmware version 1605)

[g@parker scarlett2]$ scarlett2 list-all
USB Product ID, Product Name, and Firmware versions available (* = connected)
 8203 Scarlett 2nd Gen 6i6     1583, 1076
 8204 Scarlett 2nd Gen 18i8    1583, 1331
 8201 Scarlett 2nd Gen 18i20   1653, 1083
 8211 Scarlett 3rd Gen Solo    1605, 1552
 8210 Scarlett 3rd Gen 2i2     1605, 1552
 8212 Scarlett 3rd Gen 4i4     1605, 1552
 8213 Scarlett 3rd Gen 8i6     1605, 1552
 8214 Scarlett 3rd Gen 18i8    1605, 1552
 8215 Scarlett 3rd Gen 18i20   1644, 1563
*8218 Scarlett 4th Gen Solo    2115, 2096, 2082 (running: 2115)
 8219 Scarlett 4th Gen 2i2     2115, 2100, 2096, 2082
 821a Scarlett 4th Gen 4i4     2089, 2082
 8206 Clarett USB 2Pre         1552
 8207 Clarett USB 4Pre         1552
 8208 Clarett USB 8Pre         1552
 820a Clarett+ 2Pre            1993
 820b Clarett+ 4Pre            1955
 820c Clarett+ 8Pre            1955
```

## Build

On Fedora, you'll need the `alsa-lib-devel` package:

```
sudo dnf -y install alsa-lib-devel
```

On Ubuntu:

```
sudo apt -y install make gcc pkg-config libasound2-dev
```

To build & install:

```
make
sudo make install
```

## Usage

Usually, just `scarlett2 update` is all you need.

Run `scarlett2 help` and `scarlett2 about` for more information.

## Donations

This program, the kernel driver, and the GUI are Free Software,
developed using my personal resources, over hundreds of hours.

If you find it useful, please consider a donation to show your
appreciation:

- https://liberapay.com/gdb
- https://www.paypal.me/gdbau

## License

Copyright 2023 Geoffrey D. Bennett <g@b4.vu>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see https://www.gnu.org/licenses/.

## Disclaimer Third Parties

Focusrite, Scarlett, Clarett, and Vocaster are trademarks or
registered trademarks of Focusrite Audio Engineering Limited in
England, USA, and/or other countries. Use of these trademarks does not
imply any affiliation or endorsement of this software.

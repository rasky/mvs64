## MVS64 -- A NeoGeo emulator for Nintendo 64

<img src="https://github.com/rasky/mvs64/raw/main/screens/image0.jpg" width="320">
<img src="https://github.com/rasky/mvs64/raw/main/screens/pbobblen.png" width="320">

### Status

This emulator is in **VERY EARLY STAGE**. Only a handful of games boot or
work.

Performance on N64 is still not very good, most games do not run at full frame
rate yet.

Sound is not emulated yet.


### How to build

MVS64 is written using [libdragon](https://github.com/DragonMinded/libdragon).
The build system assumes that you will be using the official Docker container
for libdragon ([libdragon-docker](https://github.com/anacierdem/libdragon-docker)),
that will work on Windows (under WSL), Linux and Mac. First, follow the installation
instructions of libdragon-docker if you haven't already.

Once you have the docker container configured, clone mvs64:

	$ git clone https://github.com/rasky/mvs64
	$ cd mvs64

Start the docker container in the mvs64 directory:

	$ libdragon start

Now build mvs64:

	$ make mvs64 BIOS=<path/to/bios.bin> ROM=<path/to/game.zip>

When building, you need to specify the path to a NeoGeo BIOS file that you
want to use, and the path to a NeoGeo game ROM, as a zip file.

This command will create a Nintendo 64 ROM called `mvs64-<gamename>.z64`, that
you can use with an emulator or on a real console using a development kit
like 64drive or EverDrive 64.

**NOTE**: during the build, the path of the BIOS will be inspected to search for
a ROM called `sfix.sfix`, which is also part of the standard BIOS sets. That
ROM must reside in the same folder of the specified BIOS.

### How to build the PC version of mvs64

mvs64 also includes a PC build of the emulator that can be used to further
test the emulation. In general, if a bug is present in the N64 version and
not in the PC version of mvs64, it is a bug of the Nintendo 64 backend.
Otherwise, it is a bug in the NeoGeo emulation layer.

To build the PC version, run:

	$ make pctest

This will build a `emu` binary. This PC version tries to stay as close
as possible to the N64 version, so it's not optimized to be a fully standalone
PC emulator. In particular, it doesn't load standard game ZIP files, but it
uses the preprocessed ROMs that are generated as part of the N64 build system. 

To use the PC version of the emulator on a specific game, first build the N64
emulator for that game using the `make mvs64` command above.
During the build, you will notice that a folder called `game.n64/` (next to
`game.zip`) is created. That folder contains preprocessed ROMs that are then
embedded in the final `.z64` file. Pass that folder to the `emu` binary:

	$ ./emu <path/to/game.n64/>



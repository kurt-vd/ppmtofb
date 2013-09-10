# ppmtofb

## Building

	$ make
	$ make install

### Cross-Compiling
The Makefile includes config.mk when present.
This can modify the build in various ways:
* alter compiler flags
* change compiler
* change install path

Example config.mk for cross-compile:

	CC=arm-cortex_a9-linux-gnueabi-gcc

## Using
ppmtofb addresses 2 usecases:
* put a ppm image into a framebuffer
* get an image from a framebuffer an save as ppm

The latter usecase is also addressed by the fbcat program.


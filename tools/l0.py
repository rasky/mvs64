#!/usr/bin/env python3
#
###########################################################
# Create the magic vshrink table used by video.c
# given the original 000-lo.lo NeoGeo ROM.
#
# The magic table is a condensed version of the line
# ROM that is enough to still achieve pixel-perfect
# shrinking in a tile-based rendering engine.
#
# We use it for two reasons:
#
# 1) The original ROM is 128Kb in size and mvs64 has
# very little RDRAM available to waste with tables.
# 2) mvs64 uses a tile-based rendering engine because
# line-based rendering would be too slow, so the 
# original ROM isn't actually a good fit.
#
########################################################## 

import sys

if len(sys.argv) < 2:
	print("Usage: l0.py <000-lo.lo>")
	sys.exit(1)

# Open original L0 rom
ROM = open(sys.argv[1], "rb").read()

# Extract the order in which tiles/lines appear as vshrink increases.
# We use vshrinks values 1-16, where max 1 line per tile is shown,
# and we extract the order at which the tiles appear.
seq = []
old_tiles = set()
for i in range(16):
	tiles = set()
	for x in ROM[i*256:(i+1)*256]:
		if x == 0xFF:
			break
		tiles.add(x>>4)
	new = tiles - old_tiles
	seq.append(new.pop())
	old_tiles = tiles

# Calculate the magic table: magic[i] basically contains
# the ordinal position of tile #i in the shrinking sequence.
# Notice that we print it twice (second time mirrored), because
# in the runtime engine it's easier to look it up this way
# for tiles in range 16..32, where the original hardware
# uses the original ROM backwards.
magic = [seq.index(i) for i in range(16)]
print("Magic table: ", magic + magic[::-1])

############################################
# Magic functions using the magic table.
#
# These functions are the two primitives required
# to render NeoGeo sprites with pixel-perfect vertical
# shrinking with a tile-based engine (rather than
# a line-based engine, which would be easier to 
# implement but too slow for mvs64).
############################################

def magic_tile_height(vshrink, tile):
	# Given a vshrink code and the tile number,
	# compute the tile height in pixels. For instance,
	# magic_tile_height(0xBC, 4) == 12 means that
	# when using vshrink code 0xBC, the fourth tile
	# of a sprite will be exactly 12 pixel tall (shrunk
	# from 16).
	vshrink += 1
	h = vshrink // 16
	if vshrink%16 > magic[tile]:
		h += 1
	return h

def magic_line_drawn(height, y):
	# Given a tile of the specified height and y in [0,15],
	# return True if the given line is drawn, or False if
	# it should be skipped.
	return height > magic[y]

# Try to reconstruct the line ROM given the magic table
# and the magic functions, to make sure that everything
# works as expected.
data = []
for vshrink in range(256):
	for tile in range(16):
		height = magic_tile_height(vshrink, tile)

		# this tile has 0 drawn lines, just skip
		if height == 0: continue

		# For all the actually drawn lines, add an entry
		# to the line ROM.
		for y in range(16):
			if magic_line_drawn(height, y):
				data.append((tile<<4) | y)

	# Pad the line ROM with 0xFF.
	while len(data)%256 != 0:
		data.append(0xFF)

ROM2 = bytes(data + data)

# Check that the reconstructed ROM is identical to the new one.
if ROM != ROM2:
	raise AssertionError("Failure in reconstructing original ROM")

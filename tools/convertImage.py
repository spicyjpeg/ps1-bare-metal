#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""PlayStation 1 image/texture data converter

A simple script to convert image files into either raw 16bpp RGB data as
expected by the PS1's GPU, or 4bpp or 8bpp indexed color data plus a separate
16bpp color palette. Requires PIL/Pillow and NumPy to be installed.
"""

__version__ = "0.1.0"
__author__  = "spicyjpeg"

import logging
from argparse import ArgumentParser, FileType, Namespace

import numpy
from numpy import ndarray
from PIL   import Image

## RGBA to 16bpp colorspace conversion

LOWER_ALPHA_BOUND: int = 0x20
UPPER_ALPHA_BOUND: int = 0xe0

# Color 0x0000 is interpreted by the PS1 GPU as fully transparent, so black
# pixels must be changed to dark gray to prevent them from becoming transparent.
TRANSPARENT_COLOR: int = 0x0000
BLACK_COLOR:       int = 0x0421

def convertRGBAto16(
	inputData: ndarray, transparentColor: int, blackColor: int
) -> ndarray:
	source: ndarray = inputData.astype("<H")

	r:    ndarray = ((source[:, :, 0] * 249) + 1014) >> 11
	g:    ndarray = ((source[:, :, 1] * 249) + 1014) >> 11
	b:    ndarray = ((source[:, :, 2] * 249) + 1014) >> 11
	data: ndarray = r | (g << 5) | (b << 10)

	data = numpy.where(data != transparentColor, data, blackColor)

	# Process the alpha channel using a simple threshold algorithm.
	if source.shape[2] == 4:
		alpha: ndarray = source[:, :, 3]

		data = numpy.select(
			(
				alpha > UPPER_ALPHA_BOUND, # Leave as-is
				alpha > LOWER_ALPHA_BOUND  # Set semitransparency flag
			), (
				data,
				data | (1 << 15)
			),
			transparentColor
		)

	return data.reshape(source.shape[:-1])

## Indexed color image handling

def convertIndexedImage(
	imageObj: Image.Image, maxNumColors: int, transparentColor: int,
	blackColor: int
) -> tuple[ndarray, ndarray]:
	# PIL/Pillow doesn't provide a proper way to get the number of colors in a
	# palette, so here's an extremely ugly hack.
	colorDepth: int   = { "RGB": 3, "RGBA": 4 }[imageObj.palette.mode]
	clutData:   bytes = imageObj.palette.tobytes()
	numColors:  int   = len(clutData) // colorDepth

	if numColors > maxNumColors:
		raise RuntimeError(
			f"palette has too many entries ({numColors} > {maxNumColors})"
		)

	clut: ndarray = convertRGBAto16(
		numpy.frombuffer(clutData, "B").reshape(( 1, numColors, colorDepth )),
		transparentColor, blackColor
	).reshape(( numColors, ))

	# Pad the palette to 16 or 256 colors.
	if maxNumColors > numColors:
		clut = numpy.c_[ clut, numpy.zeros(maxNumColors - numColors, "<H") ]

	image: ndarray = numpy.asarray(imageObj, "B")
	if image.shape[1] % 2:
		image = numpy.c_[ image, numpy.zeros(image.shape[0], "B") ]

	# Pack two pixels into each byte for 4bpp images.
	if maxNumColors <= 16:
		image = image[:, 0::2] | (image[:, 1::2] << 4)

		if image.shape[1] % 2:
			image = numpy.c_[ image, numpy.zeros(image.shape[0], "B") ]

	return image, clut

## Main

def createParser() -> ArgumentParser:
	parser = ArgumentParser(
		description = \
			"Converts an image file into raw 16bpp image data, or 4bpp or 8bpp "
			"indexed color data plus a 16bpp palette.",
		add_help    = False
	)

	group = parser.add_argument_group("Tool options")
	group.add_argument(
		"-h", "--help",
		action = "help",
		help   = "Show this help message and exit"
	)

	group = parser.add_argument_group("Conversion options")
	group.add_argument(
		"-b", "--bpp",
		type    = int,
		choices = ( 4, 8, 16 ),
		default = 16,
		help    = \
			"Use specified color depth (4/8bpp indexed or 16bpp raw, default "
			"16bpp)",
		metavar = "4|8|16"
	)
	group.add_argument(
		"-q", "--quantize",
		type    = int,
		help    = \
			"Quantize image with the given maximum number of colors before "
			"converting it",
		metavar = "colors"
	)
	group.add_argument(
		"-T", "--transparent-color",
		type    = lambda value: int(value, 16),
		default = TRANSPARENT_COLOR,
		help    = \
			f"Use specified 16-bit hex color value for fully transparent "
			f"pixels (default 0x{TRANSPARENT_COLOR:04x})",
		metavar = "value"
	)
	group.add_argument(
		"-B", "--black-color",
		type    = lambda value: int(value, 16),
		default = BLACK_COLOR,
		help    = \
			f"Use specified 16-bit hex color value for black pixels (default "
			f"0x{BLACK_COLOR:04x})",
		metavar = "value"
	)

	group = parser.add_argument_group("File paths")
	group.add_argument(
		"input",
		type = Image.open,
		help = "Path to input image file"
	)
	group.add_argument(
		"imageOutput",
		type = FileType("wb"),
		help = "Path to raw image data file to generate"
	)
	group.add_argument(
		"clutOutput",
		type  = FileType("wb"),
		nargs = "?",
		help  = "Path to raw palette data file to generate"
	)

	return parser

def main():
	parser: ArgumentParser = createParser()
	args:   Namespace      = parser.parse_args()

	logging.basicConfig(
		format = "{levelname}: {message}",
		style  = "{",
		level  = logging.INFO
	)

	with args.input as inputImage:
		inputImage.load()

		match inputImage.mode, args.bpp:
			case "P", 4 | 8:
				if args.quantize is not None:
					logging.warning("requantizing indexed color image")

					image: Image.Image = inputImage.quantize(
						args.quantize, dither = Image.NONE
					)
				else:
					image: Image.Image = inputImage

			case _, 4 | 8:
				numColors: int = args.quantize or (2 ** args.bpp)
				logging.info(f"quantizing image down to {numColors} colors")

				image: Image.Image = inputImage.quantize(
					numColors, dither = Image.NONE
				)

			case "P", 16:
				if args.quantize is not None:
					parser.error("--quantize is only valid in 4/8bpp mode")

				logging.warning("converting indexed color image back to RGBA")

				image: Image.Image = inputImage.convert("RGBA")

			case _, 16:
				image: Image.Image = inputImage.convert("RGBA")

	if image.mode == "P":
		imageData, clutData = convertIndexedImage(
			image, 2 ** args.bpp, args.transparent_color, args.black_color
		)
	else:
		imageData, clutData = convertRGBAto16(
			numpy.asarray(image), args.transparent_color, args.black_color
		), None

	with args.imageOutput as _file:
		_file.write(imageData)

	if clutData is not None:
		if args.clutOutput is None:
			parser.error("path to palette data must be specified")

		with args.clutOutput as _file:
			_file.write(clutData)

if __name__ == "__main__":
	main()

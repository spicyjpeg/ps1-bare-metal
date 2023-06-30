#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""ELF executable to PlayStation 1 .EXE converter

A very simple script to convert ELF files compiled for the PS1 to the executable
format used by the BIOS, with support for setting initial $sp/$gp values and
customizing the region string (used by some emulators to determine whether they
should start in PAL or NTSC mode by default). Requires no external dependencies.
"""

__version__ = "0.1.0"
__author__  = "spicyjpeg"

from argparse    import ArgumentParser, FileType, Namespace
from dataclasses import dataclass
from enum        import IntEnum, IntFlag
from struct      import Struct
from typing      import BinaryIO, ByteString, Generator

## Utilities

def alignToMultiple(data: bytearray, alignment: int):
	padAmount: int = alignment - (len(data) % alignment)

	if padAmount < alignment:
		data.extend(b"\0" * padAmount)

def parseStructFromFile(_file: BinaryIO, _struct: Struct) -> tuple:
	return _struct.unpack(_file.read(_struct.size))

def parseStructsFromFile(
	_file: BinaryIO, _struct: Struct, count: int
) -> Generator[tuple, None, None]:
	data: bytes = _file.read(_struct.size * count)

	for offset in range(0, len(data), _struct.size):
		yield _struct.unpack(data[offset:offset + _struct.size])

## ELF file parser

ELF_HEADER_STRUCT:  Struct = Struct("< 4s 4B 8x 2H 5I 6H")
ELF_HEADER_MAGIC:   bytes  = b"\x7fELF"
PROG_HEADER_STRUCT: Struct = Struct("< 8I")

class ELFType(IntEnum):
	RELOCATABLE = 1
	EXECUTABLE  = 2
	SHARED      = 3
	CORE        = 4

class ELFArchitecture(IntEnum):
	MIPS = 8

class ELFEndianness(IntEnum):
	LITTLE = 1
	BIG    = 2

class ProgHeaderType(IntEnum):
	NONE        = 0
	LOAD        = 1
	DYNAMIC     = 2
	INTERPRETER = 3
	NOTE        = 4

class ProgHeaderFlag(IntFlag):
	EXECUTE = 1 << 0
	WRITE   = 1 << 1
	READ    = 1 << 2

@dataclass
class Segment:
	address: int
	data:    bytes
	flags:   ProgHeaderFlag

	def isReadOnly(self) -> bool:
		return not \
			(self.flags & (ProgHeaderFlag.WRITE | ProgHeaderFlag.EXECUTE))

class ELF:
	def __init__(self, _file: BinaryIO):
		# Parse the file header and perform some minimal validation.
		_file.seek(0)

		magic, wordSize, endianness, _, abi, _type, architecture, _, \
		entryPoint, progHeaderOffset, secHeaderOffset, flags, elfHeaderSize, \
		progHeaderSize, progHeaderCount, secHeaderSize, secHeaderCount, _ = \
			parseStructFromFile(_file, ELF_HEADER_STRUCT)

		self.type:         ELFType = ELFType(_type)
		self.architecture: int     = architecture
		self.abi:          int     = abi
		self.entryPoint:   int     = entryPoint
		self.flags:        int     = flags

		if magic != ELF_HEADER_MAGIC:
			raise RuntimeError("file is not a valid ELF")
		if wordSize != 1 or endianness != ELFEndianness.LITTLE:
			raise RuntimeError("ELF file must be 32-bit little-endian")
		if (
			elfHeaderSize  != ELF_HEADER_STRUCT.size or
			progHeaderSize != PROG_HEADER_STRUCT.size
		):
			raise RuntimeError("unsupported ELF format")

		# Parse the program headers and extract all loadable segments.
		self.segments: list[Segment] = []

		_file.seek(progHeaderOffset)

		for (
			headerType, fileOffset, address, _, fileLength, length, flags, _
		) in parseStructsFromFile(_file, PROG_HEADER_STRUCT, progHeaderCount):
			if headerType != ProgHeaderType.LOAD:
				continue

			# Retrieve the segment and trim or pad it if necessary.
			_file.seek(fileOffset)
			data: bytes = _file.read(fileLength)

			if length > len(data):
				data = data.ljust(length, b"\0")
			else:
				data = data[0:length]

			self.segments.append(Segment(address, data, flags))

		#_file.close()

	def flatten(self, stripReadOnly: bool = False) -> tuple[int, bytearray]:
		# Find the lower and upper boundaries of the segments' address space.
		startAddress: int = min(
			seg.address for seg in self.segments
		)
		endAddress: int = max(
			(seg.address + len(seg.data)) for seg in self.segments
		)

		# Copy all segments into a new byte array at their respective offsets.
		data: bytearray = bytearray(endAddress - startAddress)

		for seg in self.segments:
			if stripReadOnly and seg.isReadOnly():
				continue

			offset: int = seg.address - startAddress
			data[offset:offset + len(seg.data)] = seg.data

		return startAddress, data

## Main

EXE_HEADER_STRUCT: Struct = Struct("< 16s 4I 16x 2I 20x 1972s")
EXE_HEADER_MAGIC:  bytes  = b"PS-X EXE"
EXE_ALIGNMENT:     int    = 2048

def createParser() -> ArgumentParser:
	parser = ArgumentParser(
		description = \
			"Converts an ELF executable into a PlayStation 1 .EXE file.",
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
		"-r", "--region-str",
		type    = str,
		default = "",
		help    = "Add a custom region string to the header",
		metavar = "string"
	)
	group.add_argument(
		"-s", "--set-sp",
		type    = lambda value: int(value, 0),
		default = 0,
		help    = "Add an initial value for the stack pointer to the header",
		metavar = "value"
	)
	group.add_argument(
		"-g", "--set-gp",
		type    = lambda value: int(value, 0),
		default = 0,
		help    = "Add an initial value for the global pointer to the header",
		metavar = "value"
	)
	group.add_argument(
		"-S", "--strip-read-only",
		action = "store_true",
		help   = \
			"Remove all ELF segments not marked writable nor executable from "
			"the output file"
	)

	group = parser.add_argument_group("File paths")
	group.add_argument(
		"input",
		type = FileType("rb"),
		help = "Path to ELF input executable",
	)
	group.add_argument(
		"output",
		type = FileType("wb"),
		help = "Path to PS1 executable to generate"
	)

	return parser

def main():
	parser: ArgumentParser = createParser()
	args:   Namespace      = parser.parse_args()

	with args.input as _file:
		try:
			elf: ELF = ELF(_file)
		except RuntimeError as err:
			parser.error(err.args[0])

	if elf.type != ELFType.EXECUTABLE:
		parser.error("ELF file must be an executable")
	if elf.architecture != ELFArchitecture.MIPS:
		parser.error("ELF architecture must be MIPS")
	if not elf.segments:
		parser.error("ELF file must contain at least one segment")

	startAddress, data = elf.flatten(args.strip_read_only)
	alignToMultiple(data, EXE_ALIGNMENT)

	region: bytes = args.region_str.strip().encode("ascii")
	header: bytes = EXE_HEADER_STRUCT.pack(
		EXE_HEADER_MAGIC, # Magic
		elf.entryPoint,   # Entry point
		args.set_gp,      # Initial global pointer
		startAddress,     # Data load address
		len(data),        # Data size
		args.set_sp,      # Stack offset
		0,                # Stack size
		region            # Region string
	)

	with args.output as _file:
		_file.write(header)
		_file.write(data)

if __name__ == "__main__":
	main()

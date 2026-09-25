# ps1-bare-metal - (C) 2023-2026 spicyjpeg
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
# REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
# INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
# OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

cmake_minimum_required(VERSION 3.25)

# Set the path to the toolchain file, which will configure CMake to use the MIPS
# toolchain rather than its default compiler and proceed in turn to execute
# setup.cmake.
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain.cmake")

# Tell CMake about the project. The VERSION, DESCRIPTION and HOMEPAGE_URL fields
# are optional, but the project name and LANGUAGES field should be present.
project(
	ps1-bare-metal
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "PlayStation 1 bare-metal C examples"
	HOMEPAGE_URL "https://github.com/spicyjpeg/ps1-bare-metal"
)

# Set up compiler flags and initialize the Python environment used to run the
# scripts in the tools directory.
include(cmake/setup.cmake)
include(cmake/tools.cmake)

if("${PSXAVENC_PATH}" STREQUAL "PSXAVENC_PATH-NOTFOUND")
	set(skipAudioExamples ON)
	message(WARNING "Unable to find psxavenc. All examples that require it for \
audio encoding will be skipped.")
endif()

# Build a support library containing code common to all examples (standard C
# library functions) and link it by default into every executable.
add_library(
	c OBJECT
	src/libc/clz.s
	src/libc/crt0.c
	src/libc/cxxsupport.cpp
	src/libc/malloc.c
	src/libc/misc.c
	src/libc/setjmp.s
	src/libc/string.c
	src/libc/string.s
	src/vendor/printf.c
)
target_include_directories(
	c PUBLIC
	src
	src/libc
)
link_libraries(c)

# Use the helpers defined in tools.cmake to generate bitmap, palette and audio
# data for the assets used by some examples.
convertImage(assets/test16bpp.png 16 assets/test16bppTexture.dat)
convertImage(assets/test4bpp.png   4 assets/test4bppTexture.dat assets/test4bppPalette.dat)
convertImage(assets/font.png       4 assets/fontTexture.dat     assets/fontPalette.dat)
convertImage(assets/mipmap.png     8 assets/mipmapTexture.dat   assets/mipmapPalette.dat)

if(NOT skipAudioExamples)
	convertAudioSample(assets/test.wav 22050 assets/testSample.dat)
endif()

# Build the examples and embed the converted assets where appropriate.
addPS1Executable(example00_helloWorld                     src/00_helloWorld/main.c)
addPS1Executable(example01_basicGraphics                  src/01_basicGraphics/main.c)
addPS1Executable(example02_doubleBuffer  src/common/gpu.c src/02_doubleBuffer/main.c)
addPS1Executable(example03_dmaChain      src/common/gpu.c src/03_dmaChain/main.c)

addPS1Executable(example04_textures src/common/gpu.c src/04_textures/main.c)
addBinaryFile   (example04_textures textureData "${PROJECT_BINARY_DIR}/assets/test16bppTexture.dat")

addPS1Executable(example05_palettes src/common/gpu.c src/05_palettes/main.c)
addBinaryFile   (example05_palettes textureData "${PROJECT_BINARY_DIR}/assets/test4bppTexture.dat")
addBinaryFile   (example05_palettes paletteData "${PROJECT_BINARY_DIR}/assets/test4bppPalette.dat")

addPS1Executable(example06_fonts src/common/gpu.c src/06_fonts/main.c)
addBinaryFile   (example06_fonts fontTexture "${PROJECT_BINARY_DIR}/assets/fontTexture.dat")
addBinaryFile   (example06_fonts fontPalette "${PROJECT_BINARY_DIR}/assets/fontPalette.dat")

addPS1Executable(example07_orderingTable src/common/gpu.c                                    src/07_orderingTable/main.c)
addPS1Executable(example08_spinningCube  src/common/gpu.c src/common/trig.c                  src/08_spinningCube/main.c)
addPS1Executable(example09_faceLighting  src/common/gpu.c src/common/gte.c src/common/trig.c src/09_faceLighting/main.c)

addPS1Executable(example10_mipmaps src/common/gpu.c src/common/gte.c src/common/trig.c src/10_mipmaps/main.c)
addBinaryFile   (example10_mipmaps textureData "${PROJECT_BINARY_DIR}/assets/mipmapTexture.dat")
addBinaryFile   (example10_mipmaps paletteData "${PROJECT_BINARY_DIR}/assets/mipmapPalette.dat")

addPS1Executable(example11_controllers src/common/font.c src/common/gpu.c src/11_controllers/main.c)
addBinaryFile   (example11_controllers fontTexture "${PROJECT_BINARY_DIR}/assets/fontTexture.dat")
addBinaryFile   (example11_controllers fontPalette "${PROJECT_BINARY_DIR}/assets/fontPalette.dat")

if(NOT skipAudioExamples)
	addPS1Executable     (example12_basicAudio src/common/font.c src/common/gpu.c src/12_basicAudio/main.c)
	addBinaryFile        (example12_basicAudio fontTexture             "${PROJECT_BINARY_DIR}/assets/fontTexture.dat")
	addBinaryFile        (example12_basicAudio fontPalette             "${PROJECT_BINARY_DIR}/assets/fontPalette.dat")
	addBinaryFileWithSize(example12_basicAudio sampleData sampleLength "${PROJECT_BINARY_DIR}/assets/testSample.dat")
endif()

addPS1Executable(example13_cdromInit src/common/font.c src/common/gpu.c src/13_cdromInit/main.c)
addBinaryFile   (example13_cdromInit fontTexture "${PROJECT_BINARY_DIR}/assets/fontTexture.dat")
addBinaryFile   (example13_cdromInit fontPalette "${PROJECT_BINARY_DIR}/assets/fontPalette.dat")

# ps1-bare-metal - (C) 2023-2024 spicyjpeg
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
include(cmake/virtualenv.cmake)

# Build a "common" library containing code shared across all examples. We are
# going to link this library into each example.
add_library(
	common OBJECT
	src/libc/crt0.c
	src/libc/cxxsupport.cpp
	src/libc/malloc.c
	src/libc/memset.s
	src/libc/misc.c
	src/libc/misc.s
	src/libc/string.c
	src/vendor/printf.c
)
target_include_directories(
	common PUBLIC
	src
	src/libc
)

# Define some helper functions to build each example.
function(addExample name)
	add_executable(${name} ${ARGN})
	target_link_libraries(${name} PRIVATE common)

	# Add a step to run convertExecutable.py after each example is compiled in
	# order to convert it into a PS1 executable. By default all custom commands
	# run from the build directory, so paths to files in the source directory
	# must be prefixed with ${PROJECT_SOURCE_DIR}.
	add_custom_command(
		TARGET     ${name} POST_BUILD
		BYPRODUCTS ${name}.psexe
		COMMAND
			"${Python3_EXECUTABLE}"
			"${PROJECT_SOURCE_DIR}/tools/convertExecutable.py"
			"$<TARGET_FILE:${name}>"
			${name}.psexe
		VERBATIM
	)
endfunction()

function(convertImage input bpp)
	add_custom_command(
		OUTPUT  ${ARGN}
		DEPENDS "${PROJECT_SOURCE_DIR}/${input}"
		COMMAND
			"${Python3_EXECUTABLE}"
			"${PROJECT_SOURCE_DIR}/tools/convertImage.py"
			-b ${bpp}
			"${PROJECT_SOURCE_DIR}/${input}"
			${ARGN}
		VERBATIM
	)
endfunction()

# Build the examples and convert any required assets.
addExample(example00_helloWorld src/00_helloWorld/main.c)

addExample(example01_basicGraphics src/01_basicGraphics/main.c)

addExample(example02_doubleBuffer src/02_doubleBuffer/main.c)

addExample(example03_dmaChain src/03_dmaChain/main.c)

addExample(example04_textures src/04_textures/main.c)
convertImage(
	src/04_textures/texture.png 16
	example04/textureData.dat
)
addBinaryFile(
	example04_textures textureData
	"${PROJECT_BINARY_DIR}/example04/textureData.dat"
)

addExample(example05_palettes src/05_palettes/main.c)
convertImage(
	src/05_palettes/texture.png 4
	example05/textureData.dat
	example05/paletteData.dat
)
addBinaryFile(
	example05_palettes textureData
	"${PROJECT_BINARY_DIR}/example05/textureData.dat"
)
addBinaryFile(
	example05_palettes paletteData
	"${PROJECT_BINARY_DIR}/example05/paletteData.dat"
)

addExample(
	example06_fonts
	src/06_fonts/gpu.c
	src/06_fonts/main.c
)
convertImage(
	src/06_fonts/font.png 4
	example06/fontTexture.dat
	example06/fontPalette.dat
)
addBinaryFile(
	example06_fonts fontTexture
	"${PROJECT_BINARY_DIR}/example06/fontTexture.dat"
)
addBinaryFile(
	example06_fonts fontPalette
	"${PROJECT_BINARY_DIR}/example06/fontPalette.dat"
)

addExample(
	example07_orderingTable
	src/07_orderingTable/gpu.c
	src/07_orderingTable/main.c
)

addExample(
	example08_spinningCube
	src/08_spinningCube/gpu.c
	src/08_spinningCube/main.c
	src/08_spinningCube/trig.c
)

addExample(
	example09_controllers
	src/09_controllers/font.c
	src/09_controllers/gpu.c
	src/09_controllers/main.c
)
convertImage(
	src/09_controllers/font.png 4
	example09/fontTexture.dat
	example09/fontPalette.dat
)
addBinaryFile(
	example09_controllers fontTexture
	"${PROJECT_BINARY_DIR}/example09/fontTexture.dat"
)
addBinaryFile(
	example09_controllers fontPalette
	"${PROJECT_BINARY_DIR}/example09/fontPalette.dat"
)

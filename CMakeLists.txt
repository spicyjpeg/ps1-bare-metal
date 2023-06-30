# ps1-bare-metal - (C) 2023 spicyjpeg
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
	DESCRIPTION  "PlayStation 1 bare-metal C/C++ template"
	HOMEPAGE_URL "https://github.com/spicyjpeg/ps1-bare-metal"
)

# Locate a working Python installation in order to run the scripts in the tools
# directory.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

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

# Define a helper function to build each example.
function(addExample name)
	add_executable(${name} ${ARGN})
	target_link_libraries(${name} PRIVATE common)

	# Add a step to run convertExecutable.py after each example is compiled in
	# order to convert it into a PS1 executable. By default all custom commands
	# run from the build directory, so paths to files in the source directory
	# must be prefixed with ${PROJECT_SOURCE_DIR}.
	add_custom_command(
		TARGET ${name} POST_BUILD
		COMMAND
			"${Python3_EXECUTABLE}"
			"${PROJECT_SOURCE_DIR}/tools/convertExecutable.py"
			"$<TARGET_FILE:${name}>" ${name}.psexe
		BYPRODUCTS ${name}.psexe
		VERBATIM
	)
endfunction()

# Build the actual examples.
addExample(chapter00_helloWorld    src/00_helloWorld/main.c)
addExample(chapter01_basicGraphics src/01_basicGraphics/main.c)
addExample(chapter02_doubleBuffer  src/02_doubleBuffer/main.c)
addExample(chapter03_dmaChain      src/03_dmaChain/main.c)

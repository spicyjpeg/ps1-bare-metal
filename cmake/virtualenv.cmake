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

# Create a user-editable variable to allow for a custom virtual environment path
# to be specified by passing -DVENV_PATH=... to CMake.
set(
	VENV_PATH "${PROJECT_SOURCE_DIR}/env"
	CACHE PATH "Directory containing Python virtual environment"
)

# If no virtual environment was already activated prior to building, attempt to
# set up the one specified by the variable.
if(NOT IS_DIRECTORY "$ENV{VIRTUAL_ENV}")
	if(IS_DIRECTORY "${VENV_PATH}")
		set(ENV{VIRTUAL_ENV} "${VENV_PATH}")
	else()
		message(FATAL_ERROR "Unable to find the Python virtual environment. \
Refer to the README to set one up in ${VENV_PATH}, or pass -DVENV_PATH=... to \
CMake to specify its location manually.")
	endif()
endif()

# Activate the environment by letting CMake search for its Python interpreter.
set(Python3_FIND_VIRTUALENV ONLY)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

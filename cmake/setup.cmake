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

# Override the default file extensions for executables and libraries. This is
# not strictly required, but it makes CMake's behavior more consistent.
set(CMAKE_EXECUTABLE_SUFFIX     .elf)
set(CMAKE_STATIC_LIBRARY_PREFIX lib)
set(CMAKE_STATIC_LIBRARY_SUFFIX .a)

# Add libgcc.a (-lgcc) to the set of libraries linked to all executables by
# default. This library ships with GCC and must be linked to anything compiled
# with it.
link_libraries(-lgcc)

# Create a dummy "flags" library that is not made up of any files, but adds the
# appropriate compiler and linker flags for PS1 executables to anything linked
# to it. The library is then added to the default set of libraries.
add_library   (flags INTERFACE)
link_libraries(flags)

target_compile_options(
	flags INTERFACE
		-g
		-Wall
		-Wa,--strip-local-absolute
		-ffreestanding
		-fno-builtin
		-fno-pic
		-nostdlib
		-fdata-sections
		-ffunction-sections
		-fsigned-char
		-fno-strict-overflow
		-msoft-float
		-march=r3000
		-mabi=32
		-mno-mt
		-mno-llsc
		-mno-abicalls
		-mgpopt
		-mno-extern-sdata
		-G8
	$<$<COMPILE_LANGUAGE:CXX>:
		# These options will only be added when compiling C++ source files.
		-fno-exceptions
		-fno-rtti
		-fno-unwind-tables
		-fno-threadsafe-statics
		-fno-use-cxa-atexit
	>
	$<IF:$<CONFIG:Debug>,
		# These options will only be added if CMAKE_BUILD_TYPE is set to Debug.
		-Og
		-mdivide-breaks
	,
		# These options will be added if CMAKE_BUILD_TYPE is not set to Debug.
		#-O3
		#-flto
	>
)
target_link_options(
	flags INTERFACE
		-static
		-nostdlib
		-Wl,-gc-sections
		-G8
		-T${CMAKE_CURRENT_LIST_DIR}/executable.ld
)

# Define a new ps1_target_incbin() command to embed the contents of a binary
# file into an executable. This is accomplished by generating an assembly
# listing that uses the .incbin directive to embed the file from a template.
function(ps1_target_incbin
	target type section symbolName sizeSymbolName path
)
	string(MAKE_C_IDENTIFIER "${symbolName}"     _symbolName)
	string(MAKE_C_IDENTIFIER "${sizeSymbolName}" _sizeSymbolName)
	string(MAKE_C_IDENTIFIER "${section}"        _section)

	cmake_path(ABSOLUTE_PATH path OUTPUT_VARIABLE _path)

	# Generate a unique name for the assembly file by hashing the target name
	# and symbol name, and place it in the current build directory.
	string(SHA1 _hash "${target} ${_symbolName}")
	set(_assemblyFile "${CMAKE_CURRENT_BINARY_DIR}/incbin_${_hash}.s")

	file(
		CONFIGURE
		OUTPUT  "${_assemblyFile}"
		CONTENT [[
.section ${_section}.${_symbolName}, "aw"
.balign 8

.global ${_symbolName}
.local ${_symbolName}_end
.type ${_symbolName}, @object
.size ${_symbolName}, (${_symbolName}_end - ${_symbolName})

${_symbolName}:
	.incbin "${_path}"
${_symbolName}_end:

.section ${_section}.${_sizeSymbolName}, "aw"
.balign 4

.global ${_sizeSymbolName}
.type ${_sizeSymbolName}, @object
.size ${_sizeSymbolName}, 4

${_sizeSymbolName}:
	.int (${_symbolName}_end - ${_symbolName})
]]
		ESCAPE_QUOTES
		NEWLINE_STYLE LF
	)

	# Add the assembly file to the target and add the embedded binary file as a
	# dependency to make sure it is built first if it needs to be built.
	target_sources("${target}" "${type}" "${_assemblyFile}")
	set_source_files_properties(
		"${_assemblyFile}" PROPERTIES OBJECT_DEPENDS "${_path}"
	)
endfunction()


# PlayStation 1 bare-metal C examples

This repository contains a series of homebrew tutorials and well-commented
examples for the original Sony PlayStation, built using no external SDKs or
tools other than an up-to-date, unmodified GCC toolchain targeting the MIPS
architecture, CMake as the build system and some Python scripts.

The following examples are currently available:

| #   | Screenshot                                                                    | Description                                                                       |
| --: | :---------------------------------------------------------------------------- | :-------------------------------------------------------------------------------- |
|   0 |                                                                               | [Printing "hello world" over the serial port](src/00_helloWorld/main.c)           |
|   1 | <img alt="Chapter 1" src="src/01_basicGraphics/screenshot.png" width="100" /> | [Initializing the GPU and drawing basic graphics](src/01_basicGraphics/main.c)    |
|   2 | <img alt="Chapter 2" src="src/02_doubleBuffer/screenshot.png" width="100" />  | [Adding double buffering and animated graphics](src/02_doubleBuffer/main.c)       |
|   3 | <img alt="Chapter 3" src="src/03_dmaChain/screenshot.png" width="100" />      | [Improving GPU drawing efficiency using DMA chains](src/03_dmaChain/main.c)       |
|   4 | <img alt="Chapter 4" src="src/04_textures/screenshot.png" width="100" />      | [Uploading a texture to VRAM and using it](src/04_textures/main.c)                |
|   5 | <img alt="Chapter 5" src="src/05_palettes/screenshot.png" width="100" />      | [Using indexed color textures and color palettes](src/05_palettes/main.c)         |
|   6 | <img alt="Chapter 6" src="src/06_fonts/screenshot.png" width="100" />         | [Implementing spritesheets and simple font rendering](src/06_fonts/main.c)        |
|   7 | <img alt="Chapter 7" src="src/07_orderingTable/screenshot.png" width="100" /> | [Using ordering tables to control GPU drawing order](src/07_orderingTable/main.c) |
|   8 | <img alt="Chapter 8" src="src/08_spinningCube/screenshot.png" width="100" />  | [Drawing a 3D spinning cube using the GTE](src/08_spinningCube/main.c)            |
|   9 | <img alt="Chapter 9" src="src/09_controllers/screenshot.png" width="100" />   | [Getting input from connected controllers](src/09_controllers/main.c)             |

New examples showing how to make use of more hardware features will be added
over time.

## Building the examples

### Installing dependencies

The only required dependencies are:

- CMake 3.25 or later;
- Python 3.10 or later;
- [Ninja](https://ninja-build.org/);
- a recent GCC toolchain configured for the `mipsel-none-elf` or
  `mipsel-linux-gnu` target triplet.

The toolchain can be installed on Windows through
[the `mips` script from the pcsx-redux project](https://github.com/grumpycoders/pcsx-redux/tree/main/src/mips/psyqo/GETTING_STARTED.md#windows),
on macOS using
[Homebrew](https://github.com/grumpycoders/pcsx-redux/tree/main/src/mips/psyqo/GETTING_STARTED.md#macos)
or on Linux through a package manager or by
[spawning it from source](https://github.com/grumpycoders/pcsx-redux/blob/main/tools/linux-mips/spawn-compiler.sh),
and should be added to your `PATH` environment variable in order to let CMake
find it. If you have any of the open-source PS1 SDKs installed there is a good
chance you already have a suitable toolchain set up (try running
`mipsel-none-elf-gcc` and `mipsel-linux-gnu-gcc` in a terminal). The other
dependencies can be obtained from their respective websites or through a package
manager.

The Python scripts require a few additional dependencies, which can be installed
through `pip` by running:

```bash
py -m pip install -r tools/requirements.txt   # Windows
pip3 install -r tools/requirements.txt        # Linux/macOS
```

### Building with an IDE

Many IDEs and text editors feature out-of-the-box support for CMake, so you
should be able to import the repo into your IDE of choice and immediately get a
working "build" button once the toolchain is set up. If you are using VS Code,
installing the
[CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
and [clangd](https://clangd.llvm.org) extensions for build integration as well
as context-sensitive suggestions is highly recommended.

If the toolchain is not listed in your `PATH` environment variable, you will
have to set the `TOOLCHAIN_PATH` CMake variable to the full path to your
toolchain's `bin` subdirectory (e.g. `/opt/mipsel-linux-gnu/bin`) using your
IDE's CMake cache editor. See your IDE's documentation for information on
accessing the cache editor; in VS Code with the CMake Tools extension, the
editor can be opened by selecting "Edit CMake Cache (UI)" from the command
palette (Ctrl+Shift+P).

### Building from the command line

If you cannot use an IDE or prefer working from the command line, simply run
these two commands from the root directory of the repository:

```bash
cmake --preset debug
cmake --build ./build
```

If you are unfamiliar with CMake, the first command is what's known as the
*configure command* and prepares the build directory for the second command,
which actually runs the compiler and generates the executables. Once the build
directory is prepared you'll no longer have to run the configure command unless
you edit the CMake scripts to e.g. add new examples or source files.

You may replace `debug` with `release` to enable release mode, which will turn
on additional compiler optimizations, remove assertions and produce smaller
binaries. Replacing it with `min-size-release` will further optimize the
executables for size at the expense of performance.

If the toolchain is not listed in your `PATH` environment variable, you will
have to pass the path to its `bin` subdirectory to the configure command via the
`-DTOOLCHAIN_PATH` option, like this:

```bash
cmake --preset debug -DTOOLCHAIN_PATH=/opt/mipsel-linux-gnu/bin
```

### Note on floating point support

The PlayStation does not have a floating point unit. While GCC can still provide
support for floats through emulation, some prebuilt versions of the
`mipsel-none-elf` and `mipsel-linux-gnu` toolchains ship with floating point
emulation partially or fully disabled at build time. For this reason, the build
scripts do not explicitly enable it.

If you have a toolchain known to have full support for software floating point
(e.g. one you built yourself with the appropriate options) but that does not
have it enabled by default, you may force it by adding the following line to
`CMakeLists.txt`:

```cmake
target_compile_options(flags INTERFACE -msoft-float)
```

Keep in mind that software floats are particularly slow, code-heavy and shall be
avoided at all costs. The included printf library has been modified to disable
the `%f` specifier, regardless of whether the toolchain supports software
floats, in order to reduce code size.

## Modifying the code

If you want to write your own examples or projects, here's a quick overview of
the non-example subfolders in the `src` directory:

- `src/libc` contains a minimal implementation of the C standard library, which
  should be enough for most purposes. Some functions have been replaced with
  optimized assembly implementations.
- `src/ps1` contains a basic support library for the hardware, currently
  consisting mostly of definitions for hardware registers and GPU commands.
- `src/vendor` is for third-party libraries (currently only the printf library).

Take a look at `CMakeLists.txt` to see how to add new executables to the build
system (I promise it won't be as scary as you imagine).

## Background

I have been occasionally asked if I could provide an example of PS1 homebrew
programming that is completely self-contained, permissively licensed and does
not depend on an external SDK. While there are a number of PS1 SDK options
around (including some I have contributed to), their workflows may not suit
everyone and some of the most popular options are additionally encumbered with
legal issues that make them a non-starter for commercial homebrew games, and/or
limitations that are hard to work around. As I have been moving away from using
such libraries myself, I set out to take what I am currently building for my
projects, clean it up and turn it into a tutorial series for other people to
follow.

I want this repo to be an introduction to bare-metal platforms and the PS1 for
anybody who already has some experience with C/C++ but not necessarily with the
process of linking and compiling, the internals of a standard library, the way
threads and IRQs work at the kernel level and so on. I strongly believe that
demystifying the inner workings of a platform can go a long way when it comes to
helping people understand it. Most 8-bit and 16-bit consoles have received a lot
of attention and excellent bare-metal tutorials have been written for them, so I
don't get why people shall just give up and use ancient proprietary SDKs from
the 1990s when it comes to the PS1.

## License

Everything in this repository, including the vendored copy of
[Marco Paland's printf library](https://github.com/mpaland/printf), is licensed
under the MIT license. If you want to build a project or even your own tutorial
series on top of my code, attribution would be highly appreciated but is not
strictly required.

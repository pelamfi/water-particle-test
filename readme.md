# Peter Lamberg's Particle "Water" Test.

A redo of an ad-hoc "water" simulation I did early in the nineties.
This is in some ways evolved version of the original, but this is still a quick hack.
It is not supposed to be physically correct, but merely interesting.

## Gradle build tool

The reason this project uses Gradle as its build tool instead of a more common CMake,
is that my main other project uses Gradle and this was originally part of the same Git repository.
Feel free to donate a CMake file via a pull request.

## Platforms

Currently only Mac OS Sierra should work, but I plan to add Windows 10 and Linux quickly.

## VSCode

Visual Studio Code should be able to open this project when the C++ tools are installed.
Also building and debugging should work with the included configuration (in `.vscode` folder.).

### A note on setting up VSCode to see system headers.

Get the system header paths on Mac with: `clang -E -v -x c++ /dev/null -fsyntax-only`.
(TODO: Add similar commands for other platforms if necesary)

Update them into `c_cpp_properties.json` for Visual Studio Code.

## Building on MAC

  * Download SDL (Simple Direct Media layer) to `external` directory
  
    cd external
    curl -o SDL2-2.0.5.tar.gz https://www.libsdl.org/release/SDL2-2.0.5.tar.gz

  * Build lib SDL

    bash build_sdl.sh

  * Build and the project

    cd ..
    gradle build
    ./build/exe/main/release/main

## Clang format

C++ code formatting instructions are in file `.clang-format` can be run using VSCode.
Current formatting built with [clangformat.com tool](https://clangformat.com/).

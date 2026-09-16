# BeebView

Author: mjd 2026
License: MIT

Description:

BeebView is a native reimplementation of Acornsoft VIEW, the word processor
originally developed for the BBC Micro.

The aim of BeebView is to retain the style, commands and editing behaviour of
VIEW while providing the conveniences expected from a modern desktop
application.

BeebView is written in C17 and is open-source software released under the MIT
License.


## Project Status

BeebView is under active development.

The project currently provides:

- SDL3 graphical desktop frontend
- ncurses terminal frontend
- Shared portable editor core
- VIEW-style TEXT and COMMAND modes
- VIEW commands and keyboard controls
- Modern keyboard shortcuts
- Mouse selection and clipboard support
- Command history and command-line editing
- Command and filename completion
- Configurable key and mouse bindings
- Configurable document workspace
- Optional line numbers
- Optional row/column display
- Native filesystem access
- BBC DFS SSD and DSD image support
- Text search and replacement
- Printing and file export
- Linux AppImage and DEB packaging
- Native Windows SDL3 build support


## VIEW Compatibility

BeebView is intended to reproduce the editing model and many of the commands
and controls of Acornsoft VIEW.

It also contains modern extensions which were not features of the original
VIEW software. These include facilities such as desktop clipboard shortcuts,
mouse operation, graphical configuration and modern filesystem integration.

Modern BeebView functionality should not be interpreted as functionality
provided by the original Acornsoft VIEW.


## Frontends

BeebView currently has two frontends.


### BeebView

The primary graphical version uses SDL3 and is available under Linux and Windows.
On Linux it can be distributed as an AppImage or DEB package.
On Windows it can be distrubted as a Inno Setup package.


### BeebView-terminal

The terminal version uses ncurses.
Both frontends use the same portable editor core.


## Building on Linux / Windows

Download: SDL3-3.4.16.zip  (or the latest version)
From:     https://github.com/libsdl-org/SDL/releases
Copy the ZIP to sdl3 directory (do not unzip it)

The normal build under linux is:
   ./build_installers.sh
Launched from a standard terminal window or the GUI

The normal build under windows is:
   set-executionpolicy -remotesigned
   ./build_Windows.ps1
Launched from a admin elevated powershell window




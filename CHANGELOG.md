## 0.5.88

- Fixed confirmation prompts such as `CONFIG RESET` being displayed twice; active confirmations are now rendered only at the live command input position, consistently with EXIT/QUIT.

## 0.5.87

- Added TAB completion for `CONFIG LOC` and `CONFIG RESET` arguments.
- Rendered CONFIG RESET confirmation inline using the same command-mode confirmation format as EXIT/QUIT.

## 0.5.86

- Make core-test temporary file and directory creation portable on Windows; remove remaining `mkstemp`/`mkdtemp` and `/tmp` assumptions from those test paths.

## 0.5.85

- Make the print/export failure-path core test portable by targeting the platform temporary directory itself instead of hard-coding `/tmp`.
- Share temporary-directory selection between Windows test output paths and the invalid-output regression check.

## 0.5.84

- Make print/export core tests use a portable process-ID helper (`_getpid()` on Windows, `getpid()` on POSIX).
- Build print/export test output paths from the Windows TEMP/TMP directory instead of assuming `/tmp`.

## 0.5.83

- Extend the Windows core-test compatibility helpers with file truncation using `_chsize_s()` while retaining POSIX `ftruncate()`.
- Correct the POSIX test directory helper to call `mkdir(path, 0700)` rather than recursing into itself.

## 0.5.82

- Added portable test helpers for directory creation and file-descriptor synchronization.
- Windows core tests now use `_mkdir()` and `_commit()` instead of POSIX-only `mkdir(path, mode)` and `fsync()`.

# 0.5.81 - Initial native Windows SDL3 build and packaging

- Add `build_Windows.ps1` for native Windows x64 builds using MSYS2 UCRT64, GCC, CMake and Ninja.
- Build against BeebView's bundled SDL3 source ZIP, run the core tests, and bundle required runtime DLLs.
- Add portable Windows ZIP and Inno Setup installer outputs, including README, CHANGELOG and the complete LICENSES tree.
- Register BeebView as an available Windows Open With application for common text/source file extensions without taking over their defaults.
- Embed the BeebView application icon in `BeebView.exe` and disable the POSIX/ncurses frontend on Windows builds.
- Make native filesystem startup/path resolution understand Windows drive-letter paths, `USERPROFILE`, `_getcwd` and `_fullpath`.

# 0.5.80 - Transactional output hardening

- Consolidate SAVE/WRITE and print/export transactional output into one private atomic-file helper.
- On Windows, create temporary output files exclusively and replace destinations with `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` instead of deleting the original before rename.
- Flush file contents before replacement on both POSIX and Windows and preserve the existing destination when replacement fails.

# 0.5.79 - AppImage licensing and project cleanup

- Include README, CHANGELOG and the complete `LICENSES/` tree inside both SDL and terminal AppImages.
- Add ncurses/tinfo copyright and license information for the libraries bundled by the terminal AppImage.
- Remove the duplicate lowercase `LICENSES/bedstead/` tree; `LICENSES/Bedstead/` remains canonical.
- Extend the STORE organizer to archive accidental `*(copy)*` files, normalize an older `Store/` directory to `STORE/`, and archive the duplicate lowercase Bedstead notice tree when encountered.

# 0.5.78 - project storage and third-party licensing

- Add a top-level `LICENSES/` tree containing SDL3, Bedstead/CC0, linuxdeploy and AppImageKit licence/notices, plus a consolidated third-party notice.
- Move Bedstead provenance and CC0 text from `third_party/bedstead/` to `LICENSES/Bedstead/` and package the complete `LICENSES/` tree with Linux installers.
- Add `tools/organize_project_store.sh` to move generated build/output trees, old installer products/logs and the unused/unlicensed historical `Mode7fonts` material into local `STORE/`; `STORE/` is ignored and is not redistribution material.
- Keep `installers/` itself as the package output directory while moving only existing generated package files/logs into `STORE/installers/`.

# 0.5.77 - AppImage desktop icon registration

- Register standalone SDL AppImages with `Icon=beebview`, using the normal XDG icon-theme name rather than an absolute/quoted icon pathname in the generated desktop entry.
- Copy the icon already packaged inside the running AppImage into the user's persistent hicolor icon theme, with fallbacks to the AppImage root icon and `.DirIcon`, so the icon remains available after the AppImage is unmounted.
- Install the packaged scalable SVG alongside the 256x256 PNG when available; desktop/icon cache refresh remains non-fatal.

# 0.5.76 - refresh standalone AppImage desktop integration

- Refresh the per-user desktop application database after the SDL AppImage creates or updates its Open With entry, so MIME handler changes are visible to file managers without package-manager installation.
- Use the installed per-user BeebView PNG path directly in the generated AppImage desktop entry, avoiding dependence on icon-theme cache lookup for the Open With icon.
- Refresh the hicolor icon cache when the helper is available; missing desktop/cache utilities remain non-fatal.
- Synchronize the checked-in SDL desktop metadata with the common textual MIME types already emitted by installer builds.

# 0.5.75 - AppImage icon and related text applications

- On standalone SDL AppImage launch, install the packaged BeebView PNG icon into the user's XDG hicolor icon theme so the generated **Open With** entry displays the BeebView icon instead of a generic application icon.
- Advertise common editable text/source MIME types in both packaged and AppImage desktop entries, including logs, C/C++ source and headers, shell/Python source, Markdown, CSV, JSON, XML and HTML, so BeebView is offered as a related application where the desktop MIME database classifies those files specifically.
- Keep the existing `text/plain` association and single-file `%f` startup behaviour.

# 0.5.74 - standalone AppImage Open With registration

- On first SDL AppImage launch, create a per-user `beebview-appimage.desktop` entry under the XDG applications directory so BeebView becomes available as an **Open With** handler for `text/plain` files without requiring a separate AppImage integration tool.
- Refresh the desktop entry whenever the AppImage is run from a different absolute path, so moving the AppImage and launching it again repairs the registered `Exec` path.
- Keep registration AppImage-only and non-fatal; DEB/package launches are unchanged and BeebView still starts if desktop registration cannot be written.

# 0.5.73 - Linux Open With integration

- Advertise the SDL BeebView desktop application as a handler for `text/plain`, allowing integrated AppImages and installed desktop packages to appear as **Open With → BeebView** choices in Linux file managers.
- Use the desktop-entry `%f` placeholder so exactly one selected file is passed to BeebView, matching its existing single startup-file command-line interface.
- Carry the same desktop metadata into the SDL AppImage AppDir and SDL DEB package.

# 0.5.72 - row/column overlay, config location and COMMAND output selection

- Add persistent `ROWCOLS [ON|OFF]`; when enabled, the 1-based cursor row and column are overlaid at the top-right of the ruler without consuming a text row.
- Add `CONFIG LOC` to report the resolved `viewbeeb.conf` path used by the running frontend.
- Add read-only mouse drag selection for existing text in the SDL COMMAND view. Ctrl+C and right-click Copy copy multi-line selected output; Cut does not modify command history/output.

# 0.5.71 - SDL build fix

- Fix the SDL build failure caused by the retired `VIEWBBC_WINDOW_TITLE` macro.
- The SDL window title now uses the canonical product name (`BeebView`) with no version suffix.

# 0.5.70

- Added `VER` to report the current BeebView release version.
- Added persistent `LINENUMS ON|OFF` display-only line numbers. The text gutter grows dynamically with the document line-count width and disappears completely when disabled.
- Mouse hit-testing now follows the dynamic text origin; clicking the line-number gutter positions the cursor at the start of that line.

# 0.5.69 - command selection mouse and keyboard fix

- Fix SDL mouse hit-testing after the dynamic-grid change: mouse coordinates now use the actual resized character grid rather than the old fixed 80x25 logical bounds. This restores COMMAND-line drag selection at the bottom of taller windows and fixes mouse access to all newly exposed rows/columns.
- Add standard Shift+Left / Shift+Right selection in COMMAND mode. Repeated Shift+cursor extends or contracts the selection from its keyboard anchor; an unshifted cursor key collapses it normally.
- Keep the existing reverse-video command selection, Ctrl+C/Ctrl+X/Ctrl+V/Ctrl+A and right-click clipboard operations.

# 0.5.68 - command-mode clipboard selection and UI version cleanup

- Show simply `BeebView` on the normal COMMAND home screen and SDL window title; the release version is shown by `HELP` / `?` instead of being permanently displayed.
- Restore the native filesystem label to `File System: Native`.
- Use the package/application version (`0.5.68`) as the single version string, retiring the old `A1.0` compatibility label.
- Add desktop-style COMMAND-line selection in SDL: left-click positions the command cursor and left-drag selects command text with reverse highlighting.
- Make configurable Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+A work in COMMAND mode as well as TEXT mode; typing, Backspace or Delete replace/delete the selected command text.
- COMMAND-mode paste never executes pasted line breaks: CR/LF/tab separators are normalised to spaces and the pasted text remains in the command editor until Return is pressed.
- The existing right-click Cut / Copy / Paste / Select All menu now operates on COMMAND-line selections too.

# 0.5.67 - hide cursor during paged output

- Hide the text cursor while HELP or other command output is paused at `-- More --`, instead of leaving the previous command cursor painted over the help text.
- Add explicit screen cursor visibility state so SDL and ncurses frontends can suppress the cursor cleanly when there is no active input field.
- Restore/show the cursor automatically whenever an actual command or text cursor position is set.

# 0.5.66 - HELP and BBC BREAK command interface

- Makes bare HELP and ? show only HELP syntax and the File, Edit, Keys and Conf subjects.
- Adds HELP CONF for SETUP, MODE, BUFFERSIZE and CONFIG; HELP FILE/EDIT/KEYS remain focused subject pages.
- Makes F12 a hard-wired BBC Micro-style BREAK: cancel transient activity, return to COMMAND mode and clear command output without discarding the document or mounted filesystem.
- Refreshes the COMMAND home screen with version, bytes free, filesystem type, editing filename and the `Type ? or HELP for help` hint.
- Removes the configurable F12=CLS binding; CLS remains an ordinary command.

# 0.5.65 - VIEW Edit/Delete Command

- Implement original VIEW `Shift+F8` Edit Command entry: type a two-letter stored command code and press Return; following text remains editable on the same line.
- Implement original VIEW `Shift+F9` Delete Command, removing the stored command from the current line without deleting its text.
- Render stored commands in VIEW's three-column left margin and move the cursor into the margin while a command code is being entered.
- Persist embedded commands in BeebView files as `XX<TAB>` line prefixes and recognise that representation when loading.
- Label Shift+F8/F9 correctly in CONFIG and HELP.

# 0.5.64 - command name completion

- Complete command names with Tab in COMMAND mode, using the same terminal-style behaviour as filename completion.
- A unique command completes to its canonical uppercase name and adds a trailing space ready for arguments.
- Ambiguous prefixes extend to their longest common prefix; pressing Tab again lists matching commands.
- Command matching is case-insensitive and filename/path completion continues normally after the command name.
- Keep the completion list derived from the command parser's canonical command-name API to avoid a separate frontend command table.

# 0.5.63 - dynamic SDL window grid

- Make the SDL frontend grow its character grid when the application window is enlarged instead of scaling the same fixed 80x25 surface. The 80x25 VIEW layout remains the minimum size.
- Recalculate visible rows and columns on SDL window/pixel-size changes while preserving the text cursor and viewport.
- Allow the text viewport to use extra columns beyond the traditional 74-column ruler width; the ruler/format width itself remains unchanged.
- Make mouse hit-testing, drag boundaries, context-menu placement and print dialogs use the current dynamic grid dimensions.
- Add safe screen/editor resize APIs and regression coverage for resize allocation and cursor visibility.

# 0.5.62 - installer ROOT_DIR repair

- Fix the installer builder security-check path to use its defined project root variable (`SRC_DIR`) instead of the undefined `ROOT_DIR`, which caused `set -u` to abort AppImage builds before linuxdeploy preparation.
- Includes the deterministic AppImage packaging changes from 0.5.61 so this patch can be applied directly before trying 0.5.61.

# 0.5.61 - deterministic AppImage packaging

- Stop relying on linuxdeploy output-plugin discovery for final AppImage creation.
- Use linuxdeploy only to prepare/bundle each AppDir, then invoke its bundled appimagetool directly with the exact requested output path.
- Verify the resulting AppImage exists and is non-empty before reporting success.
- Preserve pinned linuxdeploy download/checksum hardening and the separate SDL/terminal AppImages.

# 0.5.60 - installer version and AppImage repair

- Make CMake `project(BeebView VERSION ...)` the authoritative installer/package version instead of parsing CHANGELOG heading formatting.
- Use linuxdeploy-plugin-appimage's documented `LDAI_OUTPUT` and `LDAI_VERSION` variables so AppImages are written to the requested BeebView filenames.
- Remove stale BeebView `.deb` and `.AppImage` outputs before an installer run, preventing packages from an older successful build being mistaken for current output after a later failure.
- Keep the pinned linuxdeploy security hardening introduced in 0.5.58.

# 0.5.59 - compatibility and robustness audit

- Add deterministic malformed-input smoke tests for command parsing, SSD/DSD catalogue parsing and host document loading. These run in the normal core test binary and therefore under ASan/UBSan as well.
- Add `docs/COMPATIBILITY_AUDIT.md`, separating verified implemented behaviour from compatibility items that require an original VIEW reference instead of guessing semantics.
- Identify the currently mapped-but-unhandled Ctrl+F9, Shift+F2, Shift+F4, Shift+F5, Shift+F8 and Shift+F9 keys as explicit compatibility-audit items.
- Correct VIEW `SETUP` compatibility: K is the original justification selector. J remains accepted as a backwards-compatible BeebView alias, but status/reporting now uses F/K/W.
- Synchronise the stale CMake/CPack engineering version (which had remained at 0.5.47) with patch level 0.5.59. The human-facing BeebView version remains A1.0.

# 0.5.58 - security hardening

- Remove shell-based printer discovery/status commands; `lpstat` is now executed directly with `fork`/`exec`, closing the remaining shell-command surface in printing.
- Make document SAVE/WRITE and PRINT-to-file outputs transactional: write to a same-directory temporary file, flush/sync where supported, and rename only after a complete successful write so failed writes do not truncate the previous file.
- Save configuration through a randomized private temporary file on POSIX and force mode 0600 before atomic replacement.
- Validate every SDL3 ZIP member before extraction, rejecting absolute paths, traversal components, backslashes and content outside the expected `SDL3-X.Y.Z/` tree. Support optional trusted `SDL3-X.Y.Z.zip.sha256` sidecars.
- Pin automatic linuxdeploy downloads to a dated release instead of the rolling `continuous` channel. Support SHA-256 verification through `LINUXDEPLOY_SHA256` or `tools/linuxdeploy-x86_64.sha256`.
- Add Release-build stack protection, `_FORTIFY_SOURCE=3`, RELRO and immediate binding on supported Linux GCC/Clang toolchains.

# 0.5.57 - source layout refactor

- Split the monolithic editor implementation into dedicated editor input/lifecycle, rendering, and command-line editing/history/completion modules without changing editor behaviour.
- Move SDL clipboard/primary-selection integration into its own frontend source module.
- Add `docs/ARCHITECTURE.md` describing public versus private headers and the intended module boundaries for future work.
- Keep the public `ViewBBCEditor` API stable; this is a structural refactor rather than a feature change.

# 0.5.56 - configured buffer shown at startup

- Refresh the editor screen immediately after applying the persisted `buffersize` setting at startup. Previously the document limit was correctly changed, but the initial screen still contained the stale 1 MiB `Bytes free` value rendered by `viewbbc_editor_init()`.
- Apply the fix to both SDL and terminal frontends.

# 0.5.55 - F12 CLS mapping

- Maps F12 to the existing CLS operation by default.
- Adds CLS to configurable key bindings, so F12 can be changed in CONFIG/config file.
- F12 clears command/help output without changing TEXT/COMMAND mode.

## 0.5.71

- Fix SDL build failure caused by the retired `VIEWBBC_WINDOW_TITLE` macro.
- The SDL window title now uses the canonical product name (`BeebView`) with no version suffix.

## 0.5.54

- Add `BUFFERSIZE [n[KB|MB]]`. With no argument it reports the current file-buffer maximum; with an argument it accepts bytes, KB or MB with optional whitespace (`65536`, `100 KB`, `10MB`).
- Enforce a 32 KB minimum and 100 MB maximum, reject shrinking below the current document size, and perform a best-effort host allocator check before accepting a larger limit. `Bytes free` immediately reflects the new maximum.
- Persist the buffer maximum as a `buffersize = ...` section in `viewbeeb.conf` and apply it before command-line/startup file loading in both SDL and terminal frontends.

## 0.5.53

- Fix `./build.sh` failing immediately under `set -u` with `ORIGIN: unbound variable`. The CMake `$ORIGIN` runtime-path argument is now single-quoted as one shell argument, so Bash passes the literal token to CMake instead of trying to expand an `ORIGIN` shell variable.
- Keep the 0.5.52 successful-build cleanup behaviour unchanged: completed builds are staged in `out/bin/` before `build/` and the automatically extracted SDL3 source tree are removed; failed builds retain their temporary trees.

## 0.5.52

- Make `./build.sh` treat `build/` as a temporary CMake workspace: after configure, build, tests and output staging all succeed, the directory is deleted automatically. Failed builds retain it for diagnosis.
- Preserve runnable normal-build outputs under `out/bin/` before cleanup. `BeebView` is built with an `$ORIGIN` runtime search path and the private `libSDL3.so.0` is staged beside it when SDL3 is shared; `BeebView-terminal` is staged there as well when available.
- Confirm the installer builder follows the same successful-only cleanup policy: its temporary package work tree is removed and `out/build/linux-installer/` is deleted by default, with `--keep-build` remaining as the explicit diagnostic override.

## 0.5.51

- Clean up the automatically extracted `sdl3/SDL3-X.Y.Z/` source tree after a completely successful normal build, while retaining the source ZIP for the next build.
- Apply the same successful-build cleanup to SDL installer builds. Failed builds deliberately leave the extracted SDL3 tree in place for diagnosis.
- Never delete a tree supplied through `VIEWBBC_SDL3_SOURCE`; explicit developer overrides remain entirely under the caller's control.
- Add a guarded SDL cleanup helper that refuses to remove non-versioned paths or directories outside the project's `sdl3/` directory.

## 0.5.50

- Make bundled SDL3 source selection dynamic. `./build.sh` and SDL installer builds now choose the highest-version source archive named exactly `sdl3/SDL3-X.Y.Z.zip` and unpack it automatically before configuring CMake.
- Keep compatibility with archives stored in the older `sdl3/downloads/` directory, while preferring an equal-version archive dropped directly into `sdl3/`.
- Remove previously extracted version-shaped SDL3 source directories before unpacking the selected release, so replacing the source ZIP cannot leave a stale tree in use.
- Ignore platform/runtime archives such as `SDL3-X.Y.Z-win32-x64.zip` and `SDL3-devel-X.Y.Z-mingw.tar.gz`; only the SDL source-release naming form is eligible.
- Validate the selected ZIP before deleting an extracted source tree, verify the resulting `CMakeLists.txt`, and retain `VIEWBBC_SDL3_SOURCE` as an explicit override for development/debugging.
- Update `bootstrap.sh` to install `unzip` rather than requiring a system `libsdl3-dev`; the normal graphical build now consistently uses the bundled dynamically selected SDL3 source.

## 0.5.49

- Add configurable `Ctrl+F` Find and `Ctrl+H` Find & Replace shortcuts. Find opens COMMAND mode prefilled with `SEARCH `; Find & Replace opens it prefilled with `CHANGE `.
- Move the desktop clipboard shortcuts into the normal key configuration system: Paste (`Ctrl+V`), Clipboard Copy (`Ctrl+C`), Cut (`Ctrl+X`) and Select All (`Ctrl+A`).
- Show Find, Find & Replace, Paste, Clipboard Copy, Cut and Select All as editable rows in the SDL CONFIG screen, with the same conflict checking and alias support as other configurable keys.
- Remove the SDL hard-coded A/C/V/X interception so edited clipboard bindings actually take effect. Bare cursor arrows and Escape remain hardwired as before.

## 0.5.48

- Replace the free-form SDL `Print target` setting with a user-facing `Print destination` choice: **Printer** or **File**.
- Add a CUPS/lpstat-backed printer picker to CONFIG. Installed queues are listed, the default queue is marked, and queues not accepting jobs are shown as such.
- When the configured destination is File, `PRINT` now asks for **Text**, **PDF** or **OpenDocument (.odt)** and then asks for a filename; the appropriate extension is added automatically and existing files require overwrite confirmation. The ncurses frontend provides the same type/name prompt.
- Add native TXT, PDF and ODT exporters. `file:` remains accepted as a backward-compatible alias for plain text; advanced one-shot targets may also use `text:`, `pdf:` and `odt:`.
- Strengthen file-output error reporting: open, write, flush and close failures are detected and report the host error rather than claiming success.
- Strengthen printer error reporting: reject unsafe queue names, detect a disabled named queue when the spooler exposes it, detect broken print pipes and `lp` launch/exit failures, and distinguish successful OS-spooler acceptance from later physical-printer faults. Jam/out-of-paper/fault conditions that occur after buffering remain the spooler's responsibility and cannot be synchronously guaranteed by BeebView.

## 0.5.47

- Rename the built graphical executable from `viewbbc` to `BeebView`; `./build.sh` now produces `build/BeebView`.
- Rename the terminal executable from `viewbbc-terminal` to `BeebView-terminal`.
- Change the SDL window title from `ViewBBC A1.0` to `BeebView A1.0`.
- Update Linux desktop launchers, AppImage launch commands, Debian package payloads, diagnostic binary names, build output and documentation to use the BeebView executable name consistently.
- Keep internal C API identifiers and `VIEWBBC_*` CMake/environment options unchanged to avoid an unnecessary source/API compatibility break.

## 0.5.46

- Enter TEXT mode automatically after a startup file is successfully loaded, for both SDL and terminal frontends.
- Extend startup mounting syntax to `mount:[ssd:|dsd:]<file>` so an image type can be supplied explicitly when the filename extension is not `.ssd` or `.dsd`.
- Add `--mountlist:[ssd:|dsd:]<file>`: open a DFS image, list every file on all sides using `:0.` / `:2.` qualified names, then exit without starting a frontend.
- Keep `--help` / `-h` as an immediate command-line help path and document the new forms and examples.

## 0.5.45

- Add startup command-line loading to both SDL and terminal frontends: `viewbbc [mount:<disc-image>] [file]`.
- Apply `mount:` before the optional file, so `viewbbc mount:"disc.ssd" "LETTER"` mounts the DFS image and loads `LETTER` from it; without `mount:`, the file is loaded from the native filesystem.
- Accept normal shell-quoted paths, case-insensitive `mount:`, and `-h`/`--help`; reject duplicate/out-of-order startup arguments with a usage message.
- Preserve the existing VIEW MOUNT/LOAD semantics and startup error status in the editor instead of inventing a separate loading path.

## 0.5.44

- Add persistent in-session command history at the VIEW `=>` prompt. Up/Down browse the latest 32 non-empty commands, consecutive duplicates are suppressed, and Down returns to the command that was being typed before history browsing began.
- Add true command-line editing: Left/Right move the command cursor, Home/End jump to either end, typing inserts at the cursor, Backspace deletes left, and Delete removes the character under the cursor.
- Add terminal-style Tab filename/path completion for LOAD, SAVE, READ, WRITE, MOUNT, CD, EXPORT and `PRINT file:`. Unique matches complete directly, directories gain `/`, ambiguous matches extend to the longest common prefix, and a second Tab lists candidates.
- Support quoted paths, nested/native paths and DFS filename completion for LOAD/READ while a DFS image is mounted. The same core implementation is shared by SDL and ncurses frontends.
- Extend HELP KEYS and README command-mode documentation and add regression tests for command editing, history and completion.

## 0.5.43

- Add original VIEW-style `SETUP` command support: F selects format mode, J justification and W insert mode; the supplied letters replace the current three-state combination.
- `SETUP` with no argument reports the current F/J/W state, and invalid setup letters are rejected without changing the editor modes.
- Audit and refresh command documentation so already-implemented marker ranges, REPLACE/FOLD/NEXT MATCH, FORMAT, printing/export and other current commands are no longer described as future work.
- Correct HELP's stale SAVE description.

## 0.5.42

- Repair a patch-application/layout problem found in the supplied working tree: 0.5.39-0.5.41 files had landed under a nested `viewbbc/` directory while the real project root remained at 0.5.38.
- Re-apply the complete terminal-paste, desktop editing/printing, and Ctrl+A/C/V/X changes to the actual project-root sources used by `build.sh`.
- No new editor behaviour is introduced here; this patch makes the already-developed 0.5.39-0.5.41 changes part of the binary that is actually built.

## 0.5.41

- Fix SDL Ctrl+A/C/V/X shortcut detection on Linux/XKB by using the physical SDL scancode as well as the translated keycode.
- In particular, Ctrl+A now reliably reaches Select All and marks/highlights the entire document in TEXT mode.

## 0.5.40

- Complete the desktop editing-polish batch: SDL double-click word selection, triple-click line selection, Shift+click extension, edge auto-scroll while dragging, and an unassigned-right-click Cut/Copy/Paste/Select All context menu.
- Add conventional SDL clipboard aliases: Ctrl+A selects the whole document with VIEW markers 1/2, Ctrl+C copies, Ctrl+X cuts, Ctrl+V pastes/replaces, and Delete/Backspace remove the selected block.
- Activate `print.target`: `PRINT` uses the configured destination, `PRINT <target>` provides a one-shot override, and `EXPORT <filename>` writes plain text. Support `printer:default`, safe named `printer:<queue>` targets through `lp`, and `file:<path>` export.
- Keep ncurses bracketed-paste behaviour from 0.5.39 and leave terminal-emulator copy/selection shortcuts under terminal control.
- Clean Linux installer temporary build trees by default; make retained CMake builds and `installers/debug/` diagnostic binaries explicit opt-ins via `--keep-build` and `--debug-binaries`.

## 0.5.39

- Add bracketed-paste handling to the ncurses frontend so terminal-emulator paste works safely and predictably.
- Keep terminal copy/select behaviour owned by the terminal emulator rather than intercepting its mouse or Ctrl+Shift shortcuts.
- Normalize CR/LF to VIEW line breaks, route pasted tabs through VIEW tab handling, and discard pasted control bytes that could otherwise trigger editor commands.

## 0.5.38

- Add SDL system clipboard integration: Ctrl+C copies the current mouse selection and Ctrl+V pastes at the text cursor.
- Publish mouse-drag selections to the platform primary selection and support the conventional Linux middle-click paste at the clicked text position.
- Preserve BeebView's existing VIEW block-copy command and configurable mouse actions; middle-click primary paste is only used when button 2 is otherwise unassigned.
- Normalize clipboard CR/LF line endings on paste and map tab characters through VIEW's tab handling.

## 0.5.37

- Fix Linux Mint/Cinnamon start-menu launching of the terminal `.deb` by using an explicit terminal-emulator launcher instead of Desktop Entry `Terminal=true`.
- Keep `/usr/bin/viewbbc-terminal` unchanged so direct terminal launches continue to use the verified package binary.
- Install `/usr/bin/viewbbc-terminal-launcher` for the menu entry, with fallbacks for common Linux terminal emulators.

## 0.5.36

- Preserve stable diagnostic copies of the exact freshly built installer executables under `installers/debug/` before packaging or cleanup.
- Add `installers/debug/SHA256SUMS` so the retained diagnostic binaries can be identified unambiguously.
- Keep `out/build/linux-installer/` when possible, but no longer rely on that transient build tree for package-build diagnostics.

## 0.5.35

- Retain `out/build/linux-installer/` after installer creation so the exact binaries placed into packages can be tested directly.
- Add `--clean-build` to restore the previous post-package cleanup behaviour when retained binaries are not needed.
- Continue cleaning temporary packaging work trees while preserving only the useful CMake build output.

## 0.5.34

- Split Linux packaging into four independent deliverables: SDL AppImage, terminal AppImage, SDL .deb and terminal .deb.
- Add installer menu choices 1-5 plus Q; choice 5 builds all four packages.
- Give SDL and terminal Debian packages separate package names so they can be installed independently or side-by-side.
- Give SDL and terminal AppImages distinct filenames and desktop identities.
- Make installer builds selective: terminal-only builds do not compile SDL3, and SDL-only builds do not build ncurses.
- Retain automatic Mode 7/Bedstead icon generation for all package variants.

## 0.5.33

- Add `build_Installers.sh` for menu-driven Linux AppImage and combined SDL/ncurses `.deb` creation.
- Generate the BeebView application icon directly from the embedded Mode 7/Bedstead bitmap: black background with `Beeb` and `View` on separate lines.
- Bundle the privately built SDL3 runtime in both AppImage and `.deb`, preserving the no-system-SDL3 policy.
- Add BeebView icon metadata to the Linux desktop launchers.
- Add a pure-Python icon generator requiring no external font or imaging library.

## 0.5.32

- Render modified-document EXIT/QUIT confirmation inline in COMMAND mode without emitting a separate `=>` prompt underneath.
- If quit is requested from TEXT mode, switch to COMMAND mode before showing the confirmation.
- On No, complete the same confirmation line with `No`, then resume on a fresh command prompt line.

## 0.5.31

- Fix CONFIG footer keyboard navigation so ACCEPT and CANCEL are both reachable and visibly selectable.
- Down from the final setting moves to ACCEPT; Left/Right switches between ACCEPT and CANCEL; Up returns to the final setting; Enter activates the selected control.

## 0.5.30

- Keep bare Left/Right/Up/Down and Escape permanently hard-wired and omit them from `viewbeeb.conf` and CONFIG mode.
- Keep Home, End, Page Up and Page Down configurable.
- Treat PC `Insert` as a second default binding for VIEW Insert Mode (`Ctrl+F4, Insert`) instead of a separate configurable action.
- Allow multiple key bindings per action in CONFIG mode; adding a binding preserves existing aliases and duplicate combinations remain rejected.
- Migrate 0.5.27-0.5.29 `key.insert` settings into the Insert Mode alias list when older config files are loaded.

## 0.5.29

- Added `CONFIG` SDL configuration mode with clickable/selectable fields.
- Added `CONFIG RESET` with Y/N confirmation and atomic default-file rewrite.
- Added configurable key capture with Ctrl/Shift/Alt, duplicate detection and reserved-key rules.
- Added mouse button 1-6 capture with optional Ctrl/Shift/Alt modifiers and duplicate detection.
- Added ACCEPT/CANCEL controls, keyboard navigation and editable scalar config values.
- Extended physical-key parsing for digits and common punctuation when used with modifiers.

## 0.5.28

- Correct the configuration directory name from `viewbeeb` to `beebview`.
- Linux configuration path is now `~/.config/beebview/viewbeeb.conf`.
- Windows configuration path is now `%USERPROFILE%/beebview/viewbeeb.conf`.
- The configuration filename remains `viewbeeb.conf`.

## 0.5.27

- Add portable `viewbeeb.conf` configuration, created automatically from current defaults.
- Use `~/.config/viewbeeb/viewbeeb.conf` on Linux and `%USERPROFILE%/viewbeeb/viewbeeb.conf` on Windows.
- Make current keyboard bindings configurable in SDL and ncurses, including aliases such as PageUp/Ctrl-Up.
- Add configurable mouse button 1-6 actions and mouse wheel line count; button 1 remains selection by default.
- Add `font = mode7` and `font.size = 20`; SDL scales the embedded Mode 7/Bedstead bitmap font while retaining the 80x25 logical editor.
- Add `print.target` configuration for future printer/text/ODF/PDF output backends without pretending PRINT is implemented yet.
- Add regression coverage for configuration defaults and key parsing.

## 0.5.25 - 2026-09-06


## 0.5.26

- Fix SDL cursor blinking after mouse input.
- Ignored SDL events, especially ordinary mouse motion, no longer restart the cursor blink timer.
- Accepted keyboard, text, mouse-button, drag and wheel input still restart the cursor in its visible phase.

- Add SDL3 mouse support for TEXT mode.
- Left-click positions the text cursor without disturbing existing VIEW markers.
- Click-drag selects a block, highlights it in reverse video, and stores the range in VIEW markers 1 and 2 so existing COPY/MOVE/DELETE/WRITE/FORMAT operations can use it immediately.
- Add mouse-wheel navigation at three text lines per wheel step while keeping the editor cursor visible and clamped safely.
- Convert window mouse coordinates through SDL's logical-presentation transform so mouse hit-testing remains correct with resizing, letterboxing and HiDPI scaling.
- Keep mouse translation in a dedicated `editor_mouse.c` / `editor_mouse.h` module; ncurses remains keyboard-only.
- Update HELP KEYS and add core regression coverage for click, selection markers/highlighting, and wheel movement.


## 0.5.24 - 2026-09-06

- Add original VIEW global `FORMAT` command: bare `FORMAT` formats all text in memory; `FORMAT m1 m2` limits formatting to a valid marker range.
- Preserve text outside a marker-limited FORMAT range and remap range endpoints after reflow.
- Add VIEW `SWAP CASE` on Shift-F1; the character under the cursor changes ASCII case and the cursor advances.
- Add VIEW `DELETE UP TO CHARACTER` on Shift-F3 followed by the delimiter character; repeated delimiters are deleted together as documented.
- Keep markers safe across DELETE UP TO CHARACTER by shifting markers after the deleted run and invalidating markers inside it.
- Update HELP EDIT/KEYS and add Release plus ASan/UBSan regression coverage.


## 0.5.23

- Restore original VIEW `CLEAR` semantics: clear markers 1 and 2.
- Add BeebView `CLS` extension to clear the command transcript.
- Extend `READ` to accept an optional marker number (`READ <filename> [marker]`).
- Add `WRITE <filename> m1 m2` for writing a marked range on the native filesystem.
- Reject WRITE while a read-only DFS image is active.
- Add original VIEW short command forms `L` (LOAD), `S` (SEARCH), and `C` (CHANGE).
- Add regression coverage for marker READ/WRITE and CLEAR/CLS behaviour.

## 0.5.22 - 2026-09-06

- Extend VIEW `CHANGE` with optional marker ranges, e.g. `CHANGE old new 1 2`.
- Add interactive VIEW `REPLACE` / `R`: matches are shown in text mode with an `RP` indicator; Y replaces, N skips, ESC cancels, and Ctrl-F1 advances to the next match.
- Add VIEW `FOLD`, `FOLD 0`, and `FOLD 1`; folding defaults on and makes CHANGE/REPLACE case-insensitive while preserving the matched word's case shape.
- Keep marker-limited change/replace bounds attached to the original marked region when replacement length changes.
- Add a bounded document-level single-match replacement primitive rather than rebuilding editor logic around command-specific buffers.
- Update `HELP EDIT` for CHANGE, REPLACE/R and FOLD.
- Add parser and regression coverage for marker-limited CHANGE, folding, interactive Y/N replacement, cancellation, and restore the existing pager regression test to the active test runner.

## 0.5.21 - 2026-09-06

- Extend VIEW `COUNT` to accept marker ranges, e.g. `COUNT 1 2`, while retaining whole-document `COUNT`.
- Extend `SEARCH` with VIEW-style marker-limited searches, e.g. `SEARCH word 1 2` and quoted multi-word searches such as `SEARCH "old phrase" 1 2`.
- Implement VIEW `NEXT MATCH` on Ctrl-F1, continuing the most recent search and respecting any active marker range.
- Keep search state bounded inside the editor and validate marker ranges before every limited search/next-match operation.
- Update `HELP EDIT` and `HELP KEYS` for marker-aware COUNT/SEARCH and Ctrl-F1.
- Add regression coverage for marker-range word counts, bounded search, quoted bounded search, and repeated NEXT MATCH.
- Leave BeebView's existing command-console `CLEAR` extension unchanged for now; original VIEW uses `CLEAR` for markers 1 and 2, so that naming conflict needs an explicit compatibility decision rather than a silent behaviour change.

## 0.5.20 - 2026-09-06

- Add VIEW FORMAT BLOCK on BBC f0 / PC F10.
- Format from the current line through the current paragraph/block, stopping before a blank line or a line beginning with SPACE/TAB, as specified by the VIEW Guide.
- Respect Format Mode: F10 does not reformat while formatting is switched off.
- Respect Justify Mode: wrapped lines are expanded to the default ruler width where possible, while the final line remains ragged.
- Keep the formatter in a dedicated `formatter.c` / `formatter.h` module and retain the byte-oriented logical-workspace checks.
- `HELP KEYS` now documents F10/F0 FORMAT BLOCK.
- Add regression coverage for wrapping, justification, paragraph boundaries, Format Mode off, and marker safety across reformatted lines.

## 0.5.19 - 2026-09-06

- Add VIEW COPY BLOCK using markers 1 and 2.
- Map PC F11 to the BBC COPY key in SDL3 and ncurses.
- Preserve the original marked block so repeated COPY operations remain possible.
- Add HELP KEYS entry and block-copy regression tests.

## 0.5.18

- Added authentic marker-defined block deletion: marker 1 is the first character, marker 2 is the position after the last character, and Ctrl-F0 deletes the half-open range.
- Added Shift-F0 MOVE BLOCK. The current cursor is the destination; destinations strictly between markers 1 and 2 are rejected.
- Kept block mutation in a dedicated `blocks.c` / `blocks.h` module rather than expanding editor core logic.
- Block edits rebuild the byte-oriented document atomically, preserve CR-separated line structure, respect the logical workspace, and update the modified flag only on success.
- `HELP KEYS` now documents Ctrl-F0 DELETE BLOCK and Shift-F0 MOVE BLOCK. COPY remains pending because original VIEW uses the BBC COPY key and no PC mapping has been chosen.
- Added regression coverage for single-line/multi-line deletion, moves before/after a block, invalid destinations, and unset/reversed markers.

## 0.5.17

- Extended `HELP` with case-insensitive subjects: `HELP EDIT`, `HELP FILES`, and `HELP KEYS`.
- `HELP EDIT` shows editing/environment commands, `HELP FILES` shows filesystem commands, and `HELP KEYS` shows the currently implemented text-mode key mappings, including marker keys.
- Plain `HELP` and `?` retain the existing complete grouped command list and paging behaviour. Unknown HELP subjects are rejected explicitly.
- Added regression coverage for HELP subject parsing.

## 0.5.16

- Added a dedicated six-marker state module as the foundation for authentic VIEW block operations.
- Implemented VIEW `SET MARKER` on Shift-F7 followed by marker number 1-6. While awaiting the number, text mode displays the manual's `MK` indicator.
- Implemented VIEW `GO TO MARKER` on Shift-F6 followed by marker number 1-6, with safe handling of unset markers and clamping if a stored column is beyond the current line.
- Marker-number input is consumed as an immediate-command argument rather than inserted into the document.
- Added marker regression tests and restored execution of the existing 0.5.15 VIEW-command regression test from the test runner.
- COPY/MOVE/DELETE block operations are deliberately not included yet: the original guide confirms that they depend on markers 1 and 2, so this patch establishes that prerequisite without inventing PC COPY-key semantics.

## 0.5.15

- Added the first additional VIEW command-mode batch: `READ`, `COUNT`, `SEARCH`, `CHANGE`, `SCREEN`, and a guarded initial `MODE` implementation.
- `READ` inserts text from the active native/DFS filesystem at the current cursor position using workspace-bounded document reconstruction.
- `COUNT` reports a whole-document word count; marker-range counting will follow when VIEW markers are implemented.
- `SEARCH` finds the first exact byte match and returns to text mode at the match. `CHANGE` performs bounded exact global replacement, including quoted multi-word arguments.
- `SCREEN` displays the current document through the command console. `MODE` reports mode 3 and currently accepts mode 3 only; other VIEW modes remain pending frontend geometry work.
- Reworked HELP into `File system:` and `Editing:` groups while retaining `BeebView A1.0`, `Commands:`, and one leading space on every command entry.
- Added command-output paging. When output reaches the bottom of the screen, BeebView waits at `-- More --`; SPACE advances one page and ESCAPE prints all remaining pages without pausing again.
- Paging is shared by HELP, SCREEN, and other long command output/listings and remains bounded by the command-output and console limits.
- Added checked document insertion/replacement arithmetic and regression tests for paging, command parsing, text insertion, search/change/count, and MODE handling.

## 0.5.14

- Reduced ncurses ESC-key ambiguity delay to 50 ms, removing the roughly one-second pause when Escape switches between command and text modes while retaining keypad/function-key decoding.
- `viewbbc-terminal` now detects when it was launched without a TTY (for example by double-clicking the executable in a Linux file manager) and safely relaunches itself in a terminal emulator.
- Terminal relaunch uses direct `execvp()` argument vectors rather than a shell and tries Linux Mint/Debian `x-terminal-emulator` first, with XDG Terminal Exec, GNOME Terminal and Konsole fallbacks.
- Added `TryExec=viewbbc-terminal` to the installed terminal desktop entry.

## 0.5.13

- Accept DOS-style `DIR` switches both separated and attached to the command, e.g. `DIR /W` and `DIR/W` (including combined forms such as `DIR/W /A`).
- Added a background `file_modified` state. Successful text edits set it; successful LOAD, SAVE and NEW clear it.
- LOAD now asks for Y/N confirmation before replacing a modified document. NEW receives the same protection to avoid accidental data loss.
- Added `EXIT` and `QUIT` commands. Command exit, Ctrl-Q and the SDL window-close request all ask for Y/N confirmation when the document has unsaved changes. Escape cancels an active confirmation.
- Confirmation arguments are copied into a fixed-size bounded buffer before execution; compact DIR parsing checks command length before examining prefix bytes.

## 0.5.12

- Indented file/folder entries in native and DFS listings so catalogue output is visually separated from command/status text.
- Native directory listings now collect, classify and sort entries with folders first, then files; each group is sorted case-insensitively by name.
- `DIR /W` now produces a real multi-column wide listing instead of merely accepting and ignoring the switch. Mounted DFS catalogues support the same `/W` presentation.
- `DIR /B` remains deliberately bare (no heading, brackets or indentation) while retaining folder-first sorting on the native filesystem.
- Native folders are marked as `[folder]` in VIEW/DIR listings and with a trailing `/` in normal LS listings.
- Directory collection is bounded to 4096 entries with checked allocation growth; command output remains bounded by the existing output limits.

## 0.5.11

- Added the human-facing BeebView version string `A1.0` in a dedicated `version.h` so the SDL title and HELP output share one source of truth.
- SDL title bar text is now `ViewBBC A1.0`.
- `HELP` / `?` now prints `BeebView A1.0`, a blank line, `Commands:`, then every command line indented by one space.
- Simplified mounted filesystem status from `Mounted:SSD:` / `Mounted:DSD:` to `SSD:` / `DSD:`.
- Text mode now starts with Insert Mode enabled by default (`FJI`). Insert and Ctrl-F4 still toggle it.
- Added Page Up and Page Down text-mode navigation. Home and End remain beginning/end-of-line operations.
- SDL maps Ctrl+Up and Ctrl+Down to Page Up and Page Down respectively; ncurses maps the terminal Page Up/Page Down keys.
- Page movement uses checked/clamped `size_t` arithmetic and preserves a stable page-relative viewport where possible.

## 0.5.10

- Added SDL3 high-DPI/display-scaling support so the Bedstead font, cursor and complete 80x25 VIEW display scale together with the desktop's content scale.
- The SDL window is now created with `SDL_WINDOW_HIGH_PIXEL_DENSITY`; the renderer keeps the fixed 960x500 logical VIEW surface and scales it to the actual window/backbuffer.
- On Windows/X11-style displays where desktop scaling is expressed as a content scale, the initial window size is enlarged accordingly. When that content scale changes while moving between displays, the window is resized proportionally to preserve the apparent VIEW/font size.
- Display scaling is bounded to a safe 1x-4x range before calculating window dimensions.

## 0.5.9

- Replaced the temporary BeebView bitmap glyph set with printable ASCII glyphs taken from the supplied Bedstead 3.261 `bedstead-20.bdf`, giving the SDL frontend a much clearer SAA5050/Mode 7 teletext-style appearance.
- Use Bedstead's native 12x20 bitmap cells directly. The 80x25 logical display is 960x500 and the initial window is 960x500, avoiding the previous tiny 6x10 glyphs and preserving crisp one-to-one pixels at the default size.
- Kept the font embedded in the executable: no SDL_ttf, installed system font, or external run-time font file is required.
- Added the Bedstead CC0 notice and provenance alongside the source tree.

## 0.5.8

- Replaced SDL3 debug-text rendering with an embedded monospaced bitmap font so BeebView controls glyph shape and spacing without an external font dependency.
- Increased SDL character cells from 8x8 to 8x10 logical pixels while retaining an 80x25 text grid.
- Changed the logical SDL display from 640x200 to 640x250 and the initial window from 1280x400 to 1280x500, giving the editor a less squashed aspect ratio.
- Kept the VIEW-style two-pixel bottom-bar cursor aligned to the bottom of the taller character cell.

## 0.5.7

- Updated the command-mode header to show `File System: Native` or `File System: Mounted:<SSD|DSD>:<image path>` and changed the editing line to `Editing: <filename>`.
- Native filesystem paths now accept `/` and `\` interchangeably, including mixed separators, absolute paths, home-relative paths, `LOAD`, `SAVE`, `CD` and `MOUNT`.
- Path separator normalization is confined to the native filesystem layer so BBC DFS filenames are not rewritten.
- Added regression tests for mixed native path separators and native/mounted filesystem status display.

## 0.5.6

- Added `CLEAR` to clear the command transcript and redraw a fresh command screen.
- Added TAB handling in text mode: TAB advances to the next `*` position on the active VIEW ruler and pads short lines with spaces safely when needed.
- Added a ruler helper module so ruler rendering and tab-stop navigation use the same definition instead of duplicated constants.
- Added checked document line padding so TAB cannot grow a line past the logical workspace limit.
- Added SDL3 and ncurses TAB key mappings plus regression tests for CLEAR and text-mode tab stops.

## 0.5.5

- Fixed DFS catalogue decoding: the packed upper-bit fields for file length and execution address were reversed.
- Real, shortened SSD images (including the supplied `Welcome.ssd`) now mount when all referenced file data is present, even when the image omits unused trailing sectors.
- Added a regression test covering packed DFS catalogue fields and compact/shrunk SSD images.

## 0.5.4

- Improved `MOUNT` diagnostics so missing files are reported separately from malformed/unsupported DFS images.
- `MOUNT` now prints the resolved native path when opening an image fails, making the active native folder unambiguous.
- Rejects directories and images smaller than a DFS catalogue before parsing.
- Expanded DFS regression tests to use full-size 200 KiB SSD images rather than only tiny synthetic images.

## 0.5.3

- Changed command mode to retain submitted commands and their output instead of repainting a single fixed prompt.
- Command mode now automatically scrolls upward as the transcript fills the screen, keeping the live `=>` prompt visible at the bottom.
- Added a bounded 512-line ring buffer for command history so long sessions cannot grow memory without limit.
- Added real keyboard-path regression tests for `CD` and `MOUNT`, plus command-screen scrolling tests.
- Hardened compact `CD` prefix detection to avoid reading beyond a short command string.
- Unknown commands now report VIEW-style `Mistake`.

# Changelog

## 0.5.2

- Added `MOUNT [SSD|DSD] <filename>` with explicit type override or case-insensitive type inference from `.ssd`/`.dsd` (plus existing `.sd`/`.dd` aliases).
- `MOUNT` with no parameters reports the mounted image path, DFS type and current DFS directory; `UNMOUNT` returns to the native filesystem.
- A mounted DFS image is now the active filesystem: `LOAD` resolves inside it and `SAVE` is safely rejected until DFS write support is implemented.
- Added `LIST`, `*.`, `*CAT`, `DIR` and `LS`. Native listings are implemented internally rather than invoking a shell; the first option subset is `DIR /A /B /W` and `LS -a/-l`.
- Added `CD [path]` for native folders and DFS directory prefixes. Compact forms such as `CD..`, `CD~`, `CD/path` and `CD\\path` are accepted.
- Native `CD` understands `.`, `..`, `~`, `/` and `\\`; BeebView keeps its own logical current folder rather than changing the parent process working directory.
- Added bounded multi-line command output and split filesystem/output handling into dedicated modules.
- Expanded `HELP`/`?` to list the newly implemented filesystem commands.
- Added parser and execution tests for mount state, read-only DFS saves, compact `CD` forms and filesystem command aliases.


## 0.5.1

- Added `HELP` and `?` command aliases.
- Added an on-screen list of currently implemented commands and short descriptions.
- Kept command help data in a separate `help.c` / `help.h` module so the command list can grow without bloating the editor core.
- Added parser and editor tests for both help aliases.

## 0.5.0

- Added a 1 MiB logical VIEW workspace with live `Bytes free` reporting.
- Added overflow/size guards around screen allocation, document growth, file reads and DFS extraction.
- Split command parsing, file I/O, DFS image handling and command execution into separate `.c/.h` modules so the editor/main loop stays small.
- Added `LOAD`, `SAVE` and `NEW` command execution.
- Added safe quoted filename parsing: matching outer quotes delimit a name, doubled quotes inside become a literal quote, and unmatched/embedded quotes remain filename characters.
- Host VIEW files are loaded as byte-oriented data; CR, LF and CRLF input is accepted and saves use BBC-style CR line endings.
- Added read-only DFS image support for `.ssd`, `.dsd`, plus `.sd`/`.dd` aliases.
- `LOAD image.ssd`/`LOAD image.dsd` mounts the image and displays its catalogue; a following `LOAD $.NAME`, `LOAD NAME`, `LOAD :0.$.NAME` or `LOAD :2.$.NAME` loads a file from the mounted image.
- Files loaded from DFS images may be edited in memory but cannot be saved back to the image yet; `SAVE filename` writes a host file instead.
- Expanded release tests for workspace exhaustion, command parsing, line-ending import and DFS extraction.

## 0.4.9

- Added a VIEW-style command/start screen shown at startup.
- Escape toggles between command mode and text-editing mode.
- Added an editable `=>` command line.
- Returning to command mode preserves the current document and editing position.

## 0.4.8

- SDL3 text cursor blinks at a 500 ms cadence.
- Cursor blink resets to visible whenever keyboard/text input is received.
- SDL3 input waits wake periodically so the cursor continues blinking while idle.

## 0.4.7

- SDL3 renders the cursor as the authentic VIEW-style two-pixel bottom bar.
- Insert/overtype state continues to be shown by VIEW's `I` mode indicator rather than changing cursor shape.

## 0.4.6

- Corrected initial VIEW text-mode flags to `FJ`: Format and Justify start enabled, Insert starts disabled.
- Added the modern PC `Insert` key as an SDL3/ncurses convenience alias for VIEW's `Ctrl-F4` Insert Mode command.
- Kept authentic BBC `DELETE` semantics on Backspace.
- Added regression tests for the default modes and Insert-key alias.

## 0.4.5

- Fixed Release-build test crash by replacing side-effecting `assert()` calls with always-active checks.

## 0.4.4

- Added `VIEWBBC_SDL3_SOURCE` support for a downloaded SDL3 source tree.
- Added `build.sh` for the Linux Mint development layout.
- Corrected the terminal build option name to `VIEWBBC_BUILD_NCURSES`.

## 0.4.3

- Added a dedicated `ViewBBC Terminal` desktop launcher using `Terminal=true`.
- Improved bootstrap diagnostics for distributions without packaged SDL3.

## 0.4.2

- Added `bootstrap.sh` for dependency checks and optional apt installation on Debian-family systems.

## 0.4.1

- SDL3 became a hard requirement when the graphical frontend is requested.

## 0.4.0

- Added SDL3 graphical frontend as the primary `viewbbc` application.
- Kept the ncurses frontend as optional `viewbbc-terminal`.

## 0.3.0

- Added early VIEW-style editing semantics and function-key commands.

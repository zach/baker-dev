Cube: the stacks base's test game, for Windows 10 or 11 (64-bit)
================================================================

Nothing to install. Unzip this folder anywhere and double-click stacks_cube.exe.
A console window opens beside the game; it shows the renderer and the data folder.

Measuring edit to see (the number the minimum-spec PC test is for)
-------------------------------------------------------------------
1. Start stacks_cube.exe and leave it running.
2. Open data\default.dta in Notepad (right-click, Open with, Notepad).
3. Change a value, for example "(spin 45" to "(spin 200", or the three
   numbers after "(color", and save (Ctrl+S).
4. The cube changes at once. The top-left corner shows
       edit to see: N ms (default.dta)
   the time from the save to the frame that showed it. Save a few edits and
   note the numbers; the target is under 1000 ms.

A file with a mistake in it (a missing parenthesis, say) is reported in the
console window with its line, and the cube keeps its last good values.

Other things to try
-------------------
F1            shows and hides the tuning panels. The Loop panel lists the
              edit-to-see times, median and worst, per file.
Tweaks panel  drag a value; "Save" writes it back into data\default.dta.

Options (from a command prompt in this folder):
  stacks_cube.exe --renderer d3d11      (or d3d12, vulkan, gl)
  stacks_cube.exe --no-vsync            present as fast as possible
  stacks_cube.exe --console-port 0      no remote console

The game also listens on 127.0.0.1:4555 (this computer only) for console
commands such as {cube.reset}; Windows may ask to allow it. Allowing it is not
needed for the test.

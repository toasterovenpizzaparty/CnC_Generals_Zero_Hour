Command & Conquer Generals Zero Hour - LAN edition
==================================================

A rebuilt game executable (generalszh.exe) with new network code for LAN games.
It runs with the game data of your existing Zero Hour installation.

You need
  - Command & Conquer Generals Zero Hour installed (for example "Command & Conquer
    The Ultimate Collection" from the EA App). The original Generals must be installed
    too; the Ultimate Collection includes both.
  - Windows 10 or 11.

Installing
  1. Extract this whole zip file to a folder (right-click > Extract All...).
  2. Double-click Install.cmd. If Windows says "Windows protected your PC", click
     "More info" and "Run anyway".
  3. Allow the administrator prompt.

The installer
  - finds your Zero Hour folder (or asks you for it),
  - copies generalszh.exe into it. The game's own files are not changed,
  - allows the game through Windows Firewall, for computers on your local network only,
  - installs Microsoft's DirectX 9 runtime if it is missing,
  - adds a "Zero Hour (LAN)" shortcut to your desktop.

Start the game with that shortcut. The EA App still starts the original game.

Playing on a LAN
  Everyone in a game needs this same version. To check, compare the checksum in
  SHA256SUMS.txt: the installer shows it too.

Removing it
  Double-click Uninstall.cmd, or delete generalszh.exe from the Zero Hour folder.

Not working yet
  - No videos (intro and campaign briefings are skipped).
  - No online play; LAN only.

Version @VERSION@ (@COMMIT@)

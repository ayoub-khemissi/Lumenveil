Lumenveil - volumetric fog, night lights, light shafts and water for the World of Warcraft 3.3.5a client

INSTALL
  1. Close the game.
  2. Copy everything from the zip into your game folder, next to Wow.exe.
  3. Start the game with Lordaeron-Play.cmd. The first time, it reads your own game files and writes where the
     world's lamps and water are (lampdata.bin, liquiddata.bin): about half a minute; nothing in your game files is
     changed. Then it starts the game. Start the game with it from now on: it keeps everything up to date.

UPDATE
  Double-click Lordaeron-Update.cmd, or start the game with Lordaeron-Play.cmd (it updates, then starts the game).
  It keeps Lumenveil and the client patches in Data (patch-W.MPQ, patch-Z.MPQ) up to date from lordaeron.eu, checks
  every download, keeps the settings you changed and gives the others their new defaults. Lordaeron-Update.cmd
  -Rollback goes back to the version you had.

KEYS
  Ctrl+F12  switches Lumenveil off and on, to compare with the plain game
  Ctrl+F7   opens the settings window
  Ctrl+F3   opens the region panel (the look of each zone)

UNINSTALL
  Close the game and double-click Lumenveil-Uninstall.cmd.

WHEN SOMETHING GOES WRONG
  Look in CoAVolFog.log next to Wow.exe. The full documentation, the source code and the issue tracker are on the
  project's page; see the link on the page you downloaded this from.

ANTIVIRUS
  Some antivirus programs warn about new DLLs that a game loads from its own folder, as it loads version.dll. To
  check the files, compare them with SHA256SUMS.txt on the release page, or run
  "gh attestation verify <file> --repo ayoub-khemissi/Lumenveil" with the GitHub CLI: it shows that GitHub built
  the file from the project's source. Report a false positive to your antivirus vendor; don't turn your antivirus off.

CREDITS
  Lumenveil is made by the Lumenveil authors: https://github.com/lumenveil/Lumenveil. It is built on CoAVolFog by
  jealous-sound (https://github.com/jealous-sound/coa-vfog), with lamp rules from Everwood, Dear ImGui, libmpq, zlib
  and bzip2 (THIRD-PARTY-NOTICES.txt). This build is Lordaeron's fork; its source and its changes:
  https://github.com/ayoub-khemissi/Lumenveil.

LEGAL
  Lumenveil is free, non-commercial software under the GNU General Public License v2 (LICENSE.txt). It contains no
  game files and no part of the game's code, changes none of your game files, sends nothing over the network and
  plays nothing for you. Any change to the game client may be against the terms of the service you play on, and
  anti-cheat software may detect it: read those terms first. You use Lumenveil at your own risk, with no warranty.
  Lumenveil is not affiliated with or endorsed by Blizzard Entertainment. World of Warcraft is a trademark or
  registered trademark of Blizzard Entertainment, Inc. More in LEGAL.txt.

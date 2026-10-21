# Legal notes

This page says what Lumenveil contains, what it deliberately leaves out, and the rules contributors follow to keep it
that way. It is not legal advice.

## What this repository and its releases contain

- Source code written for Lumenveil and for CoAVolFog, under the GNU GPL v2 (see [LICENSE](LICENSE)). Lumenveil's
  engine is a modified version of CoAVolFog; [engine/CHANGES.md](engine/CHANGES.md) lists the files changed and when.
- Third-party open-source code, each under its own licence, in `third_party` (see
  [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)). Everything compiled into a release is built from source in this
  repository; nothing is downloaded at build time.
- Settings files (`CoAVolFog.ini`, `CoAVolFog-regions.ini`): numbers chosen by hand for the effects, sorted by zone
  number, with the zone's name in a comment.
- Screenshots of the game, used to show what the mod does (see below).
- Compiled programs built from the source above: `version.dll`, `CoAVolFog.dll` and the setup tool.

## What they never contain

- No files from the game client: no textures, models, maps, sounds, interface files, game tables or executables, and
  no part of them.
- No part of the client's program code: not copied, not disassembled, not patched into a file.
- No data converted or derived from the game's files, such as light or fog tables, lamp positions, water maps, zone
  lists, texture lists or height maps made from the game's textures.
- No data from any game server.
- No modified copy of `Wow.exe` or of any other file of the game, and no instructions for modifying one.
- No copy of the game and no pointer to where to get one.

## How the mod gets what it needs about the world

The mod needs to know where the world's lamps and water are. `Lumenveil-Setup.cmd` reads this from the player's own
installed copy of the game and writes it into `lampdata.bin` and `liquiddata.bin` next to `Wow.exe` on the player's
own computer. These files are never uploaded, published or shared by the project. Other information (the game's light
colours, the camera, the time of day) is read from the running game in memory and never stored.

Some effects can use extra data files that the project doesn't make or distribute: `fogdata.bin`, `reliefdata.bin`
and `materialdata.bin`.

## Identifiers inside the code

To work with one exact build of the client, the code contains:

- memory addresses and offsets in the 12340 client;
- 64-bit hashes (FNV-1a) of short stretches of the client's code. Before Lumenveil changes or calls anything in the
  running client, it hashes the client's bytes at that place and compares the result; if they differ, that feature
  stays off. Only the hash is in the source, never the bytes;
- hashes of texture data, and names of some of the game's files and folders, used to recognise kinds of textures (for
  example, which are stone and which are grass);
- zone numbers, and zone names in comments of the regions file.

These identify the client's code and content; they reproduce none of it.

## What the mod does in the game

Lumenveil changes only what the player's own screen shows. It attaches to the running client in memory, wraps its
Direct3D device, and redirects a few of the client's drawing calls. It doesn't change any file of the game.

It doesn't:

- send anything over the network, or change what the client sends or receives;
- play for the player, move the character, or run the game's scripts or commands;
- give the player any information the game keeps from them, such as hidden units or other players' data;
- hide itself from, disable, or get around anti-cheat software, copy protection or any other technical protection
  measure.

The game's terms of use, and the rules of the service a player connects to, may forbid modified clients, and
anti-cheat software may detect the mod. Players decide for themselves whether to use it, and do so at their own risk.
The software comes with no warranty (sections 11 and 12 of the [GNU GPL v2](LICENSE)).

## Screenshots

The screenshots in `docs/screenshots` show the game with and without the mod, to show what the mod does. The game's
art in them belongs to Blizzard Entertainment, is not part of the software, and is not covered by its licence.
Blizzard's published policies allow fans to use its game content in non-commercial work that is free to see, and keep
the right to withdraw that permission. Lumenveil is non-commercial: it is free, and it is not sold, advertised or paid
for. If Blizzard asks for the screenshots to be removed, they will be.

## Trademarks

Lumenveil is an independent fan project. It is not affiliated with, endorsed by or sponsored by Blizzard
Entertainment. World of Warcraft and Blizzard Entertainment are trademarks or registered trademarks of Blizzard
Entertainment, Inc. in the U.S. and/or other countries. These names appear only to say which game client the mod
works with. The project's name and its releases don't use the game's names, logos or art.

## Rules for contributors

- Never commit or attach game files, or files made from them: extracted textures or models, `.dbc`/`.db2`/`.adt`
  files or exports of them, the `.bin` files the setup tool writes, dumps of the client's shaders, memory or code.
- Never copy the client's code into the source. Describe it by address and by what it does, and recognise it by a
  hash of its bytes.
- Don't add anything that automates play, reads what the game hides, talks to a server, lets another program drive
  the client, or hides from or gets around anti-cheat software or any other protection.
- Don't link to places that distribute the game client or its files.
- Screenshots are fine; logs are fine once personal paths and names are removed.
- New dependencies must have a licence compatible with the GNU GPL v2, be added to `third_party` as source, and get
  an entry in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Contact

If you believe something in this repository infringes your rights, open an issue titled "Rights holder request" with
what and where it is. It will be taken down while it is looked at.

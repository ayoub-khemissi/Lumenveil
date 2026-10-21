# Contributing

Thanks for helping. Bug reports, zone looks, fixes and new effects are all welcome.

## Reporting a problem

Open an issue with:

- what you see, and where and when in the game (zone, time of day, weather);
- a screenshot, ideally a pair with Lumenveil on and off (Ctrl+F12);
- your `CoAVolFog.log` from that session;
- your graphics card, and whether you use DXVK, ReShade or texture packs.

## The one rule that protects the project

**Never attach, commit or link to game files or data made from them.** That includes extracted textures and models,
`.dbc`, `.db2` or `.adt` files or exports of them, the `.bin` files `Lumenveil-Setup.cmd` writes, and dumps of the
client's shaders, memory or code. Don't link to places that distribute the game client. Screenshots and logs are
fine. Pull requests that add such files are closed. See [LEGAL.md](LEGAL.md).

## Sharing a zone's look

The region panel (Ctrl+F3) writes `CoAVolFog-regions.ini`. To propose a look for a zone, open a pull request that adds
or changes its sections in `package/CoAVolFog-regions.ini`, with a line `; <Zone name> (zone <id>)` above them and a
before/after pair for each hour you tuned. Keep zone looks close to what the plain game shows for that zone: the aim
is the same world with more atmosphere, not a different one.

## Changing the code

1. Read [docs/BUILDING.md](docs/BUILDING.md) and [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md).
2. Keep to the code rules there: no comments in C++, HLSL and CMake; a harness check for every change of behaviour,
   which fails when the change is undone.
3. Run the harness, natively and if you can on DXVK, and say in the pull request what you saw in the game.
4. Anything that reads, calls or changes the running client first checks a hash of the client's code it relies on
   (`engine/src/client_code.h`), and turns itself off (with a line in the log) when it differs. Never copy the
   client's bytes into the source.
5. Lumenveil changes only what the player's screen shows. Changes that automate play, read what the game hides, talk
   to the server, give other programs a way to drive the client, or hide from or get around anti-cheat software or
   any other protection are not accepted.

## Licence

By contributing you agree that your contribution is released under the GNU GPL v2, like the rest of the project.
New third-party code must have a compatible licence and an entry in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

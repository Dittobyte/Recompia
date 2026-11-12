Pokopia PC @VERSION@ for Linux, SteamOS and the Steam Deck

1. On a system with glibc 2.38 or newer (Ubuntu 24.04, Fedora 39, Bazzite and later) the game runs as a native Linux program and needs no Proton. On an older system (Ubuntu 22.04, Debian 12, Mint 21), install Proton Experimental in Steam first (Library, Tools; on a Steam Deck switch to Desktop Mode first).
2. Run PokopiaLauncher.AppImage and pick your dump folder (base/ and updates/<version>/, each with exefs/ and romfs/) or a .nup. Version 2.0.1a is required.
3. Press Set up. The first time it downloads its build tools (Python and Zig, about 100 MB), then builds the game and the shader cache on this PC: the native game, or pkp.exe through Proton on an older system. You can pause and continue later.
4. Press Play. The game starts directly, or through Steam's Proton on an older system.

If PokopiaLauncher.AppImage does not start (a system without FUSE): ./PokopiaLauncher.AppImage --appimage-extract-and-run
From Steam (Game Mode on the Deck): add PokopiaLauncher.AppImage as a non-Steam game with the launch options --play.

Updating: extract the new release over your install. If the update changes the game code, the launcher says so: press Set up again and it rebuilds the game. The first release with the native game asks for Set up again once on glibc 2.38 or newer. Your shaders and saves are kept.

Community and help: https://discord.recompia.com

Licences: LICENSE.txt and licenses/.

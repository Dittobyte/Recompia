# source-sha256: dfe69f0a7815483099f203214ec0ee4eb78a925447fe65fef5ef14c2d3360eee
Esta es una traducción automática del texto en inglés; puede contener errores. El texto en inglés es la versión definitiva.

Pokopia PC @VERSION@ para Linux, SteamOS y Steam Deck

1. En un sistema con glibc 2.38 o posterior (Ubuntu 24.04, Fedora 39, Bazzite y posteriores), el juego se ejecuta como programa nativo de Linux y no necesita Proton. En un sistema más antiguo (Ubuntu 22.04, Debian 12, Mint 21), instala primero Proton Experimental en Steam (Biblioteca, Herramientas; en Steam Deck, cambia primero al modo de escritorio).
2. Ejecuta PokopiaLauncher.AppImage y elige la carpeta de tu volcado (base/ y updates/<version>/, cada una con exefs/ y romfs/) o un archivo .nup. Se necesita la versión 2.0.1a.
3. Pulsa Set up. La primera vez descarga las herramientas de compilación (Python y Zig, unos 100 MB) y luego compila el juego y la caché de sombreadores en este PC: el juego nativo, o pkp.exe mediante Proton en un sistema más antiguo. Puedes pausar y continuar más tarde.
4. Pulsa Play. El juego se inicia directamente, o a través de Proton de Steam en un sistema más antiguo.

Si el archivo PokopiaLauncher.AppImage no se inicia en un sistema que no tiene FUSE: ./PokopiaLauncher.AppImage --appimage-extract-and-run
Para iniciarlo desde Steam (modo Juego en Deck): añade PokopiaLauncher.AppImage como un juego que no sea de Steam y escribe --play en las opciones de lanzamiento.

Para actualizar, extrae los archivos de la nueva versión sobre los de tu instalación. Si la actualización cambia el código del juego, el launcher te avisa: pulsa Set up otra vez para que vuelva a compilar el juego. La primera versión con el juego nativo pide Set up otra vez una sola vez con glibc 2.38 o posterior. Tus sombreadores y partidas guardadas se conservan.

Comunidad y ayuda: https://discord.recompia.com

Licencias: consulta LICENSE.txt y la carpeta licenses/.

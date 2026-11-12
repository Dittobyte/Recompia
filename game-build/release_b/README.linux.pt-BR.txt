# source-sha256: dfe69f0a7815483099f203214ec0ee4eb78a925447fe65fef5ef14c2d3360eee
Esta é uma tradução automática do texto em inglês; ela pode conter erros. O texto em inglês é a versão oficial.

Pokopia PC @VERSION@ para Linux, SteamOS e Steam Deck

1. Em um sistema com glibc 2.38 ou mais recente (Ubuntu 24.04, Fedora 39, Bazzite e posteriores), o jogo roda como um programa nativo do Linux e não precisa do Proton. Em um sistema mais antigo (Ubuntu 22.04, Debian 12, Mint 21), instale antes o Proton Experimental no Steam (Biblioteca, Ferramentas; em um Steam Deck, mude antes para o Modo Desktop).
2. Execute o PokopiaLauncher.AppImage e escolha a pasta do seu dump (base/ e updates/<version>/, cada uma com exefs/ e romfs/) ou um arquivo .nup. A versão 2.0.1a é obrigatória.
3. Aperte Set up. Na primeira vez ele baixa as ferramentas de compilação (Python e Zig, cerca de 100 MB) e depois compila o jogo e o cache de shaders neste PC: o jogo nativo, ou o pkp.exe pelo Proton em um sistema mais antigo. Você pode pausar e continuar depois.
4. Aperte Play. O jogo inicia direto, ou pelo Proton do Steam em um sistema mais antigo.

Se o PokopiaLauncher.AppImage não iniciar (um sistema sem FUSE): ./PokopiaLauncher.AppImage --appimage-extract-and-run
Pelo Steam (Modo Jogo no Deck): adicione o PokopiaLauncher.AppImage como um jogo que não é do Steam, com as opções de inicialização --play.

Atualizando: extraia a nova versão por cima da sua instalação. Se a atualização mudar o código do jogo, o launcher avisa: aperte Set up again e ele recompila o jogo. A primeira versão com o jogo nativo pede Set up again uma vez com glibc 2.38 ou mais recente. Os seus shaders e saves são mantidos.

Comunidade e ajuda: https://discord.recompia.com

Licenças: LICENSE.txt e licenses/.

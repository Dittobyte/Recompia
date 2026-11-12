# source-sha256: dfe69f0a7815483099f203214ec0ee4eb78a925447fe65fef5ef14c2d3360eee
Esta é uma tradução automática do texto em inglês; pode conter erros. O texto em inglês é a versão oficial.

Pokopia PC @VERSION@ para Linux, SteamOS e Steam Deck

1. Num sistema com glibc 2.38 ou mais recente (Ubuntu 24.04, Fedora 39, Bazzite e posteriores), o jogo corre como um programa nativo de Linux e não precisa do Proton. Num sistema mais antigo (Ubuntu 22.04, Debian 12, Mint 21), instale primeiro o Proton Experimental no Steam (Biblioteca, Ferramentas; num Steam Deck, mude primeiro para o Modo de Ambiente de Trabalho).
2. Execute o PokopiaLauncher.AppImage e escolha a pasta do seu dump (base/ e updates/<version>/, cada uma com exefs/ e romfs/) ou um ficheiro .nup. A versão 2.0.1a é obrigatória.
3. Prima Set up. Da primeira vez, descarrega as ferramentas de compilação (Python e Zig, cerca de 100 MB) e depois compila o jogo e a cache de shaders neste PC: o jogo nativo, ou o pkp.exe através do Proton num sistema mais antigo. Pode pausar e continuar mais tarde.
4. Prima Play. O jogo arranca diretamente, ou através do Proton do Steam num sistema mais antigo.

Se o PokopiaLauncher.AppImage não arrancar (um sistema sem FUSE): ./PokopiaLauncher.AppImage --appimage-extract-and-run
A partir do Steam (Modo de Jogo no Deck): adicione o PokopiaLauncher.AppImage como um jogo que não é do Steam, com as opções de arranque --play.

Atualizar: extraia a nova versão por cima da sua instalação. Se a atualização mudar o código do jogo, o launcher avisa: prima Set up again e ele volta a compilar o jogo. A primeira versão com o jogo nativo pede Set up again uma vez com glibc 2.38 ou mais recente. Os seus shaders e saves são mantidos.

Comunidade e ajuda: https://discord.recompia.com

Licenças: LICENSE.txt e licenses/.

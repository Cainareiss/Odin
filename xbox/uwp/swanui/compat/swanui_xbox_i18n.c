/* SwanStation Xbox - the texts that differ on Xbox, consulted by tr() before its own table (i18n.c).
 * port-notes/specs/S5-xbox-ui.md section 3. Keys are the REF English literals byte for byte (or new strings used only
 * by SWANUI_XBOX code); English gets `en` (the key when NULL), Portuguese (pt-PT, as i18n.c) gets `pt`, and French,
 * Spanish and Japanese get the English Xbox text. Every text stays inside Latin-1 (ui/text.c packs only that range).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "swanui_xbox_ui.h"
#include "i18n.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    const char *key; /* the English text tr() is called with: the REF literal, exactly */
    const char *en;  /* the Xbox English text; NULL: the key itself (a new string, only in SWANUI_XBOX code) */
    const char *pt;  /* Portugal Portuguese, as the rest of i18n.c; NULL: the English one */
} XboxText;

static const XboxText XBOX_TEXTS[] = {
    /* ---- controller: touchpad, DualSense, PS5 controller, motion sensor */
    /*  1 settings_screen.c:259 */ {"Touchpad shortcuts", "{Touchpad} shortcuts", "Atalhos com {Touchpad}"},
    /*  2 settings_screen.c:1243 */ {"{Touchpad}: tap = {Select}, hold = menu", "{Touchpad}: tap = PS1 Select, hold = shortcuts",
                                    "{Touchpad}: toque = Select da PS1, manter = atalhos"},
    /*  3 settings_screen.c:255 */ {"Touchpad as a mouse", "Right stick as a mouse", "Analógico direito como rato"},
    /*  4 settings_screen.c:255 */ {"Games that support the PS1 Mouse (Discworld, Lemmings...): slide a finger on the {Touchpad} to move the pointer; {R2} or {Cross} clicks, {L2} or {Circle} right-clicks.",
        "Games that support the PS1 Mouse (Discworld, Lemmings...): the right stick moves the pointer; {R2} or {Cross} clicks, {L2} or {Circle} right-clicks.",
        "Jogos compatíveis com o rato PS1 (Discworld, Lemmings...): o analógico direito move o ponteiro; {R2} ou {Cross} clica, {L2} ou {Circle} faz clique direito."},
    /*  5 settings_screen.c:257 */ {"GunCon and Justifier games: point the controller at the screen to aim (motion sensor), {R2} fires, {L2} reloads, {R3} re-centres. Automatic turns it on in every game that supports a light gun.",
        "GunCon and Justifier games: the right stick moves the crosshair, {R2} fires, {L2} reloads, {R3} re-centres. Automatic turns it on in every game that supports a light gun.",
        "Jogos GunCon e Justifier: o analógico direito move a mira, {R2} dispara, {L2} recarrega, {R3} volta a centrar. Automático liga-o em todos os jogos compatíveis com pistola."},
    /*  6 main.c:2042 */ {"Light gun: point the controller at the screen, {R2} fires, {R3} re-centres",
        "Light gun: the right stick aims, {R2} fires, {R3} re-centres",
        "Pistola de luz: o analógico direito aponta, {R2} dispara, {R3} volta a centrar"},
    /*  7 settings_screen.c:239 */ {"Up to 4 with a multitap, for games that support it: each PS5 controller is a player.",
        "Up to 4 with a multitap, for games that support it: each controller is a player.",
        "Até 4 com um multitap, nos jogos compatíveis: cada comando é um jogador."},
    /*  8 settings_screen.c:247 (group) */ {"DualSense", "Feel and special controllers", "Vibração e comandos especiais"},
    /*  9 settings_screen.c:247 */ {"Classic as the game sends it; Soft is gentler; Punchy makes light hits easier to feel, and can shake the triggers too.",
        "Classic as the game sends it; Soft is gentler; Punchy makes light hits easier to feel.",
        "Clássica como o jogo a envia; Suave é mais leve; Intensa torna os toques leves mais fáceis de sentir."},
    /* 10 settings_screen.c:241 */ {"Choose which controller is player 1, 2, 3 and 4: press {Cross} on each in turn. Kept until Odin closes.",
        "Choose which controller is player 1, 2, 3 and 4: press {Cross} on each in turn. Kept until Odin closes.",
        "Escolha que comando é o jogador 1, 2, 3 e 4: prima {Cross} em cada um, à vez. Mantido até fechar o Odin."},
    /* 11 settings_screen.c:341 (S3 sec. 9.4: up to 512 MiB, off with run-ahead) */
    {"Keeps the last 8 seconds so you can go back ({Touchpad} + {L2}). Uses about 200 MB of memory.",
        "Keeps the last 8 seconds so you can go back ({Touchpad} + {L2}). Uses up to about 500 MB of memory, and is off while Run-ahead is on.",
        "Guarda os últimos 8 segundos para poder voltar ({Touchpad} + {L2}). Usa até cerca de 500 MB de memória e fica desligado com a Antecipação ligada."},

    /* ---- Display and core option helps */
    /* 12 settings_screen.c:177 (S3 shader table) */ {"Gives the picture the look of a CRT TV, a handheld LCD or the analog NTSC signal. The most detailed ones are heavy.",
        "Gives the picture the look of a CRT TV or of the analog NTSC signal, with Odin's shaders.",
        "Dá à imagem o aspeto de uma TV CRT ou do sinal NTSC analógico, com os shaders do Odin."},
    /* 13 settings_screen.c:192 (S3 sec. 12.4 Folders/Textures = <cache>\textures) */
    {"Loads HD texture packs. Over FTP, go to /data/Odin/cache/ and create the folder textures if it doesn't exist, then create your game's folder inside it, for example \"SCUS-94900\" (Crash Bandicoot), and put the pack's PNG files there. Needs the GPU renderer. Takes effect with the next game.",
        "Loads HD texture packs. With Device Portal, create Odin\\cache\\textures in the app's LocalState, then a folder named after the game's serial inside it, for example \"SCUS-94900\" (Crash Bandicoot), and put the pack's PNG files there. Needs the GPU renderer. Takes effect with the next game.",
        "Carrega pacotes de texturas HD. Com o Device Portal, crie Odin\\cache\\textures no LocalState da aplicação e, lá dentro, uma pasta com o número de série do jogo, por exemplo \"SCUS-94900\" (Crash Bandicoot), e coloque lá os ficheiros PNG do pacote. Requer o renderizador GPU. Aplica-se no próximo jogo."},
    /* 14 core/swanstation_options.c:459 (row 88) */ {"Odin: see the credits file in the package",
        "Replaces 'VRAM write textures' with texture packs in Odin's format from Odin\\cache\\textures\\<serial>. Needs the GPU renderer.",
        "Substitui as «texturas de escrita na VRAM» por pacotes de texturas no formato do Odin de Odin\\cache\\textures\\<número de série>. Requer o renderizador GPU."},

    /* ---- Achievements (S4 owns the mapping to DuckStation's notifications) */
    /* 15 settings_screen.c:279 (S4 table: 0 TopRight, 1 and 2 TopCenter) */ {"Banner: top right. Compact: a small line at the top. Big trophy: in the middle, like the PS5's.",
        "Where Odin shows the unlock banner: Banner, top right; Compact and Big trophy, top centre.",
        "Onde o Odin mostra o aviso de desbloqueio: Faixa, em cima à direita; Compacto e Troféu grande, em cima ao centro."},
    /* 16 settings_screen.c:273 */ {"Use Sign in below with your user name and password. Only a token stays on the PS5.",
        "Use Sign in below with your user name and password. Only a token stays on the console.",
        "Use Iniciar sessão abaixo com o nome de utilizador e a palavra-passe. Só um token fica na consola."},
    /* 17 settings_screen.c:107 */ {"Sign in to RetroAchievements with your user name and password, typed on the PS5.",
        "Sign in to RetroAchievements with your user name and password, typed on the console.",
        "Inicie sessão no RetroAchievements com o nome de utilizador e a palavra-passe, escritos na consola."},

    /* ---- Library and System tabs */
    /* 18 settings_screen.c:288 */ {"The look of Odin: colours, fonts and the shelf's background.",
        "The look of Odin: colours, fonts and the shelf's background.", "O aspeto do Odin: cores, letras e o fundo da prateleira."},
    /* 19 settings_screen.c:312 */ {"Looks for games added to /data/Odin/games or a USB drive.",
        "Looks for games added to Odin\\games or a USB drive.", "Procura jogos adicionados a Odin\\games ou a uma unidade USB."},
    /* 20 settings_screen.c:351 */ {"The language of Odin's menus.", "The language of Odin's menus.", "O idioma dos menus do Odin."},
    /* 21 settings_screen.c:345 */ {"Saves the game by itself every few minutes into three auto-save slots (the oldest is replaced). Load one from the Odin menu > Auto-saves.",
        "Saves the game by itself every few minutes into three auto-save slots (the oldest is replaced). Load one from the game menu > Auto-saves.",
        "Grava o jogo sozinho a cada poucos minutos em três ranhuras (a mais antiga é substituída). Carregue uma no menu do jogo > Gravações automáticas."},
    /* 22 settings_screen.c:317 */ {"Starts the console's BIOS with no disc, to manage memory cards. While Memory Card 1 Type (Restart) is set to Libretro, the saves will not show: that card belongs to each game, and with no game there is none.",
        "Starts the console's BIOS with no disc, to manage memory cards. While Memory Card 1 Type (Restart) is set to One per game, the saves will not show: that card belongs to each game, and with no game there is none.",
        "Inicia a BIOS da consola sem disco, para gerir os cartões de memória. Enquanto Tipo do cartão de memória 1 (reiniciar) estiver em Um por jogo, as gravações não aparecem: esse cartão pertence a cada jogo e, sem jogo, não existe."},
    /* 23 settings_screen.c:598 */ {"The console whose BIOS Start BIOS boots: it runs the BIOS chosen above for that region. NTSC-U unless you change it here; it goes back to NTSC-U every time Odin opens.",
        "The console whose BIOS Start BIOS boots: it runs the BIOS chosen above for that region. NTSC-U unless you change it here; it goes back to NTSC-U every time Odin opens.",
        "A consola cuja BIOS o Iniciar BIOS arranca: usa a BIOS escolhida acima para essa região. NTSC-U, a menos que a mude aqui; volta a NTSC-U sempre que o Odin abre."},
    /* 24 core/bios_choices.c:86 (S3 Q3) */ {"OpenBIOS (built into the core)", "OpenBIOS (openbios.bin in the bios folder)", "OpenBIOS (openbios.bin na pasta bios)"},
    /* 25 settings_screen.c:584 */ {"Which BIOS the games of this region run: detect it automatically, the OpenBIOS built into the core, or a file of the BIOS folder. It applies when the next game starts.",
        "Which BIOS the games of this region run: detect it automatically, OpenBIOS (openbios.bin in the bios folder), or a file of the BIOS folder. It applies when the next game starts.",
        "A BIOS que os jogos desta região usam: detetar automaticamente, a OpenBIOS (openbios.bin na pasta bios) ou um ficheiro da pasta de BIOS. Aplica-se quando o próximo jogo arranca."},
    /* 26 core/swanstation_options.c:109 (row 52 label; S3 sec. 4.1 maps it to one card per game) */
    {"Libretro (Default)", "One per game (Default)", "Um por jogo (predefinição)"},
    /* 27 settings_screen.c:317,582 (group caption) */ {"Odin", "Odin", "Odin"},
    /* 28 settings_screen.c:323 (S4 sec. 5.3 drive rules) */ {"A folder name to look for games in on drives. It is looked for on /mnt/ext0/, /mnt/ext1/ and /mnt/usb0/ to /mnt/usb7/ at once, as the number changes with the drives plugged in: psx searches /mnt/usb0/psx, /mnt/usb1/psx and so on. Leave it empty to turn it off.",
        "A folder name to look for games in at the root of every USB drive (E:, F:...), besides Odin and Odin\\games: psx searches E:\\psx, F:\\psx and so on. The drive must be NTFS and readable by ALL APPLICATION PACKAGES. Leave it empty to turn it off.",
        "Um nome de pasta onde procurar jogos na raiz de cada unidade USB (E:, F:...), além de Odin e Odin\\games: psx procura em E:\\psx, F:\\psx, etc. A unidade tem de ser NTFS e legível por ALL APPLICATION PACKAGES. Deixe vazio para desligar."},
    /* 29 settings_screen.c:2050 */ {"Type the folder name: games are looked for on /mnt/ext0/, /mnt/ext1/ and /mnt/usb0/ to /mnt/usb7/",
        "Type the folder name: games are looked for in it at the root of every USB drive (E:\\, F:\\...)",
        "Escreva o nome da pasta: os jogos são procurados nela na raiz de cada unidade USB (E:\\, F:\\...)"},

    /* ---- shelf, toasts, paths */
    /* 30 main.c:2323 */ {"Screenshot saved in /data/Odin/screenshots", "Screenshot saved in Odin\\screenshots",
        "Captura guardada em Odin\\screenshots"},
    /* 31 coverflow.c:1903 */ {"Hold to exit Odin...", "Hold {Circle} to close Odin...", "Mantenha {Circle} premido para fechar o Odin..."},
    /* 32 coverflow.c:2310 */ {"Keep holding to close Odin...", "Keep holding to close Odin...", "Mantenha premido para fechar o Odin..."},
    /* 33 coverflow.c:973 */ {"Odin can't open /data/Odin", "Odin can't write to its data folder",
        "O Odin não consegue escrever na sua pasta de dados"},
    /* 34 coverflow.c:1184 */ {"Can't list the covers folder: unlock /data in Settings, System", "Can't list the covers folder (Odin\\covers)",
        "Não é possível listar a pasta das capas (Odin\\covers)"},
    /* 35 coverflow.c:1185 */ {"No images yet: put .png or .jpg files in /data/Odin/covers/",
        "No images yet: put .png or .jpg files in Odin\\covers (Device Portal) or in the game's folder",
        "Ainda sem imagens: coloque ficheiros .png ou .jpg em Odin\\covers (Device Portal) ou na pasta do jogo"},
    /* 36 coverflow.c:1216 */ {"Images from /data/Odin/covers/ and the game's folder. Named like the game, they are used without picking.",
        "Images from Odin\\covers and the game's folder. Named like the game, they are used without picking.",
        "Imagens de Odin\\covers e da pasta do jogo. Com o nome do jogo, são usadas sem escolher."},
    /* 37 memcards_screen.c:351 */ {"One card per game. Import from /data/Odin/import.",
        "One card per game. Import from Odin\\import (copy the files there with Device Portal).",
        "Um cartão por jogo. Importe de Odin\\import (copie lá os ficheiros com o Device Portal)."},
    /* 38 memcards_screen.c:433 (arg: app.paths.root) */ {"Put .mcr, .mcd, .srm or .gme files in %s/import",
        "Put .mcr, .mcd, .srm or .gme files in Odin\\import%.0s with Device Portal",
        "Coloque ficheiros .mcr, .mcd, .srm ou .gme em Odin\\import%.0s com o Device Portal"},
    /* 39 memcards_screen.c:248 (arg: the exported file's full path) */ {"Exported to %s", "Exported to Odin\\memcards-export%.0s",
        "Exportado para Odin\\memcards-export%.0s"},
    /* 40 menu_screen.c:644 (arg: app.paths.cheats) */ {"Put .cht files in %s, or one next to the game.",
        "Put .cht files in Odin\\cheats%.0s, or one next to the game.", "Coloque ficheiros .cht em Odin\\cheats%.0s, ou um junto ao jogo."},
    /* 41 menu_screen.c:647 (R2 = Update cheats, :612-621) */ {"tools/Odin_sync.py cheats installs the libretro cheat library.",
        "Press {R2} to download this game's cheats from the libretro database.",
        "Prima {R2} para descarregar as batotas deste jogo da base de dados do libretro."},
    /* 42 stats_screen.c:118 */ {"played in Odin", "played in Odin", "jogados no Odin"},
    /* 43 profile_screen.c:161 */ {"Not signed in yet: the points show once Odin is online",
        "Not signed in yet: the points show once Odin is online", "Ainda sem sessão iniciada: os pontos aparecem quando o Odin estiver online"},
    /* 44 profile_screen.c:200 */ {"Unlocks earned in Odin show here.", "Unlocks earned in Odin show here.",
        "As conquistas obtidas no Odin aparecem aqui."},
    /* 45 main.c:650 */ {"Odin was updated to the latest version!", "Odin was updated to the latest version!",
        "O Odin foi atualizado para a versão mais recente!"},
    /* 46 tips.c:54 */ {"%d discs: in the game, Odin menu > Disc.", "%d discs: in the game, game menu > Disc.", "%d discos: no jogo, menu do jogo > Disco."},
    /* 47 tips.c:56 */ {"Has a manual: in the game, Odin menu > Manual.", "Has a manual: in the game, game menu > Manual.",
        "Tem manual: no jogo, menu do jogo > Manual."},
    /* 48 assets/game-tips.txt:14 (tips.c:94) */ {"Up to 4 players in Odin with the multitap (Settings > Controls > Players).",
        "Up to 4 players in Odin with the multitap (Settings > Controls > Players).",
        "Até 4 jogadores no Odin com o multitap (Definições > Controlos > Jogadores)."},
    /* 49 assets/game-tips.txt:15-20 */ {"Made for a light gun, which Odin doesn't support yet.",
        "A light gun game: the right stick aims (Settings > Controls > Light gun).",
        "Jogo de pistola de luz: o analógico direito aponta (Definições > Controlos > Pistola de luz)."},
    /* 50 assets/game-tips.txt:21 */ {"On a real console it builds levels from music CDs; Odin can't load audio CDs, so only its own songs play.",
        "On a real console it builds levels from music CDs; Odin can't load audio CDs, so only its own songs play.",
        "Numa consola real cria níveis a partir de CD de música; o Odin não lê CD de áudio, só as músicas do jogo tocam."},

    /* ---- new strings of the SWANUI_XBOX code in this spec (en NULL: the key is the English text) */
    /* 51 coverflow.c empty shelf (5.1) */ {"Games: on a USB drive (NTFS), in a Odin or psx folder, one folder per game.", NULL,
        "Jogos: numa unidade USB (NTFS), numa pasta Odin ou psx, uma pasta por jogo."},
    /* 52 */ {"On a PC, give ALL APPLICATION PACKAGES read access to the drive; on the console, set it up as Media.", NULL,
        "Num PC, dê a ALL APPLICATION PACKAGES acesso de leitura à unidade; na consola, configure-a como «Media»."},
    /* 53 */ {"Or copy them with Device Portal into LocalState\\Odin\\games.", NULL,
        "Ou copie-os com o Device Portal para LocalState\\Odin\\games."},
    /* 54 */ {"BIOS: your console's BIOS file (SCPH1001.BIN is recommended) in LocalState\\Odin\\bios.", NULL,
        "BIOS: o ficheiro da BIOS da sua consola (recomenda-se SCPH1001.BIN) em LocalState\\Odin\\bios."},
    /* 55 */ {"Then press {L3} to look for games.", NULL, "Depois prima {L3} para procurar jogos."},
    /* 56 settings_screen.c Xbox PAD table (4.6) */ {"LS (left stick)", NULL, "LS (analógico esquerdo)"},
    /* 57 */ {"RS (right stick)", NULL, "RS (analógico direito)"},
    /* 58 Xbox ABOUT table (6.2) */ {"This build", NULL, "Esta versão"},
    /* 59 */ {"Private Xbox port", NULL, "Port privado para Xbox"},
    /* 60 */ {"Credits", NULL, "Créditos"},
    /* 61 */ {"Began as", NULL, "Começou como"},
    /* 62 */ {"PSXS5 by SynoPiia", NULL, "PSXS5, de SynoPiia"},
    /* 63 */ {"Emulation core", NULL, "Núcleo de emulação"},
    /* 64 */ {"Licences", NULL, "Licenças"},
    /* 65 */ {"GPL-3.0+ and each core's own licence", NULL, "GPL-3.0+ e a licença de cada núcleo"},
    /* 66 */ {"Thanks", NULL, "Agradecimentos"},
    /* 67 */ {"Covers, fonts, icons, cheats...", NULL, "Capas, letras, ícones, batotas..."},
    /* 68 */ {"Odin: the Odin interface brought to the Xbox, running on the emulation core named under Credits.", NULL,
        "Odin: a interface do Odin trazida para a Xbox, a correr sobre o núcleo de emulação indicado em Créditos."},
    /* 69 */ {"An unofficial build for personal use on a console in Developer Mode. It is not published and must not be shared: the licences of its parts do not allow them to be distributed together (see Licences).", NULL,
        "Uma versão não oficial para uso pessoal numa consola em Modo de Programador. Não é publicada e não pode ser partilhada: as licenças das suas partes não permitem distribuí-las juntas (ver Licenças)."},
    /* 70 */ {"Never included: copy your own console's BIOS (SCPH1001.BIN is recommended) into Odin\\bios with Device Portal.", NULL,
        "Nunca incluída: copie a BIOS da sua consola (recomenda-se SCPH1001.BIN) para Odin\\bios com o Device Portal."},
    /* 71 */ {"Your own backups: in Odin\\games, or on a USB drive in Odin, Odin\\games or the Games folder on drives.", NULL,
        "As suas cópias: em Odin\\games, ou numa unidade USB em Odin, Odin\\games ou na pasta de jogos nas unidades."},
    /* 72 */ {"The USB drives the app can read, with their free space. A drive must be NTFS, readable by ALL APPLICATION PACKAGES, and set up as Media on the console.", NULL,
        "As unidades USB que a aplicação consegue ler, com o espaço livre. A unidade tem de ser NTFS, legível por ALL APPLICATION PACKAGES e configurada como «Media» na consola."},
    /* 73 */ {"Saves, states, covers and settings live here, in the app's LocalState (Device Portal > File explorer). Uninstalling the app deletes it.", NULL,
        "As gravações, estados, capas e definições ficam aqui, no LocalState da aplicação (Device Portal > File explorer). Desinstalar a aplicação apaga-a."},
    /* 74 */ {"Odin: see the credits file in the package", NULL,
        "Odin: see the credits file in the package"},
    /* 75 */ {"Odin: see the credits file in the package", NULL,
        "Odin: see the credits file in the package"},
    /* 76 */ {"Odin: see the credits file in the package", NULL,
        "Odin: see the credits file in the package"},
    /* 77 */ {"Odin: see the credits file in the package", NULL,
        "Odin: see the credits file in the package"},
    /* 78 */ {"RetroAchievements and rcheevos; xlenore/psx-covers and libretro-thumbnails (covers, art); the libretro-database contributors (cheats); sodaraptor's Hypnagogia Boundless Dreams Jewel Case (CC BY 4.0, adapted); Inter, Noto Sans JP and the theme fonts (SIL OFL 1.1); Tabler Icons, stb and Nayuki's QR Code generator. Full list: Odin's THIRD_PARTY_NOTICES.md.", NULL,
        "RetroAchievements e rcheevos; xlenore/psx-covers e libretro-thumbnails (capas, arte); os contribuidores da libretro-database (batotas); a caixa Hypnagogia Boundless Dreams Jewel Case de sodaraptor (CC BY 4.0, adaptada); Inter, Noto Sans JP e as letras dos temas (SIL OFL 1.1); Tabler Icons, stb e o QR Code generator de Nayuki. Lista completa: THIRD_PARTY_NOTICES.md do Odin."},
    /* 79 Shader labels (4.4) */ {"CRT Hyllian", NULL, NULL},

    /* ---- new toasts and errors of S3 and S4 (S3 Q3), Portuguese added here */
    /* 80 S3 sec. 4 */ {"BIOS not found: copy your console's BIOS file (SCPH1001.BIN is recommended) into the bios folder.", NULL,
        "BIOS não encontrada: copie o ficheiro da BIOS da sua consola (recomenda-se SCPH1001.BIN) para a pasta bios."},
    /* 81 S3 sec. 4 */ {"The previous game is still closing.", NULL, "O jogo anterior ainda está a fechar."},
    /* 82 S3 sec. 8.3 */ {"Rewind is not available while Run-ahead is on", NULL, "Retroceder não está disponível com a Antecipação ligada"},
    /* 83 S4 sec. 6.6 */ {"Not available on Xbox", NULL, "Não disponível na Xbox"},
    /* 84 S4 sec. 3 */ {"RetroAchievements: your sign-in expired, sign in again", NULL, "RetroAchievements: a sua sessão expirou, inicie sessão de novo"},
    /* ---- Tsugaru: the games folder picked with the Xbox folder picker (compat/swanui_xbox_folder.c) */
    /* 85 settings_screen.c */ {"Games folder", NULL, "Pasta de jogos"},
    /* 86 settings_screen.c */ {"Choose the folder with your games with the Xbox file picker. Games can be directly in it or one folder per game.", NULL,
        "Escolha a pasta com os seus jogos no selector de ficheiros da Xbox. Os jogos podem estar directamente nela ou numa pasta por jogo."},
    /* 87 coverflow.c */ {"Choose the folder with your games (USB drive or disk) with the Xbox file picker.", NULL,
        "Escolha a pasta com os seus jogos (pen ou disco USB) no selector de ficheiros da Xbox."},
    /* 88 coverflow.c */ {"Supported: .cue+.bin, .chd, .pbp, .iso, .img, .ecm, .m3u. All in one folder, or one folder per game.", NULL,
        "Suportados: .cue+.bin, .chd, .pbp, .iso, .img, .ecm, .m3u. Todos numa pasta, ou uma pasta por jogo."},
    /* 89 coverflow.c */ {"Current folder: %s", NULL, "Pasta actual: %s"},
    /* 90 coverflow.c */ {"BIOS: included in Odin.", NULL, "BIOS: incluída no Odin."},
    /* 91 coverflow.c */ {"BIOS: put SCPH1001.BIN in LocalState\\Odin\\bios (Device Portal).", NULL,
        "BIOS: coloque SCPH1001.BIN em LocalState\\Odin\\bios (Device Portal)."},
    /* 92 coverflow.c */ {"Press {Cross} to select the games folder", NULL, "Prima {Cross} para escolher a pasta de jogos"},
    /* 93 swanui_xbox_folder.c */ {"Games folder selected: looking for games...", NULL, "Pasta de jogos escolhida: a procurar jogos..."},
    /* 94 swanui_xbox_folder.c */ {"No folder selected", NULL, "Nenhuma pasta escolhida"},
};

#if defined(TSUGARU_UWP)
/* TSUGARU_UWP (FM Towns): texts that say PS1, BIOS, DuckStation or PlayStation in the table above or in the REF literals,
 * rewritten for the FM Towns. Consulted before XBOX_TEXTS, so an entry here wins. Same keys, byte for byte. */
static const XboxText TOWNS_TEXTS[] = {
    {"{Touchpad}: tap = {Select}, hold = menu", "{Touchpad}: tap = Select, hold = shortcuts",
     "{Touchpad}: toque = Select, manter = atalhos"},
    {"Games that support the PS1 Mouse (Discworld, Lemmings...): slide a finger on the {Touchpad} to move the pointer; {R2} or {Cross} clicks, {L2} or {Circle} right-clicks.",
     "Games that support the mouse (many Odin games do): the right stick moves the pointer; {R2} or {Cross} clicks, {L2} or {Circle} right-clicks.",
     "Jogos compatíveis com o rato (muitos jogos do Odin): o analógico direito move o ponteiro; {R2} ou {Cross} clica, {L2} ou {Circle} faz clique direito."},
    {"BIOS", "ROM files", "Ficheiros ROM"},
    {"Never included: copy your own console's BIOS (SCPH1001.BIN is recommended) into Odin\\bios with Device Portal.",
     "Built in: the Odin system ROMs ship with Odin. Your own set (FMT_SYS.ROM, FMT_DOS.ROM, FMT_FNT.ROM, FMT_DIC.ROM) in LocalState\\Odin\\bios, or in a folder named rom inside the games folder, is used instead when present.",
     "Incluídos: as ROMs de sistema do Odin vêm com o Odin. O seu próprio conjunto (FMT_SYS.ROM, FMT_DOS.ROM, FMT_FNT.ROM, FMT_DIC.ROM) em LocalState\\Odin\\bios, ou numa pasta chamada rom dentro da pasta de jogos, é usado em vez delas quando existe."},
    {"BIOS not found: copy your console's BIOS file (SCPH1001.BIN is recommended) into the bios folder.",
     "ROM files not found: the built-in set is missing from the package and none was found in LocalState\\Odin\\bios.",
     "Ficheiros ROM não encontrados: o conjunto incluído falta no pacote e não há nenhum em LocalState\\Odin\\bios."},
    {"Supported: .cue+.bin, .chd, .pbp, .iso, .img, .ecm, .m3u. All in one folder, or one folder per game.",
     "Supported: CD images (.cue+.bin, .iso, .mds+.mdf) and floppy images (.d77, .d88, .img). All in one folder, or one folder per game.",
     "Suportados: imagens de CD (.cue+.bin, .iso, .mds+.mdf) e de disquete (.d77, .d88, .img). Todos numa pasta, ou uma pasta por jogo."},
    {"BIOS: included in Odin.", "ROM: the Odin system ROMs are built in.", "ROM: as ROMs de sistema do Odin estão incluídas."},
    {"BIOS: put SCPH1001.BIN in LocalState\\Odin\\bios (Device Portal).",
     "ROM: none found. Put FMT_SYS.ROM, FMT_DOS.ROM, FMT_FNT.ROM and FMT_DIC.ROM in LocalState\\Odin\\bios (Device Portal).",
     "ROM: nenhuma encontrada. Ponha FMT_SYS.ROM, FMT_DOS.ROM, FMT_FNT.ROM e FMT_DIC.ROM em LocalState\\Odin\\bios (Device Portal)."},
    /* ---- TSUGARU_UWP branding: Tsugaru is the product; SwanStationPS5 is credited as the interface's origin */
    {"Odin: the Odin interface brought to the Xbox, running on the emulation core named under Credits.",
     "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
    {"An unofficial build for personal use on a console in Developer Mode. It is not published and must not be shared: the licences of its parts do not allow them to be distributed together (see Licences).",
     "An unofficial build for a console in Developer Mode. Interface GPL-3.0-or-later, emulator BSD-3-Clause: if it is passed on, its source code must go with it (GPL). The system ROMs are built in; your games are your own.",
     "Uma versão não oficial para uma consola em Modo de Programador. Interface GPL-3.0-or-later, emulador BSD-3-Clause: se for passada a alguém, o código-fonte tem de ir com ela (GPL). As ROMs de sistema estão incluídas; os jogos são seus."},
    {"The person behind Odin.", "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
    {"Odin: see the credits file in the package",
     "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
    {"RetroAchievements and rcheevos; xlenore/psx-covers and libretro-thumbnails (covers, art); the libretro-database contributors (cheats); sodaraptor's Hypnagogia Boundless Dreams Jewel Case (CC BY 4.0, adapted); Inter, Noto Sans JP and the theme fonts (SIL OFL 1.1); Tabler Icons, stb and Nayuki's QR Code generator. Full list: Odin's THIRD_PARTY_NOTICES.md.",
     "Kasanova (UNZ) and the Free ODIN Project (compatible ROMs, ysflight.com); sodaraptor's Hypnagogia Boundless Dreams Jewel Case (CC BY 4.0, adapted); Inter, Noto Sans JP and the theme fonts (SIL OFL 1.1); Tabler Icons, stb and Nayuki's QR Code generator. ODIN is a trademark of Fujitsu.",
     "Kasanova (UNZ) e o Free ODIN Project (ROMs compatíveis, ysflight.com); a caixa Hypnagogia Boundless Dreams Jewel Case de sodaraptor (CC BY 4.0, adaptada); Inter, Noto Sans JP e as letras dos temas (SIL OFL 1.1); Tabler Icons, stb e o QR Code generator de Nayuki. ODIN é uma marca da Fujitsu."},
    {"Covers, fonts, icons, cheats...", "ROMs, fonts, icons, the jewel case...", "ROMs, letras, ícones, a caixa..."},
    {"Games: on a USB drive (NTFS), in a Odin or psx folder, one folder per game.",
     "Games: on a USB drive (NTFS), in a Odin or fmtowns folder, one folder per game.",
     "Jogos: numa unidade USB (NTFS), numa pasta Odin ou fmtowns, uma pasta por jogo."},
    {"A folder name to look for games in on drives. It is looked for on /mnt/ext0/, /mnt/ext1/ and /mnt/usb0/ to /mnt/usb7/ at once, as the number changes with the drives plugged in: psx searches /mnt/usb0/psx, /mnt/usb1/psx and so on. Leave it empty to turn it off.",
     "A folder name to look for games in at the root of every USB drive (E:, F:...), besides Odin and Odin\\games: fmtowns searches E:\\fmtowns, F:\\fmtowns and so on. The drive must be NTFS and readable by ALL APPLICATION PACKAGES.",
     "Um nome de pasta onde procurar jogos na raiz de cada unidade USB (E:, F:...), além de Odin e Odin\\games: fmtowns procura em E:\\fmtowns, F:\\fmtowns, etc. A unidade tem de ser NTFS e legível por ALL APPLICATION PACKAGES."},
    /* covers: there is no public FM Towns cover repository, so nothing is downloaded */
    {"Fetches covers for new games over the internet.",
     "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
    {"No images yet: put .png or .jpg files in /data/Odin/covers/",
     "No images yet: put cover.png (or <title>.png) beside the game, or .png/.jpg files in LocalState\\Odin\\covers (Device Portal)",
     "Ainda sem imagens: ponha cover.png (ou <título>.png) junto ao jogo, ou ficheiros .png/.jpg em LocalState\\Odin\\covers (Device Portal)"},
    {"Shows the game's own artwork around a 4:3 picture instead of the border, when there is one (downloaded with the covers).",
     "Shows the game's own artwork around a 4:3 picture instead of the border, when a bezel.png lies beside the game.",
     "Mostra a arte do próprio jogo à volta de uma imagem 4:3 em vez da moldura, quando há um bezel.png junto ao jogo."},
    {"Scan for the GitHub page", "Scan for the interface's GitHub page", "Leia para a página GitHub da interface"},
    {"GPL-3.0+ and each core's own licence", "GPL-3.0+ and BSD-3-Clause", "GPL-3.0+ e BSD-3-Clause"},
    {"Odin: see the credits file in the package",
     "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
    {"Odin: see the credits file in the package",
     "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
};
#endif

#if defined(ARES_UWP)
/* ARES_UWP: ares, many systems. Consulted before TOWNS_TEXTS and XBOX_TEXTS, so the FM Towns and PS1 wording above never
 * shows; "Odin" and "Odin" left in the other tables are turned into "ares" by swanui_xbox_brand(). Same keys,
 * byte for byte; en NULL: the key is the English text (a string only ARES_UWP code uses). */
static const XboxText ARES_TEXTS[] = {
    /* ---- BIOS: per system, found by its contents */
    {"BIOS", "BIOS", "BIOS"},
    /* the About rows' literals (settings_screen.c ABOUT) still say SwanStation: the keys here are those literals */
    {"Never included: copy your own console's BIOS (SCPH1001.BIN is recommended) into Odin\\bios with Device Portal.",
     "Never included. BIOS files go in LocalState\\Odin\\bios (Device Portal): they are recognised by their contents (SHA-256), any file name. PS1, Mega CD, PC Engine CD, Neo Geo, Neo Geo Pocket, Game Boy Advance, ColecoVision, MSX and the Famicom Disk System need theirs.",
     "Nunca incluídos. Os ficheiros de BIOS vão em LocalState\\Odin\\bios (Device Portal): são reconhecidos pelo conteúdo (SHA-256), com qualquer nome. PS1, Mega CD, PC Engine CD, Neo Geo, Neo Geo Pocket, Game Boy Advance, ColecoVision, MSX e Famicom Disk System precisam do seu."},
    {"BIOS not found: copy your console's BIOS file (SCPH1001.BIN is recommended) into the bios folder.",
     "This system's BIOS was not found: put it in LocalState\\Odin\\bios. It is recognised by its contents (SHA-256), any file name.",
     "A BIOS deste sistema não foi encontrada: coloque-a em LocalState\\Odin\\bios. É reconhecida pelo conteúdo (SHA-256), com qualquer nome."},
    {"Your own backups: in Odin\\games, or on a USB drive in Odin, Odin\\games or the Games folder on drives.",
     "Your own backups: in LocalState\\Odin\\games, in the folder chosen under Games folder, or on a USB drive in Odin, Odin\\games, roms or roms\\games. Loose, or one folder per system or per game.",
     "As suas cópias: em LocalState\\Odin\\games, na pasta escolhida em Pasta de jogos, ou numa unidade USB em Odin, Odin\\games, roms ou roms\\games. Soltas, ou uma pasta por sistema ou por jogo."},
    {"BIOS files go in LocalState\\Odin\\bios: they are recognised by their contents (SHA-256), any file name.", NULL,
     "Os ficheiros de BIOS vão em LocalState\\Odin\\bios: são reconhecidos pelo conteúdo (SHA-256), com qualquer nome."},
    {"No files in the bios folder yet", NULL, "Ainda sem ficheiros na pasta bios"},
    {"%d files in the bios folder", NULL, "%d ficheiros na pasta bios"},
    {"1 file in the bios folder", NULL, "1 ficheiro na pasta bios"},

    /* ---- the shelf */
    {"Supported: .cue+.bin, .chd, .pbp, .iso, .img, .ecm, .m3u. All in one folder, or one folder per game.",
     "Cartridges (.sfc .nes .gb .gbc .gba .md .sms .gg .pce .ws .ngp .col .a26 .rom .zip...), discs (.cue+.bin, .chd, .m3u) and tapes (.tzx .tap). Loose, or one folder per system or per game.",
     "Cartuchos (.sfc .nes .gb .gbc .gba .md .sms .gg .pce .ws .ngp .col .a26 .rom .zip...), discos (.cue+.bin, .chd, .m3u) e cassetes (.tzx .tap). Soltos, ou uma pasta por sistema ou por jogo."},
    {"System", NULL, "Sistema"},
    {"Fetches covers for new games over the internet.",
     "Covers are not fetched in the Odin shell. A cover.png or <title>.png beside the game, or in LocalState\\Odin\\covers, is shown.",
     "As capas não são descarregadas no shell Odin. Um cover.png ou <título>.png junto ao jogo, ou em LocalState\\Odin\\covers, é mostrado."},
    {"The shelf shows 3D jewel cases. Off: the flat covers with a reflection on the floor, as in version 1.1.8.",
     "Every game stands in 3D: discs in their jewel case (the Mega CD's US games in their tall case), cartridges in their system's box, tapes in a cassette case. Off: every game is a flat cover with a reflection on the floor.",
     "Todos os jogos ficam em 3D: os discos na sua caixa de CD (os jogos americanos do Mega CD na caixa alta), os cartuchos na caixa do seu sistema, as cassetes numa caixa de cassete. Desligado: todos os jogos são capas planas com reflexo no chão."},
    {"Jewel cases", "3D cases", "Caixas 3D"},
    {"The light on the cover in the jewel case: its shading and the glare over the lid. Off: the picture as it is.",
     "The light on the cover of the 3D cases and boxes: its shading and the glare over the plastic or the print. Off: the picture as it is.",
     "A luz na capa das caixas 3D: o sombreamento e o brilho no plástico ou na impressão. Desligado: a imagem como ela é."},
    {"When a game starts, its disc slides out of the case and spins.",
     "When a disc game starts, its disc slides out of the case and spins. Cartridges fade to the game.",
     "Quando um jogo em disco arranca, o disco sai da caixa e gira. Os cartuchos passam ao jogo com um fundido."},
    {"A folder name to look for games in on drives. It is looked for on /mnt/ext0/, /mnt/ext1/ and /mnt/usb0/ to /mnt/usb7/ at once, as the number changes with the drives plugged in: psx searches /mnt/usb0/psx, /mnt/usb1/psx and so on. Leave it empty to turn it off.",
     "A folder name to look for games in at the root of every USB drive (E:, F:...), besides Odin, Odin\\games, roms and roms\\games. The drive must be NTFS and readable by ALL APPLICATION PACKAGES.",
     "Um nome de pasta onde procurar jogos na raiz de cada unidade USB (E:, F:...), além de Odin, Odin\\games, roms e roms\\games. A unidade tem de ser NTFS e legível por ALL APPLICATION PACKAGES."},

    /* ---- About: ares, and the interface it wears */
    {"Odin: the Odin interface brought to the Xbox, running on the emulation core named under Credits.",
     "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
    {"An unofficial build for personal use on a console in Developer Mode. It is not published and must not be shared: the licences of its parts do not allow them to be distributed together (see Licences).",
     "An experimental build for a console in Developer Mode. The shell is GPL-3.0-or-later: its source code (the Odin repository and the commit used) must go with any copy. No BIOS, no game and no artwork is included.",
     "Versão experimental para uma consola em Modo de Programador. O shell é GPL-3.0-or-later: o código-fonte (o repositório Odin e o commit usado) tem de ir com qualquer cópia. Não inclui BIOS, jogos nem arte."},
    {"The person behind Odin.",
     "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
    {"Odin: see the credits file in the package",
     "Odin: see the credits file in the package",
     "Odin: see the credits file in the package"},
    {"RetroAchievements and rcheevos; xlenore/psx-covers and libretro-thumbnails (covers, art); the libretro-database contributors (cheats); sodaraptor's Hypnagogia Boundless Dreams Jewel Case (CC BY 4.0, adapted); Inter, Noto Sans JP and the theme fonts (SIL OFL 1.1); Tabler Icons, stb and Nayuki's QR Code generator. Full list: Odin's THIRD_PARTY_NOTICES.md.",
     "sodaraptor's Hypnagogia Boundless Dreams Jewel Case (CC BY 4.0, adapted); Inter, Noto Sans JP and the theme fonts (SIL OFL 1.1); Tabler Icons, stb and Nayuki's QR Code generator. Full list: Odin's THIRD_PARTY_NOTICES.md.",
     "a caixa Hypnagogia Boundless Dreams Jewel Case de sodaraptor (CC BY 4.0, adaptada); Inter, Noto Sans JP e as letras dos temas (SIL OFL 1.1); Tabler Icons, stb e o QR Code generator de Nayuki. Lista completa: THIRD_PARTY_NOTICES.md do Odin."},
    {"Covers, fonts, icons, cheats...", "higan, covers, fonts, icons...", "higan, capas, letras, ícones..."},
    {"GPL-3.0+ and each core's own licence", "GPL-3.0+", "GPL-3.0+"},
    {"Odin: see the credits file in the package",
     "Odin, by Réda Chérif-Touil, GPL-3.0-or-later (github.com/Tovarichtch/Odin). The emulator core is not in this build yet: this is the experimental shell v0.1.",
     "Odin, de Réda Chérif-Touil, GPL-3.0-or-later (github.com/Tovarichtch/Odin). O núcleo do emulador ainda não está nesta versão: é o shell experimental v0.1."},
    {"Odin: see the credits file in the package",
     "Interface: GPL-3.0-or-later. The systems' names are trademarks of their owners (Nintendo, Sega, Sony, NEC, SNK, Bandai, Atari, Coleco, Microsoft...) and Xbox of Microsoft; none is involved. An unofficial build; the games and BIOS files are yours.",
     "Interface: GPL-3.0-or-later. Os nomes dos sistemas são marcas dos seus donos (Nintendo, Sega, Sony, NEC, SNK, Bandai, Atari, Coleco, Microsoft...) e Xbox da Microsoft; nenhum está envolvido. Uma versão não oficial; os jogos e os ficheiros de BIOS são seus."},
    {"Emulator: https://github.com/Tovarichtch/Odin", NULL, "Emulador: https://github.com/Tovarichtch/Odin"},

    /* ---- Display > HDR (notes/CONTRACT.md section 3) */
    {"HDR", NULL, "HDR"},
    {"Peak brightness", NULL, "Brilho máximo"},
    {"Paper white", NULL, "Branco de referência"},
    {"Game HDR boost", NULL, "Realce HDR do jogo"},
    {"Subtle", NULL, "Subtil"},
    {"Off: the picture is always SDR. Auto: HDR when the display supports it (and the console is set to HDR). On: HDR whenever the console can send it.", NULL,
     "Desligado: a imagem é sempre SDR. Automático: HDR quando o ecrã o suporta (e a consola está em HDR). Ligado: HDR sempre que a consola o consiga enviar."},
    {"The brightest your display can show, in nits. Set it to your TV's peak (HDR settings of the console or the TV's manual): highlights are mapped up to it and not beyond.", NULL,
     "O máximo de brilho que o ecrã consegue mostrar, em nits. Defina-o para o pico da sua TV (definições HDR da consola ou manual da TV): os realces vão até ele e não além."},
    {"How bright white is in the menus and in pictures without HDR, in nits. 200 suits most rooms; raise it in a bright room.", NULL,
     "Quão brilhante é o branco nos menus e nas imagens sem HDR, em nits. 200 serve à maioria das salas; aumente-o numa sala clara."},
    {"Expands the game picture (made for SDR TVs) into HDR: highlights and bright colours rise towards the peak brightness. Off shows it as SDR at paper white.", NULL,
     "Expande a imagem do jogo (feita para TVs SDR) para HDR: os realces e as cores fortes sobem em direção ao brilho máximo. Desligado mostra-a como SDR com o branco de referência."},
    {"Your display: HDR is on.", NULL, "O seu ecrã: o HDR está ligado."},
    {"Your display supports HDR; the picture is SDR now.", NULL, "O seu ecrã suporta HDR; a imagem está em SDR agora."},
    {"Your display does not report HDR support.", NULL, "O seu ecrã não indica suporte para HDR."},

    /* ---- the game menu combinations (main.c game_screen, settings_screen.c Controls > Game menu) */
    {"Hold {L3} + {R3}, {L1} + {R1} or {Touchpad} + {Options} for the menu.", NULL,
     "Mantenha {L3} + {R3}, {L1} + {R1} ou {Touchpad} + {Options} premidos para abrir o menu."},
    {"Game menu", NULL, "Menu do jogo"},
    {"Open the game menu", NULL, "Abrir o menu do jogo"},
    {"During a game, any of these opens the game menu, whatever the system: {L3} + {R3} held for the time below, {L1} + {R1} held for 1 second, or {Touchpad} + {Options} held for half a second.", NULL,
     "Durante um jogo, qualquer uma destas abre o menu do jogo, seja qual for o sistema: {L3} + {R3} premidos durante o tempo abaixo, {L1} + {R1} premidos durante 1 segundo, ou {Touchpad} + {Options} premidos durante meio segundo."},
    {"How long {L3} + {R3} must be held to open the game menu. With Immediate, it opens as soon as both are pressed. {L1} + {R1} always takes 1 second and {Touchpad} + {Options} half a second.", NULL,
     "Quanto tempo {L3} + {R3} têm de estar premidos para abrir o menu do jogo. Com Imediato, abre assim que ambos são premidos. {L1} + {R1} demora sempre 1 segundo e {Touchpad} + {Options} meio segundo."},

    /* ---- the game menu: the running system's own settings (menu_screen.c) */
    {"%s settings", NULL, "Definições: %s"},
    {"System settings", NULL, "Definições do sistema"},
    {"General settings", NULL, "Definições gerais"},
    {"Restart to apply changes", NULL, "Reiniciar para aplicar"},
    {"Game restarted with the new settings", NULL, "Jogo reiniciado com as novas definições"},

    /* ---- Settings > Systems and a system's page (settings_screen.c) */
    {"Systems", NULL, "Sistemas"},
    {"General", NULL, "Geral"},
    {"Other systems", NULL, "Outros sistemas"},
    {"The options of this system only: region, picture, sound, controllers and the hardware it emulates. Each system keeps its own, for all its games; the ones marked with a clock apply when a game starts.", NULL,
     "As opções só deste sistema: região, imagem, som, comandos e o hardware que emula. Cada sistema guarda as suas, para todos os seus jogos; as marcadas com um relógio aplicam-se quando um jogo arranca."},
    {"%d option", NULL, "%d opção"},
    {"%d options", NULL, "%d opções"},
    {"Kept for every game of this system. The game running now takes most changes at once.", NULL,
     "Guardadas para todos os jogos deste sistema. O jogo que está a correr aplica a maioria das alterações de imediato."},
    {"Kept for every game of this system. A few options appear once one of its games has run.", NULL,
     "Guardadas para todos os jogos deste sistema. Algumas opções só aparecem depois de um dos seus jogos ter corrido."},
    {"This system has no options of its own.", NULL, "Este sistema não tem opções próprias."},
    {"Default: %s", NULL, "Predefinição: %s"},
    {"Restart the game to apply (game menu)", NULL, "Reinicie o jogo para aplicar (menu do jogo)"},
    {"Restart to apply", NULL, "Reiniciar para aplicar"},
    {"Next start", NULL, "No próximo arranque"},
    {"All defaults", NULL, "Tudo predefinido"},
    {"%s: every option back to its default", NULL, "%s: todas as opções voltaram à predefinição"},

    /* ---- the options' groups, names, values and helps (uwp/ares_host/host_ares_opts.cpp) */
    {"Audio", NULL, "Áudio"},
    {"Hardware", NULL, "Hardware"},
    {"Media", NULL, "Suportes"},
    {"Controllers", NULL, "Comandos"},
    {"Preferred region", NULL, "Região preferida"},
    {"USA (NTSC-U)", NULL, "EUA (NTSC-U)"},
    {"Japan (NTSC-J)", NULL, "Japão (NTSC-J)"},
    {"Europe (PAL)", NULL, "Europa (PAL)"},
    {"The console a game made for several regions runs on: its model, 60 or 50 Hz, and often the language. A game made for one region always runs as that region. Automatic prefers USA, then Japan, then Europe.", NULL,
     "A consola em que corre um jogo feito para várias regiões: o modelo, 60 ou 50 Hz e muitas vezes o idioma. Um jogo feito para uma só região corre sempre como essa região. Automático prefere EUA, depois Japão, depois Europa."},
    {"Skip the startup logo", NULL, "Saltar o logótipo de arranque"},
    {"Starts the game at once, without the console's own startup logo and sound.", NULL,
     "Inicia o jogo de imediato, sem o logótipo e o som de arranque da própria consola."},
    {"Pixel accuracy", NULL, "Precisão ao píxel"},
    {"Draws the picture dot by dot as the real video chip does, for the few games with effects changed in the middle of a line. It needs much more processing power: leave it off unless a game shows glitches.", NULL,
     "Desenha a imagem ponto a ponto como o chip de vídeo real, para os poucos jogos com efeitos alterados a meio de uma linha. Exige muito mais processamento: deixe-a desligada, a menos que um jogo mostre falhas."},
    {"SH-2 recompiler", NULL, "Recompilador SH-2"},
    {"Runs the 32X's two SH-2 processors through a recompiler, much faster. Off: the interpreter, slower, for a game that misbehaves.", NULL,
     "Corre os dois processadores SH-2 do 32X através de um recompilador, muito mais rápido. Desligado: o interpretador, mais lento, para um jogo que se porte mal."},
    {"TMSS licence screen", NULL, "Ecrã de licença TMSS"},
    {"Shows the \"Produced by or under license from Sega\" screen of the later Mega Drive models before the game.", NULL,
     "Mostra o ecrã \"Produced by or under license from Sega\" dos modelos mais recentes da Mega Drive antes do jogo."},
    {"Homebrew mode", NULL, "Modo homebrew"},
    {"Starts the console in the same state every time (a fixed random seed), as homebrew and test programs expect. Leave it off for commercial games.", NULL,
     "Arranca a consola sempre no mesmo estado (uma semente aleatória fixa), como esperam os programas homebrew e de teste. Deixe-o desligado nos jogos comerciais."},
    {"Game Boy Player", NULL, "Game Boy Player"},
    {"Runs the game as on the GameCube's Game Boy Player: the games made for it rumble the controller.", NULL,
     "Corre o jogo como no Game Boy Player da GameCube: os jogos feitos para ele fazem vibrar o comando."},
    {"Colour emulation", NULL, "Emulação de cor"},
    {"Shows the colours as the console's own screen did: paler on a handheld LCD, softer on a TV. Off: the raw colours, brighter and more saturated.", NULL,
     "Mostra as cores como o próprio ecrã da consola: mais pálidas num LCD portátil, mais suaves numa TV. Desligado: as cores em bruto, mais vivas e saturadas."},
    {"Screen colours", NULL, "Cores do ecrã"},
    {"Game Boy (green)", NULL, "Game Boy (verde)"},
    {"Game Boy Pocket (grey)", NULL, "Game Boy Pocket (cinzento)"},
    {"Plain grey", NULL, "Cinzento simples"},
    {"The shades of the screen: the green of the first Game Boy, the grey of the Game Boy Pocket, or plain grey.", NULL,
     "Os tons do ecrã: o verde do primeiro Game Boy, o cinzento do Game Boy Pocket, ou cinzento simples."},
    {"Interframe blending", NULL, "Mistura entre imagens"},
    {"Mixes each picture with the one before, as the slow handheld screen did, so flickering sprites and transparency effects look as intended. Off: sharper motion.", NULL,
     "Mistura cada imagem com a anterior, como o ecrã lento da portátil fazia, para que sprites intermitentes e transparências fiquem como pretendido. Desligado: movimento mais nítido."},
    {"Deep black boost", NULL, "Pretos mais profundos"},
    {"Darkens the darkest shades, so blacks look deep instead of grey.", NULL,
     "Escurece os tons mais escuros, para que os pretos pareçam profundos em vez de cinzentos."},
    {"Show overscan", NULL, "Mostrar overscan"},
    {"Shows the edges of the picture an old TV hid behind its frame; they often hold blank space or garbage.", NULL,
     "Mostra as margens da imagem que uma TV antiga escondia atrás da moldura; muitas vezes têm espaço vazio ou lixo."},
    {"Colour bleed", NULL, "Sangramento de cor"},
    {"Blurs neighbouring dots sideways as a composite TV signal did, so dithered patterns blend into the colours the artists meant.", NULL,
     "Esbate os pontos vizinhos na horizontal como o sinal de TV composto fazia, para que os padrões de pontos se fundam nas cores que os artistas pretendiam."},
    {"Screen orientation", NULL, "Orientação do ecrã"},
    {"Turned 90°", NULL, "Rodado 90°"},
    {"Upside down", NULL, "Invertido"},
    {"Turned 270°", NULL, "Rodado 270°"},
    {"Horizontal", NULL, "Horizontal"},
    {"Vertical", NULL, "Vertical"},
    {"Turns the picture, for games played with the console held sideways.", NULL,
     "Roda a imagem, para jogos jogados com a consola de lado."},
    {"Games played with the WonderSwan held upright turn the picture. Automatic follows the game.", NULL,
     "Os jogos jogados com a WonderSwan na vertical rodam a imagem. Automático segue o jogo."},
    {"LCD icons", NULL, "Ícones do LCD"},
    {"Shows the icons printed beside the WonderSwan's screen (battery, sound, orientation) that games light up.", NULL,
     "Mostra os ícones impressos ao lado do ecrã da WonderSwan (bateria, som, orientação) que os jogos acendem."},
    {"Headphones", NULL, "Auscultadores"},
    {"With headphones plugged in, the console plays in stereo; without them, in mono through its speaker.", NULL,
     "Com auscultadores ligados, a consola toca em estéreo; sem eles, em mono pelo altifalante."},
    {"CPU revision", NULL, "Revisão do processador"},
    {"Which revision of the console's processor is emulated. A few games behave differently on one of them; the default is the most common. Takes effect when the game starts again.", NULL,
     "Que revisão do processador da consola é emulada. Alguns jogos comportam-se de forma diferente numa delas; a predefinição é a mais comum. Aplica-se quando o jogo voltar a arrancar."},
    {"PPU1 revision", NULL, "Revisão da PPU1"},
    {"Which revision of the first video chip is emulated. Takes effect when the game starts again.", NULL,
     "Que revisão do primeiro chip de vídeo é emulada. Aplica-se quando o jogo voltar a arrancar."},
    {"PPU2 revision", NULL, "Revisão da PPU2"},
    {"Which revision of the second video chip is emulated; a few games show small differences. Takes effect when the game starts again.", NULL,
     "Que revisão do segundo chip de vídeo é emulada; alguns jogos mostram pequenas diferenças. Aplica-se quando o jogo voltar a arrancar."},
    {"Video memory", NULL, "Memória de vídeo"},
    {"64 KiB, as every console shipped; 128 KiB is a development-board upgrade only some homebrew uses. Takes effect when the game starts again.", NULL,
     "64 KiB, como todas as consolas vinham; 128 KiB é uma expansão de placas de desenvolvimento que só algum homebrew usa. Aplica-se quando o jogo voltar a arrancar."},
    {"VDP revision", NULL, "Revisão do VDP"},
    {"The video chip's revision: 1 (the first Japanese consoles) has quirks a few games rely on, 2 is the later one. Takes effect when the game starts again.", NULL,
     "A revisão do chip de vídeo: a 1 (as primeiras consolas japonesas) tem peculiaridades de que alguns jogos dependem, a 2 é a posterior. Aplica-se quando o jogo voltar a arrancar."},
    {"A setting of the emulated console. Takes effect when the game starts again.", NULL,
     "Uma definição da consola emulada. Aplica-se quando o jogo voltar a arrancar."},
    {"A setting of the emulated console.", NULL, "Uma definição da consola emulada."},
    {"Disk side", NULL, "Lado do disco"},
    {"Turns the disk over or puts the other disk in: the drive is emptied first and the chosen side goes in a moment later, as on the real Disk System.", NULL,
     "Vira o disco ou insere o outro disco: a unidade é esvaziada primeiro e o lado escolhido entra um momento depois, como no Disk System verdadeiro."},
    {"Ejected", NULL, "Ejetado"},
    {"Disk 1: Side A", NULL, "Disco 1: lado A"},
    {"Disk 1: Side B", NULL, "Disco 1: lado B"},
    {"Disk 2: Side A", NULL, "Disco 2: lado A"},
    {"Disk 2: Side B", NULL, "Disco 2: lado B"},
    {"Controller port", NULL, "Porta de comando"},
    {"Controller port 1", NULL, "Porta de comando 1"},
    {"Controller port 2", NULL, "Porta de comando 2"},
    {"The device plugged into this port. Automatic: what the game expects (a pad, or the paddle or light gun of the games made for one).", NULL,
     "O dispositivo ligado a esta porta. Automático: o que o jogo espera (um comando, ou o paddle ou a pistola de luz dos jogos feitos para eles)."},
    {"The device plugged into this port. Automatic: what the game expects (a pad, or the paddle or light gun of the games made for one). Takes effect when the game starts again.", NULL,
     "O dispositivo ligado a esta porta. Automático: o que o jogo espera (um comando, ou o paddle ou a pistola de luz dos jogos feitos para eles). Aplica-se quando o jogo voltar a arrancar."},
};
#endif

const char *swanui_xbox_tr(const char *english, int lang)
{
    if (!english || !english[0])
        return NULL;
#if defined(ARES_UWP)
    for (size_t i = 0; i < sizeof(ARES_TEXTS) / sizeof(ARES_TEXTS[0]); ++i)
    {
        const XboxText *t = &ARES_TEXTS[i];
        if (t->key[0] != english[0] || strcmp(t->key, english) != 0)
            continue;
        if (lang == LANG_PT && t->pt)
            return t->pt;
        return t->en ? t->en : t->key;
    }
#endif
#if defined(TSUGARU_UWP)
    for (size_t i = 0; i < sizeof(TOWNS_TEXTS) / sizeof(TOWNS_TEXTS[0]); ++i)
    {
        const XboxText *t = &TOWNS_TEXTS[i];
        if (t->key[0] != english[0] || strcmp(t->key, english) != 0)
            continue;
        if (lang == LANG_PT && t->pt)
            return t->pt;
        return t->en ? t->en : t->key;
    }
#endif
    /* a linear walk: about 90 rows, first-character filter; no lazy hash, so worker threads may call it too */
    for (size_t i = 0; i < sizeof(XBOX_TEXTS) / sizeof(XBOX_TEXTS[0]); ++i)
    {
        const XboxText *t = &XBOX_TEXTS[i];
        if (t->key[0] != english[0] || strcmp(t->key, english) != 0)
            continue;
        if (lang == LANG_PT && t->pt)
            return t->pt;
        return t->en ? t->en : t->key;
    }
    return NULL;
}

/* ---- Tsugaru: the product name on screen (TSUGARU_UWP: BRAND_TO is the emulator's name) ----
 * The REF texts name SwanStationPS5 in many places (toasts, help, the About card). Rather than a hunk per text,
 * ui/text.c passes every string it measures or draws through here. Results are cached by content, since the
 * strings come from literals and from stack buffers alike; the table never evicts (a pointer handed out stays
 * valid), and when it is full the original text is returned. A spin lock makes it safe for worker threads. */
#include <windows.h>
#include <stdbool.h>
#include <stdlib.h>

#if defined(ARES_UWP)
/* ARES_UWP: the product is ares; the Tsugaru port's own wording in the tables above ("LocalState\Odin\covers",
 * "Hold {Circle} to close Odin...") is rewritten too */
static const char *const BRAND_FROM[] = {"Odin", "Odin", "PSXS5", "Odin"};
#if defined(ODIN_UWP)
static const char BRAND_TO[] = "Odin";
#else
static const char BRAND_TO[] = "ares";
#endif /* ARES_UWP: the emulator's name, written as its authors write it */
#else
static const char *const BRAND_FROM[] = {"Odin", "Odin", "PSXS5"};
static const char BRAND_TO[] = "Odin"; /* TSUGARU_UWP: the emulator's name */
#endif

typedef struct
{
    char *from, *to;
} BrandEntry;

static BrandEntry s_brand[192];
static int s_brand_count;
static volatile LONG s_brand_lock;

static bool brand_needed(const char *s)
{
    if (strstr(s, "Odin: see the credits file in the package") || strstr(s, "SynoPiia") || strstr(s, "THIRD_PARTY"))
        return false; /* TSUGARU_UWP: the credits name the original frontend as it is */
#if defined(ARES_UWP)
    if (strstr(s, "Odin"))
        return true;
#endif
    return strstr(s, "Odin") != NULL || strstr(s, "PSXS5") != NULL;
}

/* s with every BRAND_FROM replaced by BRAND_TO; malloc'd, or NULL */
static char *brand_rewrite(const char *s)
{
    size_t cap = strlen(s) * 2 + 64;
    char *out = (char *)malloc(cap);
    if (!out)
        return NULL;
    size_t n = 0;
    while (*s)
    {
        size_t skip = 0;
        for (size_t i = 0; i < sizeof(BRAND_FROM) / sizeof(BRAND_FROM[0]) && !skip; ++i)
        {
            const size_t len = strlen(BRAND_FROM[i]);
            if (strncmp(s, BRAND_FROM[i], len) == 0)
                skip = len;
        }
        const char *piece = skip ? BRAND_TO : s;
        const size_t piece_len = skip ? sizeof(BRAND_TO) - 1 : 1;
        if (n + piece_len + 1 > cap)
        {
            cap = cap * 2 + piece_len;
            char *grown = (char *)realloc(out, cap);
            if (!grown)
            {
                free(out);
                return NULL;
            }
            out = grown;
        }
        memcpy(out + n, piece, piece_len);
        n += piece_len;
        s += skip ? skip : 1;
    }
    out[n] = '\0';
    return out;
}

const char *swanui_xbox_brand(const char *s)
{
    if (!s || !brand_needed(s))
        return s;
    while (InterlockedCompareExchange(&s_brand_lock, 1, 0) != 0)
        ;
    const char *result = s;
    int i = 0;
    for (; i < s_brand_count; ++i)
        if (strcmp(s_brand[i].from, s) == 0)
            break;
    if (i < s_brand_count)
        result = s_brand[i].to;
    else if (s_brand_count < (int)(sizeof(s_brand) / sizeof(s_brand[0])))
    {
        char *from = _strdup(s), *to = brand_rewrite(s);
        if (from && to)
        {
            s_brand[s_brand_count].from = from;
            s_brand[s_brand_count].to = to;
            ++s_brand_count;
            result = to;
        }
        else
        {
            free(from);
            free(to);
        }
    }
    InterlockedExchange(&s_brand_lock, 0);
    return result;
}

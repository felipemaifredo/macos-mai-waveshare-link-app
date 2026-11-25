# Waveshare Link

App de barra de menus do macOS que conversa com a tela touch **Waveshare ESP32-S3-Touch-LCD-7**, pela rede Wi-Fi ou por cabo USB. Vários computadores podem ficar conectados à placa ao mesmo tempo, e um botão no topo da tela troca qual deles aparece.

A tela tem até sete páginas. Para trocar de página, use o seletor no topo ou deslize o dedo para o lado:

- **Uso**: o uso do Claude, do Codex e do Antigravity. O Waveshare Link busca esses números sozinho, do mesmo jeito que o app [AI Usage](../macos-mai-ai-usage-app): ao conectar, a cada 5 min e no botão ↻. Os dois apps são independentes: o AI Usage mostra o uso na barra do topo, e o Waveshare Link manda para a placa.
- **Tocando**: música, artista, álbum, capa e progresso do que toca no Music ou no Spotify, com Anterior, Play/Pausa e Próxima.
- **Atalhos**: até 12 botões. De fábrica: Captura (abre a barra do ⌘⇧5), Microfone (silencia o microfone padrão; fica vermelho quando mudo), Apagar tela, Volume −, Volume + e Mudo.
- **Mac**: CPU (com os últimos 2 minutos), memória, bateria e rede.
- **Docker**: os containers, com estado, imagem e status. Só leitura.
- **Hora**: relógio, data e o tempo em Cachoeiro de Itapemirim (Open-Meteo).

**Relógio sem computador**: com Wi-Fi, a placa acerta a hora sozinha (NTP) e busca o tempo a cada 30 min (Open-Meteo por HTTP; o fuso vem da resposta). Enquanto nenhum computador está conectado, ela mostra o relógio em tela cheia; tocar nele abre a lista de computadores. O local do tempo vem do Mac ou do PC (`lat`/`lon`/`city` na mensagem `clock`) e fica gravado na placa.
- **Mixer**: volume independente com sliders verticais táteis para Saída Master do Mac, Mídia (Spotify/Music) e Microfone, além de seletor rápido do dispositivo de saída de áudio (Speakers, AirPods, Fone, etc.).
- **Apps**: grid de até 12 botões customizáveis para abrir aplicativos no Mac (App Launcher).
- **Discord**: controle dedicado de chamada com status, botão Mutar/Desmutar, Ensurdecer (Deafen) e Desconectar.

### Ajustes

Em **Ajustes…** (no menu do app, ou ⌘,) dá para:

- **Abas**: ligar, desligar e reordenar as abas. Pelo menos uma fica ligada. Uma aba desligada não coleta dados, e com Uso desligado o app nem busca o uso das IAs.
- **Atalhos**: criar, remover, reordenar e editar os botões da página Atalhos (nome, ícone e ação). A ação pode ser uma das prontas (captura, microfone, apagar tela, volume, mudo, play/pausa, anterior, próxima), abrir um app, abrir uma URL ou rodar um Atalho do macOS.
- **Apps**: configurar os botões dedicados da página Apps com escolha direta dos aplicativos instalados no Mac.

A placa muda na hora, sem regravar o firmware. Os ajustes ficam salvos no app (UserDefaults) e são reenviados sempre que a placa conecta.

No topo da tela, o botão com o nome do computador mostra quem está na tela (ícone verde de Wi-Fi ou USB). Toque nele para ver os computadores conectados e trocar. Aparecem também os selos **MIC** e câmera enquanto algum app usa o microfone ou uma câmera.

O ícone na barra de menus fica apagado enquanto o Mac não está na tela da placa. O menu mostra por onde a placa está conectada. O Waveshare Link não mostra o uso na barra do topo.

### Wi-Fi e vários computadores

1. Com a placa no **USB**, abra *Ajustes… → Conexão*, digite o nome da rede (SSID) e a senha e clique em **Enviar para a placa**. **Redes ocultas funcionam**: digite o nome exato da rede (maiúsculas e minúsculas contam). A placa só usa redes de **2,4 GHz**. As credenciais ficam gravadas na placa (NVS).
2. A placa se anuncia na rede (`waveshare.local`, serviço `_wslink._tcp`, porta TCP 7270) e o app a encontra sozinho. Se a rede bloquear essa busca (mDNS), preencha o IP da placa em *Endereço da placa*. O macOS pede a permissão de **Rede Local** na primeira vez.
3. Na primeira conexão pela rede, a placa mostra um **código de 6 dígitos** e o Mac pede esse código. Depois disso o Mac se conecta sozinho. Se o pedido for fechado, use *Parear pelo Wi-Fi…* no menu.

Com o Wi-Fi funcionando, o Mac usa a rede e deixa o USB em silêncio (para não aparecer duas vezes na placa). Se a rede cair, volta para o USB sozinho. Dá para desligar o Wi-Fi em *Conexão*.

**Trocar de computador**: toque no botão com o nome do computador, no topo da tela. Só o computador escolhido recebe os toques e manda dados; os outros continuam conectados, mas param de coletar. Ao trocar, a placa redesenha com as abas, os atalhos e os dados do computador escolhido. O nome de cada um vem de *Conexão → Nome deste Mac na placa*.

**Segurança**: pela rede, só computadores pareados conseguem falar com a placa. O token do pareamento não volta a passar pela rede: a cada conexão o Mac prova que o conhece com um HMAC sobre um desafio da placa, e a placa prova o mesmo de volta (assim nada na rede consegue se passar pela placa para disparar atalhos no Mac). A conexão não é criptografada, então quem estiver na mesma rede consegue ler os dados que aparecem na tela. Na lista de computadores da placa, *Esquecer pareamentos do Wi-Fi* apaga todos. O USB não precisa de pareamento.

### Processamento

O uso das IAs só é buscado com a placa conectada. Fora isso, o Mac só coleta os dados da página que está na tela: a placa avisa qual página está visível, e Mac, Docker e Hora só rodam enquanto a página deles está aberta. Tocando e Microfone não fazem polling; reagem a notificações do sistema. As câmeras são conferidas a cada 3 s enquanto a placa está conectada.

App nativo em Swift/AppKit. Não usa Xcode.

## Build

```bash
bash build.sh
```

O script compila os `.swift`, monta `build/Waveshare Link.app` e instala o app em `/Applications`. Depois do build, feche o app (Sair) e abra de novo.

A janela de Ajustes usa SwiftUI sem `@State`: no SDK atual o `@State` é uma macro, e o `swiftc` sem Xcode não tem o plugin de macros do SwiftUI. Use `@StateObject`/`ObservableObject`.

### Permissões

Na primeira vez que o Music ou o Spotify tocar, o macOS pede permissão de **Automação** para o Waveshare Link controlar cada um. Ela é necessária para ler a música e a capa.

#### Acessibilidade

As teclas de mídia e de volume são eventos de teclado simulados, e o macOS exige a permissão de **Acessibilidade** para isso. No primeiro toque, o app pede essa permissão. Libere o "Waveshare Link" em *Ajustes do Sistema → Privacidade e Segurança → Acessibilidade*. O app é assinado ad-hoc, então o macOS o trata como um app novo a cada build, e a permissão precisa ser liberada de novo. Captura e Apagar tela funcionam sem essa permissão.

## Estrutura

```
main.swift          # porta USB (SerialLink), atalhos, página Uso, barra de menus
BoardLink.swift     # conexão com a placa: handshake, pareamento, host ativo, Wi-Fi ou USB
NetLink.swift       # transporte Wi-Fi (Bonjour + TCP)
UsageProviders.swift # provedores Claude e Antigravity, copiados do AI Usage (mantenha os dois iguais), e Codex
NowPlaying.swift    # página Tocando (Music/Spotify, capa em JPEG 220x220)
AudioMixer.swift    # página Mixer (CoreAudio master/dispositivos, Spotify/Music e mic)
MicControl.swift    # microfone (CoreAudio) e câmera em uso (CoreMediaIO)
SystemStats.swift   # página Mac
DockerFeed.swift    # página Docker (docker ps)
ClockWeather.swift  # página Hora (hora + Open-Meteo)
Settings.swift      # Ajustes: abas, atalhos, apps e conexão (modelo, janela SwiftUI, execução dos atalhos)
Info.plist          # bundle "Waveshare Link" (local.waveshare-link), LSUIElement (sem ícone no Dock)
build.sh            # compila, empacota, assina ad-hoc e instala em /Applications
windows/            # app do Windows (C# / .NET 8, bandeja), mesmo protocolo; ver windows/README.md
firmware/           # firmware da placa (PlatformIO + Arduino + LVGL 9): main.cpp (hosts, pareamento), net.cpp (Wi-Fi, servidor, mDNS),
                    # ui.cpp (topo, seletor de computador, Uso, Atalhos) e ui_*.cpp (demais páginas)
```

## Firmware

### Gravar

1. Instale a extensão **PlatformIO IDE** no VS Code.
2. Ligue a placa pela porta **USB**, direto no Mac. Use um cabo USB-C **de dados** e não use dock nem hub.
3. Feche o Waveshare Link, que ocupa a porta.
4. Na pasta `firmware/`, rode `pio run -t upload`, ou clique em *Upload* (→) na barra do PlatformIO.

Não grave pela porta **UART**. O conversor dessa porta (WCH CH343) corrompe os dados com o driver do macOS, e a gravação falha com "Checksum error". A porta UART ainda serve para a conexão normal com o app.

Se a placa sumir do USB, desconecte todos os cabos para ela desligar por completo e ligue de novo. O pino EXIO5 (USB_SEL) do expansor CH422G precisa ficar em nível baixo. Em nível alto, ele desvia o USB para o transceptor CAN, e a placa desaparece do Mac. Esse estado só volta ao normal quando a placa desliga.

### Atalhos

Os botões vêm do Mac (Ajustes). `DEFAULT_KEYS` em `firmware/src/ui.cpp` só vale até a primeira conexão. Os ícones possíveis estão em `ICONS`, no mesmo arquivo, e precisam bater com `Catalog.icons`, em `Settings.swift`. O relógio usa a Montserrat embutida em `font_montserrat_ttf.c` (renderizada pelo tiny_ttf do LVGL). Cada botão envia `key:<id>`. O Mac procura o atalho com esse id nos Ajustes e roda a ação (`KeyRunner`); as ações prontas ficam em `DeckActions`, em `main.swift`.

## Protocolo

É um objeto JSON por linha, igual no USB (115200 baud) e no Wi-Fi (TCP, porta 7270). "Mac" abaixo vale para qualquer computador (host).

**Conexão (v2)**

| Direção | Mensagem |
|---|---|
| Placa → host | `{"t":"hello","dev":"ai-usage-display","v":2,"id":<MAC da placa>}`; no Wi-Fi também `"nonce"` (desafio desta conexão). No USB, a cada 1 s enquanto ninguém está ligado na porta |
| Host → placa | `{"t":"probe"}` (USB): pede um `hello` (o app abriu a porta depois dos hellos) |
| Host → placa | `{"t":"auth","hostId","name","os"}`; no Wi-Fi também `"nonce"` e `"hmac"` = HMAC-SHA256(token, `"host:"+nonce da placa`) |
| Placa → host | `{"t":"auth","ok":true,"id"}` (no Wi-Fi também `"hmac"` = HMAC-SHA256(token, `"board:"+nonce do host`)) ou `{"t":"auth","ok":false}` |
| Host → placa | `{"t":"pair","hostId","name","os"}` → a placa mostra um código; `{"t":"pair","code":"123456",…}` |
| Placa → host | `{"t":"pair","state":"code\|wrong\|failed\|busy\|nocode\|cancelled"}` e `{"t":"paired","token","id"}` |
| Placa → host | `{"t":"active","on":true\|false}`: se este host está na tela. Ao ficar ativo, o host reenvia o `layout` e o estado das páginas |
| Placa → host | `{"t":"net","state":"off\|connecting\|connected","ssid","ip","rssi"}` ao autenticar e quando muda |
| Host → placa | `{"t":"wifi","ssid","pass"}` (só pelo USB; SSID vazio esquece a rede), `{"t":"name","name"}`, `{"t":"bye"}` (sai da lista) |
| Host → placa | `{"t":"ping"}` a cada 2 s; a placa responde `{"t":"pong"}` a hosts v2 |

Hosts v1 (sem `auth`) funcionam só pelo USB: qualquer mensagem liga o host. Pelo Wi-Fi, a placa ignora tudo antes do `auth` ou do pareamento. `press` e `page` só vão para o host ativo, e só os dados dele chegam à tela.

**Páginas**

| Direção | Mensagem |
|---|---|
| Placa → Mac | `{"t":"hello"}` a cada 1 s até o Mac responder |
| Placa → Mac | `{"t":"press","id":"refresh"}` ou `{"t":"press","id":"key:<id>"}` quando alguém toca num botão |
| Placa → Mac | `{"t":"page","id":"usage\|now\|keys\|mac\|docker\|clock\|audio\|apps\|discord"}` ao trocar de página e ao conectar |
| Placa → Mac | `{"t":"press","id":"vol:...\|mute:...\|outdev:...\|app:<id>\|discord:mute\|deaf\|disconnect"}` |
| Mac → Placa | `{"t":"layout","pages":[ids na ordem],"keys":[{"id","icon","text"}],"apps":[{"id","icon","text"}]}` ao conectar e ao mudar os Ajustes |
| Mac → Placa | `{"t":"audio","master":{"vol","muted"},"media":{"app","vol","muted"},"mic":{"vol","muted"},"activeDev","devices":[{"id","name","active"}]}` ao conectar e ao mudar volumes ou dispositivos |
| Mac → Placa | `{"t":"discord","running","muted","deafened"}` |
| Mac → Placa | `{"t":"usage","providers":[{"name","pct","err","busy","at","rows":[{"sec","label","pct","reset"}]}]}` |
| Mac → Placa | `{"t":"now","app","title","artist","album","playing","pos","dur"}` e `{"t":"art","jpg":"<base64>"}` (JPEG baseline 220x220) |
| Mac → Placa | `{"t":"mic","muted","micInUse","camInUse"}` |
| Mac → Placa | `{"t":"mac","cpu","mem","memUsed","memTotal","bat","charging","ac","rx","tx"}` a cada 2 s, só com a página Mac aberta |
| Mac → Placa | `{"t":"docker","up","containers":[{"name","image","state","status"}]}` a cada 5 s, só com a página Docker aberta |
| Mac → Placa | `{"t":"clock","epoch","tz","city","lat","lon","temp","min","max","code"}` ao conectar e a cada minuto com a página Hora aberta. `epoch` acerta o relógio da placa; `lat`/`lon`/`city` dizem onde buscar o tempo |
| Mac → Placa | `{"t":"ping"}` a cada 2 s. Se a placa ficar 8 s sem receber nada de um host, ele sai da lista |

O app testa as portas `/dev/cu.usbmodem*`, `cu.usbserial*` e `cu.wchusbserial*`. Uma porta que não responde `hello` em 6 s é ignorada até ser desconectada. Ao conectar, o Mac reenvia o estado de todas as páginas. As mensagens saem por uma fila, porque uma capa leva uns 3 s pela porta UART. Os textos chegam sem acentos porque as fontes da placa só têm ASCII.

Para testar sem a placa, use `WAVESHARE_LINK_SERIAL=/dev/ttysNNN` para apontar o app para um pty.

O Claude é lido com o token do Claude Code (Keychain, item `Claude Code-credentials`) na API `api.anthropic.com/api/oauth/usage`. O Codex é lido com o login do Codex (`~/.codex/auth.json`) na API do ChatGPT `chatgpt.com/backend-api/wham/usage`, a mesma do `/status` do Codex; se a resposta não for entendida, usa o último registro de limites que o Codex gravou em `~/.codex/sessions`. O Antigravity é lido da API local do hub (`~/.gemini/antigravity/daemon/hub_*.json`). Se uma dessas APIs mudar, corrija `UsageProviders.swift` aqui e no AI Usage.

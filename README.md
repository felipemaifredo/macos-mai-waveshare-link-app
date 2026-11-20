# Waveshare Link

App de barra de menus do macOS que conversa com a tela touch **Waveshare ESP32-S3-Touch-LCD-7**, ligada ao Mac por cabo USB.

A tela tem até seis páginas. Para trocar de página, use o seletor no topo ou deslize o dedo para o lado:

- **Uso**: o uso do Claude e do Antigravity. O Waveshare Link busca esses números sozinho, do mesmo jeito que o app [AI Usage](../macos-mai-ai-usage-app): ao conectar, a cada 5 min e no botão ↻. Os dois apps são independentes: o AI Usage mostra o uso na barra do topo, e o Waveshare Link manda para a placa.
- **Tocando**: música, artista, álbum, capa e progresso do que toca no Music ou no Spotify, com Anterior, Play/Pausa e Próxima.
- **Atalhos**: até 12 botões. De fábrica: Captura (abre a barra do ⌘⇧5), Microfone (silencia o microfone padrão; fica vermelho quando mudo), Apagar tela, Volume −, Volume + e Mudo.
- **Mac**: CPU (com os últimos 2 minutos), memória, bateria e rede.
- **Docker**: os containers, com estado, imagem e status. Só leitura.
- **Hora**: relógio, data e o tempo em Cachoeiro de Itapemirim (Open-Meteo).

### Ajustes

Em **Ajustes…** (no menu do app, ou ⌘,) dá para:

- **Abas**: ligar, desligar e reordenar as abas. Pelo menos uma fica ligada. Uma aba desligada não coleta dados, e com Uso desligado o app nem busca o uso das IAs.
- **Atalhos**: criar, remover, reordenar e editar os botões da página Atalhos (nome, ícone e ação). A ação pode ser uma das prontas (captura, microfone, apagar tela, volume, mudo, play/pausa, anterior, próxima), abrir um app, abrir uma URL ou rodar um Atalho do macOS.

A placa muda na hora, sem regravar o firmware. Os ajustes ficam salvos no app (UserDefaults) e são reenviados sempre que a placa conecta.

No topo da tela, o ícone USB fica verde com o Mac conectado. Aparecem também os selos **MIC** e câmera enquanto algum app usa o microfone ou uma câmera.

O ícone na barra de menus fica apagado enquanto a placa não está conectada. O menu mostra a porta da placa. O Waveshare Link não mostra o uso na barra do topo.

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
main.swift          # conexão USB, atalhos, página Uso, barra de menus
UsageProviders.swift # provedores Claude e Antigravity, copiados do AI Usage (mantenha os dois iguais)
NowPlaying.swift    # página Tocando (Music/Spotify, capa em JPEG 220x220)
MicControl.swift    # microfone (CoreAudio) e câmera em uso (CoreMediaIO)
SystemStats.swift   # página Mac
DockerFeed.swift    # página Docker (docker ps)
ClockWeather.swift  # página Hora (hora + Open-Meteo)
Settings.swift      # Ajustes: abas e atalhos (modelo, janela SwiftUI, execução dos atalhos)
Info.plist          # bundle "Waveshare Link" (local.waveshare-link), LSUIElement (sem ícone no Dock)
build.sh            # compila, empacota, assina ad-hoc e instala em /Applications
firmware/           # firmware da placa (PlatformIO + Arduino + LVGL 9): ui.cpp (topo, Uso, Atalhos) e ui_*.cpp (demais páginas)
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

É um objeto JSON por linha, a 115200 baud:

| Direção | Mensagem |
|---|---|
| Placa → Mac | `{"t":"hello"}` a cada 1 s até o Mac responder |
| Placa → Mac | `{"t":"press","id":"refresh"}` ou `{"t":"press","id":"key:<id>"}` quando alguém toca num botão |
| Placa → Mac | `{"t":"page","id":"usage\|now\|keys\|mac\|docker\|clock"}` ao trocar de página e ao conectar |
| Mac → Placa | `{"t":"layout","pages":[ids na ordem],"keys":[{"id","icon","text"}]}` ao conectar e ao mudar os Ajustes |
| Mac → Placa | `{"t":"usage","providers":[{"name","pct","err","busy","at","rows":[{"sec","label","pct","reset"}]}]}` |
| Mac → Placa | `{"t":"now","app","title","artist","album","playing","pos","dur"}` e `{"t":"art","jpg":"<base64>"}` (JPEG baseline 220x220) |
| Mac → Placa | `{"t":"mic","muted","micInUse","camInUse"}` |
| Mac → Placa | `{"t":"mac","cpu","mem","memUsed","memTotal","bat","charging","ac","rx","tx"}` a cada 2 s, só com a página Mac aberta |
| Mac → Placa | `{"t":"docker","up","containers":[{"name","image","state","status"}]}` a cada 5 s, só com a página Docker aberta |
| Mac → Placa | `{"t":"clock","epoch","tz","city","temp","min","max","code"}` ao conectar e a cada minuto com a página Hora aberta |
| Mac → Placa | `{"t":"ping"}` a cada 2 s. Se a placa ficar 8 s sem receber nada, volta a mostrar "Aguardando o Mac..." |

O app testa as portas `/dev/cu.usbmodem*`, `cu.usbserial*` e `cu.wchusbserial*`. Uma porta que não responde `hello` em 6 s é ignorada até ser desconectada. Ao conectar, o Mac reenvia o estado de todas as páginas. As mensagens saem por uma fila, porque uma capa leva uns 3 s pela porta UART. Os textos chegam sem acentos porque as fontes da placa só têm ASCII.

Para testar sem a placa, use `WAVESHARE_LINK_SERIAL=/dev/ttysNNN` para apontar o app para um pty.

O Claude é lido com o token do Claude Code (Keychain, item `Claude Code-credentials`) na API `api.anthropic.com/api/oauth/usage`. O Antigravity é lido da API local do hub (`~/.gemini/antigravity/daemon/hub_*.json`). Se uma dessas APIs mudar, corrija `UsageProviders.swift` aqui e no AI Usage.

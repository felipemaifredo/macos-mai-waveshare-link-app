# Roteiro: continuar o app do Windows (para o Claude, rodando no Windows)

> Escrito em 2026-11-20 no Mac, onde o código foi escrito **sem compilar** (não havia .NET lá, por escolha do usuário). Ao abrir uma sessão no Windows, comece por aqui.

## Contexto em 30 segundos

- Projeto: tela touch Waveshare ESP32-S3 (firmware em `firmware/`) + app do Mac (Swift, raiz) + app do Windows (`windows/`, C# / .NET 8, WinForms de bandeja).
- A placa já está no Wi-Fi (rede oculta "Rede Felipe", IP 192.168.0.110 no último teste) e aceita até 3 computadores pela rede. O Mac já conecta e pareia. O botão no topo da placa troca o computador ativo.
- Protocolo: README da raiz, seção **Protocolo** (uma linha JSON por mensagem, TCP 7270, auth HMAC, pareamento por código). Plano: `docs/PLANO-WIFI-MULTI-HOST.md`.
- O app do Windows foi escrito copiando o comportamento de `BoardLink.swift` (handshake) e dos feeds do Mac (`NowPlaying.swift`, `AudioMixer.swift`, `SystemStats.swift`, `ClockWeather.swift`, `DockerFeed.swift`, `MicControl.swift`). Em dúvida sobre formato de mensagem, a referência é o Swift e o firmware (`firmware/src/ui_*.cpp`).
- O usuário fala português; textos da UI em português; textos enviados à placa só em ASCII (`Text.Ascii`).

## Passo 1: compilar

```powershell
dotnet --list-sdks          # precisa de 8.x; se faltar: winget install Microsoft.DotNet.SDK.8
cd windows
dotnet build
```

Corrija os erros de compilação. Pontos com mais risco (escritos de memória, nunca compilados):
- `Feeds/MediaFeed.cs`: APIs WinRT (`GlobalSystemMediaTransportControlsSessionManager`, `DataReader`, `IRandomAccessStreamReference`). O TFM `net8.0-windows10.0.19041.0` deve trazê-las.
- `Feeds/AudioFeed.cs`: nomes da API do NAudio 2.2.1 (`AudioSessionControl.GetProcessID`, `SimpleAudioVolume`, `RefreshSessions`) e a interface COM `IPolicyConfig`.
- `Discovery.cs`: `UdpClient.ReceiveAsync(CancellationToken)`, `SocketOptionName.MulticastInterface`.
- Ambiguidades de nome: `Text` (classe própria em `Util.cs`) contra `Control.Text` dentro de Forms; `Timer` (sempre `System.Windows.Forms.Timer`); `Json` (classe própria) contra o namespace `System.Text.Json`.

Não mude o protocolo para fazer compilar. Se precisar mudar mensagem, mude também no firmware e no app do Mac.

## Passo 2: testar com a placa, nesta ordem

Peça ao usuário para rodar `dotnet run` (abre console com log) e acompanhe pelo log (`%APPDATA%\WaveshareLink\log.txt`).

1. **Descoberta**: o log deve mostrar `Conectado à placa em 192.168.0.110:7270`. Se não:
   - `Test-NetConnection 192.168.0.110 -Port 7270` (alcança a porta?)
   - `Resolve-DnsName waveshare.local` (mDNS do Windows funciona?)
   - Rede do PC como *Privada*? Firewall bloqueando a resposta UDP do mDNS? Contorno: IP em Ajustes → Endereço.
2. **Pareamento**: a placa mostra o código, o PC abre a janela; após digitar, log `Pareado com a placa.` e o token aparece em `settings.json` (`tokens`).
3. **Host ativo**: a placa só passa para o PC se ele for o único conectado ou se o usuário tocar no nome dele no botão do topo. Log `Este PC está na tela da placa.`. A placa deve redesenhar com as abas do PC (`pages` em settings.json; padrão sem Uso e Discord).
4. **Cada aba**, com o PC na tela:
   - Tocando: tocar algo no Spotify/YouTube; título, capa (220x220 JPEG baseline, base64) e botões.
   - Mixer: sliders (`vol:*`), mudo (`mute:*`), trocar saída (`outdev:dN`; ids curtos porque a placa guarda só 16 caracteres).
   - Atalhos: Captura, Microfone (selo/vermelho), Apagar tela, Volume, Mudo.
   - Sistema (aba "Mac"), Hora, Docker, Apps.
5. **Troca de volta para o Mac** pelo botão da placa: o PC deve logar `Outro computador está na tela da placa.` e parar de mandar dados (timers param em `OnStatus`).

## Passo 3: pendências conhecidas (em ordem de valor)

1. **Nome da aba "Mac"**: está fixo no firmware (`PAGES` em `firmware/src/ui.cpp`). Proposta: o `layout` ganha `"names":{"mac":"PC"}` e o firmware passa a usar um nome mutável. Mexe no firmware (gravar pelo Mac, porta USB) e nos dois apps. Combinar com o usuário antes.
2. **Uso (Claude/Antigravity)**: portar `UsageProviders.swift`. No Windows, o token do Claude Code fica em `%USERPROFILE%\.claude\.credentials.json` (confirmar). Mensagem `usage` no README.
3. **Discord**: ver como `DiscordControl.swift` detecta e controla a chamada; no Windows provavelmente atalhos globais do Discord.
4. Editor de atalhos/apps na janela de Ajustes (hoje é pelo JSON).
5. USB no Windows (porta serial) como plano B: hoje o app do Windows só usa a rede.
6. Instalador / `dotnet publish` com atalho no menu Iniciar.

## Cuidados

- O firmware tem pouca RAM interna livre (~32 KB após o boot). Com Mac e PC conectados ao mesmo tempo, observe se a placa reinicia ou o Wi-Fi cai; se sim, o próximo passo é reduzir os buffers de desenho do LVGL em `firmware/src/display.cpp` (2 x 64 KB internos).
- Não grave o firmware a partir do Windows sem combinar: o fluxo de gravação documentado é pelo Mac (`~/.platformio/penv/bin/pio run -t upload`, com o app do Mac fechado).
- Commit/push só quando o usuário pedir.

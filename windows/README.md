# Waveshare Link para Windows

App de bandeja (perto do relógio) que conecta o PC à tela Waveshare pela rede Wi-Fi, com o mesmo protocolo do app do Mac. Na placa, o PC aparece no botão do topo e dá para trocar entre ele e o Mac.

## Rodar

Precisa do .NET 8 SDK (`winget install Microsoft.DotNet.SDK.8`).

```powershell
cd windows
dotnet run
```

Em Debug (`dotnet run`), abre uma janela de console com o log.

## Instalar (menu Iniciar)

```powershell
powershell -ExecutionPolicy Bypass -File windows\install.ps1
```

Fecha o app se estiver aberto, gera a versão Release (sem console) em `%LOCALAPPDATA%\Programs\WaveshareLink`, cria o atalho **Waveshare Link** no menu Iniciar e abre o app. Rode de novo depois de mudar o código para atualizar. Se *Iniciar com o Windows* estiver ligado, passa a apontar para a versão instalada. O ícone é `app.ico`.

Na primeira conexão, a placa mostra um código de 6 dígitos e o PC abre uma janela pedindo esse código.

## Requisitos de rede

- A placa precisa já estar no Wi-Fi (configurado pelo app do Mac, com o cabo USB).
- A rede do PC deve estar como **Privada** (Configurações → Rede e Internet → propriedades da rede). Em rede Pública o Windows bloqueia a busca por mDNS.
- Se o PC não achar a placa sozinho, ponha o IP dela em *Ajustes… → Endereço da placa*.

## O que funciona

| Aba | Como |
|---|---|
| Tocando | Sessões de mídia do Windows (Spotify, navegadores, Media Player…), com capa e Anterior/Play/Próxima |
| Atalhos | Ações prontas (captura Win+Shift+S, microfone, apagar tela, bloquear, volume, mídia), abrir programa ou URL, comando, script .ps1 |
| Sistema (aba "Mac" na placa) | CPU, memória, bateria, rede |
| Hora | Hora do PC + tempo do Open-Meteo (cidade nos Ajustes) |
| Mixer | Volume geral, volume do player do Tocando, microfone, troca da saída de áudio |
| Apps | Mesmo que Atalhos, botões próprios |
| Docker | `docker ps -a` (precisa do Docker no PATH) |
| Uso | Claude (token do Claude Code em `%USERPROFILE%\.claude\.credentials.json`), Codex (login do Codex em `%USERPROFILE%\.codex\auth.json`) e Antigravity (API local, `%USERPROFILE%\.gemini\antigravity\daemon\hub_*.json`). Por causa do limite das APIs: busca automática no máximo a cada 5 min (trocar de aba ou de computador não zera a contagem) e o botão ↻ no máximo 1x por minuto |

Ainda não: **Discord**. Selos MIC e câmera vêm do registro de privacidade do Windows.

## Ajustes

`%APPDATA%\WaveshareLink\settings.json` (o menu do ícone tem *Abrir pasta de ajustes e log*). A janela *Ajustes…* edita nome, endereço, abas e cidade; atalhos e apps se editam no JSON e se aplicam com *Recarregar ajustes*. O log fica em `%APPDATA%\WaveshareLink\log.txt`.

## Estrutura

```
Program.cs          # entrada, instância única
TrayApp.cs          # ícone, menu, liga a placa às fontes de dados
BoardLink.cs        # conexão TCP, handshake HMAC, pareamento, host ativo
Discovery.cs        # acha waveshare.local (consulta mDNS própria + resolução do Windows)
Config.cs           # settings.json, layout enviado à placa
KeyRunner.cs        # executa os atalhos
Feeds/              # MediaFeed, AudioFeed (+ PolicyConfig, Privacy), SystemFeed, ClockFeed, DockerFeed, UsageFeed
install.ps1         # publica em %LOCALAPPDATA%\Programs\WaveshareLink e cria o atalho no menu Iniciar
Ui/                 # PairDialog, SettingsForm
```

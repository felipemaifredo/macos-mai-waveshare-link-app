# Plano: Wi-Fi e vários computadores (Mac + Windows)

> Status: **Etapas 1 a 3 implementadas (firmware + app do Mac), falta testar na placa.** Windows (Etapa 4) não iniciado. Criado em 2026-11-20.
>
> O que mudou em relação ao plano:
> - **Transporte: TCP puro (porta 7270) em vez de WebSocket.** As mensagens continuam iguais (uma linha JSON por mensagem); TCP tem menos código na placa e no Windows (`TcpClient`), e não há navegador envolvido.
> - **Auth com HMAC nos dois sentidos**: o token só passa pela rede no pareamento. O Mac também confere a placa, para nada na rede se passar por ela e disparar atalhos.
> - **Redes ocultas**: a placa procura a rede salva pelo nome em todos os canais (`WIFI_ALL_CHANNEL_SCAN`); o SSID é digitado no app.
> - Um Mac só aparece uma vez: com o Wi-Fi ligado, o USB fica em silêncio (`bye`).
> - O protocolo está documentado no README (seção Protocolo), em vez de um `PROTOCOLO.md` separado.

## Objetivo

Ligar a placa Waveshare ao Mac **e** ao Windows ao mesmo tempo, pela rede Wi-Fi, com as mesmas páginas e funções nos dois. O USB continua funcionando, para gravar o firmware e como plano B.

## Ponto de partida (hoje)

- **Transporte:** USB serial a 115200 baud. Um objeto JSON por linha, nas duas portas da placa (`Serial` nativa e `Serial0` UART). Ver [firmware/src/main.cpp](../firmware/src/main.cpp).
- **A placa responde nas duas portas.** `sendLine` escreve em todas e `handleLine` não sabe de qual porta veio a mensagem. Não existe a ideia de "host".
- **Conexão:** a placa manda `hello` a cada 1 s até ouvir alguém. A conexão cai depois de 8 s sem receber nada. O Mac manda `ping` a cada 2 s.
- **Mensagens:**
  - Placa → host: `hello`, `press` (`refresh`, `key:<id>`, `vol:*`, `mute:*`, `outdev:*`, `discord:*`, …), `page`, `log`
  - Host → placa: `layout`, `usage`, `now`, `art`, `mac`, `docker`, `clock`, `audio`, `discord`, `mic`, `ping`
- **App do host:** só macOS (Swift/AppKit, CoreAudio, AppleScript, Acessibilidade).
- **Gargalo:** uma capa (`art`, ~20 KB) leva ~2 s pelo UART. Pelo Wi-Fi seria praticamente instantâneo.

## Decisões de design

### Quem manda na tela: seletor de host (escolhido para a v1)

- A placa guarda uma lista de hosts conectados, cada um com nome (`"Mac"`, `"PC"`) e plataforma.
- Na barra do topo, um seletor (chip com o nome do host ativo) troca o host ativo.
- **Só o host ativo** recebe `press` e `page`, e só os dados dele aparecem na tela.
- Hosts inativos continuam conectados (com ping), mas param de coletar dados. Sem `page`, eles não fazem polling, como já acontece hoje.
- Na troca de host, a placa avisa o novo host ativo (`{"t":"active"}`). Ele reenvia o `layout` e a página atual.

Ficaram para depois:
- *Host ativo automático* (manda quem foi usado por último): bom de usar, mas imprevisível.
- *Fonte por página* (Uso vem do Mac, Mixer controla o PC): flexível, mas complica os Ajustes e a UI.

### Ajustes são de cada host

Cada host tem as próprias abas, atalhos e apps, e manda o próprio `layout`. Quando o host ativo muda, a placa redesenha com o `layout` dele.

### Transporte na rede: WebSocket

- A placa roda um servidor WebSocket (porta 81, por exemplo), com até 3 clientes.
- Cada frame de texto leva um objeto JSON, igual a uma linha do serial. As mensagens não mudam de formato.
- **mDNS:** a placa se anuncia como `waveshare.local`, serviço `_wslink._tcp`. Os apps acham a placa sozinhos.
- O USB vira só mais um "host", com a mesma lógica.

### Segurança: pareamento

Sem isso, qualquer coisa na rede local poderia mandar a placa apertar atalhos no computador.

1. Na primeira conexão de um host novo, a placa mostra um código de 6 dígitos.
2. O usuário digita o código no app. O app guarda o token que a placa devolve (a placa grava o token em NVS).
3. Nas próximas conexões, o app manda `{"t":"auth","token":…,"name":"Mac","os":"macos"}` como primeira mensagem.
4. A placa ignora qualquer mensagem de quem não se autenticou. Em Ajustes da placa dá para revogar hosts.

O USB não precisa de pareamento: acesso físico já é confiança suficiente.

### Configurar o Wi-Fi

Prioridade: enviar pelo USB a partir do app do Mac (`{"t":"wifi","ssid":…,"pass":…}`), com as credenciais gravadas em NVS. Para depois: portal de configuração (modo AP) quando a placa não tiver rede salva.

## Mudanças no protocolo (v2)

| Mensagem | Direção | Para quê |
|---|---|---|
| `hello` ganha `"v":2`, `"id"` (MAC da placa) e `"pair":true/false` | placa → host | Versão e se o host precisa parear |
| `pair` `{code}` → `paired` `{token}` | host ↔ placa | Pareamento |
| `auth` `{token,name,os}` | host → placa | Primeira mensagem de um host pareado |
| `active` `{active:true/false}` | placa → host | Avisa se o host virou ativo ou deixou de ser |
| `hosts` | interno | Lista mostrada no seletor (não sai da placa) |
| `wifi` `{ssid,pass}` | host → placa (só USB) | Configura a rede |
| `net` `{ip,rssi,ssid}` | placa → host | Mostra o estado da rede no menu do app |

Compatibilidade: hosts v1 (sem `auth`) continuam funcionando **só pelo USB**, como host único.

## Etapas

### Etapa 0: documentar o protocolo
- [ ] Criar `docs/PROTOCOLO.md` com todas as mensagens e campos (tirar dos comentários no topo de cada `.swift` e `ui_*.cpp`).
- [ ] Ele vira o contrato entre a placa, o app do Mac e o app do Windows.

### Etapa 1: firmware com noção de hosts (ainda só USB)
- [ ] Trocar o `sendLine` global por `sendTo(host)` / `sendActive()`.
- [ ] Criar a estrutura `Host { transporte, nome, os, autenticado, lastRx }`. As duas portas USB viram hosts.
- [ ] Criar o host ativo, com timeout de conexão para cada host.
- [ ] Adicionar o seletor de host na barra do topo ([ui.cpp](../firmware/src/ui.cpp)).
- [ ] Testar com dois cabos (Mac na porta USB e outro computador ou pty na UART).

### Etapa 2: firmware com Wi-Fi
- [ ] Conectar o Wi-Fi com credenciais da NVS e reconectar automaticamente.
- [ ] Rodar o servidor WebSocket (ver uso de RAM com LVGL; a placa tem 8 MB de PSRAM, então o buffer de 48 KB por cliente cabe).
- [ ] Anunciar o serviço por mDNS.
- [ ] Implementar pareamento e auth, com tokens em NVS.
- [ ] Adicionar o ícone de Wi-Fi na barra do topo (ao lado do USB), com indicação de sinal.
- [ ] Receber a mensagem `wifi` pelo USB.

### Etapa 3: app do Mac com transporte de rede
- [ ] Criar o protocolo `Transport` em Swift, com duas implementações: `SerialTransport` (o `SerialLink` atual) e `WebSocketTransport` (`URLSessionWebSocketTask`).
- [ ] Achar a placa com `NWBrowser`, procurando `_wslink._tcp`.
- [ ] Criar a tela de pareamento (campo para o código) e guardar o token no Keychain.
- [ ] Respeitar `active`: quando o host estiver inativo, parar os coletores (como hoje com a placa desconectada).
- [ ] Adicionar Wi-Fi nos Ajustes (enviar SSID e senha pelo USB) e mostrar o estado da rede no menu.

### Etapa 4: app do Windows
App de bandeja em **C# / .NET 8** (WinForms `NotifyIcon` ou WinUI 3), seguindo o mesmo protocolo.

| Página | No Mac (hoje) | No Windows |
|---|---|---|
| Uso (Claude/Antigravity) | `UsageProviders.swift` | Mesma lógica HTTP, portada. Ver onde ficam as credenciais no Windows |
| Tocando | AppleScript Music/Spotify | `GlobalSystemMediaTransportControlsSessionManager` (funciona com qualquer player, inclusive a capa) |
| Atalhos | ⌘⇧5, teclas simuladas, Atalhos do macOS | `SendInput` (teclas de mídia e volume), Win+Shift+S, abrir app ou URL, rodar `.ps1` |
| Sistema ("Mac") | `SystemStats.swift` | PerformanceCounter / WMI. A página passa a usar o nome do host |
| Docker | `docker ps` | `docker ps` (igual) |
| Hora | Open-Meteo | Igual |
| Mixer | CoreAudio + AppleScript | Core Audio do Windows (NAudio): volume master, **volume por app** pelas sessões de áudio, troca de dispositivo de saída (`IPolicyConfig`) |
| Microfone/câmera | CoreAudio / CoreMediaIO | `IAudioEndpointVolume` no mic. Câmera em uso pelo registro `CapabilityAccessManager\ConsentStore\webcam` |
| Discord | `DiscordControl.swift` | Ver como o Mac detecta a chamada e replicar (atalhos globais do Discord ou RPC) |
| Apps | Abrir `.app` | Abrir `.exe` ou atalho do menu Iniciar |

- [ ] Fazer os Ajustes (abas, atalhos, apps) em JSON no `%APPDATA%`.
- [ ] Abrir junto com o Windows (registro `Run` ou pasta Startup).

### Etapa 5: acabamento
- [ ] Renomear a página "Mac" para o nome do host ativo (o `layout` manda o rótulo).
- [ ] Reconectar sozinho (placa reiniciou, troca de rede, computador saiu do repouso).
- [ ] Atualizar o README: configurar o Wi-Fi, parear, usar o seletor de host.
- [ ] Opcional: atualizar o firmware pela rede (OTA), para não precisar mais do cabo para gravar.

## Riscos e dúvidas em aberto

- **RAM e desempenho:** LVGL + Wi-Fi + WebSocket juntos podem disputar a SRAM interna, e o display RGB é sensível a isso. Medir antes de seguir com o resto.
- **Interferência no display RGB:** o tráfego do Wi-Fi pode causar *tearing* ou *drift* nos painéis RGB do ESP32-S3. Testar cedo na Etapa 2.
- **Latência dos sliders do Mixer** pelo Wi-Fi: talvez seja preciso limitar a taxa de `vol:*` no firmware.
- **Redes corporativas ou públicas:** sem mDNS ou com isolamento de clientes, fica sem descoberta. Plano B: digitar o IP no app.
- **Discord no Windows:** ver qual técnica o Mac usa hoje e se dá para replicar.
- **Mexer em dois apps ao mesmo tempo:** toda mudança de protocolo passa a valer para os dois. O `PROTOCOLO.md` da Etapa 0 é o que evita que eles se desencontrem.

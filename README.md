# Waveshare Link

App de barra de menus do macOS que conversa com a tela touch **Waveshare ESP32-S3-Touch-LCD-7**, ligada ao Mac por cabo USB.

A tela tem duas páginas. Para trocar de página, use o seletor no topo ou deslize o dedo para o lado:

- **Uso**: o uso do Claude e do Antigravity, recebido do app [AI Usage](../macos-mai-ai-usage-app). O botão **Atualizar** pede uma atualização ao AI Usage e abre o app se ele estiver fechado.
- **Atalhos**: 8 botões que agem no Mac: Captura (abre a barra do ⌘⇧5), Anterior, Play/Pausa, Próxima, Volume −, Volume + e Mudo, que são teclas de mídia, e Apagar tela.

O ícone na barra de menus fica apagado enquanto a placa não está conectada. O menu mostra a porta da placa e quando chegaram os últimos números do AI Usage.

App nativo em Swift/AppKit, em um arquivo só. Não usa Xcode.

## Build

```bash
bash build.sh
```

O script compila `main.swift`, monta `build/Waveshare Link.app` e instala o app em `/Applications`. Depois do build, feche o app (Sair) e abra de novo.

### Permissão de Acessibilidade

As teclas de mídia e de volume são eventos de teclado simulados, e o macOS exige a permissão de **Acessibilidade** para isso. No primeiro toque, o app pede essa permissão. Libere o "Waveshare Link" em *Ajustes do Sistema → Privacidade e Segurança → Acessibilidade*. O app é assinado ad-hoc, então o macOS o trata como um app novo a cada build, e a permissão precisa ser liberada de novo. Captura e Apagar tela funcionam sem essa permissão.

## Estrutura

```
main.swift   # app inteiro (conexão USB, atalhos, uso vindo do AI Usage, barra de menus)
Info.plist   # bundle "Waveshare Link" (local.waveshare-link), LSUIElement (sem ícone no Dock)
build.sh     # compila, empacota, assina ad-hoc e instala em /Applications
firmware/    # firmware da placa (PlatformIO + Arduino + LVGL 9)
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

A lista de botões fica em `KEYS`, em `firmware/src/ui.cpp`. Cada botão envia `key:<id>`, e o que ele faz no Mac fica em `DeckActions`, em `main.swift`.

## Protocolo

É um objeto JSON por linha, a 115200 baud:

| Direção | Mensagem |
|---|---|
| Placa → Mac | `{"t":"hello"}` a cada 1 s até o Mac responder |
| Placa → Mac | `{"t":"press","id":"refresh"}` ou `{"t":"press","id":"key:<id>"}` quando alguém toca num botão |
| Mac → Placa | `{"t":"usage","providers":[{"name","pct","err","busy","at","rows":[{"sec","label","pct","reset"}]}]}` |
| Mac → Placa | `{"t":"ping"}` a cada 2 s. Se a placa ficar 8 s sem receber nada, volta a mostrar "Aguardando o Mac..." |

O app testa as portas `/dev/cu.usbmodem*`, `cu.usbserial*` e `cu.wchusbserial*`. Uma porta que não responde `hello` em 6 s é ignorada até ser desconectada. Os textos chegam sem acentos porque as fontes da placa só têm ASCII.

Para testar sem a placa, use `WAVESHARE_LINK_SERIAL=/dev/ttysNNN` para apontar o app para um pty.

O uso chega do AI Usage por notificações distribuídas do macOS (`local.aiusagebar.usage`, `.request` e `.refresh`). O README do AI Usage descreve essas notificações.

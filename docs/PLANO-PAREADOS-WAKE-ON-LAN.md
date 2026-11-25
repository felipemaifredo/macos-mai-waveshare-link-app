# Plano: computadores pareados no popup e Wake-on-LAN

> Status: **só planejado, nada implementado.** Criado em 2026-11-21.
>
> Decisão por enquanto: ligar os PCs pela função de Wake-on-LAN do roteador. A etapa 1 (listar os pareados offline) é pequena e pode ser feita sozinha; as etapas 2 e 3 ficam para quando valer a pena ter o botão de ligar na placa.

## Problema

Quando nenhum computador está conectado, tocar na tela (standby) abre o popup "Computadores", que fica vazio: "Nenhum computador conectado".

- O popup ([ui.cpp](../firmware/src/ui.cpp), `fillHostPopup`) só mostra os hosts **conectados** (`hosts[i].linked`).
- Os pareados já estão na placa (`paired[]` em [main.cpp](../firmware/src/main.cpp), salvos na NVS em `hosts/h%d`), mas a UI nunca recebe essa lista.

## Por que "ligar o PC" precisa de Wake-on-LAN, e por que o IP não basta

- A placa é o **servidor** (TCP 7270). Quem abre a conexão é o app no PC. Se o PC está ligado e o app aberto, ele já conecta sozinho; a placa não tem como "puxar" a conexão.
- Para um PC desligado ou dormindo, o caminho é o **Wake-on-LAN (WoL)**: um pacote mágico em broadcast com 6 × `FF` + 16 × o **MAC** do PC (102 bytes, UDP porta 9).
- **O IP não serve:**
  - O conteúdo do pacote é o MAC. A placa de rede do PC dormindo procura o próprio MAC; o sistema operacional, que sabe o IP, está desligado.
  - Para mandar algo a um IP na rede local é preciso resolver o MAC por ARP, e um PC dormindo não responde ARP. Por isso o WoL usa broadcast.

## Etapa 1: listar os pareados offline (só firmware, sem WoL)

- Em `publishHosts()`, montar também a lista dos pareados **offline**: cada `paired[i]` cujo `hostId` não está em nenhum host conectado (USB ou Wi-Fi).
- Nova função na UI: `uiSetPaired(const UiPaired* list, int count)`, com `UiPaired { const char* name; bool canWake; }`, e um mapeamento `pairedSlots[]` no mesmo esquema de `listSlots[]`.
- Em `fillHostPopup`, abaixo dos conectados, uma seção **"Pareados (offline)"** com uma linha por PC, em cor apagada.
- "Nenhum computador conectado" só aparece quando as duas listas estão vazias.
- Chamar `publishHosts()` também depois de `storePaired` e de `onForgetHosts`.

## Etapa 2: a placa descobre e guarda o MAC (só firmware)

Não é preciso mudar os apps de macOS e Windows: quando um PC se conecta pelo Wi-Fi, a placa já recebeu pacotes dele, e a tabela ARP do lwIP tem o par IP → MAC.

- No `onAuth` e no `onPair` (Wi-Fi), consultar a tabela ARP com `h.client.remoteIP()` (`etharp_find_addr`) e gravar o MAC no pareamento.
  - **Atenção:** a consulta precisa respeitar a concorrência da pilha de rede (`LOCK_TCPIP_CORE` se `CONFIG_LWIP_TCPIP_CORE_LOCKING` estiver ativo, senão `tcpip_callback`). É a primeira coisa a testar.
- Acrescentar `uint8_t mac[6]` em `Paired`.
- **Migração sem perder pareamentos:** em `loadPaired`, usar `getBytesLength(key)`. Se o tamanho for o antigo (sem MAC), ler só essa parte e deixar o MAC zerado.
- No `onAuth`, só regravar na NVS se o MAC mudou, para poupar a flash.
- PCs que só usam USB não têm MAC descoberto; o botão deles fica apagado.

Alternativa, se algum PC nunca usar Wi-Fi: o app manda o MAC (`"mac":"AA:BB:…"` no `auth`/`pair`), descoberto pela interface do IP local da conexão (`getifaddrs` + `AF_LINK` no macOS; `NetworkInterface.GetPhysicalAddress()` no Windows). É mais código; só se precisar.

## Etapa 3: botão de ligar (WoL)

- Em [net.cpp](../firmware/src/net.cpp): `bool netWake(const uint8_t mac[6])`. Monta o pacote mágico e envia por `WiFiUDP` para `WiFi.broadcastIP()` e `255.255.255.255`, porta 9, 3 vezes.
  - Usa pouca RAM (um pcb UDP temporário); não mexe no equilíbrio de memória interna (ver as notas de Wi-Fi × display).
- Na seção de pareados, linhas com MAC viram botões (ícone `LV_SYMBOL_POWER`, "toque para ligar"). O toque chama um novo handler `onWake(int index)` (adicionado em `uiSetHostHandlers`), que chama `netWake`. O texto da linha vira "Sinal enviado…" por alguns segundos.
- Linhas sem MAC: apagadas, com a dica "conecte uma vez pelo Wi-Fi para habilitar".
- Quando o PC liga e o app abre, ele conecta sozinho e sai da lista de offline.

## Limitações

- **Windows:** WoL é confiável com cabo de rede. Precisa ativar na BIOS e nas propriedades da placa de rede ("Wake on Magic Packet"); para PC desligado de verdade, normalmente é preciso desligar a "Inicialização rápida". Pelo Wi-Fi quase nunca funciona a partir do desligado.
- **Mac:** acorda do repouso com "Despertar para acesso à rede" ligado. Pelo cabo é confiável; pelo Wi-Fi depende do modelo. Mac desligado não liga.
- WoL liga ou acorda o PC, mas **não abre o app**: ele precisa estar configurado para abrir no login.
- Só na mesma rede local (o broadcast não passa de uma rede para outra).

## Como testar

1. Etapa 1: gravar a placa (ver as notas de gravação: porta USB nativa, app fechado) e conferir que os pareados aparecem como offline com o PC desconectado.
2. Etapa 2: conectar um PC pelo Wi-Fi e conferir no log serial que o MAC foi descoberto e salvo; reiniciar a placa e conferir que os pareamentos antigos continuam lá.
3. Etapa 3: pôr o PC em repouso, tocar no botão e conferir que ele acorda e reconecta.

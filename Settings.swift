import AppKit
import SwiftUI

// MARK: - Model

/// One tab on the board. `id` is the page id the firmware knows ("usage", "now", …).
struct PageSetting: Codable, Identifiable, Equatable {
    var id: String
    var enabled: Bool
}

/// One shortcut key. `kind` is "builtin" (value = a DeckActions id such as "mic"),
/// "app" (value = app path), "url" (value = URL) or "shortcut" (value = a Shortcuts.app name).
struct KeySetting: Codable, Identifiable, Equatable {
    var id = UUID().uuidString
    var text: String
    var icon: String
    var kind: String
    var value: String

    /// The id the board sends back as "key:<id>". Built-in actions keep their own id, so the
    /// board can tell which key is the mic.
    var boardID: String { kind == "builtin" ? value : id }
}

enum Catalog {
    static let pages: [(id: String, name: String)] = [
        ("usage", "Uso"), ("now", "Tocando"), ("keys", "Atalhos"),
        ("mac", "Mac"), ("docker", "Docker"), ("clock", "Hora"),
    ]

    static let builtins: [(id: String, name: String, icon: String)] = [
        ("shot", "Captura de tela", "image"),
        ("mic", "Microfone (mudo)", "mic"),
        ("sleep", "Apagar tela", "eye_close"),
        ("voldown", "Volume -", "volume_mid"),
        ("volup", "Volume +", "volume_max"),
        ("mute", "Mudo", "mute"),
        ("play", "Play / Pausa", "play"),
        ("prev", "Anterior", "prev"),
        ("next", "Próxima", "next"),
    ]

    /// Icons the board can draw (ICONS in firmware/src/ui.cpp), with a similar SF Symbol for this window.
    static let icons: [(id: String, name: String, sf: String)] = [
        ("mic", "Microfone", "mic"), ("image", "Imagem", "photo"), ("audio", "Música", "music.note"),
        ("video", "Vídeo", "video"), ("play", "Play", "play.fill"), ("pause", "Pausa", "pause.fill"),
        ("prev", "Anterior", "backward.end.fill"), ("next", "Próxima", "forward.end.fill"),
        ("stop", "Parar", "stop.fill"), ("shuffle", "Aleatório", "shuffle"), ("loop", "Repetir", "repeat"),
        ("volume_mid", "Volume baixo", "speaker.wave.1.fill"), ("volume_max", "Volume alto", "speaker.wave.3.fill"),
        ("mute", "Mudo", "speaker.slash.fill"), ("eye_close", "Olho fechado", "eye.slash"),
        ("eye_open", "Olho", "eye"), ("home", "Casa", "house"), ("settings", "Engrenagem", "gearshape"),
        ("power", "Liga/desliga", "power"), ("refresh", "Atualizar", "arrow.clockwise"), ("bell", "Sino", "bell"),
        ("envelope", "Envelope", "envelope"), ("call", "Telefone", "phone"), ("keyboard", "Teclado", "keyboard"),
        ("file", "Arquivo", "doc"), ("directory", "Pasta", "folder"), ("edit", "Lápis", "pencil"),
        ("save", "Salvar", "square.and.arrow.down"), ("trash", "Lixeira", "trash"),
        ("download", "Baixar", "arrow.down.to.line"), ("upload", "Enviar", "arrow.up.to.line"),
        ("copy", "Copiar", "doc.on.doc"), ("paste", "Colar", "doc.on.clipboard"), ("cut", "Tesoura", "scissors"),
        ("wifi", "Wi-Fi", "wifi"), ("bluetooth", "Bluetooth", "dot.radiowaves.left.and.right"),
        ("gps", "Localização", "location"), ("charge", "Raio", "bolt.fill"), ("list", "Lista", "list.bullet"),
        ("plus", "Mais", "plus"), ("minus", "Menos", "minus"), ("ok", "Confirmar", "checkmark"),
        ("close", "Fechar", "xmark"), ("warning", "Aviso", "exclamationmark.triangle"),
        ("drive", "Disco", "internaldrive"), ("sd_card", "Cartão SD", "sdcard"), ("usb", "USB", "cable.connector"),
        ("eject", "Ejetar", "eject"), ("tint", "Gota", "drop"),
    ]

    static func sf(_ icon: String) -> String { icons.first { $0.id == icon }?.sf ?? "circle" }

    static let defaultKeys: [KeySetting] = ["shot", "mic", "sleep", "voldown", "volup", "mute"].compactMap { id in
        builtins.first { $0.id == id }.map {
            KeySetting(text: id == "mic" ? "Microfone" : id == "shot" ? "Captura" : $0.name,
                       icon: $0.icon, kind: "builtin", value: id)
        }
    }
    static let maxKeys = 12
}

/// The board's tabs and shortcut keys, saved in UserDefaults and sent to the board as
///   {"t":"layout","pages":[ids in tab order],"keys":[{"id","icon","text"}]}
@MainActor
final class Settings: ObservableObject {
    static let shared = Settings()

    @Published var pages: [PageSetting] { didSet { changed() } }
    @Published var keys: [KeySetting] { didSet { changed() } }
    var onChange: () -> Void = {}

    private init() {
        let d = UserDefaults.standard
        var pages = (d.data(forKey: "pages").flatMap { try? JSONDecoder().decode([PageSetting].self, from: $0) }) ?? []
        // Pages added in a later version show up at the end.
        for p in Catalog.pages where !pages.contains(where: { $0.id == p.id }) {
            pages.append(PageSetting(id: p.id, enabled: true))
        }
        pages.removeAll { p in !Catalog.pages.contains { $0.id == p.id } }
        self.pages = pages
        self.keys = (d.data(forKey: "keys").flatMap { try? JSONDecoder().decode([KeySetting].self, from: $0) })
            ?? Catalog.defaultKeys
    }

    func isEnabled(_ page: String) -> Bool { pages.contains { $0.id == page && $0.enabled } }

    func key(boardID: String) -> KeySetting? { keys.first { $0.boardID == boardID } }

    var layoutMessage: [String: Any] {
        [
            "t": "layout",
            "pages": pages.filter(\.enabled).map(\.id),
            "keys": keys.prefix(Catalog.maxKeys).map {
                ["id": $0.boardID, "icon": $0.icon, "text": SerialLink.ascii($0.text)]
            },
        ]
    }

    private func changed() {
        let d = UserDefaults.standard
        d.set(try? JSONEncoder().encode(pages), forKey: "pages")
        d.set(try? JSONEncoder().encode(keys), forKey: "keys")
        onChange()
    }
}

// MARK: - Running a key

@MainActor
enum KeyRunner {
    static func run(_ key: KeySetting) {
        switch key.kind {
        case "builtin":
            DeckActions.run(key.value)
        case "app":
            guard !key.value.isEmpty else { return }
            NSWorkspace.shared.openApplication(at: URL(fileURLWithPath: key.value), configuration: NSWorkspace.OpenConfiguration())
        case "url":
            var s = key.value.trimmingCharacters(in: .whitespaces)
            if !s.contains("://") { s = "https://" + s }
            if let url = URL(string: s) { NSWorkspace.shared.open(url) }
        case "shortcut":
            guard !key.value.isEmpty else { return }
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/usr/bin/shortcuts")
            p.arguments = ["run", key.value]
            try? p.run()
        default:
            break
        }
    }

    /// The user's Shortcuts.app shortcuts, for the picker.
    static func shortcutNames() async -> [String] {
        await withCheckedContinuation { cont in
            DispatchQueue.global(qos: .userInitiated).async {
                let p = Process()
                p.executableURL = URL(fileURLWithPath: "/usr/bin/shortcuts")
                p.arguments = ["list"]
                let out = Pipe()
                p.standardOutput = out
                p.standardError = FileHandle.nullDevice
                var names: [String] = []
                if (try? p.run()) != nil {
                    let data = out.fileHandleForReading.readDataToEndOfFile()
                    p.waitUntilExit()
                    names = String(decoding: data, as: UTF8.self)
                        .split(separator: "\n").map(String.init).filter { !$0.isEmpty }.sorted()
                }
                cont.resume(returning: names)
            }
        }
    }
}

// MARK: - Window

@MainActor
final class SettingsWindow {
    static let shared = SettingsWindow()
    private var window: NSWindow?

    func show() {
        if window == nil {
            let w = NSWindow(contentViewController: NSHostingController(rootView: SettingsView(settings: .shared)))
            w.title = "Ajustes do Waveshare Link"
            w.styleMask = [.titled, .closable]
            w.isReleasedWhenClosed = false
            w.center()
            window = w
        }
        NSApp.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
    }
}

struct SettingsView: View {
    @ObservedObject var settings: Settings

    var body: some View {
        TabView {
            PagesTab(settings: settings).tabItem { Text("Abas") }
            KeysTab(settings: settings).tabItem { Text("Atalhos") }
        }
        .padding()
        .frame(width: 640, height: 480)
    }
}

struct PagesTab: View {
    @ObservedObject var settings: Settings

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Escolha as abas que aparecem na placa. Arraste para mudar a ordem.")
                .foregroundStyle(.secondary)
            List {
                ForEach($settings.pages) { $page in
                    HStack {
                        Image(systemName: "line.3.horizontal").foregroundStyle(.tertiary)
                        Toggle(Catalog.pages.first { $0.id == page.id }?.name ?? page.id, isOn: $page.enabled)
                            // At least one tab stays on.
                            .disabled(page.enabled && settings.pages.filter(\.enabled).count == 1)
                    }
                    .padding(.vertical, 4)
                }
                .onMove { settings.pages.move(fromOffsets: $0, toOffset: $1) }
            }
            .listStyle(.bordered(alternatesRowBackgrounds: true))
        }
    }
}

/// The Atalhos tab's own state. (An ObservableObject instead of @State: @State is a macro in the
/// current SDK, and plain swiftc without Xcode has no SwiftUI macro plugin.)
@MainActor
final class KeysTabState: ObservableObject {
    @Published var selection: String?
    @Published var shortcuts: [String] = []
}

struct KeysTab: View {
    @ObservedObject var settings: Settings
    @StateObject private var state = KeysTabState()

    var body: some View {
        HStack(alignment: .top, spacing: 16) {
            VStack(alignment: .leading, spacing: 6) {
                List(selection: $state.selection) {
                    ForEach(settings.keys) { key in
                        Label(key.text.isEmpty ? "Sem nome" : key.text, systemImage: Catalog.sf(key.icon))
                            .tag(key.id)
                            .padding(.vertical, 2)
                    }
                    .onMove { settings.keys.move(fromOffsets: $0, toOffset: $1) }
                }
                .listStyle(.bordered(alternatesRowBackgrounds: true))
                HStack {
                    Button {
                        let k = KeySetting(text: "Novo atalho", icon: "home", kind: "url", value: "")
                        settings.keys.append(k)
                        state.selection = k.id
                    } label: { Image(systemName: "plus") }
                    .disabled(settings.keys.count >= Catalog.maxKeys)
                    Button {
                        settings.keys.removeAll { $0.id == state.selection }
                        state.selection = nil
                    } label: { Image(systemName: "minus") }
                    .disabled(state.selection == nil)
                    Spacer()
                    Text("\(settings.keys.count) de \(Catalog.maxKeys)").foregroundStyle(.secondary)
                }
            }
            .frame(width: 230)

            if let i = settings.keys.firstIndex(where: { $0.id == state.selection }) {
                KeyEditor(key: $settings.keys[i], shortcuts: state.shortcuts)
            } else {
                Text("Selecione um atalho para editar, ou toque em + para criar um. Arraste para mudar a ordem.")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .task { state.shortcuts = await KeyRunner.shortcutNames() }
    }
}

struct KeyEditor: View {
    @Binding var key: KeySetting
    let shortcuts: [String]

    var body: some View {
        Form {
            TextField("Nome", text: $key.text)
            Picker("Ícone", selection: $key.icon) {
                ForEach(Catalog.icons, id: \.id) { icon in
                    Label(icon.name, systemImage: icon.sf).tag(icon.id)
                }
            }
            Picker("Ação", selection: Binding(
                get: { key.kind },
                set: { kind in
                    key.kind = kind
                    key.value = kind == "builtin" ? (Catalog.builtins.first?.id ?? "") : ""
                }
            )) {
                Text("Ação pronta").tag("builtin")
                Text("Abrir app").tag("app")
                Text("Abrir URL").tag("url")
                Text("Rodar Atalho do macOS").tag("shortcut")
            }

            switch key.kind {
            case "builtin":
                Picker("Qual", selection: $key.value) {
                    ForEach(Catalog.builtins, id: \.id) { Text($0.name).tag($0.id) }
                }
            case "app":
                LabeledContent("App") {
                    HStack {
                        Text(key.value.isEmpty ? "Nenhum" : (key.value as NSString).lastPathComponent)
                            .foregroundStyle(key.value.isEmpty ? .secondary : .primary)
                        Button("Escolher…", action: chooseApp)
                    }
                }
            case "url":
                TextField("URL", text: $key.value, prompt: Text("https://…"))
            case "shortcut":
                if shortcuts.isEmpty {
                    TextField("Atalho", text: $key.value, prompt: Text("Nome do Atalho"))
                } else {
                    Picker("Atalho", selection: $key.value) {
                        Text("Escolha…").tag("")
                        ForEach(shortcuts, id: \.self) { Text($0).tag($0) }
                    }
                }
            default:
                EmptyView()
            }

            Text("A placa mostra o nome sem acentos.").font(.caption).foregroundStyle(.secondary)
        }
        .formStyle(.grouped)
    }

    private func chooseApp() {
        let panel = NSOpenPanel()
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        panel.allowedContentTypes = [.application]
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        key.value = url.path
        if key.text.isEmpty || key.text == "Novo atalho" {
            key.text = url.deletingPathExtension().lastPathComponent
        }
    }
}

using System.Diagnostics;
using System.Globalization;

namespace WaveshareLink.Ui;

/// Name, board address, tabs and weather city. The Atalhos and Apps buttons are edited in
/// settings.json (button "Editar atalhos e apps").
sealed class SettingsForm : Form
{
    readonly Config config;
    readonly Action onSaved;
    readonly TextBox name = new() { Width = 260 };
    readonly TextBox address = new() { Width = 260, PlaceholderText = "vazio: encontrar sozinho" };
    readonly TextBox city = new() { Width = 260 };
    readonly TextBox latitude = new() { Width = 120 };
    readonly TextBox longitude = new() { Width = 120 };
    readonly CheckedListBox pages = new() { Width = 260, Height = 190, CheckOnClick = true };
    readonly List<string> order = new();

    public SettingsForm(Config config, Action onSaved)
    {
        this.config = config;
        this.onSaved = onSaved;
        Text = "Ajustes do Waveshare Link";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Padding = new Padding(12);

        name.Text = config.HostName;
        address.Text = config.BoardAddress;
        city.Text = config.City;
        latitude.Text = config.Latitude.ToString(CultureInfo.InvariantCulture);
        longitude.Text = config.Longitude.ToString(CultureInfo.InvariantCulture);

        // Enabled tabs in their order first, then the others.
        order.AddRange(config.Pages.Where(p => Config.AllPages.Any(a => a.Id == p)));
        order.AddRange(Config.AllPages.Select(a => a.Id).Where(id => !order.Contains(id)));
        FillPages();

        var up = new Button { Text = "Subir", AutoSize = true };
        var down = new Button { Text = "Descer", AutoSize = true };
        up.Click += (_, _) => MovePage(-1);
        down.Click += (_, _) => MovePage(1);
        var moveButtons = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true };
        moveButtons.Controls.Add(up);
        moveButtons.Controls.Add(down);
        var pagesRow = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty };
        pagesRow.Controls.Add(pages);
        pagesRow.Controls.Add(moveButtons);

        var coords = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty };
        coords.Controls.Add(latitude);
        coords.Controls.Add(longitude);

        var editKeys = new Button { Text = "Editar atalhos e apps (settings.json)…", AutoSize = true };
        editKeys.Click += (_, _) => EditJson();

        var save = new Button { Text = "Salvar", AutoSize = true };
        var cancel = new Button { Text = "Cancelar", AutoSize = true, DialogResult = DialogResult.Cancel };
        save.Click += (_, _) => Save();
        AcceptButton = save;
        CancelButton = cancel;
        var buttons = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, AutoSize = true, Dock = DockStyle.Fill };
        buttons.Controls.Add(cancel);
        buttons.Controls.Add(save);

        var grid = new TableLayoutPanel { ColumnCount = 2, AutoSize = true, Dock = DockStyle.Fill };
        void Row(string label, Control c)
        {
            grid.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(0, 6, 12, 6) });
            grid.Controls.Add(c);
        }
        Row("Nome deste PC na placa", name);
        Row("Endereço da placa", address);
        Row("Abas (marque e ordene)", pagesRow);
        Row("Cidade (aba Hora)", city);
        Row("Latitude / longitude", coords);
        grid.Controls.Add(new Label());
        grid.Controls.Add(editKeys);
        grid.Controls.Add(new Label());
        grid.Controls.Add(new Label
        {
            Text = "O endereço só é preciso se a rede bloquear a busca automática (mDNS).\n"
                   + "A rede Wi-Fi da placa é configurada pelo app do Mac, com o cabo USB.",
            AutoSize = true,
            ForeColor = SystemColors.GrayText,
        });
        grid.Controls.Add(new Label());
        grid.Controls.Add(buttons);
        Controls.Add(grid);
    }

    void FillPages()
    {
        pages.Items.Clear();
        foreach (var id in order)
        {
            var label = Config.AllPages.First(a => a.Id == id).Name;
            pages.Items.Add(label, config.Pages.Contains(id));
        }
    }

    void MovePage(int delta)
    {
        int i = pages.SelectedIndex;
        int j = i + delta;
        if (i < 0 || j < 0 || j >= order.Count) return;
        var checks = Enumerable.Range(0, order.Count).Select(pages.GetItemChecked).ToList();
        (order[i], order[j]) = (order[j], order[i]);
        (checks[i], checks[j]) = (checks[j], checks[i]);
        pages.Items.Clear();
        for (int k = 0; k < order.Count; k++) pages.Items.Add(Config.AllPages.First(a => a.Id == order[k]).Name, checks[k]);
        pages.SelectedIndex = j;
    }

    void EditJson()
    {
        config.Save();
        try
        {
            Process.Start(new ProcessStartInfo("notepad.exe", $"\"{Config.FilePath}\"") { UseShellExecute = false });
            MessageBox.Show(this,
                "Edite \"keys\" (Atalhos) e \"apps\" (Apps), salve no Bloco de Notas e escolha \"Recarregar ajustes\" no menu do ícone.\n\n"
                + "Tipos: builtin (shot, mic, sleep, lock, voldown, volup, mute, play, prev, next), app, url, command, powershell.",
                "Atalhos e apps");
        }
        catch (Exception e)
        {
            Log.Error("abrir settings.json", e);
        }
    }

    void Save()
    {
        if (!double.TryParse(latitude.Text.Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out var lat)
            || !double.TryParse(longitude.Text.Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out var lon))
        {
            MessageBox.Show(this, "Latitude e longitude precisam ser números (ex.: -20.85).", "Ajustes");
            return;
        }
        var enabled = order.Where((_, i) => pages.GetItemChecked(i)).ToList();
        if (enabled.Count == 0)
        {
            MessageBox.Show(this, "Deixe pelo menos uma aba ligada.", "Ajustes");
            return;
        }
        config.HostName = name.Text.Trim().Length > 0 ? name.Text.Trim() : "PC";
        config.BoardAddress = address.Text.Trim();
        config.City = city.Text.Trim();
        config.Latitude = lat;
        config.Longitude = lon;
        config.Pages = enabled;
        config.Save();
        onSaved();
        DialogResult = DialogResult.OK;
        Close();
    }
}

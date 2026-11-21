namespace WaveshareLink.Ui;

/// Asks for the 6-digit code the board shows. Returns null when cancelled.
sealed class PairDialog : Form
{
    readonly TextBox code = new() { MaxLength = 6, Width = 160, Font = new Font("Consolas", 18f), TextAlign = HorizontalAlignment.Center };

    PairDialog(string message)
    {
        Text = "Parear com a tela Waveshare";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        TopMost = true;
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Padding = new Padding(16);

        var ok = new Button { Text = "Parear", DialogResult = DialogResult.OK, AutoSize = true };
        var cancel = new Button { Text = "Cancelar", DialogResult = DialogResult.Cancel, AutoSize = true };
        AcceptButton = ok;
        CancelButton = cancel;
        code.KeyPress += (_, e) =>
        {
            if (!char.IsDigit(e.KeyChar) && !char.IsControl(e.KeyChar)) e.Handled = true;
        };

        var buttons = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, AutoSize = true, Dock = DockStyle.Fill };
        buttons.Controls.Add(cancel);
        buttons.Controls.Add(ok);

        var layout = new TableLayoutPanel { ColumnCount = 1, AutoSize = true, Dock = DockStyle.Fill };
        layout.Controls.Add(new Label { Text = message, AutoSize = true, MaximumSize = new Size(360, 0), Margin = new Padding(0, 0, 0, 12) });
        layout.Controls.Add(code);
        layout.Controls.Add(buttons);
        Controls.Add(layout);
        Shown += (_, _) =>
        {
            Activate();
            code.Focus();
        };
    }

    public static string? Ask(string message)
    {
        using var d = new PairDialog(message);
        if (d.ShowDialog() != DialogResult.OK) return null;
        var digits = new string(d.code.Text.Where(char.IsDigit).ToArray());
        return digits.Length == 6 ? digits : digits.Length > 0 ? digits : null;
    }
}

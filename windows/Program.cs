namespace WaveshareLink;

static class Program
{
    [STAThread]
    static void Main()
    {
        using var mutex = new Mutex(true, "WaveshareLink.SingleInstance", out bool first);
        if (!first)
        {
            MessageBox.Show("O Waveshare Link já está aberto (ícone perto do relógio).", "Waveshare Link");
            return;
        }

        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.ThreadException += (_, e) => Log.Error("UI", e.Exception);
        AppDomain.CurrentDomain.UnhandledException += (_, e) => Log.Error("fatal", e.ExceptionObject as Exception);
        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            Log.Error("task", e.Exception);
            e.SetObserved();
        };

        // Everything (network, feeds, UI) runs on this thread; awaits come back to it.
        SynchronizationContext.SetSynchronizationContext(new WindowsFormsSynchronizationContext());
        Log.Info($"Iniciando. Log em {Log.FilePath}");
        Application.Run(new TrayApp());
    }
}

namespace WaveshareLink;

/// The log goes to the console (dotnet run) and to %APPDATA%\WaveshareLink\log.txt.
static class Log
{
    public static string FilePath => Path.Combine(Config.Dir, "log.txt");
    static readonly object Gate = new();

    public static void Info(string msg) => Write("INFO", msg);
    public static void Error(string where, Exception? e) => Write("ERRO", $"{where}: {e}");

    static void Write(string level, string msg)
    {
        var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} {level} {msg}";
        try { Console.WriteLine(line); } catch { }
        try
        {
            lock (Gate)
            {
                Directory.CreateDirectory(Config.Dir);
                var fi = new FileInfo(FilePath);
                if (fi.Exists && fi.Length > 2_000_000) fi.Delete();
                File.AppendAllText(FilePath, line + Environment.NewLine);
            }
        }
        catch { }
    }
}

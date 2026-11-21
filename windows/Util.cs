using System.Globalization;
using System.Text;
using System.Text.Json.Nodes;

namespace WaveshareLink;

static class Text
{
    /// The board's fonts are ASCII only: accents are dropped, anything else becomes '?'.
    public static string Ascii(string? s)
    {
        if (string.IsNullOrEmpty(s)) return "";
        var norm = s.Normalize(NormalizationForm.FormD);
        var sb = new StringBuilder(norm.Length);
        foreach (var c in norm)
        {
            if (CharUnicodeInfo.GetUnicodeCategory(c) == UnicodeCategory.NonSpacingMark) continue;
            sb.Append(c < 128 ? c : '?');
        }
        return sb.ToString();
    }

    public static string Cut(string s, int max) => s.Length <= max ? s : s[..max];
}

static class Json
{
    public static string? Str(JsonObject o, string key) =>
        o[key] is JsonValue v && v.TryGetValue(out string? s) ? s : null;

    public static bool Bool(JsonObject o, string key) =>
        o[key] is JsonValue v && v.TryGetValue(out bool b) && b;

    public static JsonArray Array(IEnumerable<JsonNode?> items)
    {
        var a = new JsonArray();
        foreach (var i in items) a.Add(i);
        return a;
    }
}

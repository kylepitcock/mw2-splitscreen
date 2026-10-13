using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace MW2Splitscreen
{
    /// <summary>Launcher preferences, stored as key=value lines next to the exe.</summary>
    sealed class Settings
    {
        public string GameDir = "";
        public string GameMode = "war";
        public string Map = "";            // "" = map rotation
        public bool VerticalSplit;         // 2 players: side by side instead of top/bottom
        public bool AudioAllPlayers;       // false = only player 1's screen plays sound
        public int Monitor;
        public int Port = 28960;
        public bool Topmost = true;
        public readonly string[] Names = new string[4];

        static string FilePath => Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "MW2Splitscreen.ini");

        public static Settings Load()
        {
            var s = new Settings();
            if (!File.Exists(FilePath)) return s;

            var kv = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var raw in File.ReadAllLines(FilePath))
            {
                var line = raw.Trim();
                int eq = line.IndexOf('=');
                if (line.StartsWith(";") || line.StartsWith("#") || eq <= 0) continue;
                kv[line.Substring(0, eq).Trim()] = line.Substring(eq + 1).Trim();
            }

            string v;
            if (kv.TryGetValue("GameDir", out v)) s.GameDir = v;
            if (kv.TryGetValue("GameMode", out v) && v.Length > 0) s.GameMode = v;
            if (kv.TryGetValue("Map", out v)) s.Map = v;
            if (kv.TryGetValue("VerticalSplit", out v)) s.VerticalSplit = v == "1";
            if (kv.TryGetValue("AudioAllPlayers", out v)) s.AudioAllPlayers = v == "1";
            if (kv.TryGetValue("Monitor", out v)) int.TryParse(v, out s.Monitor);
            if (kv.TryGetValue("Port", out v)) int.TryParse(v, out s.Port);
            if (kv.TryGetValue("Topmost", out v)) s.Topmost = v != "0";
            for (int i = 0; i < 4; i++)
                if (kv.TryGetValue("Name" + (i + 1), out v)) s.Names[i] = v;
            if (s.Port <= 0 || s.Port > 65535) s.Port = 28960;
            return s;
        }

        public void Save()
        {
            var lines = new List<string>
            {
                "; MW2 Split Screen launcher settings",
                "GameDir=" + GameDir,
                "GameMode=" + GameMode,
                "Map=" + Map,
                "VerticalSplit=" + (VerticalSplit ? "1" : "0"),
                "AudioAllPlayers=" + (AudioAllPlayers ? "1" : "0"),
                "Monitor=" + Monitor.ToString(CultureInfo.InvariantCulture),
                "Port=" + Port.ToString(CultureInfo.InvariantCulture),
                "Topmost=" + (Topmost ? "1" : "0"),
                "; Optional name overrides (blank = use your IW4x name / guest names)",
            };
            for (int i = 0; i < 4; i++) lines.Add("Name" + (i + 1) + "=" + (Names[i] ?? ""));
            try { File.WriteAllLines(FilePath, lines); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}

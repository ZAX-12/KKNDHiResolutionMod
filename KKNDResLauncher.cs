// KKND Xtreme Resolution Launcher
// Patches a copy of KKNDgame.exe (the original is never modified) so the game
// renders at a selectable resolution, then launches it.
//
// The user picks a screen resolution and an integer scale; the game renders at
// resolution / scale and GOG's ddraw.dll wrapper scales it up to the screen.
//
// Patch sites (file offsets in the GOG KKNDgame.exe):
//   0x1A50D  mov edx, 480   -> display mode height   (call to display init @ 0x41B117)
//   0x1A512  mov ecx, 640   -> display mode width
//   0x0D796  mov eax, 640   -> game viewport width   (@ 0x40E396)
//   0x0D79B  mov ecx, 480   -> game viewport height  (@ 0x40E39B)
// Everything else in the engine reads the size from these variables.
//
// With "center menus and scale videos" on, the VERSION.dll import is also renamed
// to KRESMOD.dll (dll\kresmod.c), which hooks the game's frame routine.

using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

static class Program
{
    const string OriginalExe = "KKNDgame.exe";
    // Must live in the game folder: Windows loads ddraw.dll from the exe's own
    // folder, and without GOG's wrapper the game switches the real display mode.
    const string PatchedExe = "KKNDgame_hires.exe";
    const string IniFile = "KKNDResMod.ini";
    // Copied next to the patched exe (DLLs are loaded from the exe's folder).
    const string HookDll = "KRESMOD.dll";
    const int VersionImportOffset = 0x767FA;   // "VERSION.dll" in the import table

    public const int MinWidth = 640, MinHeight = 480;

    // offset, expected opcode byte, expected original value, true = width / false = height
    static readonly object[][] Sites = {
        new object[] { 0x1A50D, (byte)0xBA, 480u, false },
        new object[] { 0x1A512, (byte)0xB9, 640u, true  },
        new object[] { 0x0D796, (byte)0xB8, 640u, true  },
        new object[] { 0x0D79B, (byte)0xB9, 480u, false },
    };

    public static string ModDir, GameDir;

    [DllImport("user32.dll")]
    static extern bool SetProcessDPIAware();

    [STAThread]
    static int Main(string[] args)
    {
        // Without this, Windows display scaling (e.g. 125%) hides the real screen size.
        try { SetProcessDPIAware(); } catch { }

        ModDir = AppDomain.CurrentDomain.BaseDirectory;
        GameDir = ModDir;
        string parent = Path.GetDirectoryName(ModDir.TrimEnd('\\'));
        if (!File.Exists(Path.Combine(GameDir, OriginalExe)) && parent != null && File.Exists(Path.Combine(parent, OriginalExe)))
            GameDir = parent;

        // "-play" launches straight away with the saved settings (for shortcuts).
        if (args.Length > 0 && args[0].Equals("-play", StringComparison.OrdinalIgnoreCase))
        {
            Settings s = Settings.Load();
            Size game;
            if (s != null && GameSize(s.Resolution, s.Scale, out game) == null)
                return Launch(game, s.CenterMenus, null) ? 0 : 1;
        }

        Application.EnableVisualStyles();
        Application.Run(new LauncherForm());
        return 0;
    }

    public class Settings
    {
        public Size Resolution;
        public int Scale = 1;
        public bool CenterMenus = true;

        static string PathOf() { return Path.Combine(ModDir, IniFile); }

        public static Settings Load()
        {
            try
            {
                var s = new Settings();
                bool any = false;
                foreach (string line in File.ReadAllLines(PathOf()))
                {
                    string[] kv = line.Split('=');
                    if (kv.Length != 2) continue;
                    string key = kv[0].Trim().ToLowerInvariant();
                    if (key == "resolution" && TryParse(kv[1], out s.Resolution)) any = true;
                    if (key == "scale") int.TryParse(kv[1].Trim(), out s.Scale);
                    if (key == "center_menus") s.CenterMenus = kv[1].Trim() != "0";
                }
                if (s.Scale < 1) s.Scale = 1;
                return any ? s : null;
            }
            catch { return null; }
        }

        public void Save()
        {
            try
            {
                File.WriteAllText(PathOf(), "[KKNDResMod]\r\nresolution=" + Resolution.Width + "x" + Resolution.Height +
                                            "\r\nscale=" + Scale + "\r\ncenter_menus=" + (CenterMenus ? 1 : 0) + "\r\n");
            }
            catch { }
        }
    }

    public static Size DesktopSize()
    {
        Rectangle b = Screen.PrimaryScreen.Bounds;
        return new Size(b.Width, b.Height);
    }

    public static bool TryParse(string text, out Size size)
    {
        size = Size.Empty;
        if (text == null) return false;
        string t = text.Split('(')[0].Replace(" ", "").ToLowerInvariant().Replace('×', 'x');
        string[] p = t.Split('x');
        int w, h;
        if (p.Length != 2 || !int.TryParse(p[0], out w) || !int.TryParse(p[1], out h)) return false;
        size = new Size(w, h);
        return true;
    }

    // Largest scale that still leaves at least 640x480 for the game.
    public static int MaxScale(Size res)
    {
        return Math.Max(1, Math.Min(res.Width / MinWidth, res.Height / MinHeight));
    }

    // Game render size for a screen resolution and scale. Returns null if OK,
    // otherwise an error message. Width is rounded down to a multiple of 8.
    public static string GameSize(Size res, int scale, out Size game)
    {
        game = Size.Empty;
        if (scale < 1) return "Scale must be at least x1.";
        game = new Size((res.Width / scale) & ~7, res.Height / scale);
        if (game.Width < MinWidth || game.Height < MinHeight)
            return "Too small: the game needs at least " + MinWidth + "x" + MinHeight + " (menus are drawn at that size).";
        if (game.Width > 7680 || game.Height > 4320)
            return "Too large: the maximum game size is 7680x4320.";
        return null;
    }

    public static bool Launch(Size game, bool centerMenus, IWin32Window owner)
    {
        try
        {
            string src = Path.Combine(GameDir, OriginalExe);
            byte[] data = File.ReadAllBytes(src);
            bool original = game.Width == 640 && game.Height == 480;

            string exe = src;
            if (!original)
            {
                foreach (object[] site in Sites)
                {
                    int off = (int)site[0];
                    if (off + 5 > data.Length || data[off] != (byte)site[1] || BitConverter.ToUInt32(data, off + 1) != (uint)site[2])
                        throw new Exception("KKNDgame.exe is not the version this mod expects (GOG release).\n" +
                                            "Mismatch at file offset 0x" + off.ToString("X") + ". Nothing was changed.");
                    uint v = (bool)site[3] ? (uint)game.Width : (uint)game.Height;
                    BitConverter.GetBytes(v).CopyTo(data, off + 1);
                }

                if (centerMenus)
                {
                    // Load KRESMOD.dll instead of VERSION.dll (same name length; it forwards VERSION.dll).
                    byte[] from = System.Text.Encoding.ASCII.GetBytes("VERSION.dll");
                    byte[] to = System.Text.Encoding.ASCII.GetBytes(HookDll);
                    for (int i = 0; i < from.Length; i++)
                        if (data[VersionImportOffset + i] != from[i])
                            throw new Exception("KKNDgame.exe has an unexpected import table. Nothing was changed.");
                    to.CopyTo(data, VersionImportOffset);

                    string dllSrc = Path.Combine(ModDir, HookDll), dllDst = Path.Combine(GameDir, HookDll);
                    if (!File.Exists(dllSrc))
                        throw new Exception(HookDll + " is missing from the mod folder. Run build.cmd, or untick \"Center menus and scale videos\".");
                    if (!File.Exists(dllDst) || !BytesEqual(File.ReadAllBytes(dllSrc), File.ReadAllBytes(dllDst)))
                        File.Copy(dllSrc, dllDst, true);
                }
                exe = Path.Combine(GameDir, PatchedExe);
                if (!File.Exists(exe) || !BytesEqual(File.ReadAllBytes(exe), data))
                    File.WriteAllBytes(exe, data);
            }

            var psi = new ProcessStartInfo(exe);
            psi.WorkingDirectory = GameDir;
            psi.UseShellExecute = true;
            Process.Start(psi);
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(owner, ex.Message, "KKND Xtreme Resolution Mod", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
    }

    static bool BytesEqual(byte[] a, byte[] b)
    {
        if (a.Length != b.Length) return false;
        for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false;
        return true;
    }
}

class LauncherForm : Form
{
    static readonly int[][] Common = {
        new[]{640,480}, new[]{800,600}, new[]{1024,768}, new[]{1280,720}, new[]{1280,960}, new[]{1280,1024},
        new[]{1366,768}, new[]{1600,900}, new[]{1680,1050}, new[]{1920,1080}, new[]{1920,1200},
        new[]{2560,1080}, new[]{2560,1440}, new[]{2560,1600}, new[]{3440,1440}, new[]{3840,1600},
        new[]{3840,2160}, new[]{5120,1440}, new[]{5120,2160}, new[]{7680,4320} };

    ComboBox resCombo, scaleCombo;
    CheckBox center;
    Label result;
    Button play;

    public LauncherForm()
    {
        Text = "KKND Xtreme - Resolution";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false; MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(480, 206);
        Font = new Font("Segoe UI", 9f);
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

        Size d = Program.DesktopSize();
        string native = d.Width + "x" + d.Height;

        var resLabel = new Label { Text = "Resolution (type any, e.g. 1600x900)", Location = new Point(12, 12), AutoSize = true };
        var scaleLabel = new Label { Text = "Scale", Location = new Point(322, 12), AutoSize = true };

        resCombo = new ComboBox { Location = new Point(12, 34), Width = 300, DropDownStyle = ComboBoxStyle.DropDown, MaxDropDownItems = 20 };
        resCombo.Items.Add(native + "  (your screen)");
        foreach (int[] c in Common)
            if (c[0] + "x" + c[1] != native) resCombo.Items.Add(c[0] + "x" + c[1]);

        scaleCombo = new ComboBox { Location = new Point(322, 34), Width = 146, DropDownStyle = ComboBoxStyle.DropDownList };

        result = new Label { Location = new Point(12, 72), Size = new Size(456, 44) };

        center = new CheckBox { Text = "Center menus and scale videos to fit the screen", Location = new Point(12, 122), AutoSize = true, Checked = true };

        play = new Button { Text = "Play", Location = new Point(292, 162), Size = new Size(85, 30) };
        var cancel = new Button { Text = "Cancel", Location = new Point(383, 162), Size = new Size(85, 30), DialogResult = DialogResult.Cancel };
        play.Click += OnPlay;
        AcceptButton = play; CancelButton = cancel;
        Controls.AddRange(new Control[] { resLabel, scaleLabel, resCombo, scaleCombo, result, center, play, cancel });

        // Restore saved settings (default: your screen at x2).
        Program.Settings saved = Program.Settings.Load();
        int wantScale = 2;
        resCombo.SelectedIndex = 0;
        if (saved != null)
        {
            string key = saved.Resolution.Width + "x" + saved.Resolution.Height;
            int found = -1;
            Size item;
            for (int i = 0; i < resCombo.Items.Count; i++)
                if (Program.TryParse(resCombo.Items[i].ToString(), out item) && item == saved.Resolution) { found = i; break; }
            if (found >= 0) resCombo.SelectedIndex = found; else resCombo.Text = key;
            wantScale = saved.Scale;
            center.Checked = saved.CenterMenus;
        }
        RebuildScales(wantScale);

        resCombo.TextChanged += (s, e) => RebuildScales(SelectedScale());
        resCombo.SelectedIndexChanged += (s, e) => RebuildScales(SelectedScale());
        scaleCombo.SelectedIndexChanged += (s, e) => UpdateResult();
    }

    int SelectedScale()
    {
        return scaleCombo.SelectedIndex >= 0 ? scaleCombo.SelectedIndex + 1 : 1;
    }

    // Offer x1..xN, where N is the largest scale that keeps the game at 640x480 or more.
    void RebuildScales(int want)
    {
        Size res;
        int max = Program.TryParse(resCombo.Text, out res) ? Program.MaxScale(res) : 1;
        if (scaleCombo.Items.Count != max)
        {
            scaleCombo.BeginUpdate();
            scaleCombo.Items.Clear();
            for (int i = 1; i <= max; i++) scaleCombo.Items.Add("x" + i);
            scaleCombo.EndUpdate();
        }
        scaleCombo.SelectedIndex = Math.Min(Math.Max(want, 1), max) - 1;
        UpdateResult();
    }

    void UpdateResult()
    {
        Size res, game;
        string err;
        if (!Program.TryParse(resCombo.Text, out res)) err = "Enter a resolution like 1600x900.";
        else err = Program.GameSize(res, SelectedScale(), out game);

        if (err != null)
        {
            result.ForeColor = Color.Firebrick;
            result.Text = err;
            play.Enabled = false;
            return;
        }
        Program.GameSize(res, SelectedScale(), out game);
        int scale = SelectedScale();
        result.ForeColor = SystemColors.ControlText;
        result.Text = "Game renders at " + game.Width + "x" + game.Height +
            (game.Width == 640 && game.Height == 480 ? " (original game, unpatched)" : "") +
            "\n" + (scale == 1 ? "1 game pixel = 1 screen pixel (smallest units, most map visible)."
                               : "1 game pixel = " + scale + "x" + scale + " screen pixels.");
        play.Enabled = true;
    }

    void OnPlay(object sender, EventArgs e)
    {
        Size res, game;
        if (!Program.TryParse(resCombo.Text, out res) || Program.GameSize(res, SelectedScale(), out game) != null) return;
        new Program.Settings { Resolution = res, Scale = SelectedScale(), CenterMenus = center.Checked }.Save();
        if (Program.Launch(game, center.Checked, this)) Close();
    }
}

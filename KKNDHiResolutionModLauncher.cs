using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

static class Program
{
    const string OriginalExe = "KKNDgame.exe";
    const string PatchedExe = "KKNDgame_hires.exe";
    const string IniFile = "KKNDHiResolutionMod.ini";
    const string HookDll = "KKNDHiResolutionMod.dll";
    // .idata: file offset 0x75A00 = RVA 0x80000, virtual size 0xE82, raw size 0x1000.
    const int IdataRaw = 0x75A00, IdataRva = 0x80000, IdataVirtualSize = 0x1F8;
    const int VersionName = 0x767FA, VersionDescriptorName = 0x75AAC;
    const int NewName = 0x76890, Config = 0x768B0;
    const int MinWidth = 640, MinHeight = 480;

    // file offset, opcode, original value, 1 = width / 0 = height
    static readonly int[][] Sites = {
        new[] { 0x1A50D, 0xBA, 480, 0 },
        new[] { 0x1A512, 0xB9, 640, 1 },
        new[] { 0x0D796, 0xB8, 640, 1 },
        new[] { 0x0D79B, 0xB9, 480, 0 },
    };

    static string ModDir, GameDir;

    [DllImport("user32.dll")]
    static extern bool SetProcessDPIAware();

    [STAThread]
    static int Main(string[] args)
    {
        try { SetProcessDPIAware(); } catch { }

        ModDir = AppDomain.CurrentDomain.BaseDirectory;
        string parent = Path.GetDirectoryName(ModDir.TrimEnd('\\'));
        GameDir = !File.Exists(Path.Combine(ModDir, OriginalExe)) && parent != null && File.Exists(Path.Combine(parent, OriginalExe))
            ? parent : ModDir;

        Settings saved = Settings.Load();
        Size game;
        if (args.Length > 0 && args[0].Equals("-play", StringComparison.OrdinalIgnoreCase) &&
            saved != null && GameSize(saved.Resolution, saved.Scale, out game) == null)
            return Launch(game, saved.CenterMenus, null) ? 0 : 1;

        Application.EnableVisualStyles();
        Application.Run(new LauncherForm(saved));
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
                foreach (string[] kv in File.ReadAllLines(PathOf()).Select(l => l.Split('=')).Where(kv => kv.Length == 2))
                {
                    string key = kv[0].Trim().ToLowerInvariant(), value = kv[1].Trim();
                    if (key == "resolution") any |= TryParse(value, out s.Resolution);
                    if (key == "scale") int.TryParse(value, out s.Scale);
                    if (key == "center_menus") s.CenterMenus = value != "0";
                }
                s.Scale = Math.Max(1, s.Scale);
                return any ? s : null;
            }
            catch { return null; }
        }

        public void Save()
        {
            try
            {
                File.WriteAllText(PathOf(), string.Format("[KKNDHiResolutionMod]\r\nresolution={0}x{1}\r\nscale={2}\r\ncenter_menus={3}\r\n",
                    Resolution.Width, Resolution.Height, Scale, CenterMenus ? 1 : 0));
            }
            catch { }
        }
    }

    public static bool TryParse(string text, out Size size)
    {
        size = Size.Empty;
        string[] p = (text ?? "").Split('(')[0].Replace(" ", "").ToLowerInvariant().Replace('×', 'x').Split('x');
        int w, h;
        if (p.Length != 2 || !int.TryParse(p[0], out w) || !int.TryParse(p[1], out h)) return false;
        size = new Size(w, h);
        return true;
    }

    public static int MaxScale(Size res)
    {
        return Math.Max(1, Math.Min(res.Width / MinWidth, res.Height / MinHeight));
    }

    // Returns null if OK, otherwise an error message. Width is rounded down to a multiple of 8.
    public static string GameSize(Size res, int scale, out Size game)
    {
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
            string exe = Path.Combine(GameDir, OriginalExe);
            if (game != new Size(640, 480))
            {
                byte[] data = File.ReadAllBytes(exe);
                foreach (int[] s in Sites)
                {
                    if (data.Length < Config + 8 || data[s[0]] != s[1] || BitConverter.ToInt32(data, s[0] + 1) != s[2])
                        throw new Exception("KKNDgame.exe is not the version this mod expects (GOG release).\n" +
                                            "Mismatch at file offset 0x" + s[0].ToString("X") + ". Nothing was changed.");
                    Put(data, s[0] + 1, s[3] == 1 ? game.Width : game.Height);
                }

                byte[] oldName = Encoding.ASCII.GetBytes("VERSION.dll\0");
                if (BitConverter.ToInt32(data, VersionDescriptorName) != VersionName - IdataRaw + IdataRva ||
                    BitConverter.ToInt32(data, IdataVirtualSize) != 0xE82 ||
                    !data.Skip(VersionName).Take(oldName.Length).SequenceEqual(oldName) ||
                    data.Skip(NewName).Take(Config + 8 - NewName).Any(b => b != 0))
                    throw new Exception("KKNDgame.exe has an unexpected import table. Nothing was changed.");
                Encoding.ASCII.GetBytes(HookDll + "\0").CopyTo(data, NewName);
                Put(data, VersionDescriptorName, NewName - IdataRaw + IdataRva);
                Encoding.ASCII.GetBytes("KHRM").CopyTo(data, Config);
                Put(data, Config + 4, centerMenus ? 1 : 0);
                Put(data, IdataVirtualSize, Config + 8 - IdataRaw);

                string dll = Path.Combine(ModDir, HookDll);
                if (!File.Exists(dll))
                    throw new Exception(HookDll + " is missing from the mod folder. Run build.cmd.");
                WriteIfChanged(Path.Combine(GameDir, HookDll), File.ReadAllBytes(dll));
                exe = Path.Combine(GameDir, PatchedExe);
                WriteIfChanged(exe, data);
            }

            Process.Start(new ProcessStartInfo(exe) { WorkingDirectory = GameDir, UseShellExecute = true });
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(owner, ex.Message, "KKNDHiResolutionMod", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
    }

    static void Put(byte[] data, int offset, int value)
    {
        BitConverter.GetBytes(value).CopyTo(data, offset);
    }

    static void WriteIfChanged(string path, byte[] data)
    {
        if (!File.Exists(path) || !File.ReadAllBytes(path).SequenceEqual(data))
            File.WriteAllBytes(path, data);
    }
}

class LauncherForm : Form
{
    static readonly string[] Common = {
        "640x480", "800x600", "1024x768", "1280x720", "1280x960", "1280x1024", "1366x768", "1600x900",
        "1680x1050", "1920x1080", "1920x1200", "2560x1080", "2560x1440", "2560x1600", "3440x1440",
        "3840x1600", "3840x2160", "5120x1440", "5120x2160", "7680x4320" };

    readonly ComboBox resCombo, scaleCombo;
    readonly CheckBox center;
    readonly Label result;
    readonly Button play;
    int wantedScale = 2;
    bool rebuilding;

    public LauncherForm(Program.Settings saved)
    {
        Text = "KKNDHiResolutionMod - KKND Xtreme";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(480, 206);
        Font = new Font("Segoe UI", 9f);
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

        Size d = Screen.PrimaryScreen.Bounds.Size;
        string native = d.Width + "x" + d.Height;

        resCombo = new ComboBox { Location = new Point(12, 34), Width = 300, DropDownStyle = ComboBoxStyle.DropDown, MaxDropDownItems = 20 };
        resCombo.Items.Add(native + "  (your screen)");
        resCombo.Items.AddRange(Common.Where(c => c != native).ToArray());
        scaleCombo = new ComboBox { Location = new Point(322, 34), Width = 146, DropDownStyle = ComboBoxStyle.DropDownList };
        result = new Label { Location = new Point(12, 72), Size = new Size(456, 44) };
        center = new CheckBox { Text = "Center menus and scale videos to fit the screen", Location = new Point(12, 122), AutoSize = true, Checked = true };
        play = new Button { Text = "Play", Location = new Point(292, 162), Size = new Size(85, 30) };
        var cancel = new Button { Text = "Cancel", Location = new Point(383, 162), Size = new Size(85, 30), DialogResult = DialogResult.Cancel };
        play.Click += OnPlay;
        AcceptButton = play;
        CancelButton = cancel;
        Controls.AddRange(new Control[] {
            new Label { Text = "Resolution (type any, e.g. 1600x900)", Location = new Point(12, 12), AutoSize = true },
            new Label { Text = "Scale", Location = new Point(322, 12), AutoSize = true },
            resCombo, scaleCombo, result, center, play, cancel });

        resCombo.SelectedIndex = 0;
        if (saved != null)
        {
            Size item;
            int found = Enumerable.Range(0, resCombo.Items.Count)
                .Where(i => Program.TryParse(resCombo.Items[i].ToString(), out item) && item == saved.Resolution)
                .DefaultIfEmpty(-1).First();
            if (found >= 0) resCombo.SelectedIndex = found;
            else resCombo.Text = saved.Resolution.Width + "x" + saved.Resolution.Height;
            wantedScale = saved.Scale;
            center.Checked = saved.CenterMenus;
        }
        RebuildScales();

        resCombo.TextChanged += (s, e) => RebuildScales();
        scaleCombo.SelectedIndexChanged += (s, e) =>
        {
            if (rebuilding) return;
            wantedScale = SelectedScale();
            UpdateResult();
        };
    }

    int SelectedScale()
    {
        return Math.Max(1, scaleCombo.SelectedIndex + 1);
    }

    // Partial input while typing allows only x1, so the scale the user picked is kept
    // in wantedScale and restored as soon as the resolution allows it again.
    void RebuildScales()
    {
        Size res;
        int max = Program.TryParse(resCombo.Text, out res) ? Program.MaxScale(res) : 1;
        rebuilding = true;
        if (scaleCombo.Items.Count != max)
        {
            scaleCombo.Items.Clear();
            scaleCombo.Items.AddRange(Enumerable.Range(1, max).Select(i => (object)("x" + i)).ToArray());
        }
        scaleCombo.SelectedIndex = Math.Min(Math.Max(wantedScale, 1), max) - 1;
        rebuilding = false;
        UpdateResult();
    }

    void UpdateResult()
    {
        Size res, game = Size.Empty;
        int scale = SelectedScale();
        string err = Program.TryParse(resCombo.Text, out res) ? Program.GameSize(res, scale, out game) : "Enter a resolution like 1600x900.";
        play.Enabled = err == null;
        result.ForeColor = err == null ? SystemColors.ControlText : Color.Firebrick;
        result.Text = err ?? "Game renders at " + game.Width + "x" + game.Height +
            (game == new Size(640, 480) ? " (original game, unpatched)" : "") + "\n" +
            (scale == 1 ? "1 game pixel = 1 screen pixel (smallest units, most map visible)."
                        : "1 game pixel = " + scale + "x" + scale + " screen pixels.");
    }

    void OnPlay(object sender, EventArgs e)
    {
        Size res, game;
        if (!Program.TryParse(resCombo.Text, out res) || Program.GameSize(res, SelectedScale(), out game) != null) return;
        new Program.Settings { Resolution = res, Scale = SelectedScale(), CenterMenus = center.Checked }.Save();
        if (Program.Launch(game, center.Checked, this)) Close();
    }
}

using System.Diagnostics;

namespace ZPlus.Setup;

internal sealed class InstallerForm : Form
{
    private static readonly Color Background = Color.FromArgb(25, 27, 33);
    private static readonly Color Surface = Color.FromArgb(34, 37, 45);
    private static readonly Color SurfaceRaised = Color.FromArgb(43, 47, 57);
    private static readonly Color Accent = Color.FromArgb(91, 166, 255);
    private static readonly Color AccentHover = Color.FromArgb(119, 183, 255);
    private static readonly Color TextPrimary = Color.FromArgb(239, 241, 245);
    private static readonly Color TextSecondary = Color.FromArgb(165, 171, 183);
    private static readonly Color Success = Color.FromArgb(93, 204, 148);

    private readonly PackageManifestReader package;
    private readonly InstallLogger logger;
    private readonly Panel content;
    private readonly Panel footer;
    private readonly Label stepLabel;
    private readonly Button backButton;
    private readonly Button nextButton;
    private readonly Button cancelButton;
    private readonly string logoPath;
    private readonly string licensePath;
    private int page;
    private bool installing;
    private bool licenseAccepted;
    private TextBox? installPathBox;
    private CheckBox? toolsBox;
    private CheckBox? docsBox;
    private CheckBox? examplesBox;
    private CheckBox? desktopBox;
    private CheckBox? licenseBox;
    private Label? diskSpaceLabel;
    private Label? existingInstallLabel;
    private Label? errorLabel;
    private Label? progressStatus;
    private Label? progressFile;
    private ProgressBar? progressBar;
    private Button? cancelInstallButton;
    private InstallRecord? completedRecord;
    private string? lastError;
    private readonly Dictionary<string, Label> componentStatuses =
        new(StringComparer.OrdinalIgnoreCase);
    private readonly System.Windows.Forms.Timer fadeTimer = new() { Interval = 18 };

    public int ExitCode { get; private set; }

    public InstallerForm(PackageManifestReader package, InstallLogger logger)
    {
        this.package = package;
        this.logger = logger;
        logoPath = Path.Combine(AppContext.BaseDirectory, "ZPlusLogo.png");
        licensePath = Path.Combine(AppContext.BaseDirectory, "LICENSE.txt");
        Text = "Z+ Setup";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(920, 660);
        MinimumSize = new Size(800, 600);
        MaximumSize = new Size(1100, 800);
        BackColor = Background;
        ForeColor = TextPrimary;
        Font = new Font("Segoe UI", 10F, FontStyle.Regular, GraphicsUnit.Point);
        FormBorderStyle = FormBorderStyle.Sizable;
        MaximizeBox = true;
        DoubleBuffered = true;
        MinimumSize = new Size(800, 600);
        Opacity = 0;
        fadeTimer.Tick += (_, _) =>
        {
            Opacity = Math.Min(1, Opacity + 0.08);
            if (Opacity >= 1) fadeTimer.Stop();
        };
        Shown += (_, _) => fadeTimer.Start();
        FormClosed += (_, _) => fadeTimer.Dispose();

        var shell = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            RowCount = 2,
            BackColor = Background,
            Margin = Padding.Empty
        };
        shell.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 225));
        shell.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        shell.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        shell.RowStyles.Add(new RowStyle(SizeType.Absolute, 78));
        Controls.Add(shell);

        var rail = BuildRail();
        shell.Controls.Add(rail, 0, 0);
        shell.SetRowSpan(rail, 2);

        var main = new Panel { Dock = DockStyle.Fill, BackColor = Background, Padding = new Padding(34, 30, 34, 12) };
        shell.Controls.Add(main, 1, 0);
        stepLabel = new Label
        {
            Dock = DockStyle.Top,
            Height = 33,
            ForeColor = TextSecondary,
            Font = new Font(Font, FontStyle.Bold)
        };
        main.Controls.Add(stepLabel);
        content = new Panel { Dock = DockStyle.Fill, BackColor = Background, AutoScroll = true };
        main.Controls.Add(content);

        footer = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = Surface,
            Padding = new Padding(20, 12, 24, 12)
        };
        shell.Controls.Add(footer, 1, 1);
        backButton = MakeButton("Back", primary: false);
        nextButton = MakeButton("Continue", primary: true);
        cancelButton = MakeButton("Cancel", primary: false);
        backButton.Anchor = AnchorStyles.Right | AnchorStyles.Top;
        nextButton.Anchor = AnchorStyles.Right | AnchorStyles.Top;
        cancelButton.Anchor = AnchorStyles.Left | AnchorStyles.Top;
        cancelButton.Click += (_, _) => CancelCurrentOperation();
        backButton.Click += (_, _) => Navigate(-1);
        nextButton.Click += async (_, _) => await NavigateNextAsync();
        footer.Controls.Add(cancelButton);
        footer.Controls.Add(backButton);
        footer.Controls.Add(nextButton);
        footer.Resize += (_, _) => LayoutFooterButtons();
        LayoutFooterButtons();
        RenderPage();
    }

    private Panel BuildRail()
    {
        var rail = new Panel { Dock = DockStyle.Fill, BackColor = Color.FromArgb(21, 23, 28), Padding = new Padding(22, 24, 18, 18) };
        var logo = new PictureBox
        {
            Location = new Point(20, 24),
            Size = new Size(72, 72),
            SizeMode = PictureBoxSizeMode.Zoom,
            BackColor = Color.Transparent
        };
        if (File.Exists(logoPath)) logo.Image = Image.FromFile(logoPath);
        rail.Controls.Add(logo);
        rail.Controls.Add(new Label
        {
            Text = "Z+",
            Location = new Point(104, 31),
            Size = new Size(90, 40),
            Font = new Font("Segoe UI Semibold", 25F, FontStyle.Bold),
            ForeColor = TextPrimary
        });
        rail.Controls.Add(new Label
        {
            Text = "PROGRAMMING LANGUAGE",
            Location = new Point(106, 69),
            Size = new Size(102, 30),
            Font = new Font("Segoe UI", 7.5F, FontStyle.Bold),
            ForeColor = TextSecondary
        });

        var release = new Label
        {
            Text = "1.1.2   •   STABLE",
            Location = new Point(22, 130),
            Size = new Size(180, 34),
            BackColor = Color.FromArgb(30, 48, 62),
            ForeColor = Accent,
            TextAlign = ContentAlignment.MiddleCenter,
            Font = new Font("Segoe UI", 9F, FontStyle.Bold)
        };
        rail.Controls.Add(release);

        string[] steps = ["Welcome", "Location", "Components", "Install", "Complete"];
        int y = 207;
        for (int i = 0; i < steps.Length; i++)
        {
            int currentStep = i;
            var dot = new Label
            {
                Text = (i + 1).ToString(),
                Location = new Point(25, y),
                Size = new Size(27, 27),
                BorderStyle = BorderStyle.FixedSingle,
                BackColor = Surface,
                ForeColor = TextSecondary,
                TextAlign = ContentAlignment.MiddleCenter,
                Font = new Font("Segoe UI", 9F, FontStyle.Bold),
                Name = $"step-{i}"
            };
            rail.Controls.Add(dot);
            rail.Controls.Add(new Label
            {
                Text = steps[i],
                Location = new Point(65, y + 2),
                Size = new Size(130, 25),
                ForeColor = TextSecondary,
                Font = new Font("Segoe UI", 10F)
            });
            y += 53;
        }
        rail.Controls.Add(new Label
        {
            Text = PackageInfo.Attribution,
            Location = new Point(20, ClientSize.Height - 55),
            Size = new Size(190, 32),
            Anchor = AnchorStyles.Left | AnchorStyles.Bottom,
            ForeColor = TextSecondary,
            Font = new Font("Segoe UI", 8.5F)
        });
        return rail;
    }

    private void RenderPage()
    {
        content.SuspendLayout();
        content.Controls.Clear();
        switch (page)
        {
            case 0: RenderWelcome(); break;
            case 1: RenderLocation(); break;
            case 2: RenderComponents(); break;
            case 3: RenderReady(); break;
            case 4: RenderProgress(); break;
            case 5: RenderComplete(); break;
        }
        content.ResumeLayout(performLayout: true);
        stepLabel.Text = page switch
        {
            0 => "WELCOME TO Z+",
            1 => "INSTALLATION LOCATION",
            2 => "CHOOSE COMPONENTS",
            3 => "READY TO INSTALL",
            4 => "INSTALLING Z+",
            _ => "INSTALLATION COMPLETE"
        };
        backButton.Visible = page is > 0 and < 4;
        nextButton.Visible = page is < 4;
        cancelButton.Visible = page is < 5;
        nextButton.Text = page switch
        {
            2 => "Review",
            3 => "Install",
            _ => "Continue"
        };
        nextButton.Enabled = page != 0 || licenseAccepted;
        UpdateStepRail();
    }

    private void RenderWelcome()
    {
        var title = Heading("Build by Miroslav Aleksandrov", 22F);
        title.Location = new Point(0, 13);
        title.Size = new Size(560, 42);
        content.Controls.Add(title);
        content.Controls.Add(Body(
            "A language, compiler, standard library and development toolchain—ready to install.",
            new Point(0, 61), new Size(580, 48)));
        content.Controls.Add(Body(
            "Version 1.1.2  •  Stable Release",
            new Point(0, 108), new Size(340, 28), Accent));
        content.Controls.Add(Body(
            "The setup verifies the compiler, standard library and a native Windows build before it reports success.",
            new Point(0, 150), new Size(585, 46)));

        var licenseTitle = Heading("License", 12F);
        licenseTitle.Location = new Point(0, 216);
        licenseTitle.Size = new Size(200, 28);
        content.Controls.Add(licenseTitle);
        var licenseText = new TextBox
        {
            Location = new Point(0, 250),
            Size = new Size(610, 190),
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            BackColor = Surface,
            ForeColor = TextSecondary,
            BorderStyle = BorderStyle.FixedSingle,
            Font = new Font("Consolas", 8.5F),
            Text = File.Exists(licensePath)
                ? File.ReadAllText(licensePath)
                : "The bundled license file is missing. Setup cannot continue."
        };
        content.Controls.Add(licenseText);
        licenseBox = new CheckBox
        {
            Text = "I have read and accept the Z+ Proprietary Software License.",
            Location = new Point(0, 453),
            Size = new Size(610, 32),
            ForeColor = TextPrimary,
            FlatStyle = FlatStyle.Flat,
            Checked = licenseAccepted
        };
        licenseBox.CheckedChanged += (_, _) =>
        {
            licenseAccepted = licenseBox.Checked;
            nextButton.Enabled = licenseAccepted;
        };
        content.Controls.Add(licenseBox);
    }

    private void RenderLocation()
    {
        content.Controls.Add(Heading("Choose where Z+ will live", 20F));
        content.Controls.Add(Body(
            "Z+ installs for this Windows user. No administrator access is required.",
            new Point(0, 43), new Size(590, 40)));
        content.Controls.Add(Body("Installation directory", new Point(0, 111), new Size(260, 24), TextSecondary));
        installPathBox = new TextBox
        {
            Location = new Point(0, 141),
            Size = new Size(490, 34),
            Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
            BackColor = Surface,
            ForeColor = TextPrimary,
            BorderStyle = BorderStyle.FixedSingle,
            Font = new Font(Font.FontFamily, 10.5F),
            Text = WindowsIntegration.ExistingInstallPath() ?? WindowsIntegration.DefaultInstallPath
        };
        installPathBox.TextChanged += (_, _) => UpdateDiskSpace();
        content.Controls.Add(installPathBox);
        var browse = MakeButton("Browse…", primary: false);
        browse.Location = new Point(500, 140);
        browse.Size = new Size(108, 36);
        browse.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        browse.Click += (_, _) =>
        {
            using var dialog = new FolderBrowserDialog
            {
                Description = "Select the Z+ installation directory",
                UseDescriptionForTitle = true,
                ShowNewFolderButton = true,
                InitialDirectory = Directory.Exists(installPathBox.Text)
                    ? installPathBox.Text
                    : Path.GetDirectoryName(installPathBox.Text) ?? ""
            };
            if (dialog.ShowDialog(this) == DialogResult.OK)
                installPathBox.Text = dialog.SelectedPath;
        };
        content.Controls.Add(browse);
        diskSpaceLabel = Body("", new Point(0, 188), new Size(600, 28), TextSecondary);
        content.Controls.Add(diskSpaceLabel);
        existingInstallLabel = Body("", new Point(0, 230), new Size(600, 56), Accent);
        content.Controls.Add(existingInstallLabel);
        UpdateDiskSpace();
        string? existing = WindowsIntegration.ExistingInstallPath();
        if (!string.IsNullOrWhiteSpace(existing))
        {
            existingInstallLabel.Text =
                $"Existing installation detected at:\n{existing}\nChoose this path to update it.";
        }
        content.Controls.Add(Body(
            "A non-empty directory that is not already owned by Z+ will not be overwritten.",
            new Point(0, 310), new Size(590, 50), TextSecondary));
    }

    private void RenderComponents()
    {
        content.Controls.Add(Heading("Select installation components", 20F));
        content.Controls.Add(Body("Required files are always installed. Optional items are real files in this release package.",
            new Point(0, 43), new Size(590, 48)));
        var required = ComponentCard("Z+ compiler and standard library",
            "Compiler, Windows runtime dependencies, standard modules and license notices",
            checkedValue: true, enabled: false, new Point(0, 108));
        content.Controls.Add(required);
        toolsBox = ComponentCheck("ZBuild project tool and templates",
            "Command-line project builder and its packaged project templates",
            checkedValue: true, new Point(0, 194));
        docsBox = ComponentCheck("Documentation",
            "Syntax reference, module guide, backend notes and tool documentation",
            checkedValue: true, new Point(0, 274));
        examplesBox = ComponentCheck("Example projects",
            "Runnable Z+ programs from the examples directory",
            checkedValue: true, new Point(0, 354));
        content.Controls.Add(toolsBox);
        content.Controls.Add(docsBox);
        content.Controls.Add(examplesBox);
        desktopBox = new CheckBox
        {
            Text = "Create a desktop shortcut",
            Location = new Point(444, 464),
            Size = new Size(400, 30),
            ForeColor = TextPrimary,
            Checked = false,
            FlatStyle = FlatStyle.Flat
        };
        content.Controls.Add(desktopBox);
    }

    private void RenderReady()
    {
        content.Controls.Add(Heading("Review your installation", 20F));
        content.Controls.Add(Body(
            $"Z+ Programming Language  •  {PackageInfo.Version} Stable",
            new Point(0, 47), new Size(600, 30), Accent));
        content.Controls.Add(Body(
            $"Install location:\n{(installPathBox?.Text ?? WindowsIntegration.DefaultInstallPath)}",
            new Point(0, 102), new Size(600, 58)));
        var componentNames = new List<string> { "Compiler, standard library and runtime dependencies" };
        if (toolsBox?.Checked == true) componentNames.Add("ZBuild and templates");
        if (docsBox?.Checked == true) componentNames.Add("Documentation");
        if (examplesBox?.Checked == true) componentNames.Add("Example projects");
        content.Controls.Add(Body(
            "Components:\n• " + string.Join("\n• ", componentNames),
            new Point(0, 180), new Size(600, 140)));
        content.Controls.Add(Body(
            "The installer will verify every selected payload file with SHA-256, install transactionally, " +
            "then check the compiler version, standard library and native compilation.",
            new Point(0, 347), new Size(600, 80), TextSecondary));
        if (!string.IsNullOrWhiteSpace(lastError))
            content.Controls.Add(Body(lastError, new Point(0, 429), new Size(610, 74),
                Color.FromArgb(255, 139, 128)));
        content.Controls.Add(Body(PackageInfo.Attribution,
            new Point(0, 516), new Size(400, 30), TextSecondary));
    }

    private void RenderProgress()
    {
        content.Controls.Add(Heading("Installing Z+", 20F));
        progressStatus = Body("Preparing verified installation…",
            new Point(0, 45), new Size(600, 30), Accent);
        content.Controls.Add(progressStatus);
        progressBar = new ProgressBar
        {
            Location = new Point(0, 80),
            Size = new Size(610, 16),
            Style = ProgressBarStyle.Continuous,
            Minimum = 0,
            Maximum = 100
        };
        content.Controls.Add(progressBar);
        progressFile = Body("Waiting for installer",
            new Point(0, 103), new Size(610, 38), TextSecondary);
        content.Controls.Add(progressFile);
        componentStatuses.Clear();
        var statusPanel = new Panel
        {
            Location = new Point(0, 146),
            Size = new Size(610, 124),
            BackColor = Surface,
            BorderStyle = BorderStyle.FixedSingle
        };
        var selected = new (string Id, string Name, bool Enabled)[]
        {
            ("core", "Compiler, runtime and standard library", true),
            ("tools", "ZBuild and templates", toolsBox?.Checked == true),
            ("documentation", "Documentation", docsBox?.Checked == true),
            ("examples", "Examples", examplesBox?.Checked == true)
        };
        int row = 10;
        foreach ((string id, string name, bool enabled) in selected)
        {
            statusPanel.Controls.Add(Body(name, new Point(12, row),
                new Size(360, 24), TextPrimary));
            Label status = Body(enabled ? "Waiting" : "Not selected",
                new Point(390, row), new Size(200, 24), TextSecondary);
            statusPanel.Controls.Add(status);
            componentStatuses[id] = status;
            row += 27;
        }
        content.Controls.Add(statusPanel);
        var logInfo = Body($"Detailed log: {logger.Path}",
            new Point(0, 276), new Size(610, 32), TextSecondary);
        content.Controls.Add(logInfo);
        errorLabel = Body("", new Point(0, 310), new Size(610, 64),
            Color.FromArgb(255, 139, 128));
        content.Controls.Add(errorLabel);
        cancelInstallButton = MakeButton("Cancel safely", primary: false);
        cancelInstallButton.Location = new Point(0, 382);
        cancelInstallButton.Size = new Size(155, 40);
        cancelInstallButton.Click += (_, _) => CancelCurrentOperation();
        content.Controls.Add(cancelInstallButton);
    }

    private void RenderComplete()
    {
        var complete = Heading("Z+ is ready", 26F);
        complete.ForeColor = Success;
        content.Controls.Add(complete);
        content.Controls.Add(Body(
            $"Version {completedRecord?.Version ?? PackageInfo.Version} Stable was verified and installed.",
            new Point(0, 54), new Size(600, 38)));
        content.Controls.Add(Body(
            $"Installation directory:\n{completedRecord?.InstallPath}",
            new Point(0, 116), new Size(600, 55), TextSecondary));
        content.Controls.Add(Body(
            "Compiler version, standard-library lookup, interpreter execution and native Windows x64 " +
            "compile-and-run checks all passed.",
            new Point(0, 200), new Size(600, 70), Success));
        content.Controls.Add(Body(PackageInfo.Attribution,
            new Point(0, 294), new Size(500, 28), TextSecondary));
        var docs = MakeButton("Open documentation", primary: false);
        docs.Location = new Point(0, 365);
        docs.Size = new Size(190, 42);
        docs.Enabled = File.Exists(Path.Combine(completedRecord!.InstallPath, "docs", "SYNTAX.md"));
        docs.Click += (_, _) => OpenDocumentation();
        content.Controls.Add(docs);
        var terminal = MakeButton("Open Z+ terminal", primary: true);
        terminal.Location = new Point(205, 365);
        terminal.Size = new Size(185, 42);
        terminal.Click += (_, _) => OpenTerminal();
        content.Controls.Add(terminal);
    }

    private async Task NavigateNextAsync()
    {
        if (page == 0)
        {
            if (!licenseAccepted) return;
            page = 1;
        }
        else if (page == 1)
        {
            try
            {
                _ = Path.GetFullPath(installPathBox?.Text ?? "");
                page = 2;
            }
            catch (Exception error) when (error is ArgumentException or NotSupportedException)
            {
                ShowError(error.Message);
                return;
            }
        }
        else if (page == 2)
        {
            page = 3;
        }
        else if (page == 3)
        {
            await BeginInstallAsync(replaceModified: false);
            return;
        }
        RenderPage();
    }

    private void Navigate(int delta)
    {
        if (installing) return;
        page = Math.Clamp(page + delta, 0, 3);
        RenderPage();
    }

    private async Task BeginInstallAsync(bool replaceModified)
    {
        installing = true;
        page = 4;
        RenderPage();
        nextButton.Enabled = false;
        cancelButton.Visible = false;
        using var cancellation = new CancellationTokenSource();
        activeCancellation = cancellation;
        var progress = new Progress<InstallProgress>(UpdateProgress);
        var options = new InstallOptions(
            installPathBox?.Text ?? WindowsIntegration.DefaultInstallPath,
            IncludeTools: toolsBox?.Checked == true,
            IncludeDocumentation: docsBox?.Checked == true,
            IncludeExamples: examplesBox?.Checked == true,
            CreateDesktopShortcut: desktopBox?.Checked == true,
            ReplaceModifiedFiles: replaceModified);
        try
        {
            completedRecord = await new InstallEngine(package, logger).InstallAsync(
                options, progress, cancellation.Token);
            page = 5;
            RenderPage();
        }
        catch (OperationCanceledException)
        {
            page = 3;
            lastError = "Installation was cancelled. Any staged changes were rolled back.";
            RenderPage();
        }
        catch (Exception error)
        {
            if (!replaceModified && error.Message.Contains(
                    "were changed since installation", StringComparison.Ordinal))
            {
                DialogResult answer = MessageBox.Show(
                    $"{error.Message}\n\nReplace these modified Z+ files with this release?",
                    "Confirm replacement",
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Warning,
                    MessageBoxDefaultButton.Button2);
                page = 3;
                installing = false;
                if (answer == DialogResult.Yes)
                {
                    await BeginInstallAsync(replaceModified: true);
                    return;
                }
            }
            page = 3;
            lastError = $"{error.Message}\n\nDetails: {logger.Path}";
            RenderPage();
        }
        finally
        {
            installing = false;
            activeCancellation = null;
            if (page != 5)
            {
                cancelButton.Visible = true;
                nextButton.Enabled = true;
            }
        }
    }

    private CancellationTokenSource? activeCancellation;

    private void UpdateProgress(InstallProgress progress)
    {
        if (IsDisposed) return;
        if (progressStatus is not null) progressStatus.Text = progress.Stage;
        if (progressFile is not null)
            progressFile.Text = progress.CurrentFile.Length == 0
                ? "Running compiler and standard-library verification…"
                : progress.CurrentFile;
        string? componentId = progress.CurrentFile.StartsWith("bin/zbuild", StringComparison.OrdinalIgnoreCase)
            ? "tools"
            : progress.CurrentFile.StartsWith("docs/", StringComparison.OrdinalIgnoreCase)
                ? "documentation"
                : progress.CurrentFile.StartsWith("examples/", StringComparison.OrdinalIgnoreCase)
                    ? "examples"
                    : progress.CurrentFile.StartsWith("bin/", StringComparison.OrdinalIgnoreCase) ||
                      progress.CurrentFile.StartsWith("stdlib/", StringComparison.OrdinalIgnoreCase) ||
                      progress.CurrentFile.StartsWith("LICENSE", StringComparison.OrdinalIgnoreCase) ||
                      progress.CurrentFile.StartsWith("THIRD-PARTY-NOTICES", StringComparison.OrdinalIgnoreCase)
                        ? "core"
                        : null;
        if (componentId is not null && componentStatuses.TryGetValue(componentId, out Label? status))
        {
            status.Text = progress.Stage switch
            {
                "Verifying compiler and standard library" => "Verifying",
                "Installing components" => "Installing",
                _ => "Verifying SHA-256"
            };
            status.ForeColor = Accent;
        }
        if (progressBar is null) return;
        if (progress.TotalBytes <= 0)
        {
            progressBar.Style = ProgressBarStyle.Marquee;
            return;
        }
        progressBar.Style = ProgressBarStyle.Continuous;
        long bounded = Math.Clamp(progress.CompletedBytes, 0, progress.TotalBytes);
        progressBar.Value = (int)Math.Min(100, bounded * 100 / progress.TotalBytes);
    }

    private void UpdateDiskSpace()
    {
        if (diskSpaceLabel is null || installPathBox is null) return;
        try
        {
            string full = Path.GetFullPath(installPathBox.Text.Trim());
            string root = Path.GetPathRoot(full)
                ?? throw new IOException("Cannot determine the installation drive.");
            var drive = new DriveInfo(root);
            diskSpaceLabel.Text = drive.IsReady
                ? $"Available disk space: {drive.AvailableFreeSpace / (1024d * 1024d):N0} MiB on {root}"
                : "Available disk space cannot be determined until the destination drive is ready.";
        }
        catch (Exception error) when (error is ArgumentException or IOException or
                                       UnauthorizedAccessException or NotSupportedException)
        {
            diskSpaceLabel.Text = $"Available disk space: unavailable ({error.Message})";
        }
    }

    private void CancelCurrentOperation()
    {
        if (installing)
        {
            activeCancellation?.Cancel();
            if (progressStatus is not null)
                progressStatus.Text = "Cancelling safely; rolling back any staged files…";
            if (cancelInstallButton is not null) cancelInstallButton.Enabled = false;
            return;
        }
        if (page == 5) ExitCode = 0;
        Close();
    }

    private void OpenDocumentation()
    {
        string path = Path.Combine(completedRecord!.InstallPath, "docs", "SYNTAX.md");
        Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
    }

    private void OpenTerminal()
    {
        var start = new ProcessStartInfo
        {
            FileName = Path.Combine(Environment.SystemDirectory, "cmd.exe"),
            WorkingDirectory = completedRecord!.InstallPath,
            UseShellExecute = false
        };
        start.ArgumentList.Add("/K");
        start.ArgumentList.Add("zp.exe --help");
        string existingPath = start.Environment.TryGetValue("PATH", out string? path)
            ? path ?? ""
            : Environment.GetEnvironmentVariable("PATH") ?? "";
        start.Environment["PATH"] = Path.Combine(completedRecord.InstallPath, "bin") +
            Path.PathSeparator + existingPath;
        Process.Start(start);
    }

    private void ShowError(string message)
    {
        logger.Error(message);
        MessageBox.Show(message, "Z+ Setup", MessageBoxButtons.OK, MessageBoxIcon.Warning);
    }

    private void LayoutFooterButtons()
    {
        if (footer.Width <= 0) return;
        cancelButton.Location = new Point(20, 16);
        nextButton.Location = new Point(footer.Width - nextButton.Width - 24, 16);
        backButton.Location = new Point(
            nextButton.Left - backButton.Width - 12, 16);
    }

    private void UpdateStepRail()
    {
        foreach (Control control in Controls)
            UpdateStepRailRecursive(control);
    }

    private void UpdateStepRailRecursive(Control parent)
    {
        foreach (Control child in parent.Controls)
        {
            if (child.Name.StartsWith("step-", StringComparison.Ordinal) &&
                int.TryParse(child.Name.AsSpan(5), out int index))
            {
                child.BackColor = index < page ? Color.FromArgb(37, 71, 58) :
                    index == page ? Color.FromArgb(30, 48, 62) : Surface;
                child.ForeColor = index < page ? Success :
                    index == page ? Accent : TextSecondary;
            }
            if (child.HasChildren) UpdateStepRailRecursive(child);
        }
    }

    private static Label Heading(string text, float size)
    {
        return new Label
        {
            Text = text,
            Location = new Point(0, 0),
            Size = new Size(620, (int)(size * 2.2F)),
            ForeColor = TextPrimary,
            Font = new Font("Segoe UI Semibold", size, FontStyle.Bold),
            AutoEllipsis = true
        };
    }

    private static Label Body(string text, Point location, Size size, Color? color = null)
    {
        return new Label
        {
            Text = text,
            Location = location,
            Size = size,
            ForeColor = color ?? TextPrimary,
            Font = new Font("Segoe UI", 10F),
            AutoEllipsis = true
        };
    }

    private static Button MakeButton(string text, bool primary)
    {
        var button = new Button
        {
            Text = text,
            Size = new Size(primary ? 126 : 100, 42),
            FlatStyle = FlatStyle.Flat,
            BackColor = primary ? Accent : SurfaceRaised,
            ForeColor = primary ? Color.FromArgb(16, 23, 31) : TextPrimary,
            Font = new Font("Segoe UI Semibold", 9.5F, FontStyle.Bold),
            Cursor = Cursors.Hand,
            UseVisualStyleBackColor = false
        };
        button.FlatAppearance.BorderSize = 0;
        button.FlatAppearance.MouseOverBackColor = primary ? AccentHover : Color.FromArgb(55, 60, 72);
        button.FlatAppearance.MouseDownBackColor = primary ? Color.FromArgb(67, 144, 230) : Surface;
        return button;
    }

    private static Panel ComponentCard(
        string title, string description, bool checkedValue, bool enabled, Point location)
    {
        var card = new Panel
        {
            Location = location,
            Size = new Size(610, 73),
            BackColor = Surface,
            BorderStyle = BorderStyle.FixedSingle
        };
        card.Controls.Add(new CheckBox
        {
            Text = title,
            Location = new Point(14, 8),
            Size = new Size(575, 27),
            Checked = checkedValue,
            Enabled = enabled,
            ForeColor = TextPrimary,
            Font = new Font("Segoe UI Semibold", 10F, FontStyle.Bold),
            FlatStyle = FlatStyle.Flat
        });
        card.Controls.Add(Body(description, new Point(40, 39),
            new Size(545, 25), TextSecondary));
        return card;
    }

    private static CheckBox ComponentCheck(
        string title, string description, bool checkedValue, Point location)
    {
        var check = new CheckBox
        {
            Text = $"{title}\n{description}",
            Location = location,
            Size = new Size(610, 72),
            Checked = checkedValue,
            ForeColor = TextPrimary,
            BackColor = Surface,
            FlatStyle = FlatStyle.Flat,
            Font = new Font("Segoe UI", 9.5F),
            Padding = new Padding(12, 7, 8, 4)
        };
        check.FlatAppearance.BorderSize = 1;
        check.FlatAppearance.BorderColor = SurfaceRaised;
        return check;
    }
}

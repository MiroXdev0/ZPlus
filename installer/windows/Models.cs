using System.Text.Json.Serialization;

namespace ZPlus.Setup;

internal sealed class PackageManifest
{
    [JsonPropertyName("product")]
    public string Product { get; set; } = "";

    [JsonPropertyName("version")]
    public string Version { get; set; } = "";

    [JsonPropertyName("files")]
    public List<PackageFile> Files { get; set; } = [];

    [JsonPropertyName("components")]
    public List<PackageComponent> Components { get; set; } = [];
}

internal sealed class PackageFile
{
    [JsonPropertyName("path")]
    public string Path { get; set; } = "";

    [JsonPropertyName("component")]
    public string Component { get; set; } = "";

    [JsonPropertyName("sha256")]
    public string Sha256 { get; set; } = "";

    [JsonPropertyName("length")]
    public long Length { get; set; }
}

internal sealed class PackageComponent
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = "";

    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("description")]
    public string Description { get; set; } = "";

    [JsonPropertyName("required")]
    public bool Required { get; set; }
}

internal sealed class InstallRecord
{
    public string Product { get; set; } = "Z+";
    public string Version { get; set; } = "";
    public string InstallPath { get; set; } = "";
    public Dictionary<string, string> Files { get; set; } =
        new(StringComparer.OrdinalIgnoreCase);
    public bool AddedPathEntry { get; set; }
    public bool AssociationApplied { get; set; }
    public string? PreviousZpAssociation { get; set; }
    public bool PreviousZpKeyExisted { get; set; }
    public List<string> ShortcutPaths { get; set; } = [];
    public List<string> Directories { get; set; } = [];
}

internal sealed record InstallOptions(
    string InstallPath,
    bool IncludeTools = true,
    bool IncludeDocumentation = true,
    bool IncludeExamples = true,
    bool CreateDesktopShortcut = false,
    bool ReplaceModifiedFiles = false);

internal sealed record InstallProgress(
    string Stage,
    string CurrentFile,
    long CompletedBytes,
    long TotalBytes);

internal sealed record ValidatedInstallLocation(
    string Path,
    InstallRecord? ExistingRecord);

internal static class PackageInfo
{
    public const string Product = "Z+";
    public const string Version = "1.1.2";
    public const string Attribution = "Build by Miroslav Aleksandrov";
    public const string ManifestFileName = "payload-manifest.json";
    public const string PayloadDirectoryName = "payload";
    public const string MarkerFileName = ".zplus-installation.json";
}

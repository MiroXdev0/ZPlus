using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;

namespace ZPlus.Setup;

internal sealed class PackageManifestReader
{
    private readonly Dictionary<string, PackageFile> byPath =
        new(StringComparer.OrdinalIgnoreCase);

    public string PackageDirectory { get; }
    public PackageManifest Manifest { get; }
    public IReadOnlyList<PackageFile> Files => Manifest.Files;

    private PackageManifestReader(string packageDirectory, PackageManifest manifest)
    {
        PackageDirectory = packageDirectory;
        Manifest = manifest;
    }

    public static PackageManifestReader Load(string setupDirectory)
    {
        string manifestPath = Path.Combine(setupDirectory, PackageInfo.ManifestFileName);
        string payloadRoot = Path.Combine(setupDirectory, PackageInfo.PayloadDirectoryName);
        if (!File.Exists(manifestPath))
            throw new InvalidDataException($"Release manifest is missing: {manifestPath}");
        if (!Directory.Exists(payloadRoot))
            throw new InvalidDataException($"Release payload directory is missing: {payloadRoot}");

        string? expectedHash = Assembly.GetExecutingAssembly()
            .GetCustomAttributes<AssemblyMetadataAttribute>()
            .FirstOrDefault(attribute =>
                attribute.Key == "ZPlusPackageManifestSha256")?.Value;
        if (string.IsNullOrWhiteSpace(expectedHash))
            throw new InvalidDataException(
                "This setup executable has no embedded payload-manifest hash. Use a release package.");

        byte[] manifestBytes = File.ReadAllBytes(manifestPath);
        string actualHash = Convert.ToHexString(SHA256.HashData(manifestBytes));
        if (!CryptographicOperations.FixedTimeEquals(
                Convert.FromHexString(expectedHash), Convert.FromHexString(actualHash)))
            throw new InvalidDataException(
                $"Release manifest SHA-256 mismatch. Expected {expectedHash}; found {actualHash}.");

        PackageManifest manifest = JsonSerializer.Deserialize<PackageManifest>(manifestBytes)
            ?? throw new InvalidDataException("Release manifest is empty or invalid JSON.");
        if (manifest.Product != PackageInfo.Product || manifest.Version != PackageInfo.Version)
            throw new InvalidDataException(
                $"Release manifest identifies {manifest.Product} {manifest.Version}, expected Z+ {PackageInfo.Version}.");
        if (manifest.Files.Count == 0 || !manifest.Components.Any(component =>
                component.Id == "core" && component.Required))
            throw new InvalidDataException("Release manifest is missing required core files or component metadata.");

        var result = new PackageManifestReader(Path.GetFullPath(payloadRoot), manifest);
        foreach (PackageFile file in manifest.Files)
        {
            ValidateRelativePath(file.Path);
            if (!result.byPath.TryAdd(file.Path, file))
                throw new InvalidDataException($"Release manifest contains a duplicate path: {file.Path}");
            if (!manifest.Components.Any(component => component.Id == file.Component))
                throw new InvalidDataException(
                    $"Manifest file '{file.Path}' refers to unknown component '{file.Component}'.");
            if (file.Length < 0 || file.Sha256.Length != 64 ||
                !file.Sha256.All(Uri.IsHexDigit))
                throw new InvalidDataException($"Manifest hash or size is invalid for '{file.Path}'.");

            string source = ResolvePayloadPath(result.PackageDirectory, file.Path);
            if (!File.Exists(source))
                throw new FileNotFoundException(
                    $"Release payload file is missing: {file.Path}", source);
            if (new FileInfo(source).Length != file.Length)
                throw new InvalidDataException($"Payload size does not match the manifest: {file.Path}");
        }
        return result;
    }

    public IReadOnlyList<PackageFile> SelectFiles(InstallOptions options)
    {
        var selectedComponents = new HashSet<string>(StringComparer.Ordinal)
        {
            "core"
        };
        if (options.IncludeTools) selectedComponents.Add("tools");
        if (options.IncludeDocumentation) selectedComponents.Add("documentation");
        if (options.IncludeExamples) selectedComponents.Add("examples");
        return Manifest.Files.Where(file => selectedComponents.Contains(file.Component))
            .ToArray();
    }

    public static string ResolvePayloadPath(string payloadRoot, string relativePath)
    {
        ValidateRelativePath(relativePath);
        string root = Path.GetFullPath(payloadRoot)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) +
            Path.DirectorySeparatorChar;
        string resolved = Path.GetFullPath(Path.Combine(
            payloadRoot, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!resolved.StartsWith(root, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Payload path escapes its root: {relativePath}");
        return resolved;
    }

    private static void ValidateRelativePath(string relativePath)
    {
        if (string.IsNullOrWhiteSpace(relativePath) || Path.IsPathRooted(relativePath) ||
            relativePath.Contains(':') || relativePath.Contains('\\') ||
            relativePath.Split('/').Any(part =>
                part.Length == 0 || part is "." or ".." ||
                part.EndsWith(' ') || part.EndsWith('.')))
            throw new InvalidDataException($"Unsafe package path: '{relativePath}'.");
    }
}

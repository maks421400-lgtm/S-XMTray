$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot 'out'
$stagingDirectory = Join-Path $outputDirectory 'S-XMTray-v0.5.2-windows-x64'
New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null
$releaseFiles = @{
    'build\S-XMTray.exe' = 'S-XMTray.exe'
    'README.md' = 'README.md'
    'README.ru.md' = 'README.ru.md'
    'LICENSE' = 'LICENSE'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'docs\DEVELOPMENT.md' = 'docs/DEVELOPMENT.md'
    'docs\images\panel-light.png' = 'docs/images/panel-light.png'
    'docs\images\panel-dark.png' = 'docs/images/panel-dark.png'
    'docs\images\panel-light-en.png' = 'docs/images/panel-light-en.png'
    'docs\images\panel-dark-en.png' = 'docs/images/panel-dark-en.png'
}
foreach ($entry in $releaseFiles.GetEnumerator()) {
    $destination = Join-Path $stagingDirectory $entry.Value
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot $entry.Key) -Destination $destination -Force
}
$archivePath = Join-Path $outputDirectory 'S-XMTray-v0.5.2-windows-x64.zip'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stream = [IO.File]::Open($archivePath, [IO.FileMode]::Create)
$archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($relativePath in ($releaseFiles.Values | Sort-Object)) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,
            (Join-Path $stagingDirectory $relativePath), $relativePath.Replace('\','/'),
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose(); $stream.Dispose() }
$checksum = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath ($archivePath + '.sha256') -Value ($checksum + '  ' + (Split-Path -Leaf $archivePath)) -Encoding ascii
Get-Item -LiteralPath $archivePath | Select-Object Name, Length

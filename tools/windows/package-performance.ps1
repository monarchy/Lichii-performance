# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$binaryDirectory = Join-Path $build 'bin'
$package = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination)
if (Test-Path -LiteralPath $package) {
    if (Get-ChildItem -LiteralPath $package -Force | Select-Object -First 1) {
        throw "Package destination must be empty: $package"
    }
}
New-Item -ItemType Directory -Path $package -Force | Out-Null

foreach ($name in @('lichii.exe', 'eden-cli.exe', 'nx2_switches.txt')) {
    Copy-Item -LiteralPath (Join-Path $binaryDirectory $name) -Destination $package
}
Get-ChildItem -LiteralPath $binaryDirectory -Filter '*.dll' -File |
    Copy-Item -Destination $package

$deploy = Join-Path $runtime 'windeployqt6.exe'
$objdump = Join-Path $runtime 'objdump.exe'
foreach ($tool in @($deploy, $objdump)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required packaging tool is missing: $tool"
    }
}
& $deploy --release --no-translations --no-compiler-runtime --no-opengl-sw `
    --no-system-dxc-compiler --no-system-d3d-compiler `
    --dir $package --plugindir (Join-Path $package 'plugins') (Join-Path $package 'lichii.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
[IO.File]::WriteAllText((Join-Path $package 'qt.conf'),
    "[Paths]`nPrefix=.`nPlugins=plugins`n", [Text.UTF8Encoding]::new($false))

# Resolve imports only from this build, its declared runtime and bundled dependencies.
$search = @($binaryDirectory, $runtime)
$dependencyRoot = Join-Path $repo '.cache\cpm'
$dependencyFiles = Get-ChildItem -LiteralPath $dependencyRoot -Filter '*.dll' -File -Recurse
$sources = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($directory in $search) {
    foreach ($dll in Get-ChildItem -LiteralPath $directory -Filter '*.dll' -File) {
        if (!$sources.ContainsKey($dll.Name)) { $sources.Add($dll.Name, $dll.FullName) }
    }
}
foreach ($dll in $dependencyFiles) {
    if (!$sources.ContainsKey($dll.Name)) { $sources.Add($dll.Name, $dll.FullName) }
}

$pending = [Collections.Generic.Queue[string]]::new()
foreach ($file in Get-ChildItem -LiteralPath $package -File -Recurse |
    Where-Object { $_.Extension -in @('.exe', '.dll') }) {
    $pending.Enqueue($file.FullName)
}
$visited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while ($pending.Count -gt 0) {
    $file = $pending.Dequeue()
    if (!$visited.Add($file)) { continue }
    $imports = & $objdump -p $file
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect imports: $file" }
    foreach ($line in $imports) {
        if ($line -notmatch '^\s*DLL Name:\s*(\S+)\s*$') { continue }
        $name = $Matches[1]
        if ([IO.Path]::GetFileName($name) -ne $name) { throw "Invalid import name: $name" }
        if ($name -match '^(api|ext)-ms-win-') { continue }
        $target = Join-Path $package $name
        if (!(Test-Path -LiteralPath $target -PathType Leaf)) {
            if (!$sources.ContainsKey($name)) {
                if (Test-Path -LiteralPath (Join-Path $env:SystemRoot ('System32\' + $name))) {
                    continue
                }
                throw "Unresolved dependency $name in $file"
            }
            Copy-Item -LiteralPath $sources[$name] -Destination $target
            $pending.Enqueue($target)
        }
    }
}

Copy-Item -LiteralPath (Join-Path $repo 'LICENSE.txt') -Destination $package
Copy-Item -LiteralPath (Join-Path $repo 'LICENSES') -Destination $package -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'docs\performance-fork.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $repo 'docs\performance-fork.ru.md') -Destination $package
New-Item -ItemType Directory -Path (Join-Path $package 'performance') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'docs\performance\pipeline-transition-results.csv') `
    -Destination (Join-Path $package 'performance')
Write-Output "Package prepared: $package"

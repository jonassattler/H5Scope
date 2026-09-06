# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# Put the MSVC x64 toolchain into the environment of every later workflow step.
#
# The Windows counterpart of tools/ci/el8.sh, and it exists for the same
# reason: the one place a build's compiler is decided should be a file in the
# repository that can be read and run by hand, not a line of YAML.
#
# The Ninja generator does not look for a compiler the way the Visual Studio
# generator does -- it expects cl.exe, the SDK headers and the libraries to be
# on the environment already, which on a developer's machine is what the
# "x64 Native Tools Command Prompt" shortcut arranges. There is no such shell
# in a workflow step, and a step cannot leave its environment behind for the
# next one except through GITHUB_ENV and GITHUB_PATH. So: run vcvars64.bat in
# a cmd of its own, diff the environment it produced against this one, and
# publish the difference.
#
# That is what the third-party msvc-dev-cmd action does. Doing it here instead
# is a dozen lines against one more action to trust in a repository that pins
# its vcpkg baseline and builds every dependency from source.
#
# Run outside Actions -- no GITHUB_ENV -- it prints what it would set, which
# is how to check it without pushing.
#
# Usage: pwsh -File tools/ci/msvc-env.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at $vswhere -- is Visual Studio installed?"
}

# -requires, so an installation carrying only the IDE is not selected and then
# found to have no cl.exe three steps later.
$install = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $install) {
    throw 'No Visual Studio installation with the x64 C++ toolchain.'
}
$install = ($install | Select-Object -First 1).Trim()

$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "No vcvars64.bat under $install"
}
Write-Host "Visual Studio: $install"

# Its banner goes to nul: what is wanted from this cmd is the output of `set`
# and nothing else, and the banner is not in KEY=VALUE form.
$after = @{}
foreach ($line in (cmd /c "`"$vcvars`" >nul 2>&1 && set")) {
    # The first `=` separates them; a value may contain as many more as it
    # likes, and PATH always does.
    $split = $line.IndexOf('=')
    if ($split -gt 0) {
        $after[$line.Substring(0, $split)] = $line.Substring($split + 1)
    }
}
if ($after.Count -eq 0) {
    throw "vcvars64.bat produced no environment -- see $vcvars"
}
if (-not $after.ContainsKey('VCToolsInstallDir')) {
    throw 'vcvars64.bat ran but set no VCToolsInstallDir; the toolchain is not there.'
}

$envFile  = $env:GITHUB_ENV
$pathFile = $env:GITHUB_PATH
$changed  = 0

foreach ($name in ($after.Keys | Sort-Object)) {
    # PATH is handled below: it is a list, and prepending its new entries is
    # not the same as replacing the runner's whole PATH with this cmd's.
    if ($name -eq 'Path' -or $name -eq 'PATH') { continue }

    $was = [System.Environment]::GetEnvironmentVariable($name)
    if ($was -eq $after[$name]) { continue }
    $changed++

    if ($envFile) {
        # The delimiter form rather than NAME=value, which cannot carry a
        # value containing a newline and silently truncates one that does.
        @(
            "$name<<H5SCOPE_MSVC_ENV"
            $after[$name]
            'H5SCOPE_MSVC_ENV'
        ) | Add-Content -Path $envFile -Encoding utf8
    } else {
        Write-Host "  $name=$($after[$name])"
    }
}

$before = ([System.Environment]::GetEnvironmentVariable('PATH') -split ';')
$added = @($after['Path'] -split ';' | Where-Object { $_ -and ($before -notcontains $_) })
if ($pathFile) {
    $added | Add-Content -Path $pathFile -Encoding utf8
} else {
    foreach ($entry in $added) { Write-Host "  PATH += $entry" }
}

Write-Host "MSVC environment: $changed variables, $($added.Count) PATH entries"

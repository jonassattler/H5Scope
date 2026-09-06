# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# Everything that has to be true of the Windows release executable before it is
# published, checked against the executable itself rather than against the
# build that produced it. The counterpart of tools/ci/verify-binary.sh, and it
# is deliberately the same shape.
#
# One check there has no equivalent here, and it is worth saying why rather
# than quietly leaving it out. check-glibc-floor.sh reads versioned symbol
# references out of an ELF and compares them against RHEL 8's glibc, because a
# binary built on a newer distribution asks for symbol versions the target
# never defined and the loader refuses it. A PE import table carries no such
# per-symbol versions -- it names DLLs and ordinals, and the same import
# resolves on every Windows that has the function at all. So there is nothing
# in the file to read a floor out of. What stands in its place is the import
# list itself, printed on every run so that a new name is visible in the log,
# and the assertion that a handful of names are absent from it.
#
# The absent ones are the whole "self-contained" claim:
#
#   Qt6*.dll, hdf5*.dll   the same check the Linux script makes with ldd. If
#                         either leaks in as a DLL the binary is not static.
#
#   vcruntime*, msvcp*, api-ms-win-crt-*, ucrtbase
#                         the Visual C++ runtime and the UCRT. x64-windows-
#                         static links both into the executable; if any of
#                         them appears here the build fell back to the dynamic
#                         CRT, and the binary then does not start on a machine
#                         without the redistributable installed. That failure
#                         looks exactly like the RHEL 8 one the Linux side
#                         goes to such lengths to avoid, and it would not show
#                         up on any machine that has ever had Visual Studio.
#
# Usage: pwsh -File tools/ci/verify-windows-binary.ps1 <executable>

param(
    [Parameter(Mandatory = $true)][string]$Binary
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Binary)) { throw "not an executable: $Binary" }
$Binary = (Resolve-Path $Binary).Path
$bindir = Split-Path -Parent $Binary

Write-Host "binary: $Binary"
Write-Host ''

# Start-Process with redirection rather than `& $Binary`, and for a reason
# specific to what is being tested. This is a GUI-subsystem executable: a
# shell does not wait for it, and its standard handles are null unless the
# launcher supplies them. Redirecting supplies them, so this both waits for
# the process and exercises the branch of attachParentConsole() that leaves an
# inherited handle alone.
#
# The other branch -- attaching to the console of an interactive shell -- is
# the one that cannot be driven from here, because a test harness that
# captures output is by definition not that case.
function Invoke-Binary {
    param([string[]]$Arguments)

    $outFile = (New-TemporaryFile).FullName
    $errFile = (New-TemporaryFile).FullName
    try {
        $process = Start-Process -FilePath $Binary -ArgumentList $Arguments `
            -Wait -PassThru -NoNewWindow `
            -RedirectStandardOutput $outFile -RedirectStandardError $errFile
        # An empty file reads back as $null, and every caller here would
        # rather have a string it can measure than a null it must test for.
        $out = Get-Content -Raw -ErrorAction SilentlyContinue $outFile
        $err = Get-Content -Raw -ErrorAction SilentlyContinue $errFile
        if ($null -eq $out) { $out = '' }
        if ($null -eq $err) { $err = '' }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Output   = $out
            Error    = $err
        }
    } finally {
        Remove-Item -Force -ErrorAction SilentlyContinue $outFile, $errFile
    }
}

# --- it starts, and says what it is ----------------------------------------
# Offscreen, like every other invocation here: a runner has no desktop, and
# constructing a QGuiApplication without one is not what is being tested.
Write-Host '== it runs'
$env:QT_QPA_PLATFORM = 'offscreen'
$version = Invoke-Binary @('--version')
if ($version.ExitCode -ne 0) {
    Write-Host "::error::--version exited $($version.ExitCode): $($version.Error)"
    exit 1
}
if (-not $version.Output -or -not $version.Output.Trim()) {
    Write-Host ('::error::--version printed nothing. On a GUI-subsystem ' +
        'executable that is the expected failure when the standard handles ' +
        'are never claimed -- see attachParentConsole() in src/main.cpp.')
    exit 1
}
Write-Host $version.Output.Trim()
Write-Host ''

# --- nothing of Qt, HDF5 or the C runtime is asked of the host -------------
Write-Host '== what it asks of the host'
if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) {
    Write-Host '::error::dumpbin is not on PATH; run tools/ci/msvc-env.ps1 first'
    exit 1
}

$dependents = & dumpbin /nologo /dependents $Binary
$imports = @(
    $dependents | ForEach-Object {
        if ($_ -match '^\s{2,}(\S+\.dll)\s*$') { $Matches[1] }
    }
) | Sort-Object -Unique

if ($imports.Count -eq 0) {
    Write-Host '::error::could not read the import table -- dumpbin output moved'
    $dependents | ForEach-Object { Write-Host $_ }
    exit 1
}
Write-Host "imports: $($imports -join ' ')"

$forbidden = @{
    '^Qt6'              = 'Qt is linked dynamically; it must be static'
    '^hdf5'             = 'HDF5 is linked dynamically; it must be static'
    '^vcruntime'        = 'the dynamic Visual C++ runtime'
    '^msvcp'            = 'the dynamic Visual C++ standard library'
    '^msvcr'            = 'the dynamic Visual C++ runtime'
    '^api-ms-win-crt-'  = 'the dynamic UCRT'
    '^ucrtbase'         = 'the dynamic UCRT'
}
$bad = 0
foreach ($pattern in $forbidden.Keys) {
    foreach ($dll in $imports) {
        if ($dll -match $pattern) {
            Write-Host ("::error::$dll -- $($forbidden[$pattern]). The triplet " +
                'is x64-windows-static, which links both the CRT and every ' +
                'dependency into the executable; this binary would not start ' +
                'on a machine without that runtime installed.')
            $bad++
        }
    }
}
if ($bad -gt 0) { exit 1 }
Write-Host 'OK: no Qt, HDF5 or C runtime DLL is asked of the host'
Write-Host ''

# --- it carries its own licences -------------------------------------------
# GPL-3.0-only section 6 conveys object code under sections 4 and 5, and
# section 4 wants a copy of the License given to every recipient along with
# the Program -- and the BSD, MIT, Zlib and libpng dependencies each want
# their notice reproduced in the materials accompanying a binary. All of it is
# compiled in; this proves it comes out again.
#
# The same four phrases the Linux script checks, deliberately: this project's
# own licence, the inventory, HDF5's required copyright notice and the font
# licence. The Windows binary links a strictly smaller set of ports, so a
# notices file that lost something would still contain most of itself.
Write-Host '== it carries its own licences'
$license = Invoke-Binary @('--license')
$notices = Invoke-Binary @('--notices')

if ($license.ExitCode -ne 0) {
    Write-Host "::error::--license exited $($license.ExitCode): $($license.Error)"
    exit 1
}
if ($notices.ExitCode -ne 0) {
    Write-Host "::error::--notices exited $($notices.ExitCode): $($notices.Error)"
    exit 1
}

$licenseLines = ($license.Output -split "`n").Count
if ($licenseLines -le 600) {
    Write-Host "::error::--license printed $licenseLines lines; the GPL is about 674"
    exit 1
}
$expected = @{
    'GNU GENERAL PUBLIC LICENSE'    = $license.Output
    'Third-party notices'           = $notices.Output
    'Copyright 2006 by The HDF Group' = $notices.Output
    'SIL OPEN FONT LICENSE'         = $notices.Output
}
foreach ($phrase in $expected.Keys) {
    if ($expected[$phrase] -notmatch [regex]::Escape($phrase)) {
        Write-Host "::error::'$phrase' is missing from what the binary printed"
        exit 1
    }
}
Write-Host 'OK: --license and --notices both answer'
Write-Host ''

# And beside the binary, laid down by the build itself (see src/CMakeLists.txt),
# which is what the release publishes.
Write-Host '== the licences are beside it too'
foreach ($name in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'THIRD-PARTY-LICENSES.txt')) {
    $path = Join-Path $bindir $name
    if (-not (Test-Path $path)) {
        Write-Host "::error::$name is not beside the binary"
        exit 1
    }
    Write-Host ("  {0,-28} {1,10} bytes" -f $name, (Get-Item $path).Length)
}

Write-Host ''
Write-Host "OK: $Binary is fit to publish"

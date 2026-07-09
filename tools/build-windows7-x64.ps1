param(
    [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')]
    [string]$Configuration = 'Release',

    [ValidateSet('auto','v145','v143','v142','v141')]
    [string]$Toolset = 'auto',

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$script:RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$script:BuildDir = Join-Path $script:RootDir 'build'
$script:FinishedDir = Join-Path $script:RootDir '0-Finished'
$script:DistDir = Join-Path $script:FinishedDir 'dist'
$script:LogDir = Join-Path $script:FinishedDir 'logs'

New-Item -ItemType Directory -Force -Path $script:DistDir | Out-Null
New-Item -ItemType Directory -Force -Path $script:LogDir | Out-Null

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:LogFile = Join-Path $script:LogDir ("build-{0}-windows7-x64.log" -f $stamp)

function Write-LogLine {
    param([string]$Message)
    $Message | Tee-Object -FilePath $script:LogFile -Append
}

function Invoke-Logged {
    param(
        [Parameter(Mandatory=$true)][string]$FilePath,
        [Parameter(Mandatory=$true)][string[]]$Arguments
    )

    Write-LogLine ''
    Write-LogLine ('> {0} {1}' -f $FilePath, ($Arguments -join ' '))

    # PowerShell 5.x can treat native stderr as a NativeCommandError when
    # $ErrorActionPreference is Stop. Some MSVC/CMake warnings are written to
    # stderr even when the tool exits successfully, so log stderr as text and
    # decide success only from the native process exit code.
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $FilePath @Arguments 2>&1 | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $script:LogFile -Append
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }

    Write-LogLine ('ExitCode: {0}' -f $code)

    if ($code -ne 0) {
        throw ('Command failed with exit code {0}: {1}' -f $code, $FilePath)
    }
}

function Remove-BuildDir {
    if (Test-Path $script:BuildDir) {
        Write-LogLine ('Removing existing build folder: {0}' -f $script:BuildDir)
        Remove-Item -LiteralPath $script:BuildDir -Recurse -Force
    }
}

function Get-VsWherePath {
    $paths = @()
    if (${env:ProgramFiles(x86)}) {
        $paths += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe')
    }
    if ($env:ProgramFiles) {
        $paths += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    }

    foreach ($p in $paths) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}

function Get-VisualStudioInstancesWithCpp {
    $instances = @()
    $seen = @{}
    $vswhere = Get-VsWherePath

    if ($vswhere) {
        try {
            $json = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json 2>$null
            if ($LASTEXITCODE -eq 0 -and $json) {
                $items = $json | ConvertFrom-Json
                foreach ($item in @($items)) {
                    if ($item.installationPath -and (Test-Path $item.installationPath)) {
                        $path = [string]$item.installationPath
                        if (-not $seen.ContainsKey($path)) {
                            $seen[$path] = $true
                            $instances += [pscustomobject]@{
                                Name = [string]$item.displayName
                                Path = $path
                                Version = [string]$item.installationVersion
                            }
                        }
                    }
                }
            }
        }
        catch {
            Write-LogLine ('vswhere scan failed: {0}' -f $_.Exception.Message)
        }
    }

    # Fallback scan for machines where vswhere is missing or not on the expected path.
    $roots = @()
    if ($env:ProgramFiles) { $roots += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio') }
    if (${env:ProgramFiles(x86)}) { $roots += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio') }

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        foreach ($yearDir in Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue) {
            foreach ($editionDir in Get-ChildItem -LiteralPath $yearDir.FullName -Directory -ErrorAction SilentlyContinue) {
                $path = $editionDir.FullName
                if ($seen.ContainsKey($path)) { continue }

                $vcvars = Join-Path $path 'VC\Auxiliary\Build\vcvars64.bat'
                $vsdev = Join-Path $path 'Common7\Tools\VsDevCmd.bat'
                if ((Test-Path $vcvars) -or (Test-Path $vsdev)) {
                    $seen[$path] = $true
                    $instances += [pscustomobject]@{
                        Name = ('Visual Studio {0} {1}' -f $yearDir.Name, $editionDir.Name)
                        Path = $path
                        Version = $yearDir.Name
                    }
                }
            }
        }
    }

    return @($instances | Sort-Object -Property @{ Expression = {
        $v = $_.Version
        if ($v -match '^(\d+)') { [int]$Matches[1] } else { 0 }
    }; Descending = $true }, Path)
}

function Test-CMakeGeneratorKnown {
    param([string]$Generator)

    try {
        $help = & $script:CMakePath --help 2>$null
        return (($help -join "`n") -match [regex]::Escape($Generator))
    }
    catch {
        return $true
    }
}

function Add-VisualStudioGeneratorAttempt {
    param(
        [System.Collections.ArrayList]$Attempts,
        [string]$Generator,
        [string]$PlatformToolset
    )

    if (Test-CMakeGeneratorKnown -Generator $Generator) {
        [void]$Attempts.Add(@{ Kind = 'VSGenerator'; Generator = $Generator; Toolset = $PlatformToolset })
    }
    else {
        Write-LogLine ('Skipping generator not known by this CMake: {0}' -f $Generator)
    }
}

function Get-BuildAttempts {
    param([string]$RequestedToolset)

    $attempts = New-Object System.Collections.ArrayList

    if ($RequestedToolset -eq 'auto') {
        # CMake 4.2+ knows the VS 2026 generator. Older CMake installs will skip it and use the NMake fallback below.
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 18 2026' 'v145'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 17 2022' 'v143'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 17 2022' 'v142'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 17 2022' 'v141'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 16 2019' 'v142'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 16 2019' 'v141'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 15 2017' 'v141'
    }
    elseif ($RequestedToolset -eq 'v145') {
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 18 2026' 'v145'
    }
    elseif ($RequestedToolset -eq 'v143') {
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 17 2022' 'v143'
    }
    elseif ($RequestedToolset -eq 'v142') {
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 17 2022' 'v142'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 16 2019' 'v142'
    }
    elseif ($RequestedToolset -eq 'v141') {
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 17 2022' 'v141'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 16 2019' 'v141'
        Add-VisualStudioGeneratorAttempt $attempts 'Visual Studio 15 2017' 'v141'
    }

    # NMake fallback is important for VS 2026 installs whose bundled CMake is too old to know
    # the "Visual Studio 18 2026" generator, but can still build after VsDevCmd sets up cl/nmake.
    foreach ($vs in Get-VisualStudioInstancesWithCpp) {
        $major = 0
        if ($vs.Version -match '^(\d+)') { $major = [int]$Matches[1] }

        $include = $false
        if ($RequestedToolset -eq 'auto') { $include = $true }
        elseif ($RequestedToolset -eq 'v145' -and $major -ge 18) { $include = $true }
        elseif ($RequestedToolset -eq 'v143' -and $major -eq 17) { $include = $true }
        elseif ($RequestedToolset -eq 'v142' -and ($major -eq 16 -or $major -eq 17)) { $include = $true }
        elseif ($RequestedToolset -eq 'v141' -and ($major -eq 15 -or $major -eq 16 -or $major -eq 17)) { $include = $true }

        if ($include) {
            [void]$attempts.Add(@{ Kind = 'DevCmdNMake'; Name = $vs.Name; Path = $vs.Path; Version = $vs.Version })
        }
    }

    return @($attempts)
}

function Get-BuiltExePath {
    $candidates = @(
        (Join-Path $script:BuildDir (Join-Path 'bin' (Join-Path $Configuration 'OffsetPorter.exe'))),
        (Join-Path $script:BuildDir (Join-Path 'bin' 'OffsetPorter.exe')),
        (Join-Path $script:BuildDir (Join-Path $Configuration 'OffsetPorter.exe')),
        (Join-Path $script:BuildDir 'OffsetPorter.exe')
    )

    foreach ($exe in $candidates) {
        if (Test-Path $exe) { return $exe }
    }

    return $null
}

function Invoke-VisualStudioGeneratorAttempt {
    param(
        [string]$Generator,
        [string]$PlatformToolset
    )

    $configureArgs = @(
        '-S', $script:RootDir,
        '-B', $script:BuildDir,
        '-G', $Generator,
        '-A', 'x64'
    )

    if ($PlatformToolset) {
        $configureArgs += @('-T', $PlatformToolset)
    }

    # Do not force CMAKE_SYSTEM_VERSION=6.1. Newer VS installs usually do not include the old Win7 SDK.
    # Windows 7 API targeting is handled inside CMakeLists.txt with WINVER/_WIN32_WINNT=0x0601.
    Invoke-Logged $script:CMakePath $configureArgs

    Invoke-Logged $script:CMakePath @(
        '--build', $script:BuildDir,
        '--config', $Configuration,
        '--', '/m'
    )
}

function Invoke-DevCmdNMakeAttempt {
    param(
        [string]$VsName,
        [string]$VsPath
    )

    $vsDevCmd = Join-Path $VsPath 'Common7\Tools\VsDevCmd.bat'
    $vcVars64 = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
    $setup = $null
    $setupArgs = ''

    if (Test-Path $vsDevCmd) {
        $setup = $vsDevCmd
        $setupArgs = '-arch=x64 -host_arch=x64'
    }
    elseif (Test-Path $vcVars64) {
        $setup = $vcVars64
        $setupArgs = ''
    }
    else {
        throw ('Could not find VsDevCmd.bat or vcvars64.bat under {0}' -f $VsPath)
    }

    $bat = Join-Path $script:BuildDir 'run-nmake-build.cmd'
    New-Item -ItemType Directory -Force -Path $script:BuildDir | Out-Null

    $lines = @(
        '@echo off',
        'setlocal EnableExtensions',
        ('echo Setting up {0}' -f $VsName.Replace('&','^&')),
        ('call "{0}" {1}' -f $setup, $setupArgs),
        'if errorlevel 1 exit /b %errorlevel%',
        'where cl.exe',
        'if errorlevel 1 exit /b %errorlevel%',
        'where nmake.exe',
        'if errorlevel 1 exit /b %errorlevel%',
        'where cmake.exe',
        'if errorlevel 1 exit /b %errorlevel%',
        ('cmake -S "{0}" -B "{1}" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE={2}' -f $script:RootDir, $script:BuildDir, $Configuration),
        'if errorlevel 1 exit /b %errorlevel%',
        ('cmake --build "{0}"' -f $script:BuildDir),
        'exit /b %errorlevel%'
    )

    Set-Content -LiteralPath $bat -Value $lines -Encoding ASCII
    Invoke-Logged $env:ComSpec @('/d','/c',('"{0}"' -f $bat))
}

Write-LogLine 'OffsetPorter C++17 Windows 7 x64 build'
Write-LogLine ('Root:          {0}' -f $script:RootDir)
Write-LogLine ('Configuration: {0}' -f $Configuration)
Write-LogLine ('Toolset:       {0}' -f $Toolset)
Write-LogLine ('Log:           {0}' -f $script:LogFile)
Write-LogLine ''

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($null -eq $cmake) {
    throw 'CMake was not found in PATH. Install CMake or open a Developer Command Prompt that can find cmake.exe.'
}
$script:CMakePath = $cmake.Source
Write-LogLine ('CMake:         {0}' -f $script:CMakePath)

if ($Clean) {
    Remove-BuildDir
}

$success = $false
$lastError = $null
$attempts = Get-BuildAttempts -RequestedToolset $Toolset

if (-not $attempts -or $attempts.Count -eq 0) {
    Write-LogLine 'No build attempts are available. Make sure Visual Studio has Desktop development with C++ installed.'
    exit 1
}

foreach ($attempt in $attempts) {
    Write-LogLine ''

    try {
        Remove-BuildDir

        if ($attempt.Kind -eq 'VSGenerator') {
            $generator = [string]$attempt.Generator
            $platformToolset = [string]$attempt.Toolset
            Write-LogLine ('=== Trying CMake generator: {0}, toolset {1}, x64 ===' -f $generator, $platformToolset)
            Invoke-VisualStudioGeneratorAttempt -Generator $generator -PlatformToolset $platformToolset
        }
        elseif ($attempt.Kind -eq 'DevCmdNMake') {
            $name = [string]$attempt.Name
            $path = [string]$attempt.Path
            Write-LogLine ('=== Trying Developer environment + NMake: {0} ===' -f $name)
            Write-LogLine ('VS Path: {0}' -f $path)
            Invoke-DevCmdNMakeAttempt -VsName $name -VsPath $path
        }
        else {
            throw ('Unknown attempt kind: {0}' -f $attempt.Kind)
        }

        $exe = Get-BuiltExePath
        if (-not $exe) {
            throw 'Expected build output was not found under the build folder.'
        }

        $outExe = Join-Path $script:DistDir 'OffsetPorter.exe'
        Copy-Item -LiteralPath $exe -Destination $outExe -Force

        Write-LogLine ''
        Write-LogLine ('Built: {0}' -f $outExe)
        $success = $true
        break
    }
    catch {
        $lastError = $_.Exception.Message
        Write-LogLine ('Attempt failed: {0}' -f $lastError)
    }
}

if (-not $success) {
    Write-LogLine ''
    Write-LogLine 'Build failed. No Visual Studio C++ generator/toolset or Developer Command Prompt/NMake attempt completed successfully.'
    Write-LogLine 'For VS 2026, install the Desktop development with C++ workload, including MSVC Build Tools for x64/x86 and C++ CMake tools for Windows.'
    Write-LogLine 'A Visual Basic-only install cannot compile this native C++17 version because it does not include cl.exe/nmake.exe.'
    Write-LogLine ('Last error: {0}' -f $lastError)
    exit 1
}

exit 0

# build_Windows.ps1 - Build and package BeebView SDL3 for Windows x64
# Run from PowerShell in the BeebView project root:
#   powershell -ExecutionPolicy Bypass -File .\build_Windows.ps1
# Options: -Choice 1-4 skips the menu; -Yes skips confirmation prompts.
param([string]$Choice = "", [switch]$Yes, [switch]$Help)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ExeName = "BeebView.exe"

function Read-CMakeVersion {
    $cmakeFile = Join-Path $Root "CMakeLists.txt"
    $text = [System.IO.File]::ReadAllText($cmakeFile)
    if ($text -match 'project\s*\(\s*BeebView\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') { return $Matches[1] }
    throw "Cannot read BeebView version from CMakeLists.txt"
}
$VER = Read-CMakeVersion

$BuildDir = Join-Path $Root "out\build\windows-ucrt64-release"
$DistDir = Join-Path $Root "out\dist\windows"
$PackageDir = Join-Path $Root "installers"

if ($Help) {
    Write-Host "Usage: powershell -File build_Windows.ps1 [-Choice 1-4] [-Yes] [-Help]"
    Write-Host "  [1] Build   [2] Build + portable ZIP   [3] Build + installer   [4] Quit"
    exit 0
}
Write-Host ""
Write-Host "+==============================================+"
Write-Host "|           BeebView Windows Build             |"
Write-Host "+==============================================+"
Write-Host "  Version: $VER"
Write-Host ""
Write-Host "    [1]  Build"
Write-Host "    [2]  Build + portable ZIP"
Write-Host "    [3]  Build + Inno Setup installer"
Write-Host "    [4]  Quit"
if ($Choice -eq "") { $Choice = Read-Host "  Your choice [1-4] (default=2)"; if ($Choice -eq "") { $Choice="2" } }
$Bundle=$false; $Zip=$false; $Installer=$false
switch ($Choice) {
    "1" { }
    "2" { $Bundle=$true; $Zip=$true }
    "3" { $Bundle=$true; $Installer=$true }
    "4" { exit 0 }
    default { throw "Invalid choice" }
}
if (-not $Yes) { $r=Read-Host "  Proceed? [Y/n]"; if ($r -match '^[Nn]') { exit 0 } }

function Invoke-Checked([string]$Desc,[string]$Exe,[string[]]$ArgumentList) {
    Write-Host "  >> $Desc"
    & $Exe @ArgumentList
    if ($LASTEXITCODE -ne 0) { throw "$Desc failed (exit code $LASTEXITCODE)" }
}

# Find/install MSYS2.  UCRT64 gives a modern Windows CRT target without Cygwin/MSYS runtime dependence.
$MSYS2Root=$null
foreach($candidate in @("C:\msys64","C:\msys2","$env:LOCALAPPDATA\msys64")) {
    if(Test-Path (Join-Path $candidate "usr\bin\bash.exe")) { $MSYS2Root=$candidate; break }
}
if($null -eq $MSYS2Root) {
    throw "MSYS2 was not found. Install the current x86_64 MSYS2 release from https://www.msys2.org/ and rerun this script."
}
$Bash=Join-Path $MSYS2Root "usr\bin\bash.exe"
$Packages=@("mingw-w64-ucrt-x86_64-gcc","mingw-w64-ucrt-x86_64-cmake","mingw-w64-ucrt-x86_64-ninja")
Invoke-Checked "Installing/checking MSYS2 build packages" $Bash @("-lc",("pacman -S --needed --noconfirm " + ($Packages -join " ")))
$Cmake=Join-Path $MSYS2Root "ucrt64\bin\cmake.exe"
$Gcc=Join-Path $MSYS2Root "ucrt64\bin\gcc.exe"
$Ninja=Join-Path $MSYS2Root "ucrt64\bin\ninja.exe"
$env:PATH="$MSYS2Root\ucrt64\bin;$MSYS2Root\usr\bin;$env:PATH"

# Use BeebView's bundled SDL3 source ZIP, matching the Linux build policy.
$sdlZips=@(Get-ChildItem (Join-Path $Root "sdl3") -Filter "SDL3-*.zip" -File)
if($sdlZips.Count -eq 0) { throw "No sdl3\SDL3-X.Y.Z.zip source archive was found." }
$sdlChoices=@()
foreach($z in $sdlZips) { if($z.BaseName -match '^SDL3-([0-9]+\.[0-9]+\.[0-9]+)$') { $sdlChoices += [pscustomobject]@{Version=[version]$Matches[1];File=$z} } }
if($sdlChoices.Count -eq 0) { throw "No versioned SDL3 source ZIP was found." }
$selected=$sdlChoices | Sort-Object Version -Descending | Select-Object -First 1
$sdlExtract=Join-Path $Root ("out\build\sdl3-windows-" + $selected.Version)
if(Test-Path $sdlExtract) { Remove-Item $sdlExtract -Recurse -Force }
New-Item -ItemType Directory -Force -Path $sdlExtract | Out-Null
Expand-Archive -Path $selected.File.FullName -DestinationPath $sdlExtract -Force
$sdlSource=Get-ChildItem $sdlExtract -Directory | Where-Object { Test-Path (Join-Path $_.FullName "CMakeLists.txt") } | Select-Object -First 1
if($null -eq $sdlSource) { throw "SDL3 ZIP did not contain the expected source directory." }
Write-Host "  >> SDL3 source: $($selected.File.Name)"

if(Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$ConfigArgs=@("-S",$Root,"-B",$BuildDir,"-G","Ninja","-DCMAKE_BUILD_TYPE=Release","-DCMAKE_C_COMPILER=$Gcc","-DCMAKE_MAKE_PROGRAM=$Ninja","-DVIEWBBC_BUILD_NCURSES=OFF","-DVIEWBBC_BUILD_TESTS=ON","-DVIEWBBC_SDL3_SOURCE=$($sdlSource.FullName)")
Invoke-Checked "Configuring BeebView" $Cmake $ConfigArgs
Invoke-Checked "Building BeebView $VER" $Cmake @("--build",$BuildDir)
Invoke-Checked "Running core tests" (Join-Path $MSYS2Root "ucrt64\bin\ctest.exe") @("--test-dir",$BuildDir,"--output-on-failure")
$ExePath=Join-Path $BuildDir $ExeName
if(-not (Test-Path $ExePath)) { $ExePath=(Get-ChildItem $BuildDir -Filter $ExeName -Recurse | Select-Object -First 1).FullName }
if(-not $ExePath -or -not (Test-Path $ExePath)) { throw "Cannot find $ExeName in build output." }

function Copy-DependencyDlls([string]$ExePath,[string]$Destination) {
    $dlls=New-Object System.Collections.Generic.HashSet[string]
    $exeMsys=$ExePath -replace '\\','/'
    if($exeMsys -match '^([A-Za-z]):/(.*)$') { $exeMsys='/' + $Matches[1].ToLower() + '/' + $Matches[2] }
    $output=& $Bash "-lc" "ldd '$exeMsys'"
    if($LASTEXITCODE -ne 0) { throw "ldd failed for $ExePath" }
    foreach($line in $output) {
        $candidate=$null
        if($line -match '=>\s+([A-Za-z]:\\[^\r\n]+?\.dll)\s') { $candidate=$Matches[1] }
        elseif($line -match '=>\s+(/[^\s]+\.dll)') { $candidate=$Matches[1] }
        elseif($line -match '^\s*(/[^\s]+\.dll)') { $candidate=$Matches[1] }
        if($null -eq $candidate) { continue }
        if($candidate -match '^/c/Windows/' -or $candidate -match '^[Cc]:\\Windows\\') { continue }
        if($candidate.StartsWith("/ucrt64/")) { $candidate=Join-Path "$MSYS2Root\ucrt64" ($candidate.Substring(8)-replace '/','\') }
        elseif($candidate -match '^/([A-Za-z])/(.*)$') { $candidate=$Matches[1].ToUpper()+":\"+($Matches[2]-replace '/','\') }
        if(Test-Path $candidate) { [void]$dlls.Add((Resolve-Path $candidate).Path) }
    }
    foreach($dll in $dlls) { Copy-Item $dll $Destination -Force; Write-Host "     $(Split-Path -Leaf $dll)" }
    if(-not (Test-Path (Join-Path $Destination "SDL3.dll"))) {
        $sdlDll=Get-ChildItem $BuildDir -Filter "SDL3.dll" -Recurse | Select-Object -First 1
        if($null -ne $sdlDll) { Copy-Item $sdlDll.FullName $Destination -Force }
    }
    if(-not (Test-Path (Join-Path $Destination "SDL3.dll"))) { throw "Runtime bundle is missing SDL3.dll." }
}

if($Bundle) {
    if(Test-Path $DistDir) { Remove-Item $DistDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
    Copy-Item $ExePath $DistDir -Force
    Copy-DependencyDlls $ExePath $DistDir
    foreach($f in @("README.md","CHANGELOG.md")) { Copy-Item (Join-Path $Root $f) $DistDir -Force }
    Copy-Item (Join-Path $Root "LICENSES") $DistDir -Recurse -Force
}
if($Zip) {
    New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null
    $zipPath=Join-Path $PackageDir "BeebView-SDL-$VER-Windows-x86_64.zip"
    if(Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $DistDir "*") -DestinationPath $zipPath -Force
    Write-Host "  >> ZIP: $zipPath"
}

function Find-InnoSetup {
    foreach($p in @("C:\Program Files (x86)\Inno Setup 6\ISCC.exe","C:\Program Files\Inno Setup 6\ISCC.exe")) { if(Test-Path $p){return $p} }
    $cmd=Get-Command ISCC.exe -ErrorAction SilentlyContinue; if($null -ne $cmd){return $cmd.Source}; return $null
}
if($Installer) {
    $iscc=Find-InnoSetup
    if($null -eq $iscc) { throw "Inno Setup 6 is required for option 3. Install it from https://jrsoftware.org/isinfo.php and rerun." }
    $iss=Join-Path $env:TEMP "BeebView-$VER-setup.iss"
    $out=$PackageDir; $bundle=$DistDir
    $issText=@"
[Setup]
AppId={{D8967A18-9694-4E4C-AD1B-7B840BC93F51}
AppName=BeebView
AppVersion=$VER
AppPublisher=BeebView
DefaultDirName={autopf}\BeebView
DefaultGroupName=BeebView
DisableProgramGroupPage=yes
OutputDir=$out
OutputBaseFilename=BeebView-SDL-$VER-Windows-x86_64-setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\BeebView.exe
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "$bundle\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\BeebView"; FileName: "{app}\BeebView.exe"
Name: "{autodesktop}\BeebView"; FileName: "{app}\BeebView.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\BeebView.exe"" ""%1"""; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".txt"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".log"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".c"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".h"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".cpp"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".hpp"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".md"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".csv"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".json"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".xml"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\BeebView.exe\SupportedTypes"; ValueType: string; ValueName: ".html"; ValueData: ""

[Run]
Filename: "{app}\BeebView.exe"; Description: "{cm:LaunchProgram,BeebView}"; Flags: nowait postinstall skipifsilent
"@
    $issText | Out-File $iss -Encoding utf8
    Invoke-Checked "Creating Windows installer" $iscc @($iss)
    Remove-Item $iss -Force -ErrorAction SilentlyContinue
}
Write-Host ""
Write-Host "  EXE: $ExePath"
if($Bundle){Write-Host "  Bundle: $DistDir"}

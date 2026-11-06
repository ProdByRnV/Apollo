# Installs Apollo on Windows and opens it, in one command. In PowerShell:
#
#   irm https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.ps1 | iex
#
# What it does, in order:
#
#   1. Asks GitHub which release is the latest, and finds its Windows download.
#   2. If that version is already installed, opens it and stops.
#   3. Otherwise downloads it, and checks it against the checksums published
#      with the release. A download that does not match is not installed.
#   4. Installs the application for this user, in
#      %LOCALAPPDATA%\Programs\Apollo, with an entry in the Start menu; and the
#      plugin in C:\Program Files\Common Files\VST3, the folder every DAW
#      looks in. That folder needs administrator rights, so Windows asks once.
#      Said no to, the plugin goes in this user's own VST3 folder instead,
#      %LOCALAPPDATA%\Programs\Common\VST3, and the DAW has to be told about it.
#   5. Opens Apollo.
#
# WHY THERE IS NO "WINDOWS PROTECTED YOUR PC". Windows marks what a web browser
# downloads, and SmartScreen questions marked programs that are not signed with
# a paid certificate, which Apollo is not. What PowerShell downloads is not
# marked, and anything Apollo that was is unmarked here.
#
# If GitHub cannot be reached, it opens an Apollo already on this computer:
# installed, or unzipped in Downloads or on the Desktop.
#
# Run it again to update. A DAW that has the plugin open goes on using the
# copy it loaded until it is restarted; the new one is in place for then.
#
# For testing: APOLLO_ZIP=<file> installs that archive instead of downloading
# one; APOLLO_NO_LAUNCH=1 installs without opening anything; APOLLO_INSTALL_DIR,
# APOLLO_VST3_DIR and APOLLO_START_MENU put things somewhere else; and
# APOLLO_GITHUB_TOKEN is sent to the GitHub API.
#
# Written for Windows PowerShell 5.1, which every Windows 10 and 11 has, and in
# plain ASCII, which it reads the same however the file reaches it.
#
# Everything is inside one function, and nothing calls `exit`: run through
# `iex`, an `exit` would close the user's PowerShell window with the message
# still in it.

function Install-Apollo
{
    $ErrorActionPreference = 'Stop'

    # Without this, Invoke-WebRequest in Windows PowerShell redraws a progress
    # bar for every block and a 10 MB download takes minutes.
    $ProgressPreference = 'SilentlyContinue'

    # GitHub refuses the TLS versions Windows PowerShell 5.1 offers by default.
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $repository = 'ProdByRnV/Apollo'
    $latest = "https://api.github.com/repos/$repository/releases/latest"
    $package = 'windows-x86_64'

    function Say([string] $text) { Write-Host $text }

    #---------------------------------------------------------------------------
    # Where things go.

    $programDir = if ($env:APOLLO_INSTALL_DIR) { $env:APOLLO_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA 'Programs\Apollo' }
    $installedExe = Join-Path $programDir 'Apollo.exe'
    $versionFile = Join-Path $programDir 'VERSION'

    $systemVst3 = if ($env:APOLLO_VST3_DIR) { $env:APOLLO_VST3_DIR } else { Join-Path $env:CommonProgramFiles 'VST3' }
    $userVst3 = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'

    $startMenu = if ($env:APOLLO_START_MENU) { $env:APOLLO_START_MENU } else { Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs' }

    function Get-InstalledVersion
    {
        if ((Test-Path -LiteralPath $installedExe) -and (Test-Path -LiteralPath $versionFile)) {
            return (Get-Content -LiteralPath $versionFile -TotalCount 1).Trim()
        }
        return ''
    }

    #---------------------------------------------------------------------------
    # Opening it.

    function Open-Apollo([string] $exe)
    {
        if ($env:APOLLO_NO_LAUNCH -eq '1') {
            Say 'Not opening it (APOLLO_NO_LAUNCH is set).'
            return
        }

        Say 'Opening Apollo...'
        Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe)
    }

    # Apollo's window is drawn by Microsoft Edge WebView2, which Windows 11 has
    # and some Windows 10 installations do not. Reported; the sound works
    # without it, and the window is empty until it is installed.
    function Test-WebView2
    {
        $client = 'Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'

        foreach ($key in @("HKLM:\SOFTWARE\WOW6432Node\$client", "HKLM:\SOFTWARE\$client", "HKCU:\Software\$client")) {
            $item = Get-ItemProperty -LiteralPath $key -Name pv -ErrorAction SilentlyContinue
            if ($item -and $item.pv -and $item.pv -ne '0.0.0.0') { return $true }
        }

        return $false
    }

    #---------------------------------------------------------------------------
    # No connection: open whichever Apollo is already here.

    function Open-OneAlreadyHere
    {
        $found = $null

        if (Test-Path -LiteralPath $installedExe) {
            $found = $installedExe
        }
        else {
            foreach ($place in @((Join-Path $env:USERPROFILE 'Downloads'), (Join-Path $env:USERPROFILE 'Desktop'))) {
                if (-not (Test-Path -LiteralPath $place)) { continue }

                $found = Get-ChildItem -LiteralPath $place -Filter 'Apollo.exe' -File -Recurse -Depth 4 -ErrorAction SilentlyContinue |
                         Select-Object -First 1 -ExpandProperty FullName
                if ($found) { break }
            }
        }

        if (-not $found) { return $false }

        Unblock-File -LiteralPath $found -ErrorAction SilentlyContinue
        Say "Found Apollo at $found"
        Open-Apollo $found
        return $true
    }

    #---------------------------------------------------------------------------
    # Replacing the plugin.
    #
    # Copied in beside the old one and swapped by renaming, so nothing is ever
    # half deleted. Windows lets the folder of a plugin a DAW has open be
    # renamed but not deleted, so the old one is set aside as
    # Apollo.vst3.outgoing, which no DAW scans for, until the next run clears
    # it. Where even the rename is refused, the old plugin is left as it was.
    # The same text runs as it is, or in a PowerShell Windows has given
    # administrator rights to, which is why it is a string and not a function.

    $replacePlugin = @'
param([string] $Source, [string] $Folder)
$ErrorActionPreference = 'Stop'
$target = Join-Path $Folder 'Apollo.vst3'
$incoming = Join-Path $Folder 'Apollo.vst3.incoming'
$outgoing = Join-Path $Folder 'Apollo.vst3.outgoing'
try {
    New-Item -ItemType Directory -Force -Path $Folder | Out-Null
    foreach ($leftover in @($incoming, $outgoing)) {
        if (Test-Path -LiteralPath $leftover) { Remove-Item -LiteralPath $leftover -Recurse -Force }
    }
    Copy-Item -LiteralPath $Source -Destination $incoming -Recurse -Force
    if (Test-Path -LiteralPath $target) {
        try { Rename-Item -LiteralPath $target -NewName 'Apollo.vst3.outgoing' }
        catch { Remove-Item -LiteralPath $incoming -Recurse -Force; exit 3 }
    }
    Rename-Item -LiteralPath $incoming -NewName 'Apollo.vst3'
    if (Test-Path -LiteralPath $outgoing) { Remove-Item -LiteralPath $outgoing -Recurse -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $outgoing) { exit 4 }
    exit 0
}
catch { exit 2 }
'@

    # Runs $replacePlugin and returns its exit code: 0 done, 4 done while the
    # old one is still open, 3 in use and left as it was, 2 failed, and 1223
    # when Windows' permission prompt was answered no.
    function Invoke-ReplacePlugin([string] $source, [string] $folder, [bool] $elevate)
    {
        $script = Join-Path $work 'replace-plugin.ps1'
        Set-Content -LiteralPath $script -Value $replacePlugin -Encoding ASCII

        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$script`"",
                       '-Source', "`"$source`"", '-Folder', "`"$folder`"")

        try {
            if ($elevate) {
                $process = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
            }
            else {
                $process = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -NoNewWindow -Wait -PassThru
            }
            return $process.ExitCode
        }
        catch {
            # Start-Process throws, rather than returning, when the prompt is
            # refused.
            return 1223
        }
    }

    function Test-Administrator
    {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        return ([Security.Principal.WindowsPrincipal] $identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    }

    # Installs the plugin, preferring the folder every DAW scans. @returns the
    # folder it went into and whether a program still has the old one open, or
    # throws with what to do.
    function Install-Plugin([string] $source)
    {
        # A folder given for testing, or one this user can already write to,
        # needs no prompt.
        $canWrite = $false
        if ($env:APOLLO_VST3_DIR -or (Test-Administrator)) {
            $canWrite = $true
        }

        if ($canWrite) {
            $result = Invoke-ReplacePlugin $source $systemVst3 $false
        }
        else {
            Say "Windows will ask for permission to put the plugin in $systemVst3."
            $result = Invoke-ReplacePlugin $source $systemVst3 $true
        }

        if ($result -eq 0 -or $result -eq 4) { return @{ Folder = $systemVst3; InUse = ($result -eq 4) } }
        if ($result -eq 3) { throw 'the plugin is open in a program, probably your DAW, so it could not be replaced. Close your DAW and run this again.' }

        if ($result -eq 1223) {
            Say 'Permission was not given, so the plugin goes in your own VST3 folder instead.'
        }
        else {
            Say "The plugin could not be put in $systemVst3, so it goes in your own VST3 folder instead."
        }

        $result = Invoke-ReplacePlugin $source $userVst3 $false

        if ($result -eq 0 -or $result -eq 4) { return @{ Folder = $userVst3; InUse = ($result -eq 4) } }
        if ($result -eq 3) { throw 'the plugin is open in a program, probably your DAW, so it could not be replaced. Close your DAW and run this again.' }

        throw "the plugin could not be installed in $userVst3."
    }

    #---------------------------------------------------------------------------
    # The download.

    $work = Join-Path ([IO.Path]::GetTempPath()) ('apollo.' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null

    try {
        if (-not [Environment]::Is64BitOperatingSystem) {
            throw 'Apollo needs 64-bit Windows.'
        }

        $arm = ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') -or ($env:PROCESSOR_ARCHITEW6432 -eq 'ARM64')

        $version = ''

        if ($env:APOLLO_ZIP) {
            if (-not (Test-Path -LiteralPath $env:APOLLO_ZIP)) { throw "no archive at $($env:APOLLO_ZIP)" }
            $archive = (Resolve-Path -LiteralPath $env:APOLLO_ZIP).Path
            $version = '(local archive)'
            Say "Installing Apollo from $archive"
        }
        else {
            Say 'Looking for the latest Apollo...'

            $headers = @{ 'User-Agent' = 'Apollo installer' }
            if ($env:APOLLO_GITHUB_TOKEN) { $headers['Authorization'] = "Bearer $($env:APOLLO_GITHUB_TOKEN)" }

            try {
                $release = Invoke-RestMethod -Uri $latest -Headers $headers -UseBasicParsing
            }
            catch {
                Say 'GitHub could not be reached. Looking for an Apollo already on this computer...'
                if (-not (Open-OneAlreadyHere)) {
                    throw 'no connection, and no Apollo on this computer to open. Connect to the internet and run this again.'
                }
                return
            }

            $zip = $release.assets | Where-Object { $_.name -like "*-$package.zip" } | Select-Object -First 1
            $sums = $release.assets | Where-Object { $_.name -eq 'SHA256SUMS.txt' } | Select-Object -First 1

            if (-not $release.tag_name -or -not $zip -or -not $sums) {
                throw "the latest release has no download for Windows ($package)."
            }

            $version = $release.tag_name -replace '^v', ''

            if ((Get-InstalledVersion) -eq $version) {
                Say "Apollo $version is already installed."
                Open-Apollo $installedExe
                return
            }

            $archive = Join-Path $work $zip.name

            Say "Downloading Apollo $version for Windows..."
            try {
                Invoke-WebRequest -Uri $zip.browser_download_url -OutFile $archive -UseBasicParsing
                Invoke-WebRequest -Uri $sums.browser_download_url -OutFile (Join-Path $work 'SHA256SUMS.txt') -UseBasicParsing
            }
            catch {
                throw 'the download did not finish. Run this again.'
            }

            $expected = ''
            foreach ($line in Get-Content -LiteralPath (Join-Path $work 'SHA256SUMS.txt')) {
                $fields = $line.Trim() -split '\s+', 2
                if ($fields.Count -eq 2 -and $fields[1].TrimStart('*') -eq $zip.name) { $expected = $fields[0].ToLowerInvariant() }
            }

            $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()

            if (-not $expected -or $expected -ne $actual) {
                throw 'the download does not match its published checksum, so it was not installed. Run this again.'
            }

            Say 'Checked: the download matches its published checksum.'
        }

        #-----------------------------------------------------------------------
        # Unpacking, and finding what is in it. Searched for rather than
        # assumed, as on a Mac and Linux.

        $unpacked = Join-Path $work 'unpacked'
        Expand-Archive -LiteralPath $archive -DestinationPath $unpacked -Force
        Get-ChildItem -LiteralPath $unpacked -Recurse -File | Unblock-File -ErrorAction SilentlyContinue

        $app = Get-ChildItem -LiteralPath $unpacked -Filter 'Apollo.exe' -File -Recurse -Depth 3 |
               Where-Object { $_.FullName -notlike '*.vst3\*' } | Select-Object -First 1
        $plugin = Get-ChildItem -LiteralPath $unpacked -Filter 'Apollo.vst3' -Directory -Recurse -Depth 3 |
                  Select-Object -First 1

        if (-not $app) { throw 'the download has no Apollo application in it.' }

        #-----------------------------------------------------------------------
        # Installing.

        $running = Get-Process -Name Apollo -ErrorAction SilentlyContinue |
                   Where-Object { $_.Path -and ($_.Path -eq $installedExe) }

        if ($running) {
            throw 'Apollo is open. Close it and run this again.'
        }

        New-Item -ItemType Directory -Force -Path $programDir | Out-Null
        Copy-Item -LiteralPath $app.FullName -Destination $installedExe -Force
        Set-Content -LiteralPath $versionFile -Value $version -Encoding ASCII

        New-Item -ItemType Directory -Force -Path $startMenu | Out-Null
        $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $startMenu 'Apollo.lnk'))
        $shortcut.TargetPath = $installedExe
        $shortcut.WorkingDirectory = $programDir
        $shortcut.Description = 'Apollo, a wavetable synthesiser'
        $shortcut.Save()

        $pluginFolder = $null
        $pluginInUse = $false
        if ($plugin) {
            $installed = Install-Plugin $plugin.FullName
            $pluginFolder = $installed.Folder
            $pluginInUse = $installed.InUse
        }

        Say ''
        Say "Apollo $version is installed."
        Say "  Application   $installedExe"

        if ($pluginFolder) {
            Say "  Plugin        $(Join-Path $pluginFolder 'Apollo.vst3')"

            if ($pluginInUse) {
                Say '                Your DAW has the old one open: restart it to use this one.'
            }
            elseif ($pluginFolder -eq $userVst3) {
                Say "                Add $userVst3 to your DAW's VST3 folders, then rescan."
            }
            else {
                Say '                Rescan your plugins in your DAW to find it.'
            }
        }

        Say '  Next time, open it from the Start menu.'

        if ($arm) {
            Say ''
            Say 'This is Windows on ARM. Apollo runs here through Windows'' x64 emulation, and its'
            Say 'plugin loads only in a DAW that runs as x64.'
        }

        if (-not (Test-WebView2)) {
            Say ''
            Say 'Apollo''s window needs Microsoft Edge WebView2 Runtime, which this computer does not'
            Say 'seem to have. Install it from https://developer.microsoft.com/microsoft-edge/webview2/'
            Say '(the "Evergreen Bootstrapper"), then open Apollo again.'
        }

        Say ''
        Open-Apollo $installedExe
    }
    finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

try {
    Install-Apollo
}
catch {
    Write-Host ''
    Write-Host "Apollo: $($_.Exception.Message)" -ForegroundColor Red

    # A test, or anything else running this as a script, is told it failed;
    # somebody at a prompt keeps their window.
    if ($env:APOLLO_NO_LAUNCH -eq '1') { $global:LASTEXITCODE = 1; throw }
}

# Builds the Release version (tray only, no console) into %LOCALAPPDATA%\Programs\WaveshareLink
# and adds "Waveshare Link" to the Start menu. Run again after changing the code to update it.
#   powershell -ExecutionPolicy Bypass -File windows\install.ps1
$ErrorActionPreference = 'Stop'
$dest = Join-Path $env:LOCALAPPDATA 'Programs\WaveshareLink'
$exe = Join-Path $dest 'WaveshareLink.exe'

# The running app locks its files.
$running = Get-Process WaveshareLink -ErrorAction SilentlyContinue
if ($running) {
    Write-Host 'Fechando o Waveshare Link aberto...'
    $running | Stop-Process -Confirm:$false
    Start-Sleep -Seconds 1
}

dotnet publish (Join-Path $PSScriptRoot 'WaveshareLink.csproj') -c Release -o $dest --nologo
if ($LASTEXITCODE -ne 0) { throw 'dotnet publish falhou.' }

$lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'Waveshare Link.lnk'
$shell = New-Object -ComObject WScript.Shell
$s = $shell.CreateShortcut($lnk)
$s.TargetPath = $exe
$s.WorkingDirectory = $dest
$s.IconLocation = "$exe,0"
$s.Description = 'Conecta este PC à tela Waveshare'
$s.Save()

# "Iniciar com o Windows" points at the exe that turned it on: keep it on the installed one.
$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if (Get-ItemProperty $run -Name WaveshareLink -ErrorAction SilentlyContinue) {
    Set-ItemProperty $run -Name WaveshareLink -Value "`"$exe`""
}

Write-Host "Instalado em $dest"
Write-Host "Atalho: $lnk"
Start-Process $exe

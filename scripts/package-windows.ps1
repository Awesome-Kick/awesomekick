# Package the Windows simulator build as dist/Kickray-Windows.zip.
#
#   pwsh scripts/package-windows.ps1 [-BuildDir Kick/build/sim]
#
# The firmware looks for assets/, demos/, templates/ and content/ next to KT_KickEM.exe and writes projects,
# preferences and logs there too (see kick_paths.cpp), so unzip it to a folder you can write to.
param( [string]$BuildDir = "" )
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
if( -not $BuildDir ) { $BuildDir = Join-Path $Repo "Kick/build/sim" }
$Dist = Join-Path $Repo "dist"

$Exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "KT_KickEM.exe" | Select-Object -First 1
if( -not $Exe ) { throw "KT_KickEM.exe not found below $BuildDir" }

$Out = Join-Path $Dist "Kickray"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Out, (Join-Path $Dist "Kickray-Windows.zip")
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Copy-Item $Exe.FullName (Join-Path $Out "KT_KickEM.exe")
foreach( $d in "assets", "demos", "templates", "content" ) {
    Copy-Item -Recurse (Join-Path $Repo "Kick/$d") (Join-Path $Out $d)
}
Compress-Archive -Path $Out -DestinationPath (Join-Path $Dist "Kickray-Windows.zip")
Write-Host "Packaged: $(Join-Path $Dist 'Kickray-Windows.zip')"

# Convierte las capturas BMP de build/shots a PNG para verlas más fácil.
Add-Type -AssemblyName System.Drawing
$dir = Join-Path $PSScriptRoot "..\build\shots"
Get-ChildItem (Join-Path $dir "*.bmp") | ForEach-Object {
    $img = [System.Drawing.Image]::FromFile($_.FullName)
    $img.Save(($_.FullName -replace '\.bmp$', '.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $img.Dispose()
}

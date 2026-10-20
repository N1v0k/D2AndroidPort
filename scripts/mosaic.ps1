# Junta capturas BMP de build/shots (o de una subcarpeta) en un mosaico.
#   mosaic.ps1 -Names detras,delante -Out muro_lados              (recorta el centro y amplía x2)
#   mosaic.ps1 -Dir bord -Names b1,b2 -Out bordes -Full -Scale 0.25 -Cols 4
param(
    [Parameter(Mandatory = $true)] [string[]] $Names,
    [Parameter(Mandatory = $true)] [string] $Out,
    [string] $Dir = "",
    [int] $Cols = 2,
    [int[]] $Crop = @(250, 150, 300, 225),
    [switch] $Full,
    [double] $Scale = 2.0
)
Add-Type -AssemblyName System.Drawing
$base = Join-Path $PSScriptRoot "..\build\shots"
$dirPath = if ($Dir) { Join-Path $base $Dir } else { $base }

$imgs = @()
foreach ($n in $Names) { $imgs += [System.Drawing.Image]::FromFile((Join-Path $dirPath ($n + ".bmp"))) }
if ($Full) { $sw = $imgs[0].Width; $sh = $imgs[0].Height; $sx = 0; $sy = 0 }
else { $sx = $Crop[0]; $sy = $Crop[1]; $sw = $Crop[2]; $sh = $Crop[3] }
$cw = [int]($sw * $Scale)
$ch = [int]($sh * $Scale)
$rows = [math]::Ceiling($Names.Count / $Cols)

$canvas = New-Object System.Drawing.Bitmap ($cw * [math]::Min($Cols, $Names.Count)), ($ch * $rows)
$g = [System.Drawing.Graphics]::FromImage($canvas)
$g.InterpolationMode = if ($Scale -lt 1) { 'HighQualityBilinear' } else { 'NearestNeighbor' }
$g.PixelOffsetMode = 'Half'
$font = New-Object System.Drawing.Font 'Arial', 14
$src = New-Object System.Drawing.Rectangle $sx, $sy, $sw, $sh
for ($i = 0; $i -lt $imgs.Count; $i++) {
    $dst = New-Object System.Drawing.Rectangle (($i % $Cols) * $cw), ([math]::Floor($i / $Cols) * $ch), $cw, $ch
    $g.DrawImage($imgs[$i], $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
    $g.DrawString($Names[$i], $font, [System.Drawing.Brushes]::Yellow, $dst.X + 4, $dst.Y + 4)
    $imgs[$i].Dispose()
}
$canvas.Save((Join-Path $base ($Out + ".png")), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$canvas.Dispose()

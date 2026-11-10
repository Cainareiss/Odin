# Odin (experimental) v0.1 - placeholder package art, drawn here with System.Drawing (no third-party artwork).
# A neutral emblem (a ring, a vertical shaft and two strokes: an original abstract mark), dark background, text
# "Odin (experimental)". No Sega or Gaelco art, no thumbnails, no box art.
# Usage: powershell -ExecutionPolicy Bypass -File make_assets_odin.ps1 -OutDir <uwp\package\Assets>
param(
    [string]$OutDir = 'C:\claude-work\odinuwp\uwp\package\Assets'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$bg = [System.Drawing.Color]::FromArgb(255, 20, 20, 22)     # #141416, the manifest's background
$gold = [System.Drawing.Color]::FromArgb(255, 216, 170, 76) # #D8AA4C, the default theme's accent
$ink = [System.Drawing.Color]::FromArgb(255, 236, 232, 222)

function New-Canvas([int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear($bg)
    return @($bmp, $g)
}

# The emblem centred at (cx, cy) with radius r: a gold ring, a vertical shaft through it, two strokes off the shaft.
function Draw-Emblem($g, [float]$cx, [float]$cy, [float]$r) {
    $pen = New-Object System.Drawing.Pen $gold, ([float]([Math]::Max(2.0, $r * 0.12)))
    $g.DrawEllipse($pen, $cx - $r, $cy - $r, 2 * $r, 2 * $r)
    $pen2 = New-Object System.Drawing.Pen $gold, ([float]([Math]::Max(2.0, $r * 0.10)))
    $g.DrawLine($pen2, $cx, $cy - $r * 0.72, $cx, $cy + $r * 0.72)
    $g.DrawLine($pen2, $cx - $r * 0.42, $cy - $r * 0.25, $cx, $cy - $r * 0.55)
    $g.DrawLine($pen2, $cx + $r * 0.42, $cy - $r * 0.25, $cx, $cy - $r * 0.55)
    $pen.Dispose()
    $pen2.Dispose()
}

function Save-Png($bmp, [string]$name) {
    $path = Join-Path $OutDir $name
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host ("{0}  {1}x{2}" -f $path, $bmp.Width, $bmp.Height)
}

# Square tiles: the emblem only
foreach ($spec in @(@('StoreLogo.png', 50), @('Square44x44Logo.png', 44), @('Square71x71Logo.png', 71),
                    @('Square150x150Logo.png', 150), @('Square310x310Logo.png', 310))) {
    $size = [int]$spec[1]
    $c = New-Canvas $size $size
    Draw-Emblem $c[1] ($size / 2.0) ($size / 2.0) ($size * 0.36)
    Save-Png $c[0] $spec[0]
    $c[1].Dispose(); $c[0].Dispose()
}

# Wide tile: emblem left, the name right
$c = New-Canvas 310 150
Draw-Emblem $c[1] 62 75 44
$font = New-Object System.Drawing.Font 'Segoe UI', 22, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
$small = New-Object System.Drawing.Font 'Segoe UI', 13, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$brush = New-Object System.Drawing.SolidBrush $ink
$accent = New-Object System.Drawing.SolidBrush $gold
$c[1].DrawString('Odin', $font, $brush, 118, 52)
$c[1].DrawString('(experimental)', $small, $accent, 120, 86)
Save-Png $c[0] 'Wide310x150Logo.png'
$c[1].Dispose(); $c[0].Dispose()

# Splash: emblem centred, the name under it
$c = New-Canvas 620 300
Draw-Emblem $c[1] 310 118 70
$big = New-Object System.Drawing.Font 'Segoe UI', 30, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
$sub = New-Object System.Drawing.Font 'Segoe UI', 16, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$c[1].DrawString('Odin (experimental)', $big, $brush, 180, 210)
$c[1].DrawString('shell v0.1: no emulator core in this build', $sub, $accent, 186, 252)
Save-Png $c[0] 'SplashScreen.png'
$c[1].Dispose(); $c[0].Dispose()

# The no-cover card the shelf shows for a game without art (swanui\assets\nocover2.png). The carried-over file
# was an FM Towns placeholder, so it is replaced by this neutral one (same name, same size).
$coverDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'swanui\assets'
$c = New-Canvas 500 500
Draw-Emblem $c[1] 250 190 118
$cf = New-Object System.Drawing.Font 'Segoe UI', 44, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
$cs = New-Object System.Drawing.Font 'Segoe UI', 26, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$c[1].DrawString('ODIN', $cf, $brush, 170, 370)
$c[1].DrawString('no cover', $cs, $accent, 190, 430)
$coverPath = Join-Path $coverDir 'nocover2.png'
$c[0].Save($coverPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("{0}  {1}x{2}" -f $coverPath, $c[0].Width, $c[0].Height)
$c[1].Dispose(); $c[0].Dispose()

Write-Host 'done'

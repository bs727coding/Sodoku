<#
.SYNOPSIS
    Renders the app icon (res\app.ico, multi-resolution PNG-compressed ICO) with GDI+.
    Only needed when changing the design; the generated icon is checked in.
.EXAMPLE
    .\tools\make_icon.ps1                       # writes res\app.ico
    .\tools\make_icon.ps1 -Preview preview.png  # also writes a 256 px preview
#>
param([string]$Out = "$PSScriptRoot\..\res\app.ico", [string]$Preview = '')

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

function New-RoundedPath([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function Add-Digit($g, [string]$text, [single]$cx, [single]$cy, [single]$size, $brush) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $family = New-Object System.Drawing.FontFamily('Segoe UI Semibold')
    $path.AddString($text, $family, 0, $size, (New-Object System.Drawing.PointF(0, 0)), [System.Drawing.StringFormat]::GenericTypographic)
    $b = $path.GetBounds()
    $m = New-Object System.Drawing.Drawing2D.Matrix
    $m.Translate($cx - $b.X - $b.Width / 2, $cy - $b.Y - $b.Height / 2)
    $path.Transform($m)
    $g.FillPath($brush, $path)
}

function Render-Icon([int]$n) {
    $bmp = New-Object System.Drawing.Bitmap $n, $n, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    # Tile with a diagonal blue gradient
    $m = [single]($n * 0.04)
    $tile = New-RoundedPath $m $m ($n - 2 * $m) ($n - 2 * $m) ([single]($n * 0.22))
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF(0, 0)), (New-Object System.Drawing.PointF($n, $n)),
        [System.Drawing.Color]::FromArgb(255, 0x47, 0x9B, 0xFF), [System.Drawing.Color]::FromArgb(255, 0x10, 0x4C, 0xD6))
    $g.FillPath($grad, $tile)

    # 3x3 box of rounded cells; the centre cell is solid white
    $cell = [single]($n * 0.205); $gap = [single]($n * 0.045)
    $start = [single](($n - (3 * $cell + 2 * $gap)) / 2)
    $soft = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(56, 255, 255, 255))
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $ink = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x14, 0x52, 0xD9))
    $digitBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(240, 255, 255, 255))
    $digits = @{ 0 = '4'; 8 = '7'; 2 = '1'; 6 = '3' }
    for ($i = 0; $i -lt 9; $i++) {
        $x = $start + ($i % 3) * ($cell + $gap)
        $y = $start + [math]::Floor($i / 3) * ($cell + $gap)
        $p = New-RoundedPath $x $y $cell $cell ([single]($cell * 0.22))
        if ($i -eq 4) { $g.FillPath($white, $p) } else { $g.FillPath($soft, $p) }
        if ($n -ge 48) {
            if ($i -eq 4) { Add-Digit $g '9' ($x + $cell / 2) ($y + $cell / 2) ([single]($cell * 0.95)) $ink }
            elseif ($digits.ContainsKey($i)) { Add-Digit $g $digits[$i] ($x + $cell / 2) ($y + $cell / 2) ([single]($cell * 0.85)) $digitBrush }
        }
    }
    $g.Dispose()
    return $bmp
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256
$pngs = @()
foreach ($s in $sizes) {
    $bmp = Render-Icon $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    if ($Preview -and $s -eq 256) { $bmp.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose()
    $pngs += , $ms.ToArray()
}

# ICO container: header, directory entries, then PNG payloads.
$fs = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $pngs[$i].Length
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$len); $bw.Write([uint32]$offset)
    $offset += $len
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Flush()
[System.IO.File]::WriteAllBytes((Resolve-Path -LiteralPath (Split-Path $Out)).Path + '\' + (Split-Path $Out -Leaf), $fs.ToArray())
"Wrote $Out ($($fs.Length) bytes, sizes: $($sizes -join ', '))"

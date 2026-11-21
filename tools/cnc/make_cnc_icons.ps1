<#
  Generates the icons added by the Turtle CNC build (95x80, 24-bit BMP) in the style of the Turtle theme
  (ring with a colour gradient on black, white symbol):
    zero_x, zero_y, zero_z   set the work zero of one axis (G92 X0 / Y0 / Z0)
    goto_zero                move to the work zero (G0 X0 Y0)
    knob_x, knob_y           which axis the knob moves on the Jog screen
    feed_move                Feed move screen (one axis to a target at a cutting feed)
    fm_ax_x, fm_ax_y         Feed move axis
    fm_abs, fm_rel           Feed move mode: go to a position / move by a distance
    fm_feed, fm_send         Feed move feed (mm/min) and Send
    fm_p20, fm_p50, fm_p100  feed presets (mm/min)

  Usage (Windows PowerShell 7):  ./tools/cnc/make_cnc_icons.ps1 [-OutDir <bmp folder>]
#>
param(
  [string]$OutDir = (Join-Path $PSScriptRoot '..\..\Copy to SD Card root directory to update\THEME_Turtle theme\TFT43\bmp'),
  [string]$Preview
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$D = [System.Drawing.Drawing2D.DashCap]
$C = [System.Drawing.Color]

function Col([string]$hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }

function NewIcon([string]$c1, [string]$c2) {
  $bmp = New-Object System.Drawing.Bitmap 95, 80, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
  $g.Clear($C::Black)
  $rect = New-Object System.Drawing.RectangleF 11.5, 4, 72, 72
  $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, (Col $c1), (Col $c2), 45.0
  $pen = New-Object System.Drawing.Pen $brush, 3.5
  $g.DrawEllipse($pen, $rect)
  $pen.Dispose(); $brush.Dispose()
  @{ bmp = $bmp; g = $g }
}

function Cross($g) {
  $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(115, 255, 255, 255)), 2
  $g.DrawLine($pen, 22, 40, 73, 40); $g.DrawLine($pen, 47.5, 14, 47.5, 66)
  $pen.Dispose()
}

function Text($g, [string]$t, [string]$hex, [float]$cx, [float]$size) {
  $font = New-Object System.Drawing.Font 'Bahnschrift SemiBold SemiConden', $size, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
  $fmt = New-Object System.Drawing.StringFormat
  $fmt.Alignment = [System.Drawing.StringAlignment]::Center
  $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
  $b = New-Object System.Drawing.SolidBrush (Col $hex)
  $g.DrawString($t, $font, $b, (New-Object System.Drawing.RectangleF ($cx - 40), 18, 80, 46), $fmt)
  $b.Dispose(); $font.Dispose(); $fmt.Dispose()
}

# small bold label centred on (cx, cy), e.g. ABS / REL under a symbol
function Label($g, [string]$t, [string]$hex, [float]$cx, [float]$cy, [float]$size) {
  $font = New-Object System.Drawing.Font 'Arial', $size, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
  $fmt = New-Object System.Drawing.StringFormat
  $fmt.Alignment = [System.Drawing.StringAlignment]::Center
  $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
  $b = New-Object System.Drawing.SolidBrush (Col $hex)
  $g.DrawString($t, $font, $b, (New-Object System.Drawing.RectangleF ($cx - 40), ($cy - 14), 80, 28), $fmt)
  $b.Dispose(); $font.Dispose(); $fmt.Dispose()
}

# arrow pointing right from x1 to x2 at height y
function Arrow($g, [string]$hex, [float]$x1, [float]$x2, [float]$y, [float]$head, [float]$width) {
  $p = New-Object System.Drawing.Pen (Col $hex), $width
  $p.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $p.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $p.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
  $g.DrawLine($p, $x1, $y, $x2, $y)
  $g.DrawLines($p, [System.Drawing.PointF[]]@((New-Object System.Drawing.PointF ($x2 - $head), ($y - $head)), (New-Object System.Drawing.PointF $x2, $y), (New-Object System.Drawing.PointF ($x2 - $head), ($y + $head))))
  $p.Dispose()
}

# arrow up to a yellow stop line: move to a target
function ArrowToStop($g, [string]$hex) {
  Arrow $g $hex 20 60 40 9 4
  $y = New-Object System.Drawing.Pen (Col '#ffd60a'), 4
  $y.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $y.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $g.DrawLine($y, 70, 24, 70, 56)
  $y.Dispose()
}

function Save($ic, [string]$name) {
  $ic.g.Dispose()
  $path = Join-Path $OutDir "$name.bmp"
  $ic.bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Bmp)
  $script:made += , @{ name = $name; bmp = $ic.bmp }
  "  $path"
}

$script:made = @()
New-Item -ItemType Directory -Force $OutDir | Out-Null

foreach ($a in @(@('zero_x', 'X0', '#ff3b30', '#ff9500'), @('zero_y', 'Y0', '#34c759', '#c6f432'), @('zero_z', 'Z0', '#2ec5f0', '#5e9cff'))) {
  $ic = NewIcon $a[2] $a[3]
  Cross $ic.g
  Text $ic.g $a[1] '#ffffff' 47.5 32
  Save $ic $a[0]
}

# goto_zero: arrow into a target
$ic = NewIcon '#af52de' '#ff2d92'
$w = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 3
$ic.g.DrawEllipse($w, 49, 30, 20, 20)
$ic.g.FillEllipse([System.Drawing.Brushes]::White, 56.2, 37.2, 5.6, 5.6)
$o = New-Object System.Drawing.Pen (Col '#ff9f0a'), 4
$o.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $o.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$o.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$ic.g.DrawLine($o, 21, 40, 44, 40)
$ic.g.DrawLines($o, [System.Drawing.PointF[]]@((New-Object System.Drawing.PointF 36, 31), (New-Object System.Drawing.PointF 45, 40), (New-Object System.Drawing.PointF 36, 49)))
$w.Dispose(); $o.Dispose()
Save $ic 'goto_zero'

# knob_x / knob_y: knob with notch and turning arc, axis letter
foreach ($a in @(@('knob_x', 'X', '#ff5a3c'), @('knob_y', 'Y', '#8bd43a'))) {
  $ic = NewIcon '#ffd60a' '#ff9500'
  $w = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 3
  $ic.g.DrawEllipse($w, 21, 25, 30, 30)
  $w.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $w.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $ic.g.DrawLine($w, 36, 29, 36, 35)
  $y = New-Object System.Drawing.Pen (Col '#ffd60a'), 2.5
  $ic.g.DrawArc($y, 15, 19, 42, 42, 120, 120)
  $w.Dispose(); $y.Dispose()
  Text $ic.g $a[1] $a[2] 66 34
  Save $ic $a[0]
}

# Feed move
$ic = NewIcon '#1e90ff' '#2ec5f0'
ArrowToStop $ic.g '#ffffff'
Save $ic 'feed_move'

foreach ($a in @(@('fm_ax_x', 'X', '#ff3b30', '#ff9500', '#ff5a3c'), @('fm_ax_y', 'Y', '#34c759', '#c6f432', '#8bd43a'))) {
  $ic = NewIcon $a[2] $a[3]
  Text $ic.g $a[1] $a[4] 47.5 40
  Save $ic $a[0]
}

$ic = NewIcon '#ffd60a' '#ff9500'
Cross $ic.g
$ic.g.FillEllipse((New-Object System.Drawing.SolidBrush (Col '#ffd60a')), 55, 22, 10, 10)
Label $ic.g 'ABS' '#ffffff' 47.5 55 20
Save $ic 'fm_abs'

$ic = NewIcon '#ffd60a' '#ff9500'
Arrow $ic.g '#ffffff' 24 70 30 7 3
Label $ic.g 'REL' '#ffffff' 47.5 55 20
Save $ic 'fm_rel'

$ic = NewIcon '#1e90ff' '#2ec5f0'
Text $ic.g 'F' '#2ec5f0' 47.5 36
Save $ic 'fm_feed'

$ic = NewIcon '#1d8f4e' '#34c759'
ArrowToStop $ic.g '#34c759'
Save $ic 'fm_send'

foreach ($a in @(@('fm_p20', '20', 26), @('fm_p50', '50', 26), @('fm_p100', '100', 22))) {
  $ic = NewIcon '#1e90ff' '#2ec5f0'
  Text $ic.g $a[1] '#ffffff' 47.5 $a[2]
  Save $ic $a[0]
}

if ($Preview) {
  $sheet = New-Object System.Drawing.Bitmap (95 * $script:made.Count), 80
  $g = [System.Drawing.Graphics]::FromImage($sheet)
  for ($i = 0; $i -lt $script:made.Count; $i++) { $g.DrawImage($script:made[$i].bmp, 95 * $i, 0) }
  $g.Dispose(); $sheet.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png); $sheet.Dispose()
}
foreach ($m in $script:made) { $m.bmp.Dispose() }

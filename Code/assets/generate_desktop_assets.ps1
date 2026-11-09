$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
if (-not ('ShriDesktopAssets' -as [type])) {
  $drawingReferences = @([System.Drawing.Bitmap].Assembly.Location, [System.Drawing.Color].Assembly.Location)
  $drawingReferences += Get-ChildItem -LiteralPath $PSHOME -Filter 'System.Private.Windows.*.dll' | ForEach-Object { $_.FullName }
  $drawingReferences = $drawingReferences | Select-Object -Unique
  Add-Type -ReferencedAssemblies $drawingReferences -TypeDefinition @"
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Text;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

public static class ShriDesktopAssets {
    static void Atlas(BinaryWriter writer, FontFamily family, float size, FontStyle style, int width, int height) {
        const int scale = 4, count = 95;
        writer.Write((ushort)width); writer.Write((ushort)height);
        writer.Write((ushort)32); writer.Write((ushort)count);
        writer.Write((ushort)size); writer.Write((ushort)0);
        byte[] advances = new byte[count];
        byte[] atlas = new byte[count * width * height];
        using (Font font = new Font(family, size * scale, style, GraphicsUnit.Pixel))
        using (Bitmap bitmap = new Bitmap(width * scale, height * scale, PixelFormat.Format32bppArgb))
        using (Graphics graphics = Graphics.FromImage(bitmap))
        using (StringFormat format = (StringFormat)StringFormat.GenericTypographic.Clone()) {
            format.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces | StringFormatFlags.NoClip;
            graphics.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
            for (int glyph = 0; glyph < count; glyph++) {
                string character = ((char)(glyph + 32)).ToString();
                graphics.Clear(Color.Transparent);
                float advance = graphics.MeasureString(character, font, PointF.Empty, format).Width / scale;
                advances[glyph] = (byte)Math.Max(1, Math.Min(width, (int)Math.Round(advance)));
                graphics.DrawString(character, font, Brushes.White, PointF.Empty, format);
                BitmapData data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                byte[] pixels = new byte[data.Stride * bitmap.Height];
                Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);
                bitmap.UnlockBits(data);
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int coverage = 0;
                        for (int sy = 0; sy < scale; sy++)
                            for (int sx = 0; sx < scale; sx++)
                                coverage += pixels[(y * scale + sy) * data.Stride + (x * scale + sx) * 4 + 3];
                        atlas[(glyph * height + y) * width + x] = (byte)((coverage + 8) / 16);
                    }
                }
            }
        }
        writer.Write(advances);
        writer.Write(atlas);
    }

    public static void Create(string directory) {
        using (PrivateFontCollection ui = new PrivateFontCollection())
        using (PrivateFontCollection mono = new PrivateFontCollection()) {
            ui.AddFontFile(Path.Combine(directory, "fonts/Lato-Regular.ttf"));
            ui.AddFontFile(Path.Combine(directory, "fonts/Lato-Bold.ttf"));
            mono.AddFontFile(Path.Combine(directory, "fonts/IBMPlexMono-Regular.ttf"));
            using (BinaryWriter writer = new BinaryWriter(File.Create(Path.Combine(directory, "ui-fonts.bin")))) {
                writer.Write(0x31544e46u); writer.Write(5u);
                Atlas(writer, ui.Families[0], 14, FontStyle.Regular, 24, 22);
                Atlas(writer, ui.Families[0], 18, FontStyle.Regular, 32, 28);
                Atlas(writer, ui.Families[0], 28, FontStyle.Bold, 40, 40);
                Atlas(writer, ui.Families[0], 56, FontStyle.Bold, 64, 72);
                Atlas(writer, mono.Families[0], 16, FontStyle.Regular, 12, 24);
            }
        }
        using (Image source = Image.FromFile(Path.Combine(directory, "wallpaper-ai-human.png")))
        using (Bitmap bitmap = new Bitmap(1280, 800, PixelFormat.Format24bppRgb)) {
            using (Graphics graphics = Graphics.FromImage(bitmap)) {
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                float factor = Math.Max(1280f / source.Width, 800f / source.Height);
                float width = source.Width * factor, height = source.Height * factor;
                graphics.DrawImage(source, new RectangleF((1280 - width) / 2, (800 - height) / 2, width, height));
            }
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, 1280, 800), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            byte[] pixels = new byte[data.Stride * 800];
            Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);
            bitmap.UnlockBits(data);
            using (BinaryWriter writer = new BinaryWriter(File.Create(Path.Combine(directory, "wallpaper-modern.rgb")))) {
                writer.Write(1280u); writer.Write(800u);
                for (int y = 0; y < 800; y++) {
                    for (int x = 0; x < 1280; x++) {
                        int index = y * data.Stride + x * 3;
                        writer.Write(pixels[index + 2]); writer.Write(pixels[index + 1]); writer.Write(pixels[index]);
                    }
                }
            }
        }
    }
}
"@
}
[ShriDesktopAssets]::Create($PSScriptRoot)
$fontLicenses = "YAAI desktop bitmap atlases. Original font acknowledgements and licenses.`r`n`r`n" +
  (Get-Content -LiteralPath "$PSScriptRoot\fonts\Lato-OFL.txt" -Raw) + "`r`n`r`n" +
  (Get-Content -LiteralPath "$PSScriptRoot\fonts\IBMPlexMono-OFL.txt" -Raw)
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'font-licenses.txt'), $fontLicenses)

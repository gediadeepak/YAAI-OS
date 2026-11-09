param([Parameter(Mandatory = $true)][string]$Path)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
if (-not ('ShriScreenCapture' -as [type])) {
  $references = @([System.Drawing.Bitmap].Assembly.Location, [System.Drawing.Color].Assembly.Location)
  $references += Get-ChildItem -LiteralPath $PSHOME -Filter 'System.Private.Windows.*.dll' | ForEach-Object { $_.FullName }
  Add-Type -ReferencedAssemblies ($references | Select-Object -Unique) -TypeDefinition @"
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class ShriScreenCapture {
    static string Token(byte[] bytes, ref int position) {
        while (position < bytes.Length && bytes[position] <= 32) position++;
        int start = position;
        while (position < bytes.Length && bytes[position] > 32) position++;
        return System.Text.Encoding.ASCII.GetString(bytes, start, position - start);
    }
    public static void Convert(string source, string destination) {
        byte[] bytes = File.ReadAllBytes(source);
        int position = 0;
        if (Token(bytes, ref position) != "P6") throw new InvalidDataException("Expected a QEMU P6 screen capture.");
        int width = int.Parse(Token(bytes, ref position));
        int height = int.Parse(Token(bytes, ref position));
        if (Token(bytes, ref position) != "255" || width < 1 || height < 1) throw new InvalidDataException("Invalid image header.");
        if (bytes[position++] == 13 && position < bytes.Length && bytes[position] == 10) position++;
        if ((long)width * height * 3 != bytes.Length - position) throw new InvalidDataException("Invalid pixel data length.");
        using (Bitmap bitmap = new Bitmap(width, height, PixelFormat.Format24bppRgb)) {
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
            try {
                byte[] pixels = new byte[data.Stride * height];
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int input = position + (y * width + x) * 3;
                        int output = y * data.Stride + x * 3;
                        pixels[output] = bytes[input + 2];
                        pixels[output + 1] = bytes[input + 1];
                        pixels[output + 2] = bytes[input];
                    }
                }
                Marshal.Copy(pixels, 0, data.Scan0, pixels.Length);
            } finally { bitmap.UnlockBits(data); }
            bitmap.Save(destination, ImageFormat.Png);
        }
    }
}
"@
}
$sourcePath = (Resolve-Path -LiteralPath $Path).Path
$destinationPath = [System.IO.Path]::ChangeExtension($sourcePath, '.png')
[ShriScreenCapture]::Convert($sourcePath, $destinationPath)
Write-Output $destinationPath

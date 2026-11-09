$ErrorActionPreference = 'Stop'
if (-not ('StartupChime' -as [type])) {
Add-Type -TypeDefinition @"
using System;
using System.IO;
public static class StartupChime {
    public static void Create(string path) {
        const int rate = 48000;
        const int frames = 153600;
        double[] left = new double[frames];
        double[] right = new double[frames];
        double[] frequencies = {130.8128, 261.6256, 329.6276, 391.9954, 523.2511, 783.9909};
        for (int voice = 0; voice < frequencies.Length; voice++) {
            double start = 0.06 + voice * 0.065;
            double pan = (voice % 2 == 0) ? 0.27 : 0.73;
            for (int i = (int)(start * rate); i < frames; i++) {
                double t = (double)i / rate - start;
                double attack = 1.0 - Math.Exp(-t * 18.0);
                double envelope = attack * Math.Exp(-t * (1.35 + voice * 0.08));
                double phase = 2.0 * Math.PI * frequencies[voice] * t;
                double tone = Math.Sin(phase) + 0.17 * Math.Sin(phase * 2.002) + 0.055 * Math.Sin(phase * 3.0);
                double value = tone * envelope * (voice == 0 ? 0.55 : 0.24);
                left[i] += value * (1.0 - pan);
                right[i] += value * pan;
            }
        }
        // Cross-channel echoes build a soft, wide tail.
        int[] delays = {6576, 10704, 18192, 25008};
        double[] gains = {0.21, 0.16, 0.10, 0.06};
        for (int i = frames - 1; i >= 0; i--) {
            for (int echo = 0; echo < delays.Length; echo++) {
                int source = i - delays[echo];
                if (source >= 0) {
                    left[i] += right[source] * gains[echo];
                    right[i] += left[source] * gains[echo];
                }
            }
            double fade = Math.Min(1.0, (frames - 1 - i) / (rate * 0.55));
            left[i] *= fade;
            right[i] *= fade;
        }
        double peak = 0.0001;
        for (int i = 0; i < frames; i++) {
            peak = Math.Max(peak, Math.Max(Math.Abs(left[i]), Math.Abs(right[i])));
        }
        using (BinaryWriter writer = new BinaryWriter(File.Create(path))) {
            writer.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));
            writer.Write(36 + frames * 4);
            writer.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt "));
            writer.Write(16); writer.Write((short)1); writer.Write((short)2);
            writer.Write(rate); writer.Write(rate * 4); writer.Write((short)4); writer.Write((short)16);
            writer.Write(System.Text.Encoding.ASCII.GetBytes("data")); writer.Write(frames * 4);
            for (int i = 0; i < frames; i++) {
                writer.Write((short)Math.Round(left[i] / peak * 14000));
                writer.Write((short)Math.Round(right[i] / peak * 14000));
            }
        }
    }
}
"@
}
[StartupChime]::Create((Join-Path $PSScriptRoot 'startup.wav'))

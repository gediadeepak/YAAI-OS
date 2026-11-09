param(
  [Parameter(Mandatory = $true)][string]$DiskPath,
  [Parameter(Mandatory = $true)][string]$ExpectedImagePath
)
$ErrorActionPreference = 'Stop'
if (-not ('ShriDiskVerifier' -as [type])) {
Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Security.Cryptography;

public static class ShriDiskVerifier {
    static void Check(bool condition, string message) {
        if (!condition) throw new InvalidDataException(message);
    }
    static uint Crc(byte[] bytes, int count) {
        uint crc = 0xffffffff;
        for (int i = 0; i < count; i++) {
            crc ^= bytes[i];
            for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
        return ~crc;
    }
    static byte[] Read(FileStream disk, ulong offset, int size) {
        byte[] bytes = new byte[size];
        disk.Position = (long)offset;
        int position = 0;
        while (position < size) {
            int count = disk.Read(bytes, position, size - position);
            if (count == 0) throw new EndOfStreamException();
            position += count;
        }
        return bytes;
    }
    static bool Equal(byte[] left, byte[] right) {
        if (left.Length != right.Length) return false;
        for (int i = 0; i < left.Length; i++) if (left[i] != right[i]) return false;
        return true;
    }
    static void Header(byte[] header) {
        Check(System.Text.Encoding.ASCII.GetString(header, 0, 8) == "EFI PART", "Missing GPT header");
        uint expected = BitConverter.ToUInt32(header, 16);
        Array.Clear(header, 16, 4);
        Check(Crc(header, 92) == expected, "GPT header CRC mismatch");
    }
    class Volume {
        public ulong Start, Data;
        public uint ClusterSize, Root, FatSectors, Clusters;
        public byte[] Fat;
    }
    static Volume OpenVolume(FileStream disk, ulong start) {
        byte[] boot = Read(disk, start * 512, 512);
        Check(BitConverter.ToUInt16(boot, 510) == 0xaa55 && BitConverter.ToUInt16(boot, 11) == 512, "Invalid FAT boot sector");
        Check(boot[16] == 2 && BitConverter.ToUInt16(boot, 17) == 0 && BitConverter.ToUInt16(boot, 22) == 0, "Invalid FAT32 layout");
        uint reserved = BitConverter.ToUInt16(boot, 14);
        uint sectors = BitConverter.ToUInt32(boot, 32);
        Volume volume = new Volume();
        volume.Start = start;
        volume.FatSectors = BitConverter.ToUInt32(boot, 36);
        volume.ClusterSize = boot[13];
        Check(volume.ClusterSize != 0 && (volume.ClusterSize & (volume.ClusterSize - 1)) == 0, "Invalid cluster size");
        volume.Clusters = (sectors - reserved - 2 * volume.FatSectors) / volume.ClusterSize;
        Check(volume.Clusters >= 65525 && volume.Clusters < 0x0ffffff5, "Volume is not FAT32");
        Check(Equal(boot, Read(disk, (start + 6) * 512, 512)), "FAT boot backup mismatch");
        volume.Root = BitConverter.ToUInt32(boot, 44);
        volume.Data = start + reserved + 2 * volume.FatSectors;
        volume.Fat = Read(disk, (start + reserved) * 512, checked((int)volume.FatSectors * 512));
        Check(Equal(volume.Fat, Read(disk, (start + reserved + volume.FatSectors) * 512, volume.Fat.Length)), "FAT copies differ");
        return volume;
    }
    static ulong ClusterOffset(Volume volume, uint cluster) {
        Check(cluster >= 2 && cluster < volume.Clusters + 2, "Cluster outside volume");
        return (volume.Data + (cluster - 2) * volume.ClusterSize) * 512;
    }
    static byte[] Entry(FileStream disk, Volume volume, uint directory, string name) {
        byte[] bytes = Read(disk, ClusterOffset(volume, directory), (int)volume.ClusterSize * 512);
        for (int offset = 0; offset < bytes.Length && bytes[offset] != 0; offset += 32) {
            if (System.Text.Encoding.ASCII.GetString(bytes, offset, 11) == name) {
                byte[] entry = new byte[32]; Array.Copy(bytes, offset, entry, 0, 32); return entry;
            }
        }
        throw new InvalidDataException("Missing directory entry " + name);
    }
    static uint EntryCluster(byte[] entry) {
        return ((uint)BitConverter.ToUInt16(entry, 20) << 16) | BitConverter.ToUInt16(entry, 26);
    }
    static byte[] FileBytes(FileStream disk, Volume volume, byte[] entry) {
        byte[] bytes = new byte[BitConverter.ToUInt32(entry, 28)];
        uint cluster = EntryCluster(entry);
        int written = 0;
        while (written < bytes.Length) {
            int count = Math.Min(bytes.Length - written, (int)volume.ClusterSize * 512);
            byte[] data = Read(disk, ClusterOffset(volume, cluster), count);
            Array.Copy(data, 0, bytes, written, count);
            written += count;
            cluster = BitConverter.ToUInt32(volume.Fat, checked((int)cluster * 4)) & 0x0fffffff;
            Check(written == bytes.Length ? cluster >= 0x0ffffff8 : cluster >= 2 && cluster < 0x0ffffff8, "Broken file chain");
        }
        return bytes;
    }
    public static string Verify(string diskPath, string imagePath) {
        using (FileStream disk = File.OpenRead(diskPath)) {
            ulong last = (ulong)disk.Length / 512 - 1;
            byte[] mbr = Read(disk, 0, 512);
            Check(BitConverter.ToUInt16(mbr, 510) == 0xaa55 && mbr[450] == 0xee, "Missing protective MBR");
            byte[] primary = Read(disk, 512, 512), backup = Read(disk, last * 512, 512);
            Header(primary); Header(backup);
            Check(BitConverter.ToUInt64(primary, 24) == 1 && BitConverter.ToUInt64(primary, 32) == last, "Invalid primary GPT location");
            Check(BitConverter.ToUInt64(backup, 24) == last && BitConverter.ToUInt64(backup, 32) == 1, "Invalid backup GPT location");
            byte[] entries = Read(disk, 1024, 16384);
            Check(Crc(entries, entries.Length) == BitConverter.ToUInt32(primary, 88), "Partition CRC mismatch");
            Check(Equal(entries, Read(disk, (last - 32) * 512, 16384)), "Backup partition entries differ");
            Check(BitConverter.ToUInt32(primary, 88) == BitConverter.ToUInt32(backup, 88), "GPT partition CRCs differ");
            ulong espStart = BitConverter.ToUInt64(entries, 32), espEnd = BitConverter.ToUInt64(entries, 40);
            ulong dataStart = BitConverter.ToUInt64(entries, 160), dataEnd = BitConverter.ToUInt64(entries, 168);
            Check(espStart == 2048 && espEnd < dataStart && dataEnd <= last - 33, "Invalid partition boundaries");
            Volume esp = OpenVolume(disk, espStart);
            byte[] native = Read(disk, dataStart * 512, 512);
            Check(System.Text.Encoding.ASCII.GetString(native,0,8) == "YAAIFS02", "Missing native data filesystem v2");
            Check(Equal(native,Read(disk,(dataStart+1)*512,512)),"Native root backup mismatch");
            Check(BitConverter.ToUInt64(native,24) == dataEnd-dataStart+1, "Native volume size mismatch");
            uint nativeCrc=BitConverter.ToUInt32(native,32); Array.Clear(native,32,4);
            Check(Crc(native,512)==nativeCrc, "Native root CRC mismatch");
            uint efi = EntryCluster(Entry(disk, esp, esp.Root, "EFI        "));
            uint boot = EntryCluster(Entry(disk, esp, efi, "BOOT       "));
            uint settingsDirectory = EntryCluster(Entry(disk, esp, efi, "SHRI       "));
            byte[] image = FileBytes(disk, esp, Entry(disk, esp, boot, "BOOTX64 EFI"));
            Check(Equal(image, File.ReadAllBytes(imagePath)), "Installed EFI executable differs from source");
            byte[] config = FileBytes(disk, esp, Entry(disk, esp, settingsDirectory, "SETUP   CFG"));
            Check(config.Length == 64 && BitConverter.ToUInt64(config, 0) == 0x3154455349524853UL && BitConverter.ToUInt32(config, 8) == 1, "Invalid setup file");
            Check(Crc(config, 56) == BitConverter.ToUInt32(config, 56), "Setup checksum mismatch");
            Check(BitConverter.ToUInt64(config, 24) == last + 1, "Saved disk capacity mismatch");
            Check(BitConverter.ToUInt64(config, 32) == espEnd - espStart + 1, "Saved EFI size mismatch");
            Check(BitConverter.ToUInt64(config, 40) == dataEnd - dataStart + 1, "Saved data size mismatch");
            Check(new Guid(new byte[] {0x7e,0x49,0xa3,0x63,0x1d,0x1a,0x1b,0x49,0xa7,0x11,0x59,0x41,0x41,0x49,0x46,0x53}) == new Guid(new ArraySegment<byte>(entries,128,16).ToArray()), "Native partition type mismatch");
            return "GPT, FAT32 EFI, native documents, EFI copy, and setup checksum verified. Zone=" + BitConverter.ToUInt32(config, 12) + ", RAM limit=" + BitConverter.ToUInt64(config, 16) + " MiB.";
        }
    }
}
"@
}
[ShriDiskVerifier]::Verify((Resolve-Path -LiteralPath $DiskPath).Path, (Resolve-Path -LiteralPath $ExpectedImagePath).Path)



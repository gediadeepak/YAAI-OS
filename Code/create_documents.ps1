param([string]$DiskPath = "$PSScriptRoot\build\YAAI-documents.img")
$ErrorActionPreference = 'Stop'
$documentPath = [IO.Path]::GetFullPath($DiskPath)
if (-not ('YaaiDocumentsVolume' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public static class YaaiDocumentsVolume {
  static uint Crc(byte[] bytes) {
    uint value = 0xffffffff;
    foreach (byte b in bytes) { value ^= b; for (int i=0;i<8;i++) value = (value>>1) ^ ((value&1)!=0 ? 0xedb88320u : 0); }
    return ~value;
  }
  public static void Create(string path) {
    if (File.Exists(path)) {
      using (var file = File.OpenRead(path)) {
        var header = new byte[512]; if (file.Read(header,0,512)!=512) throw new Exception("Existing file is not a documents volume.");
        uint expected = BitConverter.ToUInt32(header,32); Array.Clear(header,32,4);
        bool legacy=Encoding.ASCII.GetString(header,0,8)=="YAAIFS01";
        if ((!legacy && Encoding.ASCII.GetString(header,0,8)!="YAAIFS02") || expected!=Crc(header) ||
            BitConverter.ToUInt32(header,8)!=(legacy?1u:2u) || BitConverter.ToUInt32(header,12)!=512 ||
            BitConverter.ToUInt32(header,16)!=(legacy?32u:64u) || BitConverter.ToUInt32(header,20)!=(legacy?4096u:65536u) ||
            BitConverter.ToUInt64(header,24)!=(ulong)file.Length/512) throw new Exception("Existing file does not match YAAI documents format; it was preserved.");
      }
      return;
    }
    Directory.CreateDirectory(Path.GetDirectoryName(path));
    using (var file = new FileStream(path,FileMode.CreateNew,FileAccess.ReadWrite,FileShare.None)) {
      file.SetLength(16*1024*1024);
      var header = new byte[512]; Encoding.ASCII.GetBytes("YAAIFS02").CopyTo(header,0);
      BitConverter.GetBytes(2u).CopyTo(header,8); BitConverter.GetBytes(512u).CopyTo(header,12);
      BitConverter.GetBytes(64u).CopyTo(header,16); BitConverter.GetBytes(65536u).CopyTo(header,20);
      BitConverter.GetBytes(32768UL).CopyTo(header,24); Guid.NewGuid().ToByteArray().CopyTo(header,40);
      BitConverter.GetBytes(Crc(header)).CopyTo(header,32); file.Write(header,0,512); file.Write(header,0,512); file.Flush(true);
    }
  }
}
'@
}
[YaaiDocumentsVolume]::Create($documentPath)
Write-Output "Documents volume: $documentPath"


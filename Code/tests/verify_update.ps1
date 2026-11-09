param(
  [Parameter(Mandatory=$true)][string]$DiskPath,
  [Parameter(Mandatory=$true)][string]$OriginalDiskPath
)
$ErrorActionPreference='Stop'
if (-not ('YaaiUpdateVerifier' -as [type])) {
Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Text;
using System.Security.Cryptography;
public static class YaaiUpdateVerifier {
  static void Check(bool ok,string message) { if(!ok) throw new InvalidDataException(message); }
  static byte[] Read(FileStream f,long at,int size) {
    f.Position=at;byte[] p=new byte[size];int n=0;
    while(n<size) {int got=f.Read(p,n,size-n);if(got==0)throw new EndOfStreamException();n+=got;}return p;
  }
  static uint Crc(byte[] p,int size) {
    uint c=0xffffffff;for(int i=0;i<size;i++){c^=p[i];for(int b=0;b<8;b++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return ~c;
  }
  static bool Equal(byte[] a,byte[] b) {
    if(a.Length!=b.Length)return false;for(int i=0;i<a.Length;i++)if(a[i]!=b[i])return false;return true;
  }
  static string RangeHash(FileStream f,long start,long size) {
    using(SHA256 sha=SHA256.Create()) {
      f.Position=start;byte[] buffer=new byte[65536];
      while(size>0){int n=f.Read(buffer,0,(int)Math.Min(size,buffer.Length));if(n==0)throw new EndOfStreamException();sha.TransformBlock(buffer,0,n,null,0);size-=n;}
      sha.TransformFinalBlock(buffer,0,0);return Convert.ToHexString(sha.Hash);
    }
  }
  class Fat {
    FileStream f;long data;int clusterSize;uint root;byte[] fat;
    public Fat(FileStream disk) {
      f=disk;byte[] gpt=Read(f,1024,128);long start=(long)BitConverter.ToUInt64(gpt,32);
      byte[] boot=Read(f,start*512,512);int reserved=BitConverter.ToUInt16(boot,14);
      uint fatSectors=BitConverter.ToUInt32(boot,36);clusterSize=boot[13]*512;root=BitConverter.ToUInt32(boot,44);
      Check(clusterSize>0,"Invalid FAT cluster size");data=(start+reserved+boot[16]*fatSectors)*512;
      fat=Read(f,(start+reserved)*512,checked((int)fatSectors*512));
    }
    uint Next(uint c) {Check(c>=2&&c<fat.Length/4,"Invalid FAT chain");return BitConverter.ToUInt32(fat,(int)c*4)&0x0fffffff;}
    long Offset(uint c) {Check(c>=2&&c<fat.Length/4,"Invalid cluster");return data+(c-2)*clusterSize;}
    byte[] Entry(uint directory,string name) {
      string[] parts=name.Split('.');string sfn=parts[0].ToUpperInvariant().PadRight(8)+(parts.Length>1?parts[1].ToUpperInvariant():"").PadRight(3);
      for(int count=0;directory<0x0ffffff8&&count<65536;count++,directory=Next(directory)) {
        byte[] p=Read(f,Offset(directory),clusterSize);
        for(int at=0;at<p.Length&&p[at]!=0;at+=32)
          if(p[at]!=0xe5&&p[at+11]!=15&&Encoding.ASCII.GetString(p,at,11)==sfn){byte[] e=new byte[32];Array.Copy(p,at,e,0,32);return e;}
      }throw new InvalidDataException("Missing "+name);
    }
    static uint Cluster(byte[] e) {return ((uint)BitConverter.ToUInt16(e,20)<<16)|BitConverter.ToUInt16(e,26);}
    public byte[] File(string path) {
      string[] parts=path.Split('/');uint c=root;byte[] e=null;
      foreach(string name in parts){e=Entry(c,name);c=Cluster(e);}
      byte[] p=new byte[BitConverter.ToUInt32(e,28)];int done=0;
      while(done<p.Length){int n=Math.Min(clusterSize,p.Length-done);Array.Copy(Read(f,Offset(c),n),0,p,done,n);done+=n;c=Next(c);}return p;
    }
  }
  public static string Verify(string diskPath,string originalPath) {
    using(FileStream f=File.OpenRead(diskPath))
    using(FileStream original=File.OpenRead(originalPath)) {
      Check(Equal(Read(f,0,17408),Read(original,0,17408)),"Partition table changed");
      byte[] entry=Read(f,1152,128);long first=(long)BitConverter.ToUInt64(entry,32),last=(long)BitConverter.ToUInt64(entry,40);
      string hash=RangeHash(f,first*512,(last-first+1)*512);
      Check(hash==RangeHash(original,first*512,(last-first+1)*512),"Documents partition changed");
      Fat current=new Fat(f),old=new Fat(original);
      Check(Equal(current.File("EFI/SHRI/SETUP.CFG"),old.File("EFI/SHRI/SETUP.CFG")),"Setup settings changed");
      ulong generation=0;int active=-1;
      for(int i=0;i<2;i++) {
        byte[] record;
        try{record=current.File("EFI/YAAI/BOOT"+i+".CFG");}catch(InvalidDataException){continue;}
        Check(record.Length==64&&Encoding.ASCII.GetString(record,0,8)=="YAAIBT01"&&Crc(record,60)==BitConverter.ToUInt32(record,60),"Invalid boot record "+i);
        uint slot=BitConverter.ToUInt32(record,16);Check(slot<2,"Invalid slot");
        byte[] image=current.File("EFI/YAAI/SLOT"+slot+".EFI");
        Check((ulong)image.Length==BitConverter.ToUInt64(record,24)&&Crc(image,image.Length)==BitConverter.ToUInt32(record,20),"Slot image checksum mismatch");
        ulong next=BitConverter.ToUInt64(record,8);if(next>generation){generation=next;active=(int)slot;}
      }
      Check(active>=0,"No valid installed slot");
      byte[] backup=current.File("EFI/YAAI/RECOVER.EFI");
      Check(Equal(backup,old.File("EFI/BOOT/BOOTX64.EFI")),"Previous boot image not preserved");
      return "Verified: partition table, all document bytes and saved setup unchanged; original EFI retained. Active slot="+active+", generation="+generation+", documents SHA256="+hash;
    }
  }
}
"@
}
[YaaiUpdateVerifier]::Verify((Resolve-Path -LiteralPath $DiskPath).Path,(Resolve-Path -LiteralPath $OriginalDiskPath).Path)

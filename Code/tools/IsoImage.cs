using System;
using System.IO;
using System.Text;
using System.Security.Cryptography;

public static class YaaiIso {
 const int S=2048, ImageSize=16*1024*1024, Root=257*512, Data=289*512;
 static void Check(bool x,string s){if(!x)throw new InvalidDataException(s);}
 static void Str(byte[] p,int a,int n,string s){for(int i=0;i<n;i++)p[a+i]=(byte)(i<s.Length?s[i]:' ');}
 static void U16(byte[] p,int a,int n){p[a]=(byte)n;p[a+1]=(byte)(n>>8);}
 static void U32(byte[] p,int a,int n){for(int i=0;i<4;i++)p[a+i]=(byte)(n>>(i*8));}
 static void Both16(byte[] p,int a,int n){U16(p,a,n);p[a+2]=(byte)(n>>8);p[a+3]=(byte)n;}
 static void Both32(byte[] p,int a,int n){U32(p,a,n);for(int i=0;i<4;i++)p[a+4+i]=(byte)(n>>(24-i*8));}
 static byte[] Bytes(string s){return Encoding.ASCII.GetBytes(s);}
 static int Blocks(int n){return (n+S-1)/S;}
 static void FatEntry(byte[] p,int a,string name,int cluster,int length,bool dir){
  Str(p,a,11,name);p[a+11]=(byte)(dir?16:32);U16(p,a+26,cluster);U32(p,a+28,length);
  DateTime t=DateTime.UtcNow;U16(p,a+22,(t.Hour<<11)|(t.Minute<<5)|(t.Second/2));
  U16(p,a+24,((t.Year-1980)<<9)|(t.Month<<5)|t.Day);
 }
 static void Link(byte[] p,int cluster,int next){U16(p,512+cluster*2,next);U16(p,129*512+cluster*2,next);}
 static int FatFile(byte[] p,int entry,string name,byte[] data,int first){
  int count=Math.Max(1,(data.Length+511)/512);
  Check(first+count<=(ImageSize-Data)/512+2,"EFI payload exceeds the 16 MiB boot image");
  FatEntry(p,entry,name,first,data.Length,false);
  for(int i=0;i<count;i++)Link(p,first+i,i+1==count?0xffff:first+i+1);
  Array.Copy(data,0,p,Data+(first-2)*512,data.Length);return first+count;
 }
 static byte[] MakeFat(byte[] kernel,byte[] readme,byte[] fonts){
  byte[] p=new byte[ImageSize];p[0]=0xeb;p[1]=0x3c;p[2]=0x90;Str(p,3,8,"YAAIUEFI");
  U16(p,11,512);p[13]=1;U16(p,14,1);p[16]=2;U16(p,17,512);U16(p,19,32768);p[21]=0xf8;
  U16(p,22,128);U16(p,24,32);U16(p,26,64);p[36]=0x80;p[38]=0x29;U32(p,39,0x59414149);
  Str(p,43,11,"YAAI_OS");Str(p,54,8,"FAT16");p[510]=0x55;p[511]=0xaa;
  Link(p,0,0xfff8);Link(p,1,0xffff);Link(p,2,0xffff);Link(p,3,0xffff);
  FatEntry(p,Root,"EFI",2,0,true);
  FatEntry(p,Data,".",2,0,true);FatEntry(p,Data+32,"..",0,0,true);FatEntry(p,Data+64,"BOOT",3,0,true);
  FatEntry(p,Data+512,".",3,0,true);FatEntry(p,Data+544,"..",2,0,true);
  int next=FatFile(p,Data+576,"BOOTX64 EFI",kernel,4);
  next=FatFile(p,Root+32,"README  TXT",readme,next);FatFile(p,Root+64,"FONTS   TXT",fonts,next);
  return p;
 }
 static byte[] Record(int lba,int size,byte[] name,bool dir){
  byte[] p=new byte[33+name.Length+(name.Length%2==0?1:0)];p[0]=(byte)p.Length;
  Both32(p,2,lba);Both32(p,10,size);DateTime t=DateTime.UtcNow;
  p[18]=(byte)(t.Year-1900);p[19]=(byte)t.Month;p[20]=(byte)t.Day;p[21]=(byte)t.Hour;p[22]=(byte)t.Minute;p[23]=(byte)t.Second;
  p[25]=(byte)(dir?2:0);Both16(p,28,1);p[32]=(byte)name.Length;Array.Copy(name,0,p,33,name.Length);return p;
 }
 static byte[] Dir(params byte[][] entries){
  byte[] p=new byte[S];int a=0;foreach(byte[] e in entries){Check(a+e.Length<=S,"ISO directory full");Array.Copy(e,0,p,a,e.Length);a+=e.Length;}return p;
 }
 static byte[] Path(bool big){
  byte[] p=new byte[S];string[] names={"\0","EFI","BOOT"};int[] lba={22,23,24},parent={1,1,2};int a=0;
  for(int i=0;i<3;i++){byte[] n=Bytes(names[i]);p[a]=(byte)n.Length;
   if(big){for(int k=0;k<4;k++)p[a+2+k]=(byte)(lba[i]>>(24-k*8));p[a+6]=(byte)(parent[i]>>8);p[a+7]=(byte)parent[i];}
   else{U32(p,a+2,lba[i]);U16(p,a+6,parent[i]);}
   Array.Copy(n,0,p,a+8,n.Length);a+=8+n.Length+n.Length%2;
  }return p;
 }
 static void Write(FileStream f,int lba,byte[] p){f.Position=(long)lba*S;f.Write(p,0,p.Length);}
 public static string Build(string image,string iso,string notes,string licenses){
  byte[] kernel=File.ReadAllBytes(image),readme=Bytes(notes),fonts=File.ReadAllBytes(licenses);
  Check(kernel.Length>=512&&kernel[0]=='M'&&kernel[1]=='Z',"Missing EFI executable");int pe=BitConverter.ToInt32(kernel,60);
  Check(pe>=0&&pe<=kernel.Length-94&&BitConverter.ToUInt32(kernel,pe)==0x4550&&BitConverter.ToUInt16(kernel,pe+4)==0x8664&&BitConverter.ToUInt16(kernel,pe+92)==10,"Expected x86-64 EFI executable");
  byte[] fat=MakeFat(kernel,readme,fonts);int k=32+ImageSize/S,r=k+Blocks(kernel.Length),l=r+Blocks(readme.Length),total=l+Blocks(fonts.Length);
  byte[] pvd=new byte[S];pvd[0]=1;Str(pvd,1,5,"CD001");pvd[6]=1;Str(pvd,8,32,"YAAI_OS");Str(pvd,40,32,"YAAI_OS_UEFI");
  Both32(pvd,80,total);Both16(pvd,120,1);Both16(pvd,124,1);Both16(pvd,128,S);Both32(pvd,132,34);
  U32(pvd,140,19);pvd[151]=20;byte[] rr=Record(22,S,new byte[]{0},true);Array.Copy(rr,0,pvd,156,rr.Length);
  Str(pvd,190,128,"YAAI_OS");Str(pvd,318,128,"YAAI OS");Str(pvd,446,128,"YAAI ISO EXPORTER");Str(pvd,574,128,"YAAI OS");pvd[881]=1;
  foreach(int a in new int[]{813,830,847,864})Str(pvd,a,16,"0000000000000000");
  byte[] br=new byte[S];Str(br,1,5,"CD001");br[6]=1;Str(br,7,32,"EL TORITO SPECIFICATION");U32(br,71,21);
  byte[] end=new byte[S];end[0]=255;Str(end,1,5,"CD001");end[6]=1;
  byte[] cat=new byte[S];cat[0]=1;cat[1]=0xef;Str(cat,4,24,"YAAI OS UEFI");cat[30]=0x55;cat[31]=0xaa;
  int sum=0;for(int i=0;i<32;i+=2)sum+=BitConverter.ToUInt16(cat,i);U16(cat,28,(-sum)&0xffff);
  cat[32]=0x88;U16(cat,38,32768);U32(cat,40,32);
  byte[] root=Dir(Record(22,S,new byte[]{0},true),Record(22,S,new byte[]{1},true),Record(23,S,Bytes("EFI"),true),
   Record(21,S,Bytes("BOOT.CAT;1"),false),Record(32,ImageSize,Bytes("EFI.IMG;1"),false),
   Record(r,readme.Length,Bytes("README.TXT;1"),false),Record(l,fonts.Length,Bytes("FONTS.TXT;1"),false));
  byte[] efi=Dir(Record(23,S,new byte[]{0},true),Record(22,S,new byte[]{1},true),Record(24,S,Bytes("BOOT"),true));
  byte[] boot=Dir(Record(24,S,new byte[]{0},true),Record(23,S,new byte[]{1},true),Record(k,kernel.Length,Bytes("BOOTX64.EFI;1"),false));
  using(FileStream f=new FileStream(iso,FileMode.CreateNew,FileAccess.Write,FileShare.None)){
   f.SetLength((long)total*S);Write(f,16,pvd);Write(f,17,br);Write(f,18,end);Write(f,19,Path(false));Write(f,20,Path(true));
   Write(f,21,cat);Write(f,22,root);Write(f,23,efi);Write(f,24,boot);Write(f,32,fat);Write(f,k,kernel);Write(f,r,readme);Write(f,l,fonts);f.Flush(true);
  }return Verify(iso,image);
 }
 static byte[] Read(FileStream f,long at,int size){
  f.Position=at;byte[] p=new byte[size];int done=0;while(done<size){int n=f.Read(p,done,size-done);if(n==0)throw new EndOfStreamException();done+=n;}return p;
 }
 static string Hash(byte[] p){using(SHA256 s=SHA256.Create())return BitConverter.ToString(s.ComputeHash(p)).Replace("-","");}
 static int Find(byte[] dir,string name){for(int i=0;i+32<=dir.Length&&dir[i]!=0;i+=32)if(Encoding.ASCII.GetString(dir,i,11)==name)return i;throw new InvalidDataException("Missing FAT entry "+name);}
 public static string Verify(string iso,string image){
  using(FileStream f=File.OpenRead(iso)){
   byte[] p=Read(f,16*S,S),br=Read(f,17*S,S);Check(p[0]==1&&Encoding.ASCII.GetString(p,1,5)=="CD001","Invalid ISO9660 volume");
   Check(BitConverter.ToUInt32(p,80)*(long)S==f.Length,"ISO size mismatch");
   Check(br[0]==0&&Encoding.ASCII.GetString(br,7,23)=="EL TORITO SPECIFICATION","Missing El Torito descriptor");
   byte[] cat=Read(f,(long)BitConverter.ToUInt32(br,71)*S,64);int sum=0;for(int i=0;i<32;i+=2)sum+=BitConverter.ToUInt16(cat,i);
   Check(cat[0]==1&&cat[1]==0xef&&cat[30]==0x55&&cat[31]==0xaa&&(sum&0xffff)==0,"Invalid UEFI boot catalog");
   Check(cat[32]==0x88&&cat[33]==0&&BitConverter.ToUInt16(cat,38)==32768,"Invalid EFI boot entry");
   byte[] fat=Read(f,(long)BitConverter.ToUInt32(cat,40)*S,ImageSize);
   Check(fat[510]==0x55&&fat[511]==0xaa&&BitConverter.ToUInt16(fat,11)==512&&fat[13]==1&&fat[16]==2,"Invalid FAT16 geometry");
   int reserved=BitConverter.ToUInt16(fat,14)*512,fs=BitConverter.ToUInt16(fat,22)*512,root=reserved+2*fs;
   int data=root+BitConverter.ToUInt16(fat,17)*32;
   for(int i=0;i<fs;i++)Check(fat[reserved+i]==fat[reserved+fs+i],"FAT copies differ");
   byte[] dir=new byte[16384];Array.Copy(fat,root,dir,0,dir.Length);int cluster=0,entry=0;
   string[] names={"EFI        ","BOOT       ","BOOTX64 EFI"};
   for(int i=0;i<3;i++){entry=Find(dir,names[i]);cluster=BitConverter.ToUInt16(dir,entry+26);if(i<2){dir=new byte[512];Array.Copy(fat,data+(cluster-2)*512,dir,0,512);}}
   int length=checked((int)BitConverter.ToUInt32(dir,entry+28));byte[] payload=new byte[length];int done=0,steps=0;
   while(done<length){
    Check(cluster>=2&&cluster<(ImageSize-data)/512+2&&++steps<=32768,"Invalid FAT chain");
    int n=Math.Min(512,length-done);Array.Copy(fat,data+(cluster-2)*512,payload,done,n);done+=n;cluster=BitConverter.ToUInt16(fat,reserved+cluster*2);
   }
   Check(cluster>=0xfff8&&Hash(payload)==Hash(File.ReadAllBytes(image)),"Embedded EFI differs from build");
   return "Verified ISO9660, UEFI El Torito checksum, FAT16 chains, both FATs and BOOTX64.EFI SHA256="+Hash(payload);
  }
 }
}

$ErrorActionPreference='Stop'
$codeDirectory=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not ('YaaiIso' -as [type])){Add-Type -Path (Join-Path $codeDirectory 'tools\IsoImage.cs')}
$testDirectory=Join-Path $codeDirectory ('build\iso-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$efi=Join-Path $testDirectory 'test.efi'
$iso=Join-Path $testDirectory 'test.iso'
$invalidIso=Join-Path $testDirectory 'invalid.iso'
$license=Join-Path $testDirectory 'license.txt'
try {
  $payload=[byte[]]::new(512)
  $payload[0]=0x4d;$payload[1]=0x5a;$payload[60]=128
  $payload[128]=0x50;$payload[129]=0x45;$payload[132]=0x64;$payload[133]=0x86
  $payload[152]=0x0b;$payload[153]=2;$payload[220]=10
  [IO.File]::WriteAllBytes($efi,$payload)
  [IO.File]::WriteAllText($license,'ISO exporter fixture only.')
  [YaaiIso]::Build($efi,$iso,'ISO export test',$license) | Out-Null
  $original=[IO.File]::ReadAllBytes($iso)
  $bad=[byte[]]$original.Clone()
  $catalog=[BitConverter]::ToUInt32($bad,17*2048+71)*2048
  $bad[$catalog+1]=0
  [IO.File]::WriteAllBytes($iso,$bad)
  $rejected=$false
  try {[YaaiIso]::Verify($iso,$efi) | Out-Null} catch {$rejected=$true}
  if(-not $rejected){throw 'Damaged boot catalog was accepted'}
  $bad=[byte[]]$original.Clone()
  $image=[BitConverter]::ToUInt32($bad,$catalog+40)*2048
  $sector=[BitConverter]::ToUInt16($bad,$image+11)
  $root=$image+([BitConverter]::ToUInt16($bad,$image+14)+2*[BitConverter]::ToUInt16($bad,$image+22))*$sector
  $data=$root+[BitConverter]::ToUInt16($bad,$image+17)*32
  $bad[$data+2*$sector+300]=$bad[$data+2*$sector+300] -bxor 1
  [IO.File]::WriteAllBytes($iso,$bad)
  $rejected=$false
  try {[YaaiIso]::Verify($iso,$efi) | Out-Null} catch {$rejected=$true}
  if(-not $rejected){throw 'Damaged EFI payload was accepted'}
  [IO.File]::WriteAllBytes($iso,$original)
  $rejected=$false
  try {[YaaiIso]::Build($efi,$iso,'test',$license) | Out-Null} catch {$rejected=$true}
  if(-not $rejected){throw 'Existing output was overwritten'}
  [YaaiIso]::Verify($iso,$efi) | Out-Null
  $payload[132]=0x64;$payload[133]=0xaa
  [IO.File]::WriteAllBytes($efi,$payload)
  $rejected=$false
  try {[YaaiIso]::Build($efi,$invalidIso,'test',$license) | Out-Null} catch {$rejected=$true}
  if(-not $rejected -or (Test-Path -LiteralPath $invalidIso)){throw 'Non-x86 EFI payload was accepted'}
  Write-Output 'ISO export tests passed: valid media, corrupt catalog, corrupt payload, no overwrite and wrong architecture.'
} finally {
  foreach($file in @($efi,$iso,$invalidIso,$license)){
    if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file}
  }
  Remove-Item -LiteralPath $testDirectory
}

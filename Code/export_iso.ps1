param([string]$OutputPath)
$ErrorActionPreference='Stop'
$codeDirectory=$PSScriptRoot
if($OutputPath){$OutputPath=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)}
& (Join-Path $codeDirectory 'build.ps1')
if(-not $OutputPath) {
  $id=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
  $OutputPath=Join-Path $codeDirectory "build\YAAI-OS-UEFI-$id.iso"
}
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
if(Test-Path -LiteralPath $OutputPath){throw "Output already exists: $OutputPath"}
if(-not ('YaaiIso' -as [type])){Add-Type -Path (Join-Path $codeDirectory 'tools\IsoImage.cs')}
$readme=@(
 'YAAI OS - x86-64 UEFI ISO',
 '=========================',
 'Boot using UEFI firmware with Secure Boot disabled. This is not a BIOS ISO.',
 '',
 'Hyper-V: select Generation 2, fixed startup RAM (512 MiB or more), and DVD first.',
 'Attach this ISO to the virtual DVD drive.',
 'IMPORTANT: Hyper-V keyboard/mouse and VHDX storage use synthetic devices.',
 'YAAI lacks the necessary Hyper-V/VMBus drivers. Creating an ISO does not add',
 'these drivers. A usable Hyper-V desktop and persistent installation are not',
 'yet supported. Keep using VirtualBox/QEMU for the working desktop.',
 '',
 'To try the desktop in QEMU/VirtualBox, select Try without installing.',
 'The ISO is read-only; it is not a writable documents disk.',
 'Font license notices are included in FONTS.TXT.'
)-join [Environment]::NewLine
[YaaiIso]::Build((Join-Path $codeDirectory 'build\BOOTX64.EFI'),$OutputPath,$readme,(Join-Path $codeDirectory 'assets\font-licenses.txt'))
Write-Output "UEFI ISO: $OutputPath"

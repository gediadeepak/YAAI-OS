param([string]$VMName = 'SHR.I OS')
$ErrorActionPreference = 'Stop'
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$info = & $vbox showvminfo $VMName --machinereadable
if ($LASTEXITCODE -ne 0) { throw "Cannot find VirtualBox VM '$VMName'." }
if ($info -notcontains 'VMState="poweroff"') { throw 'Shut down the VM fully, then run this script again. Its disks and running session were preserved.' }
$documents = Join-Path (Split-Path $PSScriptRoot -Parent) 'VDI\YAAI-documents.vdi'
& "$PSScriptRoot\export_documents.ps1" -DiskPath $documents
$targetSlot = -1
foreach ($device in @(0,1)) {
  $row = $info | Where-Object { $_ -match ('^"IDE-1-' + $device + '"=') }
  if ($row) {
    $medium = (($row -split '=',2)[1]).Trim('"').Replace('\\','\')
    if ($medium -eq $documents) { Write-Output 'Documents disk is already attached.'; return }
    if ($medium -eq 'none') { $targetSlot = $device; break }
  } else { $targetSlot = $device; break }
}
if ($targetSlot -lt 0) { throw 'Both IDE secondary slots are occupied. Attach the documents VDI to a free IDE slot using VM settings.' }
& $vbox storageattach $VMName --storagectl IDE --port 1 --device $targetSlot --type hdd --medium $documents
if ($LASTEXITCODE -ne 0) { throw 'Cannot attach documents disk. Check that the VM has an IDE controller named IDE.' }
Write-Output 'Documents disk attached. Boot the latest YAAI image and press F4.'

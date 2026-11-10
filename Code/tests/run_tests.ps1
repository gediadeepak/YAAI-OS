$ErrorActionPreference = 'Stop'
$projectDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $projectDirectory
& "$projectDirectory\build.ps1"
$llvmBin = 'C:\Program Files\LLVM\bin'
$testDirectory = Join-Path $projectDirectory 'build\memory-tests'
New-Item -ItemType Directory -Force "$testDirectory\esp\EFI\BOOT" | Out-Null

& "$llvmBin\clang.exe" --target=x86_64-pc-windows-msvc -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -DSHRI_TEST_BOOT -Dkernel_main=memory_probe_main -c boot.c -o "$testDirectory\boot_probe.obj"
if ($LASTEXITCODE -ne 0) { throw 'Test loader compilation failed.' }
$testObjects = @("$testDirectory\boot_probe.obj")
foreach ($source in @('memory_test', 'clock_test', 'mouse_test', 'gui_test', 'network_test', 'input_events_test', 'storage_test','settings_test','settings_ui_test','launcher_test', 'display_mode_test', 'files_test','volumes_test','editor_test','folders_test','apps_test','browser_test','boot_update_test', 'memory_boot_test')) {
  & "$llvmBin\clang.exe" --target=x86_64-pc-windows-msvc -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -mno-red-zone -mno-stack-arg-probe -c "tests\$source.c" -o "$testDirectory\$source.obj"
  if ($LASTEXITCODE -ne 0) { throw "$source compilation failed." }
  $testObjects += "$testDirectory\$source.obj"
}
$objects = @('kernel', 'audio', 'chime', 'exception_stubs', 'console', 'exceptions', 'keyboard', 'shell', 'memory', 'runtime', 'setup', 'installer', 'install_disk', 'clock', 'ps2', 'mouse', 'desktop','shell_ui', 'gui', 'gui_assets', 'devices', 'network', 'platform_compat', 'platform_hal', 'platform_clock', 'input_events', 'storage', 'settings', 'platform_store', 'display_mode', 'files', 'platform_disk', 'file_app','file_browser', 'volumes', 'power', 'editor', 'window_manager', 'apps', 'notes_app', 'calculator', 'calculator_app','settings_app','boot_update', 'vbox_pointer','boot_efi') |
  ForEach-Object { "build\$_.obj" }
& "$llvmBin\lld-link.exe" /subsystem:efi_application /entry:efi_main /nodefaultlib "/out:$testDirectory\esp\EFI\BOOT\BOOTX64.EFI" @testObjects @objects
if ($LASTEXITCODE -ne 0) { throw 'Test linking failed.' }

foreach ($ram in @('128M', '512M')) {
  $vars = Join-Path $testDirectory "vars-$ram.fd"
  $serialLog = Join-Path $testDirectory "serial-$ram.log"
  Copy-Item 'C:\Program Files\qemu\share\edk2-i386-vars.fd' $vars
  $arguments = @(
    '-machine', 'q35', '-m', $ram,
    '-drive', 'if=pflash,format=raw,readonly=on,file=C:\Program Files\qemu\share\edk2-x86_64-code.fd',
    '-drive', "if=pflash,format=raw,file=$vars",
    '-drive', "if=none,id=osdisk,format=raw,snapshot=on,file=fat:$testDirectory\esp",
    '-device', 'ide-hd,drive=osdisk,bootindex=1',
    '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    '-display', 'none', '-serial', "file:$serialLog", '-monitor', 'none', '-net', 'none', '-no-reboot'
  ) | ForEach-Object { '"' + $_ + '"' }
  $process = Start-Process -FilePath 'C:\Program Files\qemu\qemu-system-x86_64.exe' -ArgumentList $arguments -WorkingDirectory $projectDirectory -WindowStyle Hidden -PassThru
  if (-not $process.WaitForExit(45000)) {
    $process.Kill()
    throw "Memory/clock tests timed out with $ram RAM. See $serialLog"
  }
  $process.Refresh()
  $log = Get-Content -LiteralPath $serialLog -Raw
  if ($process.ExitCode -ne 1 -or $log -notmatch 'MEMORY TESTS PASSED' -or $log -notmatch 'CLOCK TESTS PASSED' -or $log -notmatch 'MOUSE TESTS PASSED' -or $log -notmatch 'GUI TESTS PASSED' -or $log -notmatch 'NETWORK TESTS PASSED' -or $log -notmatch 'INPUT EVENT TESTS PASSED' -or $log -notmatch 'STORAGE TESTS PASSED' -or $log -notmatch 'DISPLAY MODE TESTS PASSED' -or $log -notmatch 'FILESYSTEM TESTS PASSED') {
    throw "Tests failed with $ram RAM. $log"
  }
  Write-Output "Memory, clock, mouse, GUI, network, input and storage tests passed with $ram RAM."
}

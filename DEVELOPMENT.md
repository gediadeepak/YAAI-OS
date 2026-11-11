# YAAI OS developer guide

YAAI OS boots as a 64-bit UEFI application and takes control of the machine through its own C kernel. The desktop, apps and filesystem are shared between the x86-64 PC and ARM64 QEMU virt builds. The build selects a hardware backend for each target. Read the [architecture notes](Code/ARCHITECTURE.txt) for low-level details and the [README](README.md) for installation and update instructions.

## Build, run and test

Run these commands from PowerShell in the project workspace. LLVM/LLD and QEMU are used by the existing scripts. The scripts specify their Windows installation paths.

```powershell
cd 'G:\Project\YAAI-OS\Code'
.\build.ps1                        # build\BOOTX64.EFI
.\build.ps1 -Target arm64          # build\arm64\BOOTAA64.EFI
.\launch_qemu.ps1                  # x86-64, starts setup if the system disk is missing
.\launch_qemu.ps1 -Installer       # boot the current installer alongside the system disk
.\launch_arm64.ps1                 # ARM64 QEMU virt live desktop and serial console
.\tests\run_tests.ps1
.\tests\run_arm64_tests.ps1
```

Use a separate test VM and disk when investigating installer or filesystem writes. Keep the exact executable, disk image, firmware settings and reproduction steps for a failing run. Record the Git commit with `git rev-parse --short HEAD` and check for local edits with `git status --short`.

[build.ps1](Code/build.ps1) selects the target-specific sources, compiles the freestanding code and links the EFI executable. [export_iso.ps1](Code/export_iso.ps1) uses [IsoImage.cs](Code/tools/IsoImage.cs) to package the x86-64 executable as a bootable UEFI DVD image. [export_virtualbox.ps1](Code/export_virtualbox.ps1) creates an installer VDI and a separate blank system VDI. Its `-InstallerOnly` switch exports update media for an existing installation. Running a build alone does not update an already installed VM.

## Follow a boot from firmware to desktop

1. [boot.c](Code/boot.c) starts at `efi_main()`. The PC build calls the boot-slot dispatcher and installer. [installer.c](Code/installer.c) handles setup, live boot and existing-install choices using UEFI console and disk protocols. [install_disk.c](Code/install_disk.c) writes the confirmed installation target. The ARM64 live build skips this installer path.
2. The loader selects a UEFI GOP display mode through [display_mode.c](Code/display_mode.c), validates the framebuffer and allocates the desktop buffers. [boot_info.h](Code/boot_info.h) defines the data passed to the kernel, including the framebuffer, setup data, memory descriptors and runtime-service pointer. [uefi.h](Code/uefi.h) defines the firmware interfaces used by the loader.
3. `efi_main()` gets the final memory map and calls `ExitBootServices`. The map key describes that specific map. If the map changes, the loader can obtain a new key and retry. Keep firmware allocation and logging calls out of the interval between the final map read and exit call.
4. [kernel.c](Code/kernel.c) starts at `kernel_main()`. It sets up the console and exceptions, initializes the physical page allocator in [memory.c](Code/memory.c), restores settings, mounts documents and runs the splash and startup audio. It selects the graphical desktop when both drawing buffers exist and the display is at least 640×480. Otherwise it runs the command shell.
5. [desktop.c](Code/desktop.c) starts at `desktop_run()`. It lays out the desktop, initializes platform input, then polls keyboard, pointer and network events. It redraws the scene when state changes and copies the result to the framebuffer. [shell.c](Code/shell.c) handles command dispatch, while [console.c](Code/console.c) handles terminal text and mirrors console messages to serial.

## Find the code for a feature

| Area | Files and responsibilities |
| --- | --- |
| Desktop and windows | [desktop.c](Code/desktop.c) routes events, draws the wallpaper, widget sidebar and dock, and composes windows and panels. [window_manager.c](Code/window_manager.c) owns window geometry, focus, stacking and layout state. The sidebar is drawn only when widgets are enabled and the active window is outside maximized layout. |
| Drawing | [gui.c](Code/gui.c) draws shapes and text. [screen.h](Code/screen.h) declares framebuffer and timing helpers. [gui_assets.S](Code/gui_assets.S) links the desktop assets. |
| Launcher and Quick Settings | [shell_ui.c](Code/shell_ui.c) contains launcher search, icons and shared colors. [quick_settings.c](Code/quick_settings.c) handles its own layout and input, calendar, audio controls and appearance. Power buttons use the desktop confirmation flow. |
| App lifecycle | [apps.h](Code/apps.h) defines the app callbacks and filesystem service table. [apps.c](Code/apps.c) connects registered apps to the window manager and dispatches open, draw, input and close calls. |
| Files and Notes | [file_browser.c](Code/file_browser.c) presents folders, file operations and Trash. [notes_app.c](Code/notes_app.c) connects Notes to the app registry. [file_app.c](Code/file_app.c) coordinates editor rendering, document loading and saving. [editor.c](Code/editor.c) owns text, selection, clipboard and undo. |
| Calculator and Settings | [calculator_app.c](Code/calculator_app.c) handles Calculator input and drawing, with arithmetic in [calculator.c](Code/calculator.c). [settings_app.c](Code/settings_app.c) implements the settings pages and search. |
| Preferences and time | [settings.c](Code/settings.c) validates preferences and requests persistence via [storage.c](Code/storage.c). [platform/uefi_store.c](Code/platform/uefi_store.c) provides UEFI variable storage. [setup.c](Code/setup.c) defines setup validation and checksum logic. [clock.c](Code/clock.c) reads UTC through the platform and applies the configured time-zone rules. |
| Native filesystem | [block.h](Code/block.h) defines sector I/O. [volumes.c](Code/volumes.c) validates GPT and bounds access to the documents partition. [files.c](Code/files.c) mounts YAAIFS and manages directory entries, file contents, Trash and checksums. |
| Updates and power | [boot_efi.c](Code/boot_efi.c) handles EFI-side boot files and dispatch. [boot_update.c](Code/boot_update.c) verifies boot slots and update records. [power.c](Code/power.c) flushes storage before requesting a reset or shutdown. |
| Shared hardware interface | [platform/platform.h](Code/platform/platform.h) defines the services the kernel calls. [platform/uefi_hooks.h](Code/platform/uefi_hooks.h) defines preparation while firmware boot services still exist. [platform/compat.c](Code/platform/compat.c) adapts serial output and frame timing for shared code. |
| x86-64 hardware | [platform/x86_64/hal.c](Code/platform/x86_64/hal.c) connects the shared kernel to PC drivers. See [ps2.c](Code/ps2.c), [keyboard.c](Code/keyboard.c), [mouse.c](Code/mouse.c), [vbox_pointer.c](Code/platform/x86_64/vbox_pointer.c), [ata_disk.c](Code/platform/x86_64/ata_disk.c), [rtc.c](Code/platform/x86_64/rtc.c), [audio.c](Code/audio.c) and [network.c](Code/network.c). |
| ARM64 hardware | [platform/aarch64/hal.c](Code/platform/aarch64/hal.c) targets QEMU virt. [virtio_input.c](Code/platform/aarch64/virtio_input.c) and [virtio_disk.c](Code/platform/aarch64/virtio_disk.c) handle input and storage. [vectors.S](Code/platform/aarch64/vectors.S) handles exception entry. [input_events.c](Code/input_events.c) handles portable input event decoding. |

### Trace a desktop click

Start at the platform pointer reader, then follow the desktop event loop into `target_at()` and `click()` in [desktop.c](Code/desktop.c). Check whether a confirmation, Quick Settings, launcher or context menu owns the click before looking at the app. For app controls, follow `apps_click()` into the registered app's callback. For visual glitches, compare the hit rectangle with the rectangle drawn by that callback. Check the active window, maximized state and redraw flag if the click changes state but the screen stays stale.

### Trace a Notes save

Start with the Notes key or button callback, then follow [file_app.c](Code/file_app.c) through the app filesystem service into [files.c](Code/files.c). Check the mounted volume and the returned `FileResult` before investigating drawing. New installations put documents on a native partition of the system disk. The filesystem validates records and alternates between banks when committing file changes. [volumes.c](Code/volumes.c) translates partition-relative sectors before the ATA or Virtio backend performs the I/O. Appearance and volume preferences instead use the firmware variable store, so check that store when preferences fail to persist across cold boots.

## Debug from the last successful checkpoint

| Last message or symptom | Where to start |
| --- | --- |
| Firmware cannot find a boot medium | Check firmware mode, boot order and the attached image. Check the ISO's EFI payload with `tests\verify_iso.ps1` using the matching build executable. |
| Setup starts but rejects a disk | Read the exact installer message and follow target enumeration and validation in `installer.c` and `install_disk.c`. Confirm which disk is the installer and which is the target. |
| `Starting YAAI OS...` appears, then startup stops | Check the memory map and `ExitBootServices` in `boot.c`. Check serial output for the kernel entry marker to distinguish a handoff failure from an early kernel failure. |
| `YAAI OS: kernel entered after ExitBootServices` | The handoff succeeded. Check the next marker in `kernel_main()`, such as allocator initialization or document mounting. |
| `KERNEL: splash complete; memory map received` | Check the desktop buffer and display-size conditions, then follow `desktop_run()` or the shell fallback. |
| `PLATFORM INPUT INITIALIZATION FAILED.` | Check the target backend and the virtual input device. On x86-64, follow `platform_input_initialize()` into keyboard setup and VirtualBox pointer detection. |
| `DESKTOP: modern UI ready` appears but a feature misbehaves | Check the event, hit test and app callback. Look for layout, save, network and power messages after desktop startup. |
| `FILES: no documents volume` or save failure | Check disk attachment, storage controller, GPT and YAAIFS validation. Test on a disk copy if the issue involves writes. |
| `SETTINGS: save failed / session only` | Check firmware runtime services and whether the VM reuses its writable firmware variable store. |

The two QEMU test suites boot test images at 128 MiB and 512 MiB. Their logs are under `Code\build\memory-tests\serial-*.log` and `Code\build\arm64-tests\serial-*.log`. The ARM64 launcher also writes `serial.log` in its generated run folder. For a custom x86 QEMU launch, add `-serial file:yaai-debug.log` to the QEMU arguments to capture guest serial output. Guest console messages can also be read on screen.

To locate a message and add a small checkpoint around a suspected failure:

```powershell
rg -n -F 'PLATFORM INPUT INITIALIZATION FAILED.' .
rg -n 'platform_input_initialize|keyboard_initialize|ps2_initialize' .
```

Use the firmware console while boot services are active and the existing `serial_write()` path in the kernel. Place a marker before and after a suspected call and log its return status. Avoid logging every frame or every iteration of a hardware polling loop. Rebuild, reproduce the same steps and compare the last marker with the previous run.

### Example — Hyper-V reaches setup, then stops on input initialization

The installer accepted input through UEFI. The error originates in the native input setup after the kernel handoff. The x86 backend tries PS/2 keyboard setup and VirtualBox pointer detection. If native input is unavailable, the current build tries COM1 and enters the command shell when it finds a serial port. Hyper-V Generation 2 exposes synthetic input devices. Supporting an interactive Hyper-V desktop requires a Hyper-V input backend. Persistent installation also needs a runtime driver for its virtual storage. Reproduce the same build in VirtualBox or QEMU to check the current desktop while isolating the Hyper-V-specific failure. [Microsoft's Hyper-V generation device comparison](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/plan/should-i-create-a-generation-1-or-2-virtual-machine-in-hyper-v)

## Verify a change before handing it off

- Boot through the feature's usual entry point. For a desktop change, exercise the same click or key the user would use. For a fullscreen change, test both entering and leaving fullscreen.
- Run the affected suite in [Code/tests](Code/tests). Test shared code on both architectures. The firmware test entry points are [memory_boot_test.c](Code/tests/memory_boot_test.c) and [arm_boot_test.c](Code/tests/arm_boot_test.c).
- For installer or update changes, use a scratch disk, capture the boot log, and verify that the system boots again after detaching the installer. For packaging changes, run `tests\iso_export_test.ps1` and use `tests\verify_iso.ps1` to check the matching ISO and EFI payload.
- Keep the source commit and logs with the reproduction. Include the VM backend, firmware mode, disk attachments and last successful marker when reporting a failure.

## Hyper-V COM1 console test

Use the current x86-64 ISO from `Code\export_iso.ps1`. With the Generation 2 test VM off, use an elevated PowerShell to attach COM1 to a local named pipe. The VM name below matches the Hyper-V test VM.

```powershell
Set-VMComPort -VMName 'YAAI OS' -Number 1 -Path '\\.\pipe\yaai-com1'
```

Start the VM and select **Try without installing**. When the guest displays **Using COM1 for input**, connect a named-pipe terminal to `\\.\pipe\yaai-com1` and type `help`. To run the same check from PowerShell without an additional terminal client:

```powershell
$pipe = [IO.Pipes.NamedPipeClientStream]::new('.', 'yaai-com1', [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
try {
    $pipe.Connect(5000)
    $command = [Text.Encoding]::ASCII.GetBytes("help`r")
    $pipe.Write($command, 0, $command.Length)
    $buffer = [byte[]]::new(4096)
    $output = ''
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ($output -notmatch 'HELP  - SHOW COMMANDS') {
        $remaining = [int]($deadline - [DateTime]::UtcNow).TotalMilliseconds
        if ($remaining -le 0) { throw 'Timed out waiting for the YAAI help response.' }
        $read = $pipe.ReadAsync($buffer, 0, $buffer.Length)
        if (!$read.Wait($remaining)) { throw 'Timed out waiting for serial data.' }
        if ($read.Result -eq 0) { throw 'The guest serial pipe closed.' }
        $output += [Text.Encoding]::ASCII.GetString($buffer, 0, $read.Result)
    }
    $output
} finally {
    $pipe.Dispose()
}
```

QEMU verification used the PC machine with `i8042=off`, then sent `help` through COM1 after boot. On Hyper-V, use the named-pipe result to confirm the serial path on the actual host. VMConnect keyboard and mouse input and VHDX storage still require their own guest drivers. [Microsoft's COM port setup instructions](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/plan/should-i-create-a-generation-1-or-2-virtual-machine-in-hyper-v#add-a-com-port-for-kernel-debugging)
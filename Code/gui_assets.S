.section .rdata,"dr"
.balign 16
.globl gui_fonts_start
gui_fonts_start:
.incbin "assets/ui-fonts.bin"
.globl gui_fonts_end
gui_fonts_end:
.balign 16
.globl gui_wallpaper_start
gui_wallpaper_start:
.incbin "assets/wallpaper-modern.rgb"
.globl gui_wallpaper_end
gui_wallpaper_end:
.balign 16
// Retain human-readable font copyright and OFL metadata in the installed image.
.globl gui_font_licenses
gui_font_licenses:
.incbin "assets/font-licenses.txt"
.byte 0

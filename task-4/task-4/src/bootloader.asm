bits 16
org 0x7C00

start:
    mov si, msg
    call print

    mov ah, 0x02          ; BIOS read sectors
    mov al, 2             ; Read 2 sectors
    mov ch, 0             ; Cylinder
    mov cl, 2             ; Sector 2 (boot = 1)
    mov dh, 0             ; Head
    mov dl, 0             ; Drive 0 (floppy)
    mov bx, 0x1000        ; Load to 0x1000
    mov es, bx
    xor bx, bx
    int 0x13              ; BIOS interrupt

    jc disk_error         ; jump if error

    jmp 0x1000:0000       ; Jump to loaded kernel

disk_error:
    mov si, err
    call print
    jmp $

print:
    mov ah, 0x0E
.print_loop:
    lodsb
    or al, al
    jz .done
    int 0x10
    jmp .print_loop
.done:
    ret

msg db "LilHabOS booting...", 0
err db "Disk error!", 0

times 510 - ($ - $$) db 0
dw 0xAA55

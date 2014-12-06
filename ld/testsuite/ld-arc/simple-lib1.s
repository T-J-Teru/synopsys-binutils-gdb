        .section ".data.string", "aw"
        .global unused_string
unused_string:
        .word   message

        .section ".rodata", "a"
message:
        .asciz "Hello World\n"

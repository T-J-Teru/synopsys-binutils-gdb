        .section ".data.string", "aw"
        .hidden unused_string
        .global unused_string
unused_string:
        .word   message

        .section ".rodata", "a"
message:
        .asciz "Hello World\n"

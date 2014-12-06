#as: -mA7 -EL
#objdump: -Dr -EL

.*: +file format elf32-.*arc

Disassembly of section \.text:

00000000 <_foo>:
   0:	00 d8                   	mov_s      r0,0

Disassembly of section \.data:

00000000 <_abc>:
   0:	00 00 00 00             	           
			0: R_ARC_32	\.text
#...
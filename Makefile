# Cross-compiler settings for Ubuntu using Vincent Riviere's cross compiler
# for Mint/TOS. In addition to the basic package you'l also need gemlib
# included in the cflib-m68k-atari-mint package

OBJS_BLITLIFE=life.o blitlife.o

CC=m68k-atari-mint-gcc
CFLAGS=-O3 -fomit-frame-pointer -m68000 -std=gnu99

AS=m68k-atari-mint-as
ASFLAGS=-m68000

LD=m68k-atari-mint-gcc
LDFLAGS=-m68000 -fomit-frame-pointer

all: blitlife.prg

dmp: blitlife.dmp

%.dmp: %.prg
	 m68k-atari-mint-objdump -D $<  > $@

blitlife.prg: $(OBJS_BLITLIFE) Makefile
	$(LD) $(LDFLAGS) $(OBJS_BLITLIFE) -lgem  -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

CC := gcc
#CC := clang

CFLAGS  ?= -std=c99 -pedantic -Wall -Wextra -O3 -s -pipe
LDFLAGS ?=

HDR = *.h
DST = bin/yaft-debug.apk
SRC = tools/mkfont_bdf.c
DESTDIR =
PREFIX  = $(DESTDIR)/usr

all: $(DST)

mkfont_bdf: tools/mkfont_bdf.c tools/font.h tools/bdf.h
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

jni/glyph.h: mkfont_bdf
	./mkfont_bdf table/alias fonts/milkjf_k16.bdf fonts/milkjf_8x16.bdf fonts/milkjf_8x16r.bdf > jni/glyph.h

$(DST): jni/glyph.h
	android update project --path ./ --target android-17
	ndk-build
	ant debug

install:
	adb install -r $(DST)

clean:
	rm -rf mkfont_bdf jni/glyph.h libs/ bin/ obj/ proguard-project.txt  local.properties  project.properties

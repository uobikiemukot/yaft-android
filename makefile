DST = bin/yaft-debug.apk
DESTDIR =
PREFIX  = $(DESTDIR)/usr

all: $(DST)

$(DST):
	android update project --path ./ --target android-17
	ndk-build
	ant debug

install:
	adb install -r $(DST)

clean:
	rm -rf libs/ bin/ obj/ proguard-project.txt local.properties project.properties

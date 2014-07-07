# yaft-android

android port of yaft (yet another framebuffer terminal)

## dependency

maybe you need android-ndk, android-sdk and apache-ant.
here is package list in Archlinux (some packages found in AUR).

-	android-ndk
-	android-sdk
-	android-sdk-build-tools
-	android-sdk-platform-tools
-	android-platform-17
-	apache-ant

## build

before make, check your PATH variable.
(we will use android/ndk-build/ant/adb)

~~~
$ make
~~~

## install

~~~
$ make install
~~~

or use *.apk in apk/

gcc src/*.c -ISTB -ISDL2 -IEGL -IGLESv1_CM -IGLESV2 -Iandroid -I(c4android:GCCROOT)(c4android:PREFIX)/include/SDL2 -WI,--no-undefined -shared -g -O0 -D_DEBUG

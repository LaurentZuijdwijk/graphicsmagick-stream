SRC=$(wildcard src/*.c)

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
LIBS = -L/usr/local/opt/libarchive/lib -I/usr/local/opt/libarchive/include -larchive
else
LIBS = -L/usr/local/lib -larchive
endif


build: $(SRC) Makefile
	cd ./lib && make -C lib
	mkdir -p bin
	gcc $(SRC) ./lib/lib/libimagequant.a  -o bin/convert $(LIBS) -O `GraphicsMagickWand-config --cflags --cppflags --ldflags --libs` 


clean:
	rm -f bin/convert

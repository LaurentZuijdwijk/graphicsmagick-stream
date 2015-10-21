
SRC=$(wildcard src/*.c)

build: $(SRC) Makefile
	cd ./lib && make -C lib

	mkdir -p bin
	gcc $(SRC) ./src/lodepng.h ./lib/lib/libimagequant.a -o bin/convert -L/usr/local/lib -larchive -lpng -O `GraphicsMagickWand-config --cflags --cppflags --ldflags --libs` 

clean:
	rm -f bin/convert

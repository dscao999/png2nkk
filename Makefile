DEFS = -DPNG_SETJMP_NOT_SUPPORTED

CFLAGS += $(DEFS) -g
LDFLAGS += -g

.phony: all

all: png2nkk

png2nkk: nkkpng.o
	$(LINK.o) $^ -lpng -lz -o $@

clean:
	rm -f *.o png2nkk

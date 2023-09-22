.PHONY: clean
CFLAGS  := -Wall -Werror -g
LD      := gcc
LDLIBS  := -lrdmacm -libverbs -lpthread
APPS    := client server
all: ${APPS}
clean:
	rm -f *.o ${APPS}

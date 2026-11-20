EE_BIN = RED_CITY.ELF

EE_OBJS = src/main.o

EE_LIBS = -ldebug

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.eeglobal

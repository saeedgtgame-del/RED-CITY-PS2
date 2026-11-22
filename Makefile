
EE_BIN = RED_CITY.ELF

EE_OBJS = src/main.o src/nexon_engine.o

EE_LIBS = -lpad -ldraw -lgraph -lmath3d -lpacket -ldma -ldebug

EE_INCS += -I$(PS2SDK)/ee/include
EE_INCS += -I$(PS2SDK)/common/include

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

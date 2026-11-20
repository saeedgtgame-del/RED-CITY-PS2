
EE_BIN = RED_CITY.ELF

EE_OBJS = src/main.o

EE_LIBS = -ldebug

all: $(EE_BIN)

src/main.o: src/main.c
	$(EE_CXX) -c $< -o $@

$(EE_BIN): $(EE_OBJS)
	$(EE_CC) -o $@ $(EE_OBJS) $(EE_LDFLAGS) $(EE_LIBS)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.eeglobal

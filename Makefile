CC=ppc-morphos-gcc-11
STRIP=ppc-morphos-strip

CFLAGS=-O3 -Wshadow -Winit-self -Wall -Wno-missing-field-initializers -Wno-unused-result -Wno-strict-aliasing -Wextra -Wunused -Wunreachable-code  -Wno-stringop-overflow -I./src -noixemul -D__STDC_LIMIT_MACROS -D__MORPHOS_SHAREDLIBS -I/gg/usr/local/include -I/gg/usr/local/include/SDL2 -DEXTERNAL_LIBFLAC -DNDEBUG -DHAS_LIBFLAC \


OFILES=	src/gfx/*.c src/modloaders/*.c src/smploaders/*.c src/*.c
OBJECTS = $(shell echo $(OFILES) | sed -e 's,\.c,\.o,g')

APP=pt2-clone
INC=
LINKPATH=

LIBS= -L/gg/usr/local/lib -lSDL2 -lFLAC -lGL -lc -lm -noixemul

all: $(APP)

$(APP): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(LINKPATH) $(LIBS) -o  $(APP)
	$(STRIP) --strip-unneeded --remove-section=.comment $(APP) -o $(APP).strip

.c.o:
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

dump:
	objdump --disassemble-all --reloc $(APP) >$(APP).s
clean:
	rm -rf  $(OBJECTS)
	rm -f $(APP)

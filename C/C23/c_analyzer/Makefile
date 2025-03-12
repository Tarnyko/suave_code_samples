NAME := c_analyzer

CFLAGS := -std=c23 -fPIC

SANITIZE_FLAGS := -fsanitize-trap -fsanitize=undefined

ifeq ($(OS),Windows_NT)
    CFLAGS := $(CFLAGS) -I_deps _deps\getline.c
    EXE := .exe
    DLL := .dll
else
    SANITIZE_FLAGS := $(SANITIZE_FLAGS),address,bounds
    DLL := .so
endif

ifdef DEBUG
	CFLAGS := $(CFLAGS) -g $(SANITIZE_FLAGS)
endif
###


all: $(NAME)$(EXE) plugins

plugins: sample$(DLL)

clean:
	rm -f $(NAME)$(EXE) plugins/*$(DLL)
###


$(NAME)$(EXE): $(NAME).c
	${CC} $(CFLAGS) $(NAME).c -o $(NAME)$(EXE)

sample$(DLL): plugins/sample.c
	${CC} $(CFLAGS) -shared plugins/sample.c -o plugins/sample$(DLL)

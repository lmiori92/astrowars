#!/bin/make

EXE=vfdemu


ifeq ($(PLAT), WIN)
PATH2:=$(PATH)
export PATH:=/usr/i686-w64-mingw32/bin:$(PATH)
SHELL := env PATH=$(PATH) /bin/bash
SDL_CONFIG = $(shell which sdl-config)
PREFIX=i686-w64-mingw32
CC=$(PREFIX)-gcc
EXE=vfdemu.exe
endif

CFLAGS=$(shell sdl-config --cflags)

LIBS=$(shell sdl-config --libs) -lSDL_image -lm


.PHONY: all test vfdemu



all: $(EXE)

test:
	@echo $(SDL_CONFIG)
	@echo $(PATH)
	@echo $(SHELL)

$(EXE): vfd_emu.o caveman.o astrowars.o sonytaax44.o ucom4_cpu.o lib/SDL_rotozoom.o
	$(CC) -ggdb *.o lib/*.o $(LIBS) -o $(EXE)

%.o: %.c $(DEPS)
	$(CC) -ggdb -c -o $@ $< $(CFLAGS)

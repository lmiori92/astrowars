/************************
 *
 * MULTI VFD EMULATOR
 * 
 * (c) 2016 MikeDX
 * 
 * http://github.com/MikeDX/astrowars
 *
 * driver.h
 *
 *************************/


#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <stdio.h>
#include <string.h>
#include "ucom4_cpu.h"

// GAME DRIVER DEFINITION


typedef struct _gamedriver {

	void (*prepare_display)(ucom4cpu *cpu);
	void (*cpu_exec)(int ticks);

	void (*setup_gfx)(void);
	void (*close_gfx)(void);
	void (*display_update)(void);

	uint8_t (*input_r)(ucom4cpu *cpu, int index);
	void (*output_w)(ucom4cpu *cpu, int index, uint8_t data);

	char name[255];

	char rom[255];
	int romsize;
	ucom4cpu *cpu;

} vfd_game;

extern vfd_game *active_game;

#include "astrowars.h"
#include "caveman.h"
#include "sonytaax44.h"

#endif

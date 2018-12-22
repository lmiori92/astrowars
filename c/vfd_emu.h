/************************
 *
 * ASTRO WARS EMULATOR
 * 
 * Should evolve to multi ucom4 / VFD EMU
 *
 * (c) 2016 MikeDX
 * 
 *************************/

#ifndef _VFD_EMU_H_
#define _VFD_EMU_H_

#include "ucom4_cpu.h"
#include <SDL.h>
#include <SDL_image.h>


#ifdef __DEFINED_HERE__
#define GLOBAL extern;
#else
#define GLOBAL
#endif

#define INPUTS_NUM          20

extern SDL_Surface *screen;
void level_w(ucom4cpu *cpu, uint8_t data);
extern uint8_t inputs[INPUTS_NUM];
#endif

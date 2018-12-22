/************************
 *
 * ASTRO WARS EMULATOR
 * 
 * Should evolve to multi ucom4 / VFD EMU
 *
 * (c) 2016 MikeDX
 * 
 *************************/
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#include "vfd_emu.h"
#include "driver.h"

#define FPS 50
#define VOLUME 200

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


SDL_Surface *screen;

vfd_game *active_game;

#define MAX_EVENTS 65535

// RECORD + PLAYBACK

uint8_t inputs[INPUTS_NUM];

uint8_t input_data;
uint8_t old_input_data;

struct input_event {
  uint32_t cycle;
  uint8_t val;
} events[MAX_EVENTS], *pevent = NULL;


extern uint8_t audiobuf[1024];
extern int aindex;
extern int audiosize;

SDL_AudioSpec wanted, obtained;
int sound_pos  = 0;
int last_a = 0;



ucom4cpu cpu;

int load_rom(ucom4cpu *cpu, char *file, int size) 
{
	FILE *f;
	int len;
	int result;

	char rompath[1024];

	strcpy(rompath,"res/");
	strcat(rompath,file);


	f=fopen(rompath,"rb");
	
	if(!f) {
		printf("Failed to open rom [%s]\n", rompath);
		return 0;
	}

	fseek(f, 0, SEEK_END);
	len = ftell(f);
//	printf("ROM [%s]\nLENGTH: [0x%X]\n",file, len);
	if(len!=size) {
		fclose(f);
		return 0;
	}
	fseek(f, 0, SEEK_SET);

	result = fread(cpu->rom,1,len,f);

	fclose(f);
	
	return result;	

}



int totalticks = 0;
int running = 1;

uint32_t next_ms = 0;

SDL_Event event;

uint32_t get_ms() {
	return SDL_GetTicks();
}

void do_inputs(void) {
	
	uint8_t bit;

	while(SDL_PollEvent( &event)) {
		
		bit = 0;

		if(event.type == SDL_KEYDOWN)
			bit=1;

		switch( event.type ) {

			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if(!pevent) {
					switch(event.key.keysym.sym) {

						case SDLK_SPACE: // FIRE
							inputs[0]=bit;
							break;

						case SDLK_LEFT: // LEFT
							inputs[1]=bit;
							break;

						case SDLK_RIGHT: // RIGHT
							inputs[2]=bit;
							break;

						case SDLK_2: // SELECT
							inputs[3]=bit;
							break;

						case SDLK_1: // START
							inputs[4]=bit;
							break;

                        case SDLK_m: // MUTING
                            inputs[5]=bit;
                            break;

                        case SDLK_q: // PHONO
                            inputs[6]=bit;
                            break;
                        case SDLK_w: // TUNER
                            inputs[7]=bit;
                            break;
                        case SDLK_e: // DAD/AUX
                            inputs[8]=bit;
                            break;
                        case SDLK_r: // TAPE-1
                            inputs[9]=bit;
                            break;
                        case SDLK_t: // TAPE-2
                            inputs[10]=bit;
                            break;
                        case SDLK_y: // COPY1-2
                            inputs[11]=bit;
                            break;
                        case SDLK_a: // BASS -
                            inputs[12]=bit;
                            break;
                        case SDLK_z: // BASS +
                            inputs[13]=bit;
                            break;
                        case SDLK_s: // TREBLE -
                            inputs[14]=bit;
                            break;
                        case SDLK_x: // TREBLE +
                            inputs[15]=bit;
                            break;
                        case SDLK_d: // LOW FILTER
                            inputs[16]=bit;
                            break;
                        case SDLK_f: // HIGH FILTER
                            inputs[17]=bit;
                            break;
                        case SDLK_p: // REMOTE
                            inputs[18]=bit;
                            break;
//						case SDLK_q:
//							running = 0;
//							break;

						default:
							break;

					}
				}
				break;
			case SDL_QUIT:
				running = 0;
				break;
			default:
				break;
		}
	}

}

void mainloop(void) {

	int x = 0;

	do_inputs();

// 	if((!pevent && get_ms() <next_ms) || cpu.audio_avail > obtained.samples) {
// #ifdef __EMSCRIPTEN__
// 	if( cpu.audio_avail > obtained.samples && !pevent) {
// //		printf("Audio: %d %d\n",cpu.audio_avail, obtained.samples);
// 		return;
// 	}
// #endif

	if(get_ms() >= next_ms || pevent) {
		next_ms +=1000/FPS;

		input_data = 0;

		for (x=0;x<INPUTS_NUM;x++) {
			input_data |= inputs[x]<<x;
		}

		if(!pevent && input_data!=old_input_data)
			printf("%08x %02x\n", cpu.totalticks, input_data);

		old_input_data = input_data;

		if(pevent) {
			if (cpu.totalticks >= pevent->cycle) {
				for(x=0;x<INPUTS_NUM;x++) {
					inputs[x]=(pevent->val & (1<<x)) ? 1:0;
				}
				++pevent;
			}
		}

		if(pevent && !pevent->cycle) {
			printf("Playback ended\n");
			pevent = NULL;
			next_ms = get_ms();
			cpu.audio_avail = 0;
		}

		// if(pevent) {
		// 	for(x=0;x<5;x++)
		// 		inputs[x]=pinputs[x];
		// }

		totalticks +=ucom4_exec(&cpu, cpu.cpu_rate/FPS);//400000/284);

// #ifndef HAS_SDL
// 		for(x=0;x<cpu.display_maxy;x++) {
// 			for(y=cpu.display_maxx-1;y>=0;y--) {
// 				printf("%d",(cpu.display_cache[x]&1<<y)?1:0);
// 			}
// 			printf("\n");
// 		}
// 		printf("\n");
// #endif

	}

	if(!pevent) {
//		if(get_ms()<next_ms+1000/FPS)
			active_game->display_update();	
	}
}

void level_w(ucom4cpu *cpu, uint8_t data) {
	data *=VOLUME;
	cpu->audio_level = data;
}


void fill_audio(void *udata, Uint8 *stream, int len)
{

	int z;

    len = ( len > cpu.audio_avail ? cpu.audio_avail : len );

	for(z=0;z<len;z++) {
		stream[z] = audiobuf[sound_pos];//(rand()*1)*255;//(uint8_t)audiobuf[a];//*sin(F*(double)z); 
		audiobuf[sound_pos]=0;
		sound_pos++;
		if(sound_pos>=10240)
			sound_pos=0;
	}

	cpu.audio_avail -= len;

//    SDL_MixAudio(stream, sbuf, len, SDL_MIX_MAXVOLUME);

}

int init_sound(void) {

    /* Set the audio format */
    cpu.sound_frequency = 44100;

    wanted.freq = cpu.sound_frequency;
    wanted.format = AUDIO_U8;
    wanted.channels = 1;    /* 1 = mono, 2 = stereo */
    wanted.samples = 512;   /* Good low-latency value for callback */
    wanted.callback = fill_audio;
    wanted.userdata = NULL;

    /* Open the audio device, forcing the desired format */
    if ( SDL_OpenAudio(&wanted, &obtained) < 0 ) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        return(-1);
    }
    cpu.sound_frequency = obtained.freq;

    return(0);
}

void cleanup(void) {
	active_game->close_gfx();
	SDL_CloseAudio();
	SDL_Quit();

}


int main(int argc, char *argv[])
{

	SDL_Init(SDL_INIT_EVERYTHING);
	init_sound();
	memset(audiobuf,0,sizeof(audiobuf));

	atexit(cleanup);

	active_game = &game_sonytaax44;
//	active_game = &game_astrowars;
//	active_game = &game_caveman;

	if(argc>1) {
		if(!strcmp(argv[1],"caveman")) {
			active_game = &game_caveman;
			argv++;
			argc--;
		}
	}

	if(argc>1) {
		FILE *f = fopen(argv[1],"r");
		if(!f) {
			printf("Cannot open replay file\n");
			return (-1);
		}

    	for ( pevent = events ; 2 == fscanf(f, "%x %hhx", &(pevent->cycle), &(pevent->val)) ; pevent++ );
	
    	fclose(f);

    	printf("replaying %lu events\n", pevent - events);
    	pevent->cycle = 0;
    	pevent = events;
    	argc--,
    	argv++;

	}

	next_ms = get_ms();

	cpu.cpu_rate = 100000;

	active_game->setup_gfx();

	active_game->cpu = &cpu;

	ucom4_reset(&cpu);
	if(load_rom(&cpu, active_game->rom, active_game->romsize)!=active_game->romsize) {
		printf("Failed to load astrowars.rom\n");
		return -1;
	}

	// active_game->cpu->rom[0x768]=0x0;
	// active_game->cpu->rom[0x769]=0x0;
//	active_game->cpu->rom[0x18]=0x48;
//	active_game->cpu->rom[0x769]=0x0;

	// active_game->cpu->rom[0x0]=0x0;
	// active_game->cpu->rom[0x1]=0x0;
	// active_game->cpu->rom[0x2]=0x0;
	// active_game->cpu->rom[0x3]=0x0;


#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(mainloop,0,1);
#else
	while(running) {
		mainloop();
	}

	SDL_Quit();
#endif
	return 0;
}

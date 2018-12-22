#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>
#include "driver.h"
#include "vfd_emu.h"
#include "astrowars.h"

#include "lib/SDL_rotozoom.h"

#define TAAX44_GRID_A       (0)
#define TAAX44_GRID_B       (1)
#define TAAX44_GRID_C       (2)
#define TAAX44_GRID_D       (3)
#define TAAX44_GRID_E       (4)
#define TAAX44_GRID_F       (5)

vfd_game game_sonytaax44 = {
	.prepare_display    = sonytaax44_prepare_display,
	.rom                = "D553C-200.rom",
	.romsize            = 0x800,
	.setup_gfx          = sonytaax44_setup_gfx,
	.close_gfx          = sonytaax44_close_gfx,
	.display_update     = sonytaax44_display_update,
	.input_r            = sonytaax44_input_r,
	.output_w           = sonytaax44_output_w,
	.name               = "sonytaax44"
};

SDL_Surface *gfx[10][15];
SDL_Surface *bg,*bezel,*vfd_display, *tmpscreen;

int gfx_x[50][50];
int gfx_y[50][50];

typedef struct
{
    bool        clock_old;
    bool        strobed;
    uint32_t    data;
} t_asp_processor;

typedef struct
{
    bool        clock_old;
    bool        clock_data;
    uint8_t     mode;
    uint8_t     address;
    uint16_t    data;
    uint16_t    cells[16];
} t_NVRAM;

#define NVRAM_SB                (0U)
#define NVRAM_RTNS              (1U)
#define NVRAM_WTNS              (2U)
#define NVRAM_WRT               (3U)
#define NVRAM_MSTNS             (4U)
#define NVRAM_ERS               (5U)
#define NVRAM_READ              (6U)
#define NVRAM_MCTNS             (7U)

t_asp_processor ASP;
t_NVRAM         NVRAM;

void asp_process(t_asp_processor *asp, bool strobe, bool clock, bool bit)
{
    if (asp != NULL)
    {
        if ((asp->clock_old == false) && (clock == true))
        {
            /* Sample data at clock rising edge */
            asp->data = (uint32_t)(asp->data << 1) | (uint32_t)bit;
        }
        asp->clock_old = clock;
        asp->strobed   = strobe;
    }
}

void asp_print_strobed(t_asp_processor *asp)
{
    if (asp != NULL)
    {
        if (asp->strobed == true)
        {
            printf("ASP data received %08X\n", asp->data);
            /* reset strobe state and zero the received data after usage */
            asp->strobed = false;
            asp->data = 0;
        }
    }
}

void NVRAM_store(t_NVRAM *nvram, char* filename)
{
    FILE *f = NULL;
    size_t retval;
    f = fopen(filename, "wb");
    if (f != NULL)
    {
        retval = fwrite(nvram->cells, sizeof(nvram->cells), 1, f);
        if (retval != 1)
        {
            printf("ERROR: writing NVRAM to disk failed.");
        }
        fclose(f);
    }
}

void NVRAM_load(t_NVRAM *nvram, char* filename)
{
    FILE *f = NULL;
    size_t retval;
    f = fopen(filename, "rb");
    if (f != NULL)
    {
        retval = fread(nvram->cells, sizeof(nvram->cells), 1, f);
        if (retval != 1)
        {
            printf("ERROR: reading NVRAM from disk failed.");
        }
        fclose(f);
    }
}

void NVRAM_process(t_NVRAM *nvram, uint8_t clock, uint8_t data_in, uint8_t *data_out)
{
    if ((nvram->mode != NVRAM_SB) && (clock != 0xFF))
    {
        /* Process the clock signal if not in standby mode and clock signal available */
        if ((nvram->clock_old == false) && (clock == true))
        {
            /* Clock data in or out now or the next input cycle */
            nvram->clock_data = true;
        }
        nvram->clock_old = clock;
    }

    switch(nvram->mode)
    {
        case NVRAM_SB   :
            /* Standby */
            nvram->clock_old = false;
            break;
        case NVRAM_RTNS :
            /* Information of the data register relayed by the
             * READ operation are put out from the D I/O terminal */
            if ((data_out != NULL) && (nvram->clock_data == true))
            {

                *data_out = nvram->data & 0x01; /* write the data out */
                nvram->data >>= 1;              /* shift the data for next round */
                nvram->clock_data = false;      /* acknowledge clock cycle */
            }
            break;
        case NVRAM_WTNS :
            /* Informations to be memorized are relayed to the data
             * register from the D I/O terminal
             */
            if ((data_in != 255) && (nvram->clock_data == true))
            {
                nvram->data <<= 1;
                nvram->data |= data_in;
                nvram->clock_data = false;      /* acknowledge clock cycle */
            }
            break;
        case NVRAM_WRT  :
            /* Memorize the informations relayed by the WTNS operation
             * in the designated address
             */
            printf("write %d \n", nvram->data);
            nvram->cells[nvram->address] = 0;

            for (int i = 0; i < 16; i++)
            {
                nvram->cells[nvram->address] |= ((nvram->data >> i) & 0x01) << (15-i);
            }

            NVRAM_store(nvram, "NVRAM.bin");
            break;
        case NVRAM_ERS  :
            /* Clears the information memorized in the designated
             * address
             */
            nvram->cells[nvram->address] = 0x00;
            break;
        case NVRAM_READ :
            /* Relay the memorized informations in the designated
             * address to the data register
             */
            nvram->data = nvram->cells[nvram->address];
            printf("read %d \n", nvram->data);
            break;
        case NVRAM_MSTNS:
            /* The control signals which follow the MSTNS operation are
             * processed in accordance with the station memory
             */
            break;
        case NVRAM_MCTNS:
            /* The control signals which follow the MCTNS operation
             * are processed in accordance with the last channel memory
             */
            break;
        default:
            break;
    }
}

void NVRAM_MODE_process(t_NVRAM *nvram, uint8_t mode)
{
    char *mode_name = "<INVALID>";
    char *mode_names[] = {"SB:    STANDBY",
                          "RTNS:  DATA REG TO OUTPUT (READ)",
                          "WTNS:  INPUT TO DATA REG (WTNS)",
                          "WRT:   MEMORIZE INFORMATION TO ADDRESS",
                          "MSTNS: STATION MEMORY",
                          "ERS:   ERASE ADDRESS",
                          "READ:  READ INFO FROM ADDRESS TO DATA REG",
                          "MCTNS: LAST CHANNEL MEMORY"};

    if (nvram != NULL)
    {
        if (mode != nvram->mode)
        {
            if (mode < sizeof(mode_names))
            {
                mode_name = mode_names[mode];
            }
            printf("NVRAM MODE %d (%s)\n", mode, mode_name);
            nvram->mode = mode;
        }
    }
}

void NVRAM_ADDR_process(t_NVRAM *nvram, uint8_t address)
{
    if (nvram != NULL)
    {
        //if (address != nvram->address)
        {
            printf("NVRAM ADDR %d\n", address);
            nvram->address = address;
        }
    }
}

void sonytaax44_close_gfx(void) {
	int x,y;

	for(x=0;x<15;x++) {
		for(y=0;y<10;y++) {
			if(gfx[x][y]) {
				SDL_FreeSurface(gfx[x][y]);
				gfx[x][y]=NULL;
			}
		}
	}
	SDL_FreeSurface(bg);
	SDL_FreeSurface(bezel);
	SDL_FreeSurface(vfd_display);
	SDL_FreeSurface(tmpscreen);
}

#define BEZEL 0

#define VOLUME_BAR_SIZE_X       (10)
#define VOLUME_BAR_SIZE_Y       (40)

void sonytaax44_load_volume_bar(uint8_t index, uint8_t x, uint8_t y)
{
    gfx_x[y][x] = 20 + (index * VOLUME_BAR_SIZE_X) + (index * 10);
    gfx_y[y][x] = 100;
    gfx[y][x] = IMG_Load("res/gfx/sonytaax44/VOLUME_BAR.png");
}

void sonytaax44_load_generic(char *filename, uint8_t x, uint8_t y, uint16_t posx, uint16_t posy)
{
    gfx_x[y][x] = posx;
    gfx_y[y][x] = posy;
    gfx[y][x] = IMG_Load(filename);
}

void sonytaax44_setup_gfx(void) {
	int x = 0;
	int y = 0;
	char filename[255];
	
	/* Load the content of the NVRAM */
	NVRAM_load(&NVRAM, "NVRAM.bin");

	IMG_Init(IMG_INIT_PNG);
	SDL_Rect rect;
	memset(gfx_x,0,sizeof(gfx_x));
	memset(gfx_y,0,sizeof(gfx_y));

//	bg=IMG_Load("res/gfx/astrowars/bg3.png");

//	bezel=IMG_Load("res/gfx/astrowars/bezel.png");

//	if(BEZEL)
//		tmpscreen=IMG_Load("res/gfx/astrowars/bezel.png");
//	else
//		tmpscreen = bg;

//	screen = SDL_SetVideoMode(tmpscreen->w,tmpscreen->h,32,SDL_HWSURFACE);
	tmpscreen = screen = SDL_SetVideoMode(800,450,32,SDL_HWSURFACE);

	vfd_display = screen;

	//vfd_display=IMG_Load("res/gfx/astrowars/bg3.png");

	sonytaax44_load_volume_bar(0, 1, TAAX44_GRID_C);
    sonytaax44_load_volume_bar(1, 2, TAAX44_GRID_C);
    sonytaax44_load_volume_bar(2, 3, TAAX44_GRID_C);
    sonytaax44_load_volume_bar(3, 4, TAAX44_GRID_C);

    sonytaax44_load_volume_bar(4, 5, TAAX44_GRID_C);
    sonytaax44_load_volume_bar(5, 6, TAAX44_GRID_C);
    sonytaax44_load_volume_bar(6, 7, TAAX44_GRID_C);

    // WRONG START
    sonytaax44_load_volume_bar(7, 8, TAAX44_GRID_D + 2);
    sonytaax44_load_volume_bar(8, 9, TAAX44_GRID_D + 2);
    // WRONG END
    sonytaax44_load_volume_bar(9, 7, TAAX44_GRID_D);
    sonytaax44_load_volume_bar(10, 6, TAAX44_GRID_D);
    sonytaax44_load_volume_bar(11, 5, TAAX44_GRID_D);
    sonytaax44_load_volume_bar(12, 4, TAAX44_GRID_D);

    sonytaax44_load_volume_bar(13, 3, TAAX44_GRID_D);
    sonytaax44_load_volume_bar(14, 2, TAAX44_GRID_D);
    sonytaax44_load_volume_bar(15, 1, TAAX44_GRID_D);
    sonytaax44_load_volume_bar(16, 0, TAAX44_GRID_D);

    sonytaax44_load_generic("res/gfx/sonytaax44/VOLUME.png", 0, TAAX44_GRID_C, 20, 25);
    sonytaax44_load_generic("res/gfx/sonytaax44/BALANCE.png", 10, TAAX44_GRID_F, 20, 50);
    sonytaax44_load_generic("res/gfx/sonytaax44/MUTING.png", 10, TAAX44_GRID_A, 325, 20);

	// GRID 0
	// VOLUME BARS
	for (x = 0; x < 17; x++)
	{
	    // Prepare positions
//	    gfx_x[VOLUME_BAR_GRID][x] = 20 + (x * VOLUME_BAR_SIZE_X) + (x * 10);
//	    gfx_y[VOLUME_BAR_GRID][x] = 10;
//
//	    // Repeat for testing
//        gfx_x[VOLUME_BAR_GRID+1][x] = 20 + (x * VOLUME_BAR_SIZE_X) + (x * 10);
//        gfx_y[VOLUME_BAR_GRID+1][x] = 10;
//        gfx_x[VOLUME_BAR_GRID+2][x] = 20 + (x * VOLUME_BAR_SIZE_X) + (x * 10);
//        gfx_y[VOLUME_BAR_GRID+2][x] = 10;
//        gfx_x[VOLUME_BAR_GRID+3][x] = 20 + (x * VOLUME_BAR_SIZE_X) + (x * 10);
//        gfx_y[VOLUME_BAR_GRID+3][x] = 10;
//
//	    // Load the GFX resource
//	    sprintf(filename,"res/gfx/sonytaax44/%d-%d.png", VOLUME_BAR_GRID, (x+1));
//	    gfx[VOLUME_BAR_GRID][x] = IMG_Load(filename);
//        gfx[VOLUME_BAR_GRID+1][x] = IMG_Load(filename);
//        gfx[VOLUME_BAR_GRID+2][x] = IMG_Load(filename);
//        gfx[VOLUME_BAR_GRID+3][x] = IMG_Load(filename);

//        if(gfx[VOLUME_BAR_GRID][x] != NULL)
//        {
//            rect.x=gfx_x[VOLUME_BAR_GRID][x];
//            rect.y=gfx_y[VOLUME_BAR_GRID][x];
//            rect.w=gfx[VOLUME_BAR_GRID][x]->w;
//            rect.h=gfx[VOLUME_BAR_GRID][x]->h;
//            SDL_BlitSurface(gfx[VOLUME_BAR_GRID][x], NULL, screen, &rect);
//
//            rect.x=gfx_x[VOLUME_BAR_GRID+1][x];
//            rect.y=gfx_y[VOLUME_BAR_GRID+1][x];
//            rect.w=gfx[VOLUME_BAR_GRID+1][x]->w;
//            rect.h=gfx[VOLUME_BAR_GRID+1][x]->h;
//            SDL_BlitSurface(gfx[VOLUME_BAR_GRID+1][x], NULL, screen, &rect);
//
//            rect.x=gfx_x[VOLUME_BAR_GRID+2][x];
//            rect.y=gfx_y[VOLUME_BAR_GRID+2][x];
//            rect.w=gfx[VOLUME_BAR_GRID+2][x]->w;
//            rect.h=gfx[VOLUME_BAR_GRID+2][x]->h;
//            SDL_BlitSurface(gfx[VOLUME_BAR_GRID+2][x], NULL, screen, &rect);
//
//            rect.x=gfx_x[VOLUME_BAR_GRID+3][x];
//            rect.y=gfx_y[VOLUME_BAR_GRID+3][x];
//            rect.w=gfx[VOLUME_BAR_GRID+3][x]->w;
//            rect.h=gfx[VOLUME_BAR_GRID+3][x]->h;
//            SDL_BlitSurface(gfx[VOLUME_BAR_GRID+3][x], NULL, screen, &rect);
//        }
	}


    
//	for(x=0;x<15;x++)
//	{
//		for(y=0;y<10;y++)
//		{
//			sprintf(filename,"res/gfx/astrowars/%d.%d.png", y, x);
//
//			gfx[y][x]=IMG_Load(filename);
//
//			if(gfx[y][x]) {
//				rect.x=gfx_x[y][x];
//				rect.y=gfx_y[y][x];
//				rect.w=gfx[y][x]->w;
//				rect.h=gfx[y][x]->h;
//				SDL_BlitSurface(gfx[y][x],NULL, vfd_display,&rect);
//			}
//		}
//	}
	
	//SDL_BlitSurface(vfd_display, NULL, screen, NULL);

	SDL_Flip(screen);
}

void sonytaax44_display_update(void) {
	int x,y;
	SDL_Rect rect;
	SDL_Surface *tmp;

	SDL_FillRect(tmpscreen, NULL, SDL_MapRGB(tmpscreen->format, 0,0,0));

	SDL_FillRect(vfd_display, NULL, SDL_MapRGB(vfd_display->format, 0,0,0));

	for(x=0;x<12;x++) {
		for(y=0;y<6;y++)
		{
			if(gfx[y][x] && (active_game->cpu->display_cache[y]&1<<x))
			{
				rect.x=gfx_x[y][x];
				rect.y=gfx_y[y][x];
				rect.w=gfx[y][x]->w;
				rect.h=gfx[y][x]->h;
				SDL_BlitSurface(gfx[y][x],NULL, vfd_display,&rect);
			}
		}
	}

//	SDL_UnlockSurface( vfd_display );

//	SDL_LockSurface( screen );

if(BEZEL) {
	rect.x=192;
	rect.y=84;
	rect.w=274-182;
	rect.h=362-84;

	tmp = rotozoomSurface(vfd_display, 0, .35,1);//rect.w/vfd_display->w,1);

	SDL_BlitSurface(tmp, NULL, tmpscreen, &rect);

	SDL_FreeSurface(tmp);

	SDL_BlitSurface(bezel, NULL, tmpscreen, NULL);

	SDL_BlitSurface(tmpscreen, NULL, screen, NULL);

//	SDL_UnlockSurface( screen );
	} else {
		SDL_BlitSurface(vfd_display,NULL,screen,NULL);
	}
	SDL_Flip(screen);
	SDL_PauseAudio(0);

}

void sonytaax44_prepare_display(ucom4cpu *cpu) {
	//uint16_t grid = BITSWAP16(cpu->grid,15,14,13,12,11,10,0,1,2,3,4,5,6,7,8,9);
	//uint16_t plate = BITSWAP16(cpu->plate,15,3,2,6,1,5,4,0,11,10,7,12,14,13,8,9);

    static uint32_t asd = 0;
    if (cpu->grid)
    {
//        asd++;
    }

    static int counter = 0;

//    printf("plate %d; grid %d\n", cpu->plate, cpu->grid);

    ucom4_display_matrix(cpu, 13, 6, cpu->plate, cpu->grid);

}

void sonytaax44_output_w(ucom4cpu *cpu, int index, uint8_t data)
{
	index &= 0xf;
	data &= 0xf;

	int shift;

//	if (index == NEC_UCOM4_PORTC)
//	{
//	    printf("PORTC;%d;%d;%d;%d\n", (data & 0x1),
//	                                  ((data >> 1) & 0x1),
//	                                  ((data >> 2) & 0x1),
//	                                  ((data >> 3) & 0x1)
//	            );
//	}
//    if (index == NEC_UCOM4_PORTD)
//    {
//        printf("PORTD;%d;%d;%d;%d\n", (data & 0x1),
//                                     ((data >> 1) & 0x1),
//                                     ((data >> 2) & 0x1),
//                                     ((data >> 3) & 0x1)
//                );
//    }
//    if (index == NEC_UCOM4_PORTF)
//    {
//        printf("PORTF;%d;%d;%d;%d\n", (data & 0x1),
//                                          ((data >> 1) & 0x1),
//                                          ((data >> 2) & 0x1),
//                                          ((data >> 3) & 0x1)
//                );
//    }
//    if (index == NEC_UCOM4_PORTG)
//    {
//        printf("PORTG;%d;%d;%d;%d\n", (data & 0x1),
//                                          ((data >> 1) & 0x1),
//                                          ((data >> 2) & 0x1),
//                                          ((data >> 3) & 0x1)
//                );
//    }

	if(index == NEC_UCOM4_PORTI)
		data &=0x7;
	
	switch (index) {
		case NEC_UCOM4_PORTC:
		case NEC_UCOM4_PORTD:
			// E3: speaker out
			//if (index == NEC_UCOM4_PORTE)
			//	level_w(cpu, data >> 3 & 1);

			// C,D,E01: vfd matrix grid

			shift = (index - NEC_UCOM4_PORTC) * 4;
			cpu->plate = (cpu->plate & ~(0xf << shift)) | (data << shift);
			active_game->prepare_display(cpu);
			//printf("plate CD %d\n", cpu->plate);
			break;
		case NEC_UCOM4_PORTF:

		    if ((data >> 0) & 0x1)
		    {
		        // VOLUME 8
		        //printf("data0\n");
		    }
		    if ((data >> 1) & 0x1)
            {
                //printf("data1\n");
            }

		    /* only the first 3 bits are plates */
            shift = 2 * 4;
            cpu->plate = (cpu->plate & ~(0x7 << shift)) | ((data & 0x7) << shift);
            //printf("plate F %d\n", cpu->plate);

            /* last bit is a GRID */
            //shift = (index - NEC_UCOM4_PORTG) * 4;
            cpu->grid = (cpu->grid & ~(0x1 << 4)) | (((data >> 3) & 0x1) << 4);

            /* Prepare the display view */
            active_game->prepare_display(cpu);
            //printf("F GRID %d\n", cpu->grid);
		    break;
		case NEC_UCOM4_PORTG:
		    // Grid outputs
            shift = (index - NEC_UCOM4_PORTG) * 4;

		    if (data == 12)
		    {
		        //cpu->grid = (cpu->grid & ~(0x3 << 5)) | (0x1 << 5);
		        data = 1;
		        shift=5;
		    }
		    else
		    {
	            cpu->grid = (cpu->grid & ~(1 << 5)) | (1 << shift);
		    }

            cpu->grid = (cpu->grid & ~(0xf << shift)) | (data << shift);

            //printf("G GRID %d\n", cpu->grid);

            active_game->prepare_display(cpu);

		    break;
		case NEC_UCOM4_PORTH:
		    /* Address port, 3-bits */
            NVRAM_ADDR_process(&NVRAM, data);
            break;
		case NEC_UCOM4_PORTI:
		    /* Mode decoder port, 3-bits */
            NVRAM_MODE_process(&NVRAM, data & 0x7);
			break;
		case NEC_UCOM4_PORTE:
		    if ((data >> 3) & 0x1)
		    {
		        static int relay_drive_act = 0;
		        /* Relay Drive Active: do the initial interrupt */
		        if (relay_drive_act != ((data >> 3) & 0x1))
		        {
		            printf("Relay Drive Activated\n");
		            relay_drive_act = ((data >> 3) & 0x1);
		        }
		    }

            /* ASP clock pin; ASP data pin */
            asp_process(&ASP, (bool)(data & 0x01), (bool)((data >> 2) & 0x01), (bool)((data >> 1) & 0x01));
            asp_print_strobed(&ASP);

            /* NVRAM (when not in standby): gets data input from MCU */
            NVRAM_process(&NVRAM, (uint8_t)((data >> 2) & 0x01), (uint8_t)((data >> 1) & 0x01), NULL);

		    break;
		default:
			//printf("Write to unknown port: %d\n",index);
			break;

	}
	cpu->port_out[index] = data;
}

uint8_t sonytaax44_input_r(ucom4cpu *cpu, int index)
{
	index &= 0xf;
	uint8_t inp = 0;
	uint8_t nvram_bit;

	// try all "highs"
	//inp = 0xF;

	switch (index)
	{
		case NEC_UCOM4_PORTA:

		    //inp &= ~(1 << 1);               /* TEST: normal operation is "LOW" (0V)*/



		    inp = 0x0;      // mettendo a F cambia il comportamento della PORT-C
		                    // e sul 024 incoinciano a muoversi ciclicamente diverse uscite
		    //inp |= inputs[18];
		    inp = inputs[18] << 1;
            /* NVRAM (when not in standby): produces data for the MCU */
            NVRAM_process(&NVRAM, 255, 255, &nvram_bit);
            inp |= nvram_bit << 2;

			break;
		case NEC_UCOM4_PORTB:

		    if ((cpu->grid >> TAAX44_GRID_B) & 0x01)
		    {
		        inp = inputs[2] << 1;               /* Volume Up;...;... matrix */
                inp |= inputs[1] & 0x01;            /* Volume Dw;...;... matrix */
                inp |= inputs[5] << 2;              /* MUTING */
		    }
		    else if ((cpu->grid >> TAAX44_GRID_C) & 0x01)
		    {
                inp  = inputs[16] << 0;             /* subsonic filter */
                inp |= inputs[17] << 1;             /* high filter */
                inp |= inputs[3] << 2;              /* Balance R;...;... matrix */
                inp |= inputs[4] << 3;              /* Balance L;...;... matrix */
		    }
		    else if ((cpu->grid >> TAAX44_GRID_A) & 0x01)
		    {
                inp  = inputs[9] << 0;              /* TAPE 1 */
                inp |= inputs[10] << 1;              /* TAPE 2 */
                inp |= inputs[11] << 2;              /*  TAPE 1-2 COPY */
		    }
		    else if ((cpu->grid >> TAAX44_GRID_E) & 0x01)
		    {
                inp  = inputs[6] << 0;              /* TUNER */
                inp |= inputs[7] << 1;              /* TUNER */
                inp |= inputs[8] << 2;              /* DAD/AUX */
		    }
            else if ((cpu->grid >> TAAX44_GRID_D) & 0x01)
            {
                inp  = inputs[12] << 0;              /* bass - */
                inp |= inputs[13] << 1;              /* bass + */
                inp |= inputs[14] << 2;              /* treble - */
                inp |= inputs[15] << 3;              /* treble + */
            }

			break;

		case NEC_UCOM4_PORTC:
            //inp = 0xF;
		    break;

        case NEC_UCOM4_PORTD:
            //inp = 0xF;
            break;

	}

	return inp & 0xf;
}



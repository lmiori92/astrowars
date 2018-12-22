#include "driver.h"
#include "ucom4_cpu.h"
#include <stdint.h>

extern vfd_game game_sonytaax44;

void sonytaax44_prepare_display(ucom4cpu *cpu);
void sonytaax44_setup_gfx(void);
void sonytaax44_display_update(void);
void sonytaax44_output_w(ucom4cpu *cpu, int index, uint8_t data);
uint8_t sonytaax44_input_r(ucom4cpu *cpu, int index);
void sonytaax44_close_gfx(void);

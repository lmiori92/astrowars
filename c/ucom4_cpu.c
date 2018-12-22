/************************
 *
 * UCOM4 CPU EMULATOR
 * 
 * Should evolve to multi ucom4 / VFD EMU
 *
 * (c) 2016 MikeDX
 * 
 * Thanks to MAME Source
 *
 *************************/

#include "driver.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define false 0
#define true  1

int audiosize = 10240;
uint8_t audiobuf[10240];
int aindex = 0;

void push_stack(ucom4cpu *cpu);

void ucom4_reset(ucom4cpu *cpu) {
	cpu->pc         = 0;
	cpu->tc         = 0;
	cpu->acc        = 0;
	cpu->carry_f    = 0;
	cpu->dpl        = 0;
	cpu->skip       = 0;
	cpu->int_f      = 0;
	cpu->inte_f     = 0;
	cpu->icount     = 0;
	cpu->old_icount = 0;
	cpu->bitmask    = 0;
	cpu->prgmask    = 0x7FF;
	cpu->datamask   = 0x7F;
	cpu->family     = NEC_UCOM43;
	cpu->timer_f    = 0;
	cpu->stack_levels = 3;
	memset(cpu->ram,0,sizeof(cpu->ram));
	memset(cpu->port_out,0,sizeof(cpu->port_out));
	memset(cpu->display_state,0,sizeof(cpu->display_state));
	memset(cpu->display_cache,~0,sizeof(cpu->display_cache));
	memset(cpu->display_decay,0,sizeof(cpu->display_decay));
	memset(cpu->display_segmask,0,sizeof(cpu->display_segmask));
	
	//memset(cpu->port, 0, sizeof(cpu->port));

//	cpu->imp_mix = 0;
	
	cpu->plate = 0;
	cpu->grid = 0;
	cpu->display_wait = 33;
	cpu->decay_ticks = 0;
	cpu->totalticks = 0;
	cpu->audio_avail = 0;
}

void do_interrupt(ucom4cpu *cpu) {
    /* Added interrupt routine:
     * - push onto stack
     * - jump to interrupt vector */
    cpu->icount--;
    push_stack(cpu);
    cpu->pc = 0xf << 2;
    cpu->int_f = 0;
    cpu->inte_f = (cpu->family == NEC_UCOM43) ? 0 : 1;
}


void increment_pc(ucom4cpu *cpu)
{
	// upper bits (field register) don't auto-increment
	cpu->pc = (cpu->pc & ~0xff) | ((cpu->pc + 1) & 0xff);
}

void fetch_arg(ucom4cpu *cpu)
{
	// 2-byte opcodes: STM/LDI/CLI/CI, JMP/CAL, OCD
	if ((cpu->op & 0xfc) == 0x14 || (cpu->op & 0xf0) == 0xa0 || cpu->op == 0x1e)
	{
		cpu->icount--;
		cpu->arg = cpu->rom[cpu->pc];
		increment_pc(cpu);
	}
}

void read_op(ucom4cpu *cpu) {
	cpu->op = cpu->rom[cpu->pc];	
}


// internal helpers

uint8_t ram_r(ucom4cpu *cpu)
{
	uint16_t address = cpu->dph << 4 | cpu->dpl;
	return cpu->ram[address & cpu->datamask] & 0xf;
}

void ram_w(ucom4cpu *cpu, uint8_t data)
{
	uint16_t address = cpu->dph << 4 | cpu->dpl;
	cpu->ram[address & cpu->datamask]= data & 0xf;
}

void pop_stack(ucom4cpu *cpu)
{
	cpu->pc = cpu->stack[0] & cpu->prgmask;
	for (int i = 0; i < cpu->stack_levels-1; i++)
		cpu->stack[i] = cpu->stack[i+1];
}

void push_stack(ucom4cpu *cpu)
{
	for (int i = cpu->stack_levels-1; i >= 1; i--)
		cpu->stack[i] = cpu->stack[i-1];
	cpu->stack[0] = cpu->pc;
}

void ucom4_display_decay(ucom4cpu *cpu);

void ucom4_display_update(ucom4cpu *cpu)
{
	uint32_t active_state[0x20];
	uint32_t ds;
	int decay_time = 80;

	// handle decay

	while(cpu->decay_ticks >= decay_time) {
		cpu->decay_ticks -= decay_time;
		ucom4_display_decay(cpu);
	}
	for (int y = 0; y < cpu->display_maxy; y++)
	{
		active_state[y] = 0;

		for (int x = 0; x <= cpu->display_maxx; x++)
		{
			// turn on powered segments
			if (cpu->display_state[y] >> x & 1)
				cpu->display_decay[y][x] = cpu->display_wait;

			// determine active state
			ds = (cpu->display_decay[y][x] != 0) ? 1 : 0;
			active_state[y] |= (ds << x);
		}
	}

// 	// on difference, send to output
// 	for (y = 0; y < cpu->display_maxy; y++) {
// 		if (cpu->display_cache[y] != active_state[y])
// 		{
// 			// if (cpu->display_segmask[y] != 0)
// 			// 	output().set_digit_value(y, active_state[y] & m_display_segmask[y]);

// 			mul = (cpu->display_maxx <= 10) ? 10 : 100;
// 			for (int x = 0; x <= cpu->display_maxx; x++)
// 			{
// 				int state = active_state[y] >> x & 1;
// 				char buf1[0x10]; // lampyx
// 				char buf2[0x10]; // y.x

// 				if (x == cpu->display_maxx)
// 				{
// 					// always-on if selected
// 					sprintf(buf1, "lamp%da", y);
// 					sprintf(buf2, "%d.a", y);
// //					printf("%s=%d %s=%d\n",buf1, state, buf2, state);
// 				}
// 				else
// 				{
// 					sprintf(buf1, "lamp%d", y * mul + x);
// 					sprintf(buf2, "%d.%d", y, x);
// 				}
// 				// output().set_value(buf1, state);
// 				// output().set_value(buf2, state);
// //				printf("[%d] ",state);
// 			}
// //			printf("\n");
// 		}
// //		printf("\n");
// 	}
	memcpy(cpu->display_cache, active_state, sizeof(cpu->display_cache));
}


void ucom4_display_decay(ucom4cpu *cpu) {
	int x,y;

	for (y = 0; y < cpu->display_maxy; y++)
		for (x = 0; x <= cpu->display_maxx; x++)
			if (cpu->display_decay[y][x] != 0)
				cpu->display_decay[y][x]--;

//	display_update(cpu);

}


void set_display_size(ucom4cpu *cpu, int maxx, int maxy)
{
	cpu->display_maxx = maxx;
	cpu->display_maxy = maxy;
}

void ucom4_display_matrix(ucom4cpu *cpu, int maxx, int maxy, int setx, int sety) {
	uint32_t mask = (1 << maxx) - 1;
	int y;
	set_display_size(cpu, maxx, maxy);

	// update current state
	for (y = 0; y < maxy; y++)
		cpu->display_state[y] = (sety >> y & 1) ? ((setx & mask) | (1 << maxx)) : 0;

	// printf("DISPLAY DATA: \n");
	// for (y = 0; y < maxy; y++) {
	// 	cpu->display_state[y] = (sety >> y & 1) ? ((setx & mask) | (1 << maxx)) : 0;
	// 	for (x=16;x>=0;x--) {
	// 		printf("[%d]",(cpu->display_state[y]&1<<x) ? 1:0);
	// 	}
	// 	printf("\n");

	// }

//	if (update) {
		ucom4_display_update(cpu);
//		update = 0;
//	}

}

FILE *sound_out = 0;

// basic instruction set

void op_illegal(ucom4cpu *cpu)
{
	printf("Unknown opcode $%02X at $%03X\n", cpu->op, cpu->prev_pc);
}


// Load

void op_li(ucom4cpu *cpu)
{
	// LI X: Load ACC with X
	// note: only execute the first one in a sequence of LI
	if ((cpu->prev_op & 0xf0) != (cpu->op & 0xf0))
		cpu->acc = cpu->op & 0x0f;
}

void op_lm(ucom4cpu *cpu)
{
	// LM X: Load ACC with RAM, xor DPh with X
	cpu->acc = ram_r(cpu);
	cpu->dph ^= (cpu->op & 0x03);
}

void op_ldi(ucom4cpu *cpu)
{
	// LDI X: Load DP with X
	cpu->dph = cpu->arg >> 4 & 0xf;
	cpu->dpl = cpu->arg & 0x0f;
}

void op_ldz(ucom4cpu *cpu)
{
	// LDZ X: Load DPh with 0, Load DPl with X
	cpu->dph = 0;
	cpu->dpl = cpu->op & 0x0f;
}


// Store

void op_s(ucom4cpu *cpu)
{
	// S: Store ACC into RAM
	ram_w(cpu, cpu->acc);
}


// Transfer

void op_tal(ucom4cpu *cpu)
{
	// TAL: Transfer ACC to DPl
	cpu->dpl = cpu->acc;
}

void op_tla(ucom4cpu *cpu)
{
	// TLA: Transfer DPl to ACC
	cpu->acc = cpu->dpl;
}


// Exchange

void op_xm(ucom4cpu *cpu)
{
	// XM X: Exchange ACC with RAM, xor DPh with X
	uint8_t old_acc = cpu->acc;
	cpu->acc = ram_r(cpu);
	ram_w(cpu,old_acc);
	cpu->dph ^= (cpu->op & 0x03);
}

void op_xmi(ucom4cpu *cpu)
{
	// XMI X: Exchange ACC with RAM, xor DPh with X, Increment DPl, skip next on carry
	op_xm(cpu);
	cpu->dpl = (cpu->dpl + 1) & 0xf;
	cpu->skip = (cpu->dpl == 0);
}

void op_xmd(ucom4cpu *cpu)
{
	// XMD X: Exchange ACC with RAM, xor DPh with X, Decrement DPl, skip next on carry
	op_xm(cpu);
	cpu->dpl = (cpu->dpl - 1) & 0xf;
	cpu->skip = (cpu->dpl == 0xf);
}


// Arithmetic

void op_ad(ucom4cpu *cpu)
{
	// AD: Add RAM to ACC, skip next on carry
	cpu->acc += ram_r(cpu);
	cpu->skip = ((cpu->acc & 0x10) != 0);
	cpu->acc &= 0xf;
}

void op_adc(ucom4cpu *cpu)
{
	// ADC: Add RAM and carry to ACC, store Carry F/F
	cpu->acc += ram_r(cpu) + cpu->carry_f;
	cpu->carry_f = cpu->acc >> 4 & 1;
	cpu->acc &= 0xf;
}

void op_ads(ucom4cpu *cpu)
{
	// ADS: Add RAM and carry to ACC, store Carry F/F, skip next on carry
	op_adc(cpu);
	cpu->skip = (cpu->carry_f != 0);
}

void op_daa(ucom4cpu *cpu)
{
	// DAA: Add 6 to ACC to adjust decimal for BCD Addition
	cpu->acc = (cpu->acc + 6) & 0xf;
}

void op_das(ucom4cpu *cpu)
{
	// DAS: Add 10 to ACC to adjust decimal for BCD Subtraction
	cpu->acc = (cpu->acc + 10) & 0xf;
}


// Logical

void op_exl(ucom4cpu *cpu)
{
	// EXL: Xor ACC with RAM
	cpu->acc ^= ram_r(cpu);
}


// Accumulator

void op_cma(ucom4cpu *cpu)
{
	// CMA: Complement ACC
	cpu->acc ^= 0xf;
}

void op_cia(ucom4cpu *cpu)
{
	// CIA: Complement ACC, Increment ACC
	cpu->acc = ((cpu->acc ^ 0xf) + 1) & 0xf;
}


// Carry Flag

void op_clc(ucom4cpu *cpu)
{
	// CLC: Reset Carry F/F
	cpu->carry_f = 0;
}

void op_stc(ucom4cpu *cpu)
{
	// STC: Set Carry F/F
	cpu->carry_f = 1;
}

void op_tc(ucom4cpu *cpu)
{
	// TC: skip next on Carry F/F
	cpu->skip = (cpu->carry_f != 0);
}


// Increment and Decrement

void op_inc(ucom4cpu *cpu)
{
	// INC: Increment ACC, skip next on carry
	cpu->acc = (cpu->acc + 1) & 0xf;
	cpu->skip = (cpu->acc == 0);
}

void op_dec(ucom4cpu *cpu)
{
	// DEC: Decrement ACC, skip next on carry
	cpu->acc = (cpu->acc - 1) & 0xf;
	cpu->skip = (cpu->acc == 0xf);
}

void op_ind(ucom4cpu *cpu)
{
	// IND: Increment DPl, skip next on carry
	cpu->dpl = (cpu->dpl + 1) & 0xf;
	cpu->skip = (cpu->dpl == 0);
}

void op_ded(ucom4cpu *cpu)
{
	// DED: Decrement DPl, skip next on carry
	cpu->dpl = (cpu->dpl - 1) & 0xf;
	cpu->skip = (cpu->dpl == 0xf);
}


// Bit Manipulation

void op_rmb(ucom4cpu *cpu)
{
	// RMB B: Reset a single bit of RAM
	ram_w(cpu, ram_r(cpu) & ~cpu->bitmask);
}

void op_smb(ucom4cpu *cpu)
{
	// SMB B: Set a single bit of RAM
	ram_w(cpu, ram_r(cpu) | cpu->bitmask);
}

void op_reb(ucom4cpu *cpu)
{
	// REB B: Reset a single bit of output port E
	cpu->icount--;
	active_game->output_w(cpu, NEC_UCOM4_PORTE, cpu->port_out[NEC_UCOM4_PORTE] & ~cpu->bitmask);
}

void op_seb(ucom4cpu *cpu)
{
	// SEB B: Set a single bit of output port E
	cpu->icount--;
	active_game->output_w(cpu, NEC_UCOM4_PORTE, cpu->port_out[NEC_UCOM4_PORTE] | cpu->bitmask);
}

void op_rpb(ucom4cpu *cpu)
{
	// RPB B: Reset a single bit of output port (DPl)
	active_game->output_w(cpu, cpu->dpl, cpu->port_out[cpu->dpl] & ~cpu->bitmask);
}

void op_spb(ucom4cpu *cpu)
{
	// SPB B: Set a single bit of output port (DPl)
	active_game->output_w(cpu, cpu->dpl, cpu->port_out[cpu->dpl] | cpu->bitmask);
}


// Jump, Call and Return

void op_jmpcal(ucom4cpu *cpu)
{
	// JMP A: Jump to Address / CAL A: Call Address
	if (cpu->op & 0x08)
		push_stack(cpu);
	cpu->pc = ((cpu->op & 0x07) << 8 | cpu->arg) & cpu->prgmask;
}

void op_jcp(ucom4cpu *cpu)
{
	// JCP A: Jump to Address in current page
	cpu->pc = (cpu->pc & ~0x3f) | (cpu->op & 0x3f);
}

void op_jpa(ucom4cpu *cpu)
{
	// JPA: Jump to (ACC) in current page
	cpu->icount--;
	cpu->pc = (cpu->pc & ~0x3f) | (cpu->acc << 2);
}

void op_czp(ucom4cpu *cpu)
{
	// CZP A: Call Address (short)
	push_stack(cpu);
	cpu->pc = (cpu->op & 0x0f) << 2;
}

void op_rt(ucom4cpu *cpu)
{
	// RT: Return from subroutine
	cpu->icount--;
	pop_stack(cpu);
}

void op_rts(ucom4cpu *cpu)
{
	// RTS: Return from subroutine, skip next
	op_rt(cpu);
	cpu->skip = true;
}


// Skip

void op_ci(ucom4cpu *cpu)
{
	// CI X: skip next on ACC equals X
	cpu->skip = (cpu->acc == (cpu->arg & 0x0f));

	if ((cpu->arg & 0xf0) != 0xc0)
		printf("CI opcode unexpected upper arg $%02X at $%03X\n", cpu->arg & 0xf0, cpu->prev_pc);
}

void op_cm(ucom4cpu *cpu)
{
	// CM: skip next on ACC equals RAM
	cpu->skip = (cpu->acc == ram_r(cpu));
}

void op_cmb(ucom4cpu *cpu)
{
	// CMB B: skip next on bit(ACC) equals bit(RAM)
	cpu->skip = ((cpu->acc & cpu->bitmask) == (ram_r(cpu) & cpu->bitmask));
}

void op_tab(ucom4cpu *cpu)
{
	// TAB B: skip next on bit(ACC)
	cpu->skip = ((cpu->acc & cpu->bitmask) != 0);
}

void op_cli(ucom4cpu *cpu)
{
	// CLI X: skip next on DPl equals X
	cpu->skip = (cpu->dpl == (cpu->arg & 0x0f));

	if ((cpu->arg & 0xf0) != 0xe0)
		printf("CLI opcode unexpected upper arg $%02X at $%03X\n", cpu->arg & 0xf0, cpu->prev_pc);
}

void op_tmb(ucom4cpu *cpu)
{
	// TMB B: skip next on bit(RAM)
	cpu->skip = ((ram_r(cpu) & cpu->bitmask) != 0);
}

void op_tpa(ucom4cpu *cpu)
{
	// TPA B: skip next on bit(input port A)
	cpu->skip = ((active_game->input_r(cpu, NEC_UCOM4_PORTA) & cpu->bitmask) != 0);
}

void op_tpb(ucom4cpu *cpu)
{
	// TPB B: skip next on bit(input port (DPl))
	cpu->skip = ((active_game->input_r(cpu, cpu->dpl) & cpu->bitmask) != 0);
}


// Interrupt

void op_tit(ucom4cpu *cpu)
{
	// TIT: skip next on Interrupt F/F, reset Interrupt F/F
	cpu->skip = (cpu->int_f != 0);
	cpu->int_f = 0;
}


// Parallel I/O

void op_ia(ucom4cpu *cpu)
{
	// IA: Input port A to ACC
	cpu->icount--;
	cpu->acc = active_game->input_r(cpu, NEC_UCOM4_PORTA);
}

void op_ip(ucom4cpu *cpu)
{
	// IP: Input port (DPl) to ACC
	cpu->acc = active_game->input_r(cpu, cpu->dpl);
}

void op_oe(ucom4cpu *cpu)
{
	// OE: Output ACC to port E
	cpu->icount--;
	active_game->output_w(cpu, NEC_UCOM4_PORTE, cpu->acc);
}

void op_op(ucom4cpu *cpu)
{
	// OP: Output ACC to port (DPl)
	active_game->output_w(cpu, cpu->dpl, cpu->acc);
}

void op_ocd(ucom4cpu *cpu)
{
	// OCD X: Output X to ports C and D
	active_game->output_w(cpu, NEC_UCOM4_PORTD, cpu->arg >> 4);
	active_game->output_w(cpu, NEC_UCOM4_PORTC, cpu->arg & 0xf);
}


// CPU Control

void op_nop(ucom4cpu *cpu)
{
	// NOP: No Operation
}



// uCOM-43 extended instructions

uint8_t check_op_43(ucom4cpu *cpu)
{
	// these opcodes are officially only supported on uCOM-43
	if (cpu->family != NEC_UCOM43)
		printf("Using uCOM-43 opcode $%02X at $%03X\n", cpu->op, cpu->prev_pc);

	return (cpu->family == NEC_UCOM43);
}

// extra registers reside in RAM
enum
{
	UCOM43_X = 0,
	UCOM43_Y,
	UCOM43_R,
	UCOM43_S,
	UCOM43_W,
	UCOM43_Z,
	UCOM43_F
};

uint8_t ucom43_reg_r(ucom4cpu *cpu, int index)
{
	return cpu->ram[cpu->datamask - index] & 0xf;
}

void ucom43_reg_w(ucom4cpu *cpu, int index, uint8_t data)
{
	cpu->ram[cpu->datamask - index]= data & 0xf;
}



// Transfer

void op_taw(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// TAW: Transfer ACC to W
	cpu->icount--;
	ucom43_reg_w(cpu, UCOM43_W, cpu->acc);
}

void op_taz(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// TAZ: Transfer ACC to Z
	cpu->icount--;
	ucom43_reg_w(cpu, UCOM43_Z, cpu->acc);
}

void op_thx(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// THX: Transfer DPh to X
	cpu->icount--;
	ucom43_reg_w(cpu, UCOM43_X, cpu->dph);
}

void op_tly(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// TLY: Transfer DPl to Y
	cpu->icount--;
	ucom43_reg_w(cpu, UCOM43_Y, cpu->dpl);
}


// Exchange

void op_xaw(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// XAW: Exchange ACC with W
	cpu->icount--;
	uint8_t old_acc = cpu->acc;
	cpu->acc = ucom43_reg_r(cpu, UCOM43_W);
	ucom43_reg_w(cpu, UCOM43_W, old_acc);
}

void op_xaz(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// XAZ: Exchange ACC with Z
	cpu->icount--;
	uint8_t old_acc = cpu->acc;
	cpu->acc = ucom43_reg_r(cpu, UCOM43_Z);
	ucom43_reg_w(cpu, UCOM43_Z, old_acc);
}

void op_xhr(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// XHR: Exchange DPh with R
	cpu->icount--;
	uint8_t old_dph = cpu->dph;
	cpu->dph = ucom43_reg_r(cpu, UCOM43_R);
	ucom43_reg_w(cpu, UCOM43_R, old_dph);
}

void op_xhx(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// XHX: Exchange DPh with X
	cpu->icount--;
	uint8_t old_dph = cpu->dph;
	cpu->dph = ucom43_reg_r(cpu, UCOM43_X);
	ucom43_reg_w(cpu, UCOM43_X, old_dph);
}

void op_xls(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// XLS: Exchange DPl with S
	cpu->icount--;
	uint8_t old_dpl = cpu->dpl;
	cpu->dpl = ucom43_reg_r(cpu, UCOM43_S);
	ucom43_reg_w(cpu, UCOM43_S, old_dpl);
}

void op_xly(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// XLY: Exchange DPl with Y
	cpu->icount--;
	uint8_t old_dpl = cpu->dpl;
	cpu->dpl = ucom43_reg_r(cpu, UCOM43_Y);
	ucom43_reg_w(cpu, UCOM43_Y, old_dpl);
}

void op_xc(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// XC: Exchange Carry F/F with Carry Save F/F
	uint8_t c = cpu->carry_f;
	cpu->carry_f = cpu->carry_s_f;
	cpu->carry_s_f = c;
}


// Flag

void op_sfb(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// SFB B: Set a single bit of FLAG
	cpu->icount--;
	ucom43_reg_w(cpu, UCOM43_F, ucom43_reg_r(cpu, UCOM43_F) | cpu->bitmask);
}

void op_rfb(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// RFB B: Reset a single bit of FLAG
	cpu->icount--;
	ucom43_reg_w(cpu, UCOM43_F, ucom43_reg_r(cpu, UCOM43_F) & ~cpu->bitmask);
}

void op_fbt(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// FBT B: skip next on bit(FLAG)
	cpu->icount--;
	cpu->skip = ((ucom43_reg_r(cpu, UCOM43_F) & cpu->bitmask) != 0);
}

void op_fbf(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// FBF B: skip next on not bit(FLAG)
	cpu->icount--;
	cpu->skip = ((ucom43_reg_r(cpu, UCOM43_F) & cpu->bitmask) == 0);
}


// Accumulator

void op_rar(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// RAR: Rotate ACC Right through Carry F/F
	uint8_t c = cpu->acc & 1;
	cpu->acc = cpu->acc >> 1 | cpu->carry_f << 3;
	cpu->carry_f = c;
}


// Increment and Decrement

void op_inm(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// INM: Increment RAM, skip next on carry
	uint8_t val = (ram_r(cpu) + 1) & 0xf;
	ram_w(cpu, val);
	cpu->skip = (val == 0);
}

void op_dem(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// DEM: Decrement RAM, skip next on carry
	uint8_t val = (ram_r(cpu) - 1) & 0xf;
	ram_w(cpu, val);
	cpu->skip = (val == 0xf);
}


// Timer

void op_stm(ucom4cpu *cpu)
{
//	printf("STM %02X\n",cpu->arg);

	if (!check_op_43(cpu)) return;

	// STM X: Reset Timer F/F, Start Timer with X
	cpu->timer_f = 0;

	// on the default clockrate of 400kHz, the minimum time interval is
	// 630usec and the maximum interval is 40320usec(630*64)
	// attotime base = attotime::frocpu->ticks(4 * 63, unscaled_clock());
	// cpu->timer->adjust(base * ((cpu->arg & 0x3f) + 1));

	cpu->tc = ((cpu->arg & 0x3f) +1)*63;
	cpu->tc += (cpu->old_icount - cpu->icount);

	if ((cpu->arg & 0xc0) != 0x80)
		printf("STM opcode unexpected upper arg $%02X at $%03X\n", cpu->arg & 0xc0, cpu->prev_pc);
}

void op_ttm(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// TTM: skip next on Timer F/F
	cpu->skip = (cpu->timer_f != 0);
}


// Interrupt

void op_ei(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// EI: Set Interrupt Enable F/F
	cpu->inte_f = 1;
}

void op_di(ucom4cpu *cpu)
{
	if (!check_op_43(cpu)) return;

	// DI: Reset Interrupt Enable F/F
	cpu->inte_f = 0;
}






void sound_buf(ucom4cpu *cpu, int ticks) {
	cpu->sample_count += ticks * cpu->sound_frequency;
    while (cpu->sample_count >= cpu->cpu_rate) {
        cpu->sample_count -= cpu->cpu_rate;
		audiobuf[cpu->aindex]=cpu->audio_level;
		cpu->aindex++;

		if(cpu->aindex>=10240) 
			cpu->aindex=0;

		cpu->audio_avail++;

		// audiobuf[cpu->aindex]=cpu->audio_level;
		// cpu->aindex++;

		// if(cpu->aindex>=10240) 
		// 	cpu->aindex=0;

		// cpu->audio_avail++;

    }
}


int overflow = 0;

int32_t ucom4_exec(ucom4cpu *cpu, int32_t ticks) {
	int32_t totalticks = 0;
	int tickused = 0;
	cpu->icount = ticks;
	
	overflow +=ticks;

	while(overflow>0) {
		cpu->old_icount = cpu->icount;

		// handle interrupt, but not during LI($9x) or EI($31) or while skipping
		if (cpu->int_f && cpu->inte_f && (cpu->op & 0xf0) != 0x90 && cpu->op != 0x31 && !cpu->skip)
		{
			do_interrupt(cpu);
			if (cpu->icount <= 0)
				break;
		}

		// remember previous state
		cpu->prev_op = cpu->op;
		cpu->prev_pc = cpu->pc;

		// fetch next opcode
		cpu->icount--;
		read_op(cpu);
		cpu->bitmask = 1 << (cpu->op & 0x03);
		increment_pc(cpu);
		fetch_arg(cpu);

		if (cpu->skip)
		{
			cpu->skip = false;
			cpu->op = 0; // nop
		}

		// handle opcode
		switch (cpu->op & 0xf0)
		{
			case 0x80: op_ldz(cpu); break;
			case 0x90: op_li(cpu); break;
			case 0xa0: op_jmpcal(cpu); break;
			case 0xb0: op_czp(cpu); break;

			case 0xc0: case 0xd0: case 0xe0: case 0xf0: op_jcp(cpu); break;

			default:
				switch (cpu->op)
				{
			case 0x00: op_nop(cpu); break;
			case 0x01: op_di(cpu); break;
			case 0x02: op_s(cpu); break;
			case 0x03: op_tit(cpu); break;
			case 0x04: op_tc(cpu); break;
			case 0x05: op_ttm(cpu); break;
			case 0x06: op_daa(cpu); break;
			case 0x07: op_tal(cpu); break;
			case 0x08: op_ad(cpu); break;
			case 0x09: op_ads(cpu); break;
			case 0x0a: op_das(cpu); break;
			case 0x0b: op_clc(cpu); break;
			case 0x0c: op_cm(cpu); break;
			case 0x0d: op_inc(cpu); break;
			case 0x0e: op_op(cpu); break;
			case 0x0f: op_dec(cpu); break;
			case 0x10: op_cma(cpu); break;
			case 0x11: op_cia(cpu); break;
			case 0x12: op_tla(cpu); break;
			case 0x13: op_ded(cpu); break;
			case 0x14: op_stm(cpu); break;
			case 0x15: op_ldi(cpu); break;
			case 0x16: op_cli(cpu); break;
			case 0x17: op_ci(cpu); break;
			case 0x18: op_exl(cpu); break;
			case 0x19: op_adc(cpu); break;
			case 0x1a: op_xc(cpu); break;
			case 0x1b: op_stc(cpu); break;
			case 0x1c: op_illegal(cpu); break;
			case 0x1d: op_inm(cpu); break;
			case 0x1e: op_ocd(cpu); break;
			case 0x1f: op_dem(cpu); break;

			case 0x30: op_rar(cpu); break;
			case 0x31: op_ei(cpu); break;
			case 0x32: op_ip(cpu); break;
			case 0x33: op_ind(cpu); break;

			case 0x40: op_ia(cpu); break;
			case 0x41: op_jpa(cpu); break;
			case 0x42: op_taz(cpu); break;
			case 0x43: op_taw(cpu); break;
			case 0x44: op_oe(cpu); break;
			case 0x45: op_illegal(cpu); break;
			case 0x46: op_tly(cpu); break;
			case 0x47: op_thx(cpu); break;
			case 0x48: op_rt(cpu); break;
			case 0x49: op_rts(cpu); break;
			case 0x4a: op_xaz(cpu); break;
			case 0x4b: op_xaw(cpu); break;
			case 0x4c: op_xls(cpu); break;
			case 0x4d: op_xhr(cpu); break;
			case 0x4e: op_xly(cpu); break;
			case 0x4f: op_xhx(cpu); break;

			default:
				switch (cpu->op & 0xfc)
				{
			case 0x20: op_fbf(cpu); break;
			case 0x24: op_tab(cpu); break;
			case 0x28: op_xm(cpu); break;
			case 0x2c: op_xmd(cpu); break;

			case 0x34: op_cmb(cpu); break;
			case 0x38: op_lm(cpu); break;
			case 0x3c: op_xmi(cpu); break;

			case 0x50: op_tpb(cpu); break;
			case 0x54: op_tpa(cpu); break;
			case 0x58: op_tmb(cpu); break;
			case 0x5c: op_fbt(cpu); break;
			case 0x60: op_rpb(cpu); break;
			case 0x64: op_reb(cpu); break;
			case 0x68: op_rmb(cpu); break;
			case 0x6c: op_rfb(cpu); break;
			case 0x70: op_spb(cpu); break;
			case 0x74: op_seb(cpu); break;
			case 0x78: op_smb(cpu); break;
			case 0x7c: op_sfb(cpu); break;
				}
				break; // 0xfc

				}
				break; // 0xff

		} // big switch

		tickused   = cpu->old_icount - cpu->icount;
		ticks      -= tickused;
		totalticks += tickused;
		cpu->decay_ticks += tickused;
		cpu->sound_ticks += tickused;
		cpu->totalticks += tickused;

		overflow -= tickused;

		sound_buf(cpu, tickused);

		if( cpu->tc > 0 ) {
			cpu->tc -= tickused;
			if( cpu->tc <=0 ) {
				cpu->tc = 0;
				cpu->timer_f = 1;
			}
		}

	}	return totalticks;
}

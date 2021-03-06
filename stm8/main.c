/* Copyright (C) 2015 Baruch Even
 * Modified for BST900 by Derek Jennings 2019
 *
 * This file is part of the B3ST900 alternative firmware.
 *
 *  BST900 alternative firmware is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  BST900 alternative firmware is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with BST900 alternative firmware.  If not, see <http://www.gnu.org/licenses/>.
 */
#undef DETAILED_HELP
#define VERBOSECAL 1

#include "stm8s.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "main.h"
#include "fixedpoint.h"
#include "uart.h"
#include "eeprom.h"
#include "outputs.h"
#include "config.h"
#include "parse.h"
#include "adc.h"
#include "buttons.h"
#include "fsm.h"

#undef DEBUG

cfg_system_t cfg_system;
cfg_output_t cfg_output;
state_t state;

inline void iwatchdog_init(void)
{
	IWDG_KR = 0xCC; // Enable IWDG
	// The default values give us about 15msec between pings
}

inline void iwatchdog_tick(void)
{
	IWDG_KR = 0xAA; // Reset the counter
}

void commit_output()
{
	output_commit(&cfg_output, &cfg_system, state.constant_current);
}

#ifdef VERBOSECAL
#define OPTIONALARG(msg)  , msg
#else
#define OPTIONALARG(msg)
#endif


void write_calibration(calibrate_t *val  OPTIONALARG(const char *tag))
{
#ifdef VERBOSECAL
    if (tag) {
        uart_write_str(tag);
        uart_write_ch(' ');
    }
#endif
/*	//  Eliminate fixed point
    uart_write_fixed_point(val->a);
    uart_write_ch('/');
    uart_write_fixed_point(val->b);
    uart_write_ch(' ');
    uart_write_ch(' ');
*/ 
    uart_write_int32(val->a);
    uart_write_ch('/');
    uart_write_int32(val->b);
    uart_write_crlf();
}

bool handle_set_name(const char *name)
{
    uint8_t *dst = cfg_system.name;
    uint8_t *enddst = cfg_system.name+sizeof(cfg_system.name);
	while (dst < enddst) {
        if (*name==0)
            break;
		if (!isprint(*name))
			*dst = '.'; // Eliminate non-printable chars
        else
            *dst = *name;
        dst++;
        name++;
	}
	while (dst < enddst)
        *dst++ = 0;

	enddst[-1] = 0;

	uart_write_str("SNAME: ");
	uart_write_str((const char*)cfg_system.name);
	uart_write_crlf();

    return true;
}

void autocommit(void)
{
	if (cfg_system.autocommit) {
		commit_output();
	} else {
		uart_write_str("OFF" CRLF);
	}
}

bool set_output(const char *s)
{
	if (s[1] != 0)
		return false;

	if (s[0] == '0') {
		cfg_system.output = 0;
#ifdef VERBOSE
		uart_write_str("OFF" CRLF);
#endif
	} else if (s[0] == '1') {
		cfg_system.output = 1;
#ifdef VERBOSE
		uart_write_str("ON" CRLF);
#endif
	} else {
        return false;
	}

	autocommit();
	return true;
}


// voltage in cV --centiVolts
bool set_voltage(uint16_t voltage)
{
	uint16_t val = voltage;

	if (val == 0xFFFF)
		return false;

	if ((val > CAP_VMAX) || (val < CAP_VMIN)) {  // 800 .. 12000 cV
		return false;
	}

	cfg_output.vset = val;
	autocommit();

	return true;
}
bool set_voltage_arg(const char *arg)
{
    if (arg == NULL) return false;
    uint32_t tmp = parse_set_value(arg)/10;		//Convert to centiVolts
    return set_voltage(tmp);
}


/* current in mA */
bool set_current(uint16_t current)
{
	uint16_t val = current;

	if (val == 0xFFFF)
		return false;

	if ((val > CAP_CMAX) || (val < CAP_CMIN)) {  // 10 .. 15000 mA
		return false;
	}

	cfg_output.cset = val;
	autocommit();

	return true;
}
bool set_current_arg(const char *arg)
{
    if (arg == NULL) return false;
    return set_current(parse_set_value(arg));
}


bool set_option(uint8_t *opt, const char *arg)
{
	if (strcmp(arg, "1") == 0 || strcmp(arg, "YES") == 0) {
#ifdef VERBOSE
		uart_write_str("YES" CRLF);
#endif
		*opt=1;
        return true;
	} else if (strcmp(arg, "0") == 0 || strcmp(arg, "NO") == 0) {
#ifdef VERBOSE
		uart_write_str("NO" CRLF);
#endif
		*opt=0;
        return true;
	} else {
		return false;
	}
}

bool set_autocommit(const char *arg)
{
	return set_option(&cfg_system.autocommit, arg);
}

/* Enable/Disable Default start at power on
* Enables output immediately on power on
* 
*/
bool set_default(const char *arg)
{
	return set_option(&cfg_system.default_on, arg);
}

/* Enable/Disable command echo
* Repeats command with "OK" to enable connected device to
* confirm which command has been executed.
* 
*/
bool set_echo(const char *arg)
{
	return set_option(&cfg_system.echo, arg);
}



bool set_calibration(const char*cmd, const char *arg, calibrate_t*cal)
{
	char *spc;
 	calibrate_t val;
	val.a = parse_uint32(arg);
	if (val.a == 0xFFFFFFFF)
        	return false;

 	spc = strchr(arg, ' ');
	if (spc==NULL)
        	return false;

    	*spc = 0;
	val.b = parse_uint32(spc+1);
	if (val.b == 0xFFFFFFFF)
        	return false;

	cal->a = val.a;
	cal->b = val.b;
	return true;
}

void write_str(const char *prefix, const char *val)
{
	uart_write_str(prefix);
	uart_write_str(val);
	uart_write_crlf();
}

void write_onoff(const char *prefix, uint8_t on)
{
	write_str(prefix, on ? "ON" : "OFF");
}

void write_millivalue(const char *prefix, uint16_t millival)
{
	uart_write_str(prefix);
	uart_write_millivalue(millival);
	uart_write_crlf();
}

void write_centivalue(const char *prefix, uint16_t centival)
{
	uart_write_str(prefix);
	uart_write_centivalue(centival);
	uart_write_crlf();
}

void write_raw_millivalue(const char *prefix, uint16_t millival, uint16_t rawval)
{
	uart_write_str(prefix);
	uart_write_millivalue(millival);
	uart_write_ch(' ');
	uart_write_int(rawval);
	uart_write_crlf();
}



void write_raw_centivalue(const char *prefix, uint16_t centival, uint16_t rawval)
{
	uart_write_str(prefix);
	uart_write_centivalue(centival);
	uart_write_ch(' ');
	uart_write_int(rawval);
	uart_write_crlf();
}


void write_int(const char *prefix, uint16_t val)
{
	uart_write_str(prefix);
	uart_write_int(val);
	uart_write_crlf();
}

//  command handlers
bool handle_system(const char *arg)
{
    uart_write_str("M: " MODEL CRLF "V: " FW_VERSION CRLF);
    write_str("N: ", (const char*)cfg_system.name);
    write_onoff("O: ", cfg_system.default_on);
    write_onoff("E: ", cfg_system.echo);
    write_onoff("AC: ", cfg_system.autocommit);
    return true;
}
bool handle_calibration_dump(const char *arg)
{
    write_calibration(&cfg_system.vin_adc  OPTIONALARG("VIN  ADC"));
    write_calibration(&cfg_system.vout_adc OPTIONALARG("VOUT ADC"));
    write_calibration(&cfg_system.cout_adc OPTIONALARG("COUT ADC"));
    write_calibration(&cfg_system.vout_pwm OPTIONALARG("VOUT PWM"));
    return true;
}
bool handle_limit_dump(const char *arg)
{
    write_centivalue("VMIN: ", CAP_VMIN);
    write_centivalue("VMAX: ", CAP_VMAX);
    write_centivalue("VSTEP:", CAP_VSTEP);
    write_millivalue("CMIN: ", CAP_CMIN);
    write_millivalue("CMAX: ", CAP_CMAX);
    write_millivalue("CSTEP:", CAP_CSTEP);
    return true;
}
bool handle_config_dump(const char *arg)
{
    write_onoff(     "OUTPUT: ", cfg_system.output);
    write_centivalue("VSET: ", cfg_output.vset);
    write_millivalue("CSET: ", cfg_output.cset);
    return true;
}
bool handle_status_dump(const char *arg)
{
    write_onoff(         "OUTPUT: ", cfg_system.output);
//    write_raw_centivalue("VIN:  ", state.vin,  state.vin_raw );
//    write_raw_centivalue("VOUT: ", state.vout, state.vout_raw);
//    write_raw_millivalue("COUT: ", state.cout, state.cout_raw);
    write_raw_centivalue("VIN:  ", state.vin,  state.vin_raw );
    write_raw_centivalue("VOUT: ", state.vout, state.vout_raw);
    write_raw_millivalue("COUT: ", state.cout, state.cout_raw);
    write_str(           "CONSTANT: ", state.constant_current ? "CURRENT" : "VOLTAGE");
    return true;
}
bool handle_save(const char *arg)
{
    if (config_save_system(&cfg_system)==0)
        return false;
    else if (config_save_output(&cfg_output) == 0)
        return false;
    else
        return true;
}
bool handle_load(const char *arg)
{
    config_load_system(&cfg_system);
    config_load_output(&cfg_output);
    autocommit();
    return true;
}
bool handle_factory(const char *arg)
{
    config_default_system(&cfg_system);
    config_default_output(&cfg_output);
    autocommit();
    return true;
}
#ifdef DEBUG
bool handle_stuck(const char *arg)
{
    uart_write_str("STUCK" CRLF);
    uart_write_flush();
    while(1); // Induce watchdog reset
    return true;
}
#endif

bool handle_commit_output(const char*arg)
{
    commit_output();
    return true;
}

#ifdef DETAILED_HELP
#define OPTIONAL(msg) msg
#else
#define OPTIONAL(msg)
#endif
struct command {
    const char *command;
    uint8_t commandsize;
    bool (*handler)(const char *arg);
#ifdef DETAILED_HELP
    const char *helpmessage;
#endif
};

bool handle_command_help(const char*arg);

struct command commandhandlers[] = {
    { "SYSTEM", 6, handle_system,  OPTIONAL("show system information") },
    { "CALIBRATION", 11, handle_calibration_dump, OPTIONAL("show calibration data") },
    { "LIMITS", 6, handle_limit_dump, OPTIONAL("show current and voltage limits") },
    { "CONFIG", 6, handle_config_dump, OPTIONAL("show configuration") },
    { "STATUS", 6, handle_status_dump, OPTIONAL("show status of the outputs") },
    { "COMMIT", 6, handle_commit_output, OPTIONAL("commit configuration to output") },
    { "SAVE", 4,   handle_save, OPTIONAL("save settings and config to flash") },
    { "LOAD", 4,   handle_load, OPTIONAL("load settings and config from flash") },
    { "FACTORY", 7, handle_factory, OPTIONAL("restore settings and config to factory defaults") },
#if DEBUG
    { "STUCK", 5,  handle_stuck, OPTIONAL("simulate problem for debugger") },
#endif
    { "SNAME", 5, handle_set_name, OPTIONAL("configure device name") },
    { "OUTPUT", 6, set_output, OPTIONAL("turn output on/off") },
    { "VOLTAGE", 7, set_voltage_arg, OPTIONAL("set output voltage") },
    { "CURRENT", 7, set_current_arg, OPTIONAL("set output current") },
    { "AUTOCOMMIT", 10, set_autocommit, OPTIONAL("enable/disable auto commit") },
    { "DEFAULT", 7, set_default, OPTIONAL("enable/disable output on power up") },
    { "ECHO", 4, set_echo, OPTIONAL("enable/disable command echo") },
    { "HELP", 10, handle_command_help, OPTIONAL("show available commands") },
};


struct calcommand {
    const char *command;
    uint8_t commandsize;
    calibrate_t *value;
#ifdef DETAILED_HELP
    const char *helpmessage;
#endif
};

struct calcommand calibrationhandlers[] = {
    { "VINADC", 6, &cfg_system.vin_adc, OPTIONAL("configure Vin adc -> volt parameters") },
    { "VOUTADC", 7, &cfg_system.vout_adc, OPTIONAL("configure Vout adc -> volt parameters") },
    { "VOUTPWM", 7, &cfg_system.vout_pwm, OPTIONAL("configure Vout pwm -> volt parameters") },
    { "COUTADC", 7, &cfg_system.cout_adc, OPTIONAL("configure Cout adc -> ampere parameters") },
    //{ "COUTPWM", 7, &cfg_system.cout_pwm, OPTIONAL("configure Cout pwm -> ampere parameters") },
};
bool handle_command_help(const char*arg)
{
    for (int i = 0 ; i < sizeof(commandhandlers)/sizeof(struct command) ; i++)
    {
        uart_write_str(commandhandlers[i].command);
#ifdef DETAILED_HELP
        uart_write_ch('\t');
        uart_write_str(commandhandlers[i].helpmessage);
#endif
        uart_write_crlf();
    }
    for (int i = 0 ; i < sizeof(calibrationhandlers)/sizeof(struct calcommand) ; i++)
    {
        uart_write_str("CAL_");
        uart_write_str(calibrationhandlers[i].command);
#ifdef DETAILED_HELP
        uart_write_ch('\t');
        uart_write_str(calibrationhandlers[i].helpmessage);
#endif
        uart_write_crlf();
    }
    return true;
}


 
void process_input()
{
    bool ok = false;
	// Eliminate the CR/LF character
	uart_read_buf[uart_read_len-1] = 0;
	int i;
    for ( i = 0 ; i < sizeof(commandhandlers)/sizeof(struct command) ; i++)
    {
        int cmdlen = commandhandlers[i].commandsize;
        const char *arg = NULL;
        if (uart_read_buf[cmdlen]==' ')
            arg = (const char*)uart_read_buf+cmdlen+1;
        if (strncmp((const char*)uart_read_buf, commandhandlers[i].command, cmdlen) == 0)
        {
            ok = commandhandlers[i].handler(arg);
            if (ok && cfg_system.echo) {
				uart_write_str(commandhandlers[i].command);
				uart_write_str(" - ");
			}
        }
    }
    if (strncmp((const char*)uart_read_buf, "CAL_", 4) == 0) {
        for ( i = 0 ; i < sizeof(calibrationhandlers)/sizeof(struct calcommand) ; i++)
        {
            int cmdlen = calibrationhandlers[i].commandsize;
            const char *arg = NULL;
            if (uart_read_buf[4+cmdlen]==' ')
                arg = (const char*)uart_read_buf+4+cmdlen+1;
            if (strncmp((const char*)uart_read_buf+4, calibrationhandlers[i].command, cmdlen) == 0)
                ok = set_calibration(calibrationhandlers[i].command, arg, calibrationhandlers[i].value);
        }
    }
    if (ok)
        uart_write_str("OK" CRLF);
    else
        uart_write_str("E!"  CRLF);

	uart_read_len = 0;
	read_newline = 0;
}


inline void clk_init()
{
	CLK_CKDIVR = 0x00; // Set the frequency to 16 MHz
}

inline void pinout_init()
{
	// PA1 is 74HC595 SHCP, output
	// PA2 is 74HC595 STCP, output
	// PA3 is CV/CC leds, output (& input to disable)
	PA_ODR = 0;
	PA_DDR = (1<<1) | (1<<2);
	PA_CR1 = (1<<1) | (1<<2) | (1<<3);
	PA_CR2 = (1<<1) | (1<<2) | (1<<3);

	// PB4 is Enable control, output
	// PB5 is CV/CC sense, input
	PB_ODR = (1<<4); // For safety we start with off-state
	PB_DDR = (1<<4);
	PB_CR1 = (1<<4);
	PB_CR2 = 0;

	// PC3 is Fan control, output
	// PC4 is Iout sense, input adc, AIN2
	// PC5 is Vout control, output
	// PC6 is Iout control, output
	// PC7 is Button 1, input
	PC_ODR = 0;
	PC_DDR = (1<<3) | (1<<5) | (1<<6);
	PC_CR1 = ((unsigned)1<<7); // For the button
	PC_CR2 = (1<<3) | (1<<5) | (1<<6);

	// PD1 is Button 2, input
	// PD2 is Vout sense, input adc, AIN3
	// PD3 is Vin sense, input adc, AIN4
	// PD4 is 74HC595 DS, output
	PD_DDR = (1<<4);
	PD_CR1 = (1<<1) | (1<<4); // For the button
	PD_CR2 = (1<<4);
}

void config_load(void)
{
	config_load_system(&cfg_system);
	config_load_output(&cfg_output);

	if (cfg_system.default_on)
		cfg_system.output = 1;
	else
		cfg_system.output = 0;

#if DEBUG
	state.pc3 = 1;
#endif
}


void read_state(void)
{
	uint8_t tmp;

#if DEBUG
	tmp = (PC_IDR & (1<<3)) ? 1 : 0;
	if (state.pc3 != tmp) {
		uart_write_str("PC3 is now ");
		uart_write_ch('0' + tmp);
		uart_write_crlf();
		state.pc3 = tmp;
	}
#endif

	tmp = (PB_IDR & (1<<5)) ? 0 : 1;		//CC sense reversed on BST900
	if (state.constant_current != tmp) {
		state.constant_current = tmp;
		output_check_state(&cfg_system, state.constant_current);
	}

	if (adc_ready()) {
		uint16_t val = adc_read();
		uint8_t ch = ADC1_CSR & 0x0F;	// Get ADC channel

		switch (ch) {
			case 2:
				state.cout_raw = val;
				// Calculation: val * cal_cout_a * 3.3 / 1024 - cal_cout_b
				state.cout = adc_to_volt(val, &cfg_system.cout_adc);
				ch = (state.adc_counter--) ? 2 : 3;	//Repeat Cout ADC test until counter expires and we move on to Vout test
				/* Closed loop feedback to adjust Current PWM based on results of last
				 * adc result. If in CC mode measured current is less than CC target then increment PWM pulse
				 * If in CV mode current drops, then decrement pulse. 
				 * Should cause constant current to converge onto its target without any PWM calibration.
				 * This fits closer to the observed behaviour of the stock firmware where the pulse
				 * width narrows in CV mode and expands in CC node/
				 * If PWM pulse is too wide MOSFET is on for longer and current is wasted.
				 *  */

				adc_init();		// All ADC readings get screwed up without this. Dunno why? Loop timing issue?
				uint16_t ccr1H = TIM1_CCR1H;
				uint16_t ccr1 = TIM1_CCR1L | (ccr1H<<8);
				
				//Increase PWM pulse if current less than limit and we are in CC mode
				if ( state.constant_current && state.cout < cfg_output.cset ) {
					//ccr1 = ( state.cout+256 < cfg_output.cset ) ? ccr1 + 12 : ccr1 + 3;		// Fast up
					uint16_t tmp = cfg_output.cset - state.cout;		//Distance to target current
					ccr1 += (tmp/32)+1;		//Upspeed proportional to distance between current and target
				}
				
				
				//Reduce PWM pulse if in CV mode current is well below max. 
				// (Resolution of PWM counter is about 15mA)
				//if ( (state.cout > cfg_output.cset+10 || ( !state.constant_current && state.cout + 512 < cfg_output.cset)) && ccr1 >= 1) {
				if ( ( !state.constant_current && state.cout + 512 < cfg_output.cset) && ccr1 >= 1) {
					ccr1 -= 1;
				}
				
				// Halve PWM pulse if open circuit (rapid down)
				if ( state.cout == 0 ) {
					ccr1 = ccr1 >> 1;
				}
				
				
				// Reduce PWM pulse if in CC mode and current is above target
				if ( state.cout > cfg_output.cset+10 ) {
					uint16_t tmp = state.cout - (cfg_output.cset+10);		//Distance to target current
					ccr1 = (ccr1 > tmp/32 ) ? ccr1 - (tmp/32)-1 : 0;	//Down speed proportional to distance between current and target
				} 

				TIM1_CCR1H = ccr1>>8;
				TIM1_CCR1L = ccr1 & 0xFF;
				
				break;
			case 3:
				state.vout_raw = val;
				// Calculation: val * cal_vout_a * 3.3 / 1024 - cal_vout_b
				state.vout = adc_to_volt(val, &cfg_system.vout_adc);
				ch = 4;
				state.adc_counter = 8; //Reset counter for next time Cout test runs
				break;
			case 4:
				state.vin_raw = val;
				// Calculation: val * cal_vin * 3.3 / 1024
				state.vin = adc_to_volt(val, &cfg_system.vin_adc);
				ch = 2;
				break;
		}

		adc_start(ch);
	}
}

void ensure_afr0_set(void)
{
	if ((OPT2 & 1) == 0) {
		uart_flush_writes();
		if (eeprom_set_afr0()) {
			uart_write_str("reboot" CRLF);
			uart_flush_writes();
			iwatchdog_init();
			while (1); // Force a reset in a few msec
		}
		else {
			uart_write_str("E!" CRLF);
		}
	}
}

int main()
{
	unsigned long i = 0;
	button_t button = BUTTON_NONE;
	state.adc_counter = 8;		// Initialise count of Cout ADC samples
	pinout_init();
	clk_init();
	uart_init();
	pwm_init();
	adc_init();

	config_load();

	uart_write_str(CRLF MODEL " V:" FW_VERSION CRLF);

	ensure_afr0_set();

	iwatchdog_init();
	adc_start(4);
	commit_output();

	do {
		iwatchdog_tick();
		read_state();
		display_refresh();
		button=read_buttons();
		process_fsm(button, &cfg_system, &cfg_output, &state);
		uart_drive();
		if (read_newline) {
			process_input();
		}
	} while(1);
}

/* vim: set sw=8 ts=8 si et: */
/****************************************************************************
Title	:   HD44780 LCD library
Authors:   
Based on Volker Oth's lcd library (http://members.xoom.com/volkeroth)
modified by Peter Fleury's (http://jump.to/fleury). Flexible pin
configuration by Markus Ermert. Adapted for the tuxgraphics LCD display
by Guido Socher.

Software:  AVR-GCC with AVR-AS
Target:    any AVR device
Copyright: GPL V2
       
*****************************************************************************/
#include <ioavr.h>
//#include <avr/pgmspace.h>
#include "timeout.h"
#include <intrinsics.h>
#include "lcd_hw.h"
#include "lcd.h"


__flash char SymbolCGRAM[48]  = {
0x00,    /*  ........  */
0x00,    /*  ........  */
0x00,    /*  ........  */
0x00,    /*  ........  */
0x00,    /*  ........  */
0x00,    /*  ........  */
0x00,    /*  ........  */
0x00,    /*  ........  */
// символ №0
0x10,    /*  ...$....  */
0x10,    /*  ...$....  */
0x10,    /*  ...$....  */
0x10,    /*  ...$....  */
0x10,    /*  ...$....  */
0x10,    /*  ...$....  */
0x10,    /*  ...$....  */
0x10,    /*  ...$....  */
// символ №1
0x18,    /*  ...$$...  */
0x18,    /*  ...$$...  */
0x18,    /*  ...$$...  */
0x18,    /*  ...$$...  */
0x18,    /*  ...$$...  */
0x18,    /*  ...$$...  */
0x18,    /*  ...$$...  */
0x18,    /*  ...$$...  */
// символ №2
0x1C,    /*  ...$$$..  */
0x1C,    /*  ...$$$..  */
0x1C,    /*  ...$$$..  */
0x1C,    /*  ...$$$..  */
0x1C,    /*  ...$$$..  */
0x1C,    /*  ...$$$..  */
0x1C,    /*  ...$$$..  */
0x1C,    /*  ...$$$..  */
// символ №3
0x1E,    /*  ...$$$$.  */
0x1E,    /*  ...$$$$.  */
0x1E,    /*  ...$$$$.  */
0x1E,    /*  ...$$$$.  */
0x1E,    /*  ...$$$$.  */
0x1E,    /*  ...$$$$.  */
0x1E,    /*  ...$$$$.  */
0x1E,    /*  ...$$$$.  */
// символ №4
0x1F,    /*  ...$$$$$  */
0x1F,    /*  ...$$$$$  */
0x1F,    /*  ...$$$$$  */
0x1F,    /*  ...$$$$$  */
0x1F,    /*  ...$$$$$  */
0x1F,    /*  ...$$$$$  */
0x1F,    /*  ...$$$$$  */
0x1F,    /*  ...$$$$$  */
// символ №5
};

/* compatibilty macros for old style */
/*
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif

#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif
*/
/* 
** constants/macros 
*/

void lcd_e_high(void) 
{
LCD_E_PORT |= (1<<LCD_E_PIN);
}

void lcd_e_low(void)     
{
LCD_E_PORT &= ~(1<<LCD_E_PIN);
}

void lcd_cmd_mode(void)
{
LCD_RS_PORT &= ~(1<<LCD_RS_PIN);	  // RS=0  command mode
}

void lcd_data_mode(void)
{
LCD_RS_PORT |= (1<<LCD_RS_PIN); // RS=1  data mode 
}

void lcd_data_port_out(void)	
{	/* defines all data pins as output */ 
LCD_DATA_DDR_D7 |= (1<<LCD_DATA_PIN_D7);
LCD_DATA_DDR_D6 |= (1<<LCD_DATA_PIN_D6);
LCD_DATA_DDR_D5 |= (1<<LCD_DATA_PIN_D5);
LCD_DATA_DDR_D4 |= (1<<LCD_DATA_PIN_D4);
}

#if LCD_LINES==1
#define LCD_FUNCTION_DEFAULT    LCD_FUNCTION_4BIT_1LINE
#else
#define LCD_FUNCTION_DEFAULT    LCD_FUNCTION_4BIT_2LINES
#endif


/* 
** function prototypes 
*/
static void lcd_e_toggle(void);
static void lcd_out_high(u08 d);
static void lcd_out_low(u08 d);

/*
** local functions
*/


static void lcd_out_low(u08 d)
{	/* output low nibble */
	if (d&0x08)  LCD_DATA_PORT_D7 |= (1<<LCD_DATA_PIN_D7);
		else LCD_DATA_PORT_D7 &= ~(1<<LCD_DATA_PIN_D7);
	if (d&0x04)  LCD_DATA_PORT_D6 |= (1<<LCD_DATA_PIN_D6);
		else LCD_DATA_PORT_D6 &= ~(1<<LCD_DATA_PIN_D6);
	if (d&0x02)  LCD_DATA_PORT_D5 |= (1<<LCD_DATA_PIN_D5);
		else LCD_DATA_PORT_D5 &= ~(1<<LCD_DATA_PIN_D5);
	if (d&0x01)  LCD_DATA_PORT_D4 |= (1<<LCD_DATA_PIN_D4);
		else LCD_DATA_PORT_D4 &= ~(1<<LCD_DATA_PIN_D4); 
}
static void lcd_out_high(u08 d)
{	/* output high nibble */ 
	if (d&0x80)  LCD_DATA_PORT_D7 |= (1<<LCD_DATA_PIN_D7);
		else LCD_DATA_PORT_D7 &= ~(1<<LCD_DATA_PIN_D7);
	if (d&0x40)  LCD_DATA_PORT_D6 |= (1<<LCD_DATA_PIN_D6);
		else LCD_DATA_PORT_D6 &= ~(1<<LCD_DATA_PIN_D6);
	if (d&0x20)  LCD_DATA_PORT_D5 |= (1<<LCD_DATA_PIN_D5);
		else LCD_DATA_PORT_D5 &= ~(1<<LCD_DATA_PIN_D5);
	if (d&0x10)  LCD_DATA_PORT_D4 |= (1<<LCD_DATA_PIN_D4);
		else LCD_DATA_PORT_D4 &= ~(1<<LCD_DATA_PIN_D4); 
}

static void lcd_e_toggle(void)
/* toggle Enable Pin */
{
	lcd_e_high();
	__delay_cycles((CtrlClockRate/1000000)*5);
	lcd_e_low();
	__delay_cycles((CtrlClockRate/1000000)*1);
}


static void lcd_write(u08 data, u08 rs)
{
	/* configure data pins as output */
	lcd_data_port_out();
	__delay_cycles((CtrlClockRate/1000000)*8);

	/* output high nibble first */

	lcd_out_high(data);
	__delay_cycles((CtrlClockRate/1000000)*8);

	if (rs)
		lcd_data_mode();	/* RS=1: write data            */
	else
		lcd_cmd_mode();	/* RS=0: write instruction     */
	lcd_e_toggle();

	/* output low nibble */
	lcd_out_low(data);
	__delay_cycles((CtrlClockRate/1000000)*8);

	if (rs)
		lcd_data_mode();	/* RS=1: write data            */
	else
		lcd_cmd_mode();	/* RS=0: write instruction     */

	lcd_e_toggle();
}


static unsigned char lcd_waitcmd(unsigned char cmdwait)
/* this function used to loop while lcd is busy and read address i
 * counter however for this we need the RW line. This function
 * has been changed to just delay a bit. In that case the LCD
 * is only slightly slower but we do not need the RW pin. */
{
        __delay_cycles((CtrlClockRate/1000000)*9);
	/* the display needs much longer to process a command */
	if (cmdwait){
		__delay_cycles((CtrlClockRate/1000)*2);
	}
	return (0); 
}


/*
** PUBLIC FUNCTIONS 
*/

void lcd_command(u08 cmd)
/* send commando <cmd> to LCD */
{
	lcd_waitcmd(0);
	lcd_write(cmd, 0);
	lcd_waitcmd(1);
}


void lcd_gotoxy(u08 x, u08 y)
/* goto position (x,y) */
{
#if LCD_LINES==1
	lcd_command((1 << LCD_DDRAM) + LCD_START_LINE1 + x);
#endif
#if LCD_LINES==2
	if (y == 0)
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE1 + x);
	else
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE2 + x);
#endif
#if LCD_LINES==3
	if (y == 0)
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE1 + x);
	else if (y == 1)
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE2 + x);
	else if (y == 2)
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE3 + x);
#endif
#if LCD_LINES==4
	if (y == 0)
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE1 + x);
	else if (y == 1)
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE2 + x);
	else if (y == 2)
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE3 + x);
	else			/* y==3 */
		lcd_command((1 << LCD_DDRAM) + LCD_START_LINE4 + x);
#endif

}				/* lcd_gotoxy */



void lcd_putc(char c)
/* print character at current cursor position */
{
	lcd_waitcmd(0);
	lcd_write((unsigned char)c, 1);
	lcd_waitcmd(0);
}


void lcd_puts(const char *s)
/* print string on lcd  */
{
	while (*s) {
		lcd_putc(*s);
		s++;
	}

}


void lcd_puts_p(const char __flash *progmem_s)
/* print string from program memory on lcd  */
{
	register char c;

	while (c = (*(unsigned char __flash *)(progmem_s++))) { 
		lcd_putc(c);
	}

}

void InitCGram(void) // инициализация области CGRAM
{
unsigned char i;
lcd_write(0x40, 0); // установка видео-адреса в области CGRAM
__delay_cycles((CtrlClockRate/1000000)*100);
for(i=0;i<sizeof(SymbolCGRAM);i++) 
{
lcd_write(SymbolCGRAM[i],1); // загрузка символов в CGRAM
__delay_cycles((CtrlClockRate/1000000)*100);
}
}

void lcd_init(u08 dispAttr)
/* initialize display and select type of cursor */
/* dispAttr: LCD_DISP_OFF, LCD_DISP_ON, LCD_DISP_ON_CURSOR, LCD_DISP_CURSOR_BLINK */
{
    /*------ Initialize lcd to 4 bit i/o mode -------*/

	lcd_data_port_out();	/* all data port bits as output */
	LCD_RS_DDR |= (1<<LCD_RS_PIN);	/* RS pin as output */
	LCD_E_DDR |= (1<<LCD_E_PIN);	/* E  pin as output */


	//sbi(LCD_RS_PORT, LCD_RS_PIN);	/* RS pin as 1 */
	//sbi(LCD_E_PORT, LCD_E_PIN);	/* E  pin as 1 */

	__delay_cycles((CtrlClockRate/1000)*15);	/* wait 12ms or more after power-on       */

	/* initial write to lcd is 8bit */
	lcd_out_high(LCD_FUNCTION_8BIT_1LINE);
	lcd_e_toggle();
	__delay_cycles((CtrlClockRate/1000)*2);	/* delay, busy flag can't be checked here */

	lcd_out_high(LCD_FUNCTION_8BIT_1LINE);
	lcd_e_toggle();
	__delay_cycles((CtrlClockRate/1000)*2);	/* delay, busy flag can't be checked here */

	lcd_out_high(LCD_FUNCTION_8BIT_1LINE);
	lcd_e_toggle();
	__delay_cycles((CtrlClockRate/1000)*2);	/* delay, busy flag can't be checked here */

	lcd_out_high(LCD_FUNCTION_4BIT_1LINE);	/* set IO mode to 4bit */
	lcd_e_toggle();
	
		// main init for 4-bit interface
	//unsigned char i = 0;
	//for (i = 0; i < sizeof(byte_init); i++) lcd_write(byte_init[i], 0);
	
	
	/* from now the lcd only accepts 4 bit I/O, we can use lcd_command() */
	lcd_command(LCD_FUNCTION_DEFAULT);	/* function set: display lines  */
	lcd_command(LCD_DISP_OFF);	/* display off                  */
	lcd_clrscr();		/* display clear                */
	lcd_command(LCD_MODE_DEFAULT);	/* set entry mode               */
	lcd_command(dispAttr);	/* display/cursor control       */
	lcd_waitcmd(1);
        InitCGram();
}

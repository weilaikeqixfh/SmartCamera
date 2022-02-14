/*
 * wiringPi:
 *	Arduino compatable (ish) Wiring library for the Raspberry Pi
 *	Copyright (c) 2012 Gordon Henderson
 *	Additional code for pwmSetClock by Chris Hall <chris@kchall.plus.com>
 *
 *	Thanks to code samples from Gert Jan van Loo and the
 *	BCM2835 ARM Peripherals manual, however it's missing
 *	the clock section /grr/mutter/
 ***********************************************************************
 * This file is part of wiringPi:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as
 *    published by the Free Software Foundation, either version 3 of the
 *    License, or (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with wiringPi.
 *    If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
 */

// Revisions:
//	19 Jul 2012:
//		Moved to the LGPL
//		Added an abstraction layer to the main routines to save a tiny
//		bit of run-time and make the clode a little cleaner (if a little
//		larger)
//		Added waitForInterrupt code
//		Added piHiPri code
//
//	 9 Jul 2012:
//		Added in support to use the /sys/class/gpio interface.
//	 2 Jul 2012:
//		Fixed a few more bugs to do with range-checking when in GPIO mode.
//	11 Jun 2012:
//		Fixed some typos.
//		Added c++ support for the .h file
//		Added a new function to allow for using my "pin" numbers, or native
//			GPIO pin numbers.
//		Removed my busy-loop delay and replaced it with a call to delayMicroseconds
//
//	02 May 2012:
//		Added in the 2 UART pins
//		Change maxPins to numPins to more accurately reflect purpose


#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <limits.h>
#include "softPwm.h"
#include "softTone.h"
// #include <unistd.h>

#include "wiringPi.h"

#ifndef TRUE
#define TRUE (1==1)
#define FALSE (1==2)
#endif

// Environment Variables

#define ENV_DEBUG "WIRINGPI_DEBUG"
#define ENV_CODES "WIRINGPI_CODES"


struct wiringPiNodeStruct *wiringPiNodes = NULL;

// BCM Magic

#define BCM_PASSWORD  0x5A000000


// The BCM2835 has 54 GPIO pins.
//	BCM2835 data sheet, Page 90 onwards.
//	There are 6 control registers, each control the functions of a block
//	of 10 pins.
//	Each control register has 10 sets of 3 bits per GPIO pin - the ALT values
//
//	000 = GPIO Pin X is an input
//	001 = GPIO Pin X is an output
//	100 = GPIO Pin X takes alternate function 0
//	101 = GPIO Pin X takes alternate function 1
//	110 = GPIO Pin X takes alternate function 2
//	111 = GPIO Pin X takes alternate function 3
//	011 = GPIO Pin X takes alternate function 4
//	010 = GPIO Pin X takes alternate function 5
//
// So the 3 bits for port X are:
//	X / 10 + ((X % 10) * 3)

// Port function select bits

#define FSEL_INPT  0b000
#define FSEL_OUTP  0b001
#define FSEL_ALT0  0b100
#define FSEL_ALT1  0b101
#define FSEL_ALT2  0b110
#define FSEL_ALT3  0b111
#define FSEL_ALT4  0b011
#define FSEL_ALT5  0b010

// Access from ARM Running Linux
//	Taken from Gert/Doms code. Some of this is not in the manual
//	that I can find )-:

#define BCM2708_PERI_BASE                      0x20000000
#define GPIO_PADS         (BCM2708_PERI_BASE + 0x00100000)
#define CLOCK_BASE        (BCM2708_PERI_BASE + 0x00101000)
#define GPIO_BASE         (BCM2708_PERI_BASE + 0x00200000)
#define GPIO_TIMER        (BCM2708_PERI_BASE + 0x0000B000)
#define GPIO_PWM          (BCM2708_PERI_BASE + 0x0020C000)

#define PAGE_SIZE  (4*1024)
// Since BLOCK_SIZE is defined in /usr/include/linux/fs.h:
#ifdef BLOCK_SIZE
#undef BLOCK_SIZE
#endif
#define BLOCK_SIZE (6*1024) //setting 6 for support NanoPC-T3

// PWM
//	Word offsets into the PWM control region

#define PWM_CONTROL 0
#define PWM_STATUS  1
#define PWM0_RANGE  4
#define PWM0_DATA   5
#define PWM1_RANGE  8
#define PWM1_DATA   9

//	Clock regsiter offsets

#define PWMCLK_CNTL 40
#define PWMCLK_DIV  41

#define PWM0_MS_MODE    0x0080  // Run in MS mode
#define PWM0_USEFIFO    0x0020  // Data from FIFO
#define PWM0_REVPOLAR   0x0010  // Reverse polarity
#define PWM0_OFFSTATE   0x0008  // Ouput Off state
#define PWM0_REPEATFF   0x0004  // Repeat last value if FIFO empty
#define PWM0_SERIAL     0x0002  // Run in serial mode
#define PWM0_ENABLE     0x0001  // Channel Enable

#define PWM1_MS_MODE    0x8000  // Run in MS mode
#define PWM1_USEFIFO    0x2000  // Data from FIFO
#define PWM1_REVPOLAR   0x1000  // Reverse polarity
#define PWM1_OFFSTATE   0x0800  // Ouput Off state
#define PWM1_REPEATFF   0x0400  // Repeat last value if FIFO empty
#define PWM1_SERIAL     0x0200  // Run in serial mode
#define PWM1_ENABLE     0x0100  // Channel Enable

// Timer
//	Word offsets

#define TIMER_LOAD     (0x400 >> 2)
#define TIMER_VALUE    (0x404 >> 2)
#define TIMER_CONTROL  (0x408 >> 2)
#define TIMER_IRQ_CLR  (0x40C >> 2)
#define TIMER_IRQ_RAW  (0x410 >> 2)
#define TIMER_IRQ_MASK (0x414 >> 2)
#define TIMER_RELOAD   (0x418 >> 2)
#define TIMER_PRE_DIV  (0x41C >> 2)
#define TIMER_COUNTER  (0x420 >> 2)

// Locals to hold pointers to the hardware

static volatile uint32_t *gpio;
static volatile uint32_t *pwm;
static volatile uint32_t *clk;
static volatile uint32_t *pads;

#ifdef USE_TIMER
static volatile uint32_t *timer;
static volatile uint32_t *timerIrqRaw;
#endif

/**
 * add for NanoPC-T3 with S5P6818 chips
 */
#define T3_GPIO_BASE    (0xA000)
#define T3_BP_BASE      (0xC0010000)
#define POS_GPIO_OUT        0x00
#define POS_GPIO_ENB        0x04  //for GPIO out enable/1 is OUTMode 0 is InputMode
#define POS_GPIO_FAD        0x18  //for read input address
#define POS_GPIO_ALTFEN0    0x20
#define POS_GPIO_ALTFEN1    0x24

int BoardID  = 0;
//end for NanoPC-T3

/*add for BananaPro by LeMaker team*/
// for mmap BananaPro 
#define MAX_PIN_NUM  (0x40)  // 64
#define SUNXI_GPIO_BASE (0x01C20800)
#define MAP_SIZE (4096*2)
#define MAP_MASK (MAP_SIZE - 1)
//sunxi_pwm
//#define SUNXI_PWM_BASE (0x01c20e00)
#define SUNXI_PWM_BASE                 (0x01c21400)
#define SUNXI_PWM_CTRL_REG         (SUNXI_PWM_BASE)
#define SUNXI_PWM_CH0_PERIOD (SUNXI_PWM_BASE + 0x4)
#define SUNXI_PWM_CH1_PERIOD (SUNXI_PWM_BASE + 0x8)

#define SUNXI_PWM_CH0_EN          (1 << 4)
#define SUNXI_PWM_CH0_ACT_STA     (1 << 5)
#define SUNXI_PWM_SCLK_CH0_GATING (1 << 6)
#define SUNXI_PWM_CH0_MS_MODE     (1 << 7) // pulse mode
#define SUNXI_PWM_CH0_PUL_START   (1 << 8)

#define SUNXI_PWM_CH1_EN          (1 << 19)
#define SUNXI_PWM_CH1_ACT_STA     (1 << 20)
#define SUNXI_PWM_SCLK_CH1_GATING (1 << 21)
#define SUNXI_PWM_CH1_MS_MODE     (1 << 22) // pulse mode
#define SUNXI_PWM_CH1_PUL_START   (1 << 23)

#define PWM_CLK_DIV_120  0
#define PWM_CLK_DIV_180  1
#define PWM_CLK_DIV_240  2
#define PWM_CLK_DIV_360  3
#define PWM_CLK_DIV_480  4
#define PWM_CLK_DIV_12K  8
#define PWM_CLK_DIV_24K  9
#define PWM_CLK_DIV_36K  10
#define PWM_CLK_DIV_48K  11
#define PWM_CLK_DIV_72K  12

#define GPIO_PADS_BP  (0x00100000)
#define CLOCK_BASE_BP (0x00101000)
//	addr should 4K*n
//	#define GPIO_BASE_BP		(SUNXI_GPIO_BASE)
#define GPIO_BASE_BP  (0x01C20000)
#define GPIO_TIMER_BP (0x0000B000)
#define GPIO_PWM_BP   (0x01C21000)  // need 4k*n

static int wiringPinMode = WPI_MODE_UNINITIALISED;
int wiringPiCodes = FALSE;
/*end 2014.09.18*/

// Data for use with the boardId functions.
//	The order of entries here to correspond with the PI_MODEL_X
//	and PI_VERSION_X defines in wiringPi.h
//	Only intended for the gpio command - use at your own risk!
// Time for easy calculations

static uint64_t epochMilli, epochMicro;

// Misc

static int wiringPiMode = WPI_MODE_UNINITIALISED;
static volatile int pinPass = -1;

// Debugging & Return codes

int wiringPiDebug = FALSE; // guenter FALSE ;
int wiringPiReturnCodes = FALSE;

// sysFds:
//	Map a file descriptor from the /sys/class/gpio/gpioX/value

#define MAX_PIN_COUNT 74

static int sysFds [MAX_PIN_COUNT] ={
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// ISR Data

//static void (*isrFunctions [64])(void);

static int upDnConvert[3] = {7, 7, 5};

static int *pinToGpio = 0;
static int *physToGpio = 0;
static int *physToPin = 0;
static int *syspin = 0;


// Doing it the Arduino way with lookup tables...
//	Yes, it's probably more innefficient than all the bit-twidling, but it
//	does tend to make it all a bit clearer. At least to me!

// pinToGpio:
//	Take a Wiring pin (0 through X) and re-map it to the BCM_GPIO pin
//	Cope for 3 different board revisions here.

// WiringPiNr. gegeben .. -> Array GPIOx orange pi guenter neu
// A ab 0x00, B ab 0x20, C ab 0x40, D ab 0x50 ......
// 00 - 31 = PA00-PA31
// 32 - 63 = PB00-PB31
// 64 - 95 = PC00-PC31
// 96 - 127 = PD00-PD31
// 128 - 159 = PE00-PE31
// 160 - 191 = PF00-PF31
// 192 - 223 = PG00-PG31
// nanopi m1 done

// wPi number to /sys/gpio number
static int pinToGpio_m1 [MAX_PIN_COUNT] ={
    0,     6,  //  0,  1
    2,     3,  //  2,  3
    200, 201,  //  4,  5
    1,   203,  //  6,  7
    12,   11,  //  8,  9
    67,   17,  // 10, 11
    64,   65,  // 12, 13
    66,  198,  // 14, 15
    199,  -1,  // 16, 17
    -1,   -1,  // 18, 19
    -1,   20,  // 20, 21
    21,    8,  // 22, 23
    13,    9,  // 24, 25
    7,    16,  // 26, 27
    15,   14,  // 28, 29
    19,   18,  // 30, 31
    4, 5,      // 32, 33 Debug UART pins
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // ... 47
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // ... 63
    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// wPi number to /sys/gpio number
static int pinToGpio_neo [MAX_PIN_COUNT] ={
    0,         // 0
    /* 24 Pin */
    6,     2,  //  1,  2
    3,   200,  //  3,  4
    201,   1,  //  5,  6
    203,  12,  //  7,  8
    11,   67,  //  9, 10
    -1,   64,  // 11, 12
    65,   66,  // 13, 14
    198, 199,  // 15, 16
    4,     5,  // 17, 18
    17,   -1,  // 19, 20
    -1,    1,  // 21, 22
    -1,   -1,  // 23, 24
    /* 12 Pin */
    -1, -1,   // 25, 26
    -1, -1,   // 27, 28
    -1, -1,   // 29, 30
    -1, -1,   // 31, 32
    -1, -1,   // 33, 34
    -1, -1,   // 35, 36
    /* UART0 Tx, Rx */
    -1, -1,   // 37, 38
    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};


// wPi number to /sys/gpio number
static int pinToGpio_duo [MAX_PIN_COUNT] ={
    16,       // 0
    /* 32 Pin */
    -1,  14,  //  1,  2
    13,  -1,  //  3,  4
    -1,  -1,  //  5,  6
    15, 198,  //  7,  8
    199, -1,  //  9, 10
    -1,  12,  // 11, 12
    11,   4,  // 13, 14
     5, 203,  // 15, 16
    -1, 363,  // 17, 18
    -1, -1,   // 19, 20
    -1, -1,   // 21, 22
    -1, -1,   // 23, 24
    -1, -1,   // 25, 26
    -1, -1,   // 27, 28
    -1, -1,   // 29, 30
    -1, -1,   // 31, 32
    /* ---------nanopi duo end----------- */
    -1, -1,   // 33, 34
    -1, -1,   // 35, 36
    /* UART0 Tx,Rx */
    -1, -1,   // 37, 38
    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// wPi number to /sys/gpio number
static int pinToGpio_duo2 [MAX_PIN_COUNT] ={
    16,       // 0
    /* 32 Pin */
    -1,  14,  //  1,  2
    13,  -1,  //  3,  4
    -1,  -1,  //  5,  6
    15, 198,  //  7,  8
    -1, 199,  //  9, 10
    -1,  12,  // 11, 12
    11,   4,  // 13, 14
     5, 203,  // 15, 16
    -1, 363,  // 17, 18
    -1, -1,   // 19, 20
    -1, -1,   // 21, 22
    -1, -1,   // 23, 24
    -1, -1,   // 25, 26
    -1, -1,   // 27, 28
    -1, -1,   // 29, 30
    -1, -1,   // 31, 32
    /* ---------nanopi duo end----------- */
    -1, -1,  // 33, 34
    -1, -1,  // 35, 36
    /* UART0 Tx,Rx */
    -1, -1,  // 37, 38
    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// wPi number to /sys/gpio number
static int pinToGpio_neocore [MAX_PIN_COUNT] ={
    0,         // 0
    6,     2,  //  1,  2
    3,   200,  //  3,  4
    201,   1,  //  5,  6
    203,  12,  //  7,  8
    11,   67,  //  9, 10
    -1,   64,  // 11, 12
    65,   66,  // 13, 14
    198, 199,  // 15, 16
    4,     5,  // 17, 18
    17,   13,  // 19, 20
    14,   15,  // 21, 22
    16,    7,  // 23, 24
    -1, -1,    // 25, 26
    -1, -1,    // 27, 28
    -1, -1,    // 29, 30
    -1, -1,    // 31, 32
    -1, -1,    // 33, 34
    -1, -1,    // 35, 36
    -1, -1,    // 37, 38
    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// wPi number to /sys/gpio number
static int pinToGpio_t3 [MAX_PIN_COUNT] ={
    0,         // 0
    -1,   -1,  //  1,  2
    116, 112,  //  3,  4
    -1,   -1,  //  5,  6
    95,   96,  //  7,  8
    93,   94,  //  9, 10
    117, 113,  // 11, 12
    61,   60,  // 13, 14
    63,   62,  // 15, 16
    68,   71,  // 17, 18
    72,   88,  // 19, 20
    92,   58,  // 21, 22
    97,  104,  // 23, 24
    77, -1,    // 25, 26
    78, -1,    // 27, 28
    -1, -1,    // 29, 30
    -1, -1,    // 31, 32
    -1, -1,    // 33, 34
    -1, -1,    // 35, 36
    -1, -1,    // 37, 38
    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};


/*
static int pinTobcm [64] ={
    19, 18, //map to BCM GPIO0,1
    12, 11, //map to BCM GPIO2,3
    203, 20, //map to BCM GPIO4,5
    21, 17, //map to BCM GPIO6,7
    67, 65, //map to BCM GPIO8,9
    64, 66, //map to BCM GPIO10,11
    7, 8, //map to BCM GPIO12,13
    198, 199, //map to BCM GPIO14,15 
    16, 0, //map to BCM GPIO16,17 
    6, 13, //map to BCM GPIO18,19 
    15, 14, //map to BCM GPIO20,21
    3, 200, //map to BCM GPIO22,23
    201, 1, //map to BCM GPIO24,25
    9, 13, //map to BCM GPIO26,27

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 29... 44
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, //45... 60
    -1, -1, -1, -1 // ...63
};
*/

// physToGpio:
//	Take a physical pin (1 through 26) and re-map it to the BCM_GPIO pin
//	Cope for 2 different board revisions here.
//	Also add in the P5 connector, so the P5 pins are 3,4,5,6, so 53,54,55,56


static int physToGpio_neo [MAX_PIN_COUNT] ={
    -1, 
    /* 24 Pin */
    -1,   -1,  //  1,  2
    12,   -1,  //  3,  4
    11,   -1,  //  5,  6
    203, 198,  //  7,  8
    -1,  199,  //  9, 10
    0,     6,  // 11, 12
    2,    -1,  // 13, 14
    3,   200,  // 15, 16
    -1,  201,  // 17, 18
    64,   -1,  // 19, 20
    65,    1,  // 21, 22
    66,   67,  // 23, 24

    /* 12 Pin */
    -1, -1,   // 25, 26
    -1, -1,   // 27, 28
    -1, -1,   // 29, 30
    17, -1,   // 31, 32
    -1, -1,   // 33, 34
    -1, -1,   // 35, 36

    /* UART0 Tx,Rx */
    4, 5,     // 37, 38

    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int physToGpio_t3 [MAX_PIN_COUNT] ={
    -1, 
    /* 30 Pin */
    -1,   -1,  //  1,  2
    116, 112,  //  3,  4
    -1,   -1,  //  5,  6
    95,   96,  //  7,  8
    93,   94,  //  9, 10
    117, 113,  // 11, 12
    61,   60,  // 13, 14
    63,   62,  // 15, 16
    68,   71,  // 17, 18
    72,   88,  // 19, 20
    92,   58,  // 21, 22
    97,  104,  // 23, 24
    77, -1,   // 25, 26
    78, -1,   // 27, 28
    -1, -1,   // 29, 30

    /* 12 Pin */
    17, -1,   // 31, 32
    -1, -1,   // 33, 34
    -1, -1,   // 35, 36

    /* UART0 Tx,Rx */
    4, 5,     // 37, 38

    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int physToGpio_neocore [MAX_PIN_COUNT] ={
    -1, 
    /* GPIO-1 24Pin */
    -1,   -1,  //  1,  2
    12,   -1,  //  3,  4
    11,   -1,  //  5,  6
    203, 198,  //  7,  8
    -1,  199,  //  9, 10
    0,     6,  // 11, 12
    2,    -1,  // 13, 14
    3,   200,  // 15, 16
    -1,  201,  // 17, 18
    64,   -1,  // 19, 20
    65,    1,  // 21, 22
    66,   67,  // 23, 24

/* GPIO-2 24Pin */
    -1,  15,  // 25, 26  -> 1, 2
    -1,  16,  // 27, 28  -> 3, 4
    -1,  14,  // 29, 30  -> 5, 6
    -1,  13,  // 31, 32  -> 7, 8
    -1,  -1,  // 33, 34  -> 9, 10
    -1,  -1,  // 35, 36  -> 11, 12
    17,  -1,  // 37, 38  -> 13, 14
    -1,  -1,  // 39, 40  -> 15, 16
    -1,   5,  // 41, 42  -> 17, 18
    -1,   4,  // 43, 44  -> 19, 20
    -1,  -1,  // 45, 46  -> 21, 22
    -1,  -1,  // 47, 48  -> 23, 24

 /* GPIO-3 24Pin */
    -1,  -1,  // 49, 50  -> 1, 2
    -1,  -1,  // 51, 52  -> 3, 4
    -1,  -1,  // 53, 54  -> 5, 6
    -1,  -1,  // 55, 56  -> 7, 8
    -1,  -1,  // 57, 58  -> 9, 10
    -1,  -1,  // 59, 60  -> 11, 12
    -1,   7,  // 61, 62  -> 13, 14
    -1,  -1,  // 63, 64  -> 15, 16
    -1,  -1,  // 65, 66  -> 17, 18
    -1,  -1,  // 67, 68  -> 19, 20
    -1,  -1,  // 69, 70  -> 21, 22
    -1,  -1,  // 71, 72  -> 23, 24
    -1,       // 73
};

static int physToGpio_m1 [MAX_PIN_COUNT] ={
    -1,   // 0
    -1,   -1, //  1,  2
    12,   -1, //  3,  4
    11,   -1, //  5,  6
    203, 198, //  7,  8
    -1,  199, //  9, 10
    0,     6, // 11, 12
    2,    -1, // 13, 14
    3,   200, // 15, 16
    -1,  201, // 17, 18
    64,   -1, // 19, 20
    65,    1, // 21, 22
    66,   67, // 23, 24
    -1,   17, // 25, 26
    19,   18, // 27, 28
    20,   -1, // 29, 30
    21,    7, // 31, 32
    8,    -1, // 33, 34
    16,   13, // 35, 36
    9,    15, // 37, 38
    -1,   14, // 39, 40
    -1, -1, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 41-> 55
    -1, -1, -1, -1, -1, -1, -1, -1 // 56-> 63

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// phys pin number to /sys/gpio number
static int physToGpio_duo [MAX_PIN_COUNT] ={
    -1,        // 0
    /* 32 Pin */
    -1,   -1,  // 1,   2
    -1,   -1,  // 3,   4
    -1,   -1,  // 5,   6
    -1,   -1,  // 7,   8
    -1,   -1,  // 9,  10
    198, 363,  // 11, 12
    199,  -1,  // 13, 14
    15,   -1,  // 15, 16
    16,   -1,  // 17, 18
    14,   -1,  // 19, 20
    13,  203,  // 21, 22
    12,   -1,  // 23, 24
    11,   -1,  // 25, 26
    -1,   -1,  // 27, 28
    4,    -1,  // 29, 30
    5,    -1,  // 31, 32
    /* ---------nanopi duo end----------- */

    -1, -1,   // 33, 34
    -1, -1,   // 35, 36

    /* UART0 Tx,Rx */
    -1, -1,   // 37, 38

    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};


// phys pin number to /sys/gpio number
static int physToGpio_duo2 [MAX_PIN_COUNT] ={
    -1,        // 0
    /* 32 Pin */
    -1,   5,  //  1, 2
    -1,   4,  //  3, 4
    -1,  -1,  //  5, 6
    -1,  11,  //  7, 8
    363, 12,  //  9, 10
    203, 13,  // 11, 12
    -1,  14,  // 13, 14
    -1,  16,  // 15, 16
    -1,  15,  // 17, 18
    -1, 199,  // 19, 20
    -1, 198,  // 21, 22
    -1,  -1,  // 23, 24
    -1,  -1,  // 25, 26
    -1,  -1,  // 27, 28
    -1,  -1,  // 29, 30
    -1,  -1,  // 31, 32
    /* ---------nanopi duo end----------- */

     -1, -1,   // 33, 34
     -1, -1,   // 35, 36

     /* UART0 Tx,Rx */
     -1, -1,   // 37, 38

     /* 39~63 */
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

     /* 64~73 */
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

//

static int syspin_neo [MAX_PIN_COUNT] ={
    -1, -1, 2, 3, 4, 5, 6, 7, 
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int syspin_neocore [MAX_PIN_COUNT] ={
    -1, -1, 2, 3, 4, 5, 6, 7, 
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int syspin_t3 [MAX_PIN_COUNT] ={
    -1, -1, 2, 3, 4, 5, 6, 7, 
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};


static int syspin_m1 [MAX_PIN_COUNT] ={
    -1, -1, 2, 3, 4, 5, 6, 7, 
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int syspin_duo [MAX_PIN_COUNT] ={
    -1, -1, 2, 3, 4, 5, 6, 7, 
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int syspin_duo2 [MAX_PIN_COUNT] ={
    -1, -1, 2, 3, 4, 5, 6, 7, 
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/*static int edge [MAX_PIN_COUNT] ={
    -1, -1, -1, -1, 4, -1, -1, 7, //support the INT
    8, 9, 10, 11, -1, -1, 14, 15,
    -1, 17, -1, -1, -1, -1, 22, 23,
    24, 25, -1, 27, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	
    // 64~73
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};*/

static int physToPin_m1 [MAX_PIN_COUNT] = //return wiringPI pin
{
    -1,      // 0
    -1, -1,  //  1,  2
    8,  -1,  //  3,  4
    9,  -1,  //  5,  6
    7,  15,  //  7,  8
    -1, 16,  //  9, 10
    0,   1,  // 11, 12
    2,  -1,  // 13, 14
    3,   4,  // 15, 16
    -1,  5,  // 17, 18
    12, -1,  // 19, 20
    13,  6,  // 21, 22
    14, 10,  // 23, 24
    -1, 11,  // 25, 26

    30, 31,  // 27, 28
    21, -1,  // 29, 30
    22, 26,  // 31, 32
    23, -1,  // 33, 34
    24, 27,  // 35, 36
    25, 28,  // 37, 38
    -1, 29,  // 39, 40
    // Padding:

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // ... 56
    -1, -1, -1, -1, -1, -1, -1, // ... 63

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int physToPin_neo [MAX_PIN_COUNT] = //return wiringPI pin
{
    -1,       //  0
    -1, -1,   //  1,  2
    8,  -1,   //  3,  4
    9,  -1,   //  5,  6
    7,  15,   //  7,  8
    -1, 16,   //  9, 10
    0,   1,   // 11, 12
    2,  -1,   // 13, 14
    3,   4,   // 15, 16
    -1,  5,   // 17, 18
    12, -1,   // 19, 20
    13,  6,   // 21, 22
    14, 10,   // 23, 24

    -1,  -1,  // 25, 26
    -1,  -1,  // 27, 28
    -1,  -1,  // 29, 30
    19,  -1,  // 31, 32
    -1,  -1,  // 33, 34
    -1,  -1,  // 35, 36
    17,  18,  // 37, 38

    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int physToPin_t3 [MAX_PIN_COUNT] = //return wiringPI pin
{
    -1,        // 0
  -1,  -1,   // 1, 2
   3,   4,   // 3, 4
  -1,  -1,   // 5, 6
   7,   8,   // 7, 8

   9,  10,   // 9, 10
  11,  12,   //11, 12
  13,  14,   //13, 14
  15,  16,   //15, 16
  17,  18,   //17, 18
  19,  20,   //19, 20
  21,  22,   //21, 22
  23,  24,   //23, 24

  25,  -1,   //25, 26
  27,  -1,   //27, 28
  -1,  -1,   //29, 30

   /* ---------nanopc t3 end----------- */

  -1,  -1,   //31, 32
  -1,  -1,   //33, 34
  -1,  -1,   //35, 36

  -1,  -1,   //37, 38

    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};


static int physToPin_neocore [MAX_PIN_COUNT] = //return wiringPI pin
{
/* GPIO-1 24Pin */
    -1,      //  0
    -1, -1,  //  1,  2 ->  1,  2
    8,  -1,  //  3,  4 ->  3,  4
    9,  -1,  //  5,  6 ->  5,  6
    7,  15,  //  7,  8 ->  7,  8
    -1, 16,  //  9, 10 ->  9, 10
    0,   1,  // 11, 12 -> 11, 12
    2,  -1,  // 13, 14 -> 13, 14
    3,   4,  // 15, 16 -> 15, 16
    -1,  5,  // 17, 18 -> 17, 18
    12, -1,  // 19, 20 -> 19, 20
    13,  6,  // 21, 22 -> 21, 22
    14, 10,  // 23, 24 -> 23, 24

  /* GPIO-2 24Pin */
    -1,  22,  // 25, 26  ->  1,  2
    -1,  23,  // 27, 28  ->  3,  4
    -1,  21,  // 29, 30  ->  5,  6
    -1,  20,  // 31, 32  ->  7,  8
    -1,  -1,  // 33, 34  ->  9, 10
    -1,  -1,  // 35, 36  -> 11, 12
    19,  -1,  // 37, 38  -> 13, 14
    -1,  -1,  // 39, 40  -> 15, 16
    -1,  18,  // 41, 42  -> 17, 18
    -1,  17,  // 43, 44  -> 19, 20
    -1,  -1,  // 45, 46  -> 21, 22
    -1,  -1,  // 47, 48  -> 23, 24

   /* GPIO-3 24Pin */
    -1,  -1,  // 49, 50  ->  1,  2
    -1,  -1,  // 51, 52  ->  3,  4
    -1,  -1,  // 53, 54  ->  5,  6
    -1,  -1,  // 55, 56  ->  7,  8
    -1,  -1,  // 57, 58  ->  9, 10
    -1,  -1,  // 59, 60  -> 11, 12
    -1,  24,  // 61, 62  -> 13, 14
    -1,  -1,  // 63, 64  -> 15, 16
    -1,  -1,  // 65, 66  -> 17, 18
    -1,  -1,  // 67, 68  -> 19, 20
    -1,  -1,  // 69, 70  -> 21, 22
    -1,  -1,  // 71, 72  -> 23, 24
    -1,       // 73
};



static int physToPin_duo [MAX_PIN_COUNT] = //return wiringPI pin //note: same as physToWpi
{
    -1,       //  0
    -1,  -1,  //  1,  2
    -1,  -1,  //  3,  4
    -1,  -1,  //  5,  6
    -1,  -1,  //  7,  8
    -1,  -1,  //  9, 10
     8,  -1,  // 11, 12
     9,  -1,  // 13, 14
     7,  -1,  // 15, 16
     0,  -1,  // 17, 18
     2,  -1,  // 19, 20
     3,  16,  // 21, 22
    12,  -1,  // 23, 24
    13,  -1,  // 25, 26
    -1,  -1,  // 27, 28
    14,  -1,  // 29, 30
    15,  -1,  // 31, 32
     /* ---------nanopi duo end----------- */
    -1,  -1,  // 33, 34
    -1,  -1,  // 35, 36
    -1,  -1,  // 37, 38

    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static int physToPin_duo2 [MAX_PIN_COUNT] = //return wiringPI pin //note: same as physToWpi
{
    -1,      //  0
    -1, 15,  //  1,  2
    -1, 14,  //  3,  4
    -1, -1,  //  5,  6
    -1, 13,  //  7,  8
    -1, 12,  //  9, 10
    16,  3,  // 11, 12
    -1,  2,  // 13, 14
    -1,  0,  // 15, 16
    -1,  7,  // 17, 18
    -1,  9,  // 19, 20
    -1,  8,  // 21, 22
    -1, -1,  // 23, 24
    -1, -1,  // 25, 26
    -1, -1,  // 27, 28
    -1, -1,  // 29, 30
    -1, -1,  // 31, 32
    /* ---------nanopi duo end----------- */
    -1, -1,  // 33, 34
    -1, -1,  // 35, 36
    -1, -1,  // 37, 38

    /* 39~63 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    /* 64~73 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// pins available on pin out by banks
static int BP_PIN_MASK[9][32] = //[BANK]  [INDEX]
{
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,}, //PA
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 26, -1, 28, 29, 30, 31,}, //PB
    { 0,  1,  2,  3,  4, -1, -1,  7,  8, -1, -1, -1, -1, 13, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, 24, -1, -1, -1, 28, 29, 30, 31,}, //PC
    { 0,  1, -1, -1, -1, -1, -1, -1,  8, -1, -1, -1, -1, -1, 14, -1, 16, 17, -1, -1, 20, 21, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,}, //PD
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,}, //PE
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,}, //PF
    {-1, -1, -1, -1, -1, -1, 6, 7, 8, 9, -1, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,}, //PG
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,}, //PH
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,}, //PI
};



static int version = 0;
static int pwmmode = 0;

/*end 20140918*/

/*
 * Functions
 *********************************************************************************
 */


uint32_t readl(uint32_t addr) {
    uint32_t val = 0;
    uint32_t mmap_base = (addr & ~MAP_MASK);
    uint32_t mmap_seek = ((addr - mmap_base) >> 2);
    val = *(gpio + mmap_seek);
    return val;
    
}

void writel(uint32_t val, uint32_t addr) {
    uint32_t mmap_base = (addr & ~MAP_MASK);
    uint32_t mmap_seek = ((addr - mmap_base) >> 2);
    *(gpio + mmap_seek) = val;
}

const char * int2bin(uint32_t param) {
    int bits = sizeof(uint32_t)*CHAR_BIT;
    static char buffer[sizeof(uint32_t)*CHAR_BIT + 1];
    char chars[2] = {'0', '1'};
    int i,j,offset;
    for (i = 0; i < bits; i++) {
        j = bits - i - 1;
        offset = (param & (1 << j)) >> j;
        buffer[i] = chars[offset];
    }
    buffer[bits] = '\0';
    return buffer;
}


void print_pwm_reg() {
    uint32_t val = readl(SUNXI_PWM_CTRL_REG);
    uint32_t val2 = readl(SUNXI_PWM_CH0_PERIOD);
    if (wiringPiDebug) {
        printf("SUNXI_PWM_CTRL_REG: %s\n", int2bin(val));
        printf("SUNXI_PWM_CH0_PERIOD: %s\n", int2bin(val2));
    }
}

void sunxi_pwm_set_enable(int en) {
    int val = 0;
    val = readl(SUNXI_PWM_CTRL_REG);
    if (en) {
        val |= (SUNXI_PWM_CH0_EN | SUNXI_PWM_SCLK_CH0_GATING);
    }
    else {
        val &= ~(SUNXI_PWM_CH0_EN | SUNXI_PWM_SCLK_CH0_GATING);
    }
    if (wiringPiDebug)
        printf(">>function%s,no:%d,enable? :0x%x\n", __func__, __LINE__, val);
    writel(val, SUNXI_PWM_CTRL_REG);
    delay(1);
    print_pwm_reg();
}

void sunxi_pwm_set_mode(int mode) {
    int val = 0;
    val = readl(SUNXI_PWM_CTRL_REG);
    mode &= 1; //cover the mode to 0 or 1
    if (mode) { //pulse mode
        val |= (SUNXI_PWM_CH0_MS_MODE | SUNXI_PWM_CH0_PUL_START);
        pwmmode = 1;
    } else { //cycle mode
        val &= ~(SUNXI_PWM_CH0_MS_MODE);
        pwmmode = 0;
    }
    val |= (SUNXI_PWM_CH0_ACT_STA);
    if (wiringPiDebug)
        printf(">>function%s,no:%d,mode? :0x%x\n", __func__, __LINE__, val);
    writel(val, SUNXI_PWM_CTRL_REG);
    delay(1);
    print_pwm_reg();   
}

void sunxi_pwm_set_clk(int clk) {
    int val = 0;
    if (wiringPiDebug)
        printf(">>function%s,no:%d\n", __func__, __LINE__);
    // sunxi_pwm_set_enable(0);
    val = readl(SUNXI_PWM_CTRL_REG);
    if (wiringPiDebug)
        printf("read reg val: 0x%x\n", val);
    //clear clk to 0
    val &= 0xf801f0;
    val |= ((clk & 0xf) << 15); //todo check wether clk is invalid or not
    writel(val, SUNXI_PWM_CTRL_REG);
    sunxi_pwm_set_enable(1);
    if (wiringPiDebug)
        printf(">>function%s,no:%d,clk? :0x%x\n", __func__, __LINE__, val);
    delay(1);
    print_pwm_reg();
}

/**
 * ch0 and ch1 set the same,16 bit period and 16 bit act
 */
uint32_t sunxi_pwm_get_period(void) {
    uint32_t period_cys = 0;
    period_cys = readl(SUNXI_PWM_CH0_PERIOD); //get ch1 period_cys
    if (wiringPiDebug) {
        printf("periodcys: %d\n", period_cys);
    }
    period_cys &= 0xffff0000; //get period_cys
    period_cys = period_cys >> 16;
    if (wiringPiDebug)
        printf(">>func:%s,no:%d,period/range:%d", __func__, __LINE__, period_cys);
    delay(1);
    return period_cys;
}

uint32_t sunxi_pwm_get_act(void) {
    uint32_t period_act = 0;
    period_act = readl(SUNXI_PWM_CH0_PERIOD); //get ch1 period_cys
    period_act &= 0xffff; //get period_act
    if (wiringPiDebug)
        printf(">>func:%s,no:%d,period/range:%d", __func__, __LINE__, period_act);
    delay(1);
    return period_act;
}

void sunxi_pwm_set_period(int period_cys) {
    uint32_t val = 0;
    //all clear to 0
    if (wiringPiDebug)
        printf(">>func:%s no:%d\n", __func__, __LINE__);
    period_cys &= 0xffff; //set max period to 2^16
    period_cys = period_cys << 16;
    val = readl(SUNXI_PWM_CH0_PERIOD);
    if (wiringPiDebug)
        printf("read reg val: 0x%x\n", val);
    val &= 0x0000ffff;
    period_cys |= val;
    if (wiringPiDebug)
        printf("write reg val: 0x%x\n", period_cys);
    writel(period_cys, SUNXI_PWM_CH0_PERIOD);
    delay(1);
    val = readl(SUNXI_PWM_CH0_PERIOD);
    if (wiringPiDebug)
        printf("readback reg val: 0x%x\n", val);
    print_pwm_reg();
}

void sunxi_pwm_set_act(int act_cys) {
    uint32_t per0 = 0;
    //keep period the same, clear act_cys to 0 first
    if (wiringPiDebug)
        printf(">>func:%s no:%d\n", __func__, __LINE__);
    per0 = readl(SUNXI_PWM_CH0_PERIOD);
    if (wiringPiDebug)
        printf("read reg val: 0x%x\n", per0);
    per0 &= 0xffff0000;
    act_cys &= 0xffff;
    act_cys |= per0;
    if (wiringPiDebug)
        printf("write reg val: 0x%x\n", act_cys);
    writel(act_cys, SUNXI_PWM_CH0_PERIOD);
    delay(1);
    print_pwm_reg();
}

int sunxi_get_gpio_mode(int pin) {
    uint32_t regval = 0;
    int bank = pin >> 5;
    int index = pin - (bank << 5);
    int offset = ((index - ((index >> 3) << 3)) << 2);
    uint32_t reval = 0;
    volatile uint32_t phyaddr =0;

    if (BP_PIN_MASK[bank][index] == -1) {
      printf("line:%d pin(=%d) number error\n", __LINE__, pin);
      return reval;
    }

    if (BoardID == NanoPC_T3) {
      phyaddr = T3_GPIO_BASE + (bank * 4096) + POS_GPIO_ENB;
      if (wiringPiDebug)
        printf("func:%s pin:%d,  bank:%d index:%d phyaddr:0x%x\n", __func__,
               pin, bank, index, phyaddr);
      
      regval = *(uint32_t *)((uint32_t)gpio + phyaddr);
      if (wiringPiDebug)
        printf("read reg val: 0x%x offset:%d  return: %d\n", regval, offset,
               reval);
      reval = (regval >> index) & 1;
      if (wiringPiDebug)
        printf("read reg val: 0x%x offset:%d  return: %d\n", regval, offset,
               reval);
      return reval;
    }

    phyaddr = SUNXI_GPIO_BASE + (bank * 36) + ((index >> 3) << 2);
    if (wiringPiDebug)
        printf("func:%s pin:%d,  bank:%d index:%d phyaddr:0x%x\n", __func__, pin, bank, index, phyaddr);
    regval = readl(phyaddr);
    if (wiringPiDebug)
        printf("read reg val: 0x%x offset:%d  return: %d\n", regval, offset, reval);
    // reval=regval &(reval+(7 << offset));
    reval = (regval >> offset)&7;
    if (wiringPiDebug)
        printf("read reg val: 0x%x offset:%d  return: %d\n", regval, offset, reval);
    return reval;
}

void sunxi_set_gpio_mode(int pin, int mode) {
  uint32_t regval = 0;
  int bank = pin >> 5;
  int index = pin - (bank << 5);
  int offset = ((index - ((index >> 3) << 3)) << 2);
  uint32_t phyaddr = 0;

  // Preper date:
  uint32_t AltFun_addr = 0;
  int AltF_bit = 0;

  if (BP_PIN_MASK[bank][index] == -1) {
    printf("line:%d pin(%d) number error\n", __LINE__, pin);
    return;
  }

  if (BoardID == NanoPC_T3) {
    // Clear ALTFunciont
    if (index > 15) {
      AltF_bit = 2 * (index - 16);
      AltFun_addr = T3_GPIO_BASE + (bank * 4096) + POS_GPIO_ALTFEN1;
    } else {
      AltF_bit = 2 * index;
      AltFun_addr = T3_GPIO_BASE + (bank * 4096) + POS_GPIO_ALTFEN0;
    }
    if (wiringPiDebug)
      printf(
          "func:%s pin:%d,  bank:%d index:%d AltFun_addr:0x%x AltF_bit: %d "
          ".\n",
          __func__, pin, bank, index, AltFun_addr, AltF_bit);

#define regval_AltF *(uint32_t *)((uint32_t)gpio + AltFun_addr)

    if (wiringPiDebug)
      printf("Before clear,AltF values is 0x%x .\n", regval_AltF);
      
    regval_AltF &= ~(3 << AltF_bit);  // clear 2 bit with ALTFUN

    if (wiringPiDebug)
      printf("After clear, AltF values is 0x%x .\n", regval_AltF);

    // Set ALTFunc bit
    /*see more info with
    http://wiki.friendlyarm.com/wiki/images/d/d5/NanoPC-T2-T3-1603-Schematic.pdf
    GPIOB:26 28 29 30 31    fun1 [Altf_bit+1:Altf_bit]  01
    GPIOC:4 7 8 13 14 24    fun1
    
    GPIOC:28 29 30 31       fun0 [Altf_bit+1:Altf_bit]  00
    GPIOD:0 1 8 16 17 20 21 fun0
    */
    if (pin < 92) {  // setting Alt func bit.92 means GPIOC28
      regval_AltF |= (1 << AltF_bit);

      if (wiringPiDebug)
        printf("After set , AltF values is  0x%x .\n", regval_AltF);
    }

    phyaddr = T3_GPIO_BASE + (bank * 4096) + POS_GPIO_ENB;
    if (wiringPiDebug) {
      printf("func:%s pin:%d,  bank:%d index:%d phyaddr:0x%x\n", __func__, pin,
             bank, index, phyaddr);
    }
    // regval = *(uint32_t *)((uint32_t)gpio + phyaddr);
#define regvals *(uint32_t *)((uint32_t)gpio + phyaddr)  // in NanoPC-T3 ,only after #define bit address can
                                                         // save value,i don't know why....
    if (wiringPiDebug)  // waste me lots of time....  >_<
      printf("read reg val: 0x%x offset:%d .\n", regvals, offset);
      
    if (INPUT == mode) {
      regvals &= ~(1 << index);  // setting INPUT mode by bit 0

      if (wiringPiDebug) printf("Input mode set over reg val: 0x%x\n", regvals);
    } else if (OUTPUT == mode) {
      regvals |= (1 << index);  // setting OUT mode by bit 1

      if (wiringPiDebug)
        printf("Output mode set over reg val: 0x%x\n", regvals);
    } else if (PWM_OUTPUT == mode) {
      printf("not yet support NanoPC-T3 to pwm out mode!");
    }
    return;
  }

  phyaddr = SUNXI_GPIO_BASE + (bank * 36) + ((index >> 3) << 2);
  if (wiringPiDebug)
       printf("func:%s pin:%d, MODE:%d bank:%d index:%d phyaddr:0x%x\n", __func__, pin, mode, bank, index, phyaddr);
    regval = readl(phyaddr);
  if (wiringPiDebug) 
        printf("read reg val: 0x%x offset:%d\n", regval, offset);
  if (INPUT == mode) {
    regval &= ~(7 << offset);
    writel(regval, phyaddr);
    regval = readl(phyaddr);
    if (wiringPiDebug) 
    printf("Input mode set over reg val: 0x%x\n", regval);
  } else if (OUTPUT == mode) {
    regval &= ~(7 << offset);
    regval |= (1 << offset);
    if (wiringPiDebug) 
    printf("Out mode ready set val: 0x%x\n", regval);
    writel(regval, phyaddr);
    regval = readl(phyaddr);
    if (wiringPiDebug) 
    printf("Out mode set over reg val: 0x%x\n", regval);
  } 
  else if (PWM_OUTPUT == mode) {
    // set pin PWMx to pwm mode
    regval &= ~(7 << offset);
    regval |= (0x3 << offset);
    if (wiringPiDebug)
      printf(">>>>>line:%d PWM mode ready to set val: 0x%x\n", __LINE__, regval);
    writel(regval, phyaddr);
    delayMicroseconds(200);
    regval = readl(phyaddr);
    if (wiringPiDebug) 
    printf("<<<<<PWM mode set over reg val: 0x%x\n", regval);
    //clear all reg
    writel(0, SUNXI_PWM_CTRL_REG);
    writel(0, SUNXI_PWM_CH0_PERIOD);

    //set default M:S to 1/2
    sunxi_pwm_set_period(1024);
    sunxi_pwm_set_act(512);
    pwmSetMode(PWM_MODE_MS);
    sunxi_pwm_set_clk(PWM_CLK_DIV_120); //default clk:24M/120
    delayMicroseconds(200);
  }

  return;
}

void sunxi_digitalWrite(int pin, int value) {
    uint32_t regval = 0;
    uint32_t GPIOOUT_addr = 0;
    int bank = pin >> 5;
    int index = pin - (bank << 5);
    if (BP_PIN_MASK[bank][index] == -1) {
      printf("pin number error\n");
      return;
    }
    if (BoardID == NanoPC_T3) {
      GPIOOUT_addr = T3_GPIO_BASE + (bank * 4096);
      if (wiringPiDebug)
        printf("func:%s pin:%d, value:%d bank:%d index:%d phyaddr:0x%x\n",
               __func__, pin, value, bank, index, GPIOOUT_addr);
//in NanoPC-T3 ,only after #define bit address can save value,i don't know why....
// 在NanoPC-T3上，如果要修改寄存器里的数据，一定要#define之后操作才行，
#define regval_out *(uint32_t *)((uint32_t)gpio + GPIOOUT_addr)
      if (wiringPiDebug)
        printf("befor write reg val: 0x%x,index:%d\n", regval_out, index);
      if (0 == value) {
        regval_out &= ~(1 << index);
        if (wiringPiDebug)
          printf("LOW val set over reg val: 0x%x\n", regval_out);
      } else {
        regval_out |= (1 << index);
        if (wiringPiDebug)
          printf("HIGH val set over reg val: 0x%x\n", regval_out);
      }
      return;
    }

    uint32_t phyaddr = SUNXI_GPIO_BASE + (bank * 36) + 0x10; // +0x10 -> data reg
    if (wiringPiDebug)
      printf("func:%s pin:%d, value:%d bank:%d index:%d phyaddr:0x%x\n", __func__, pin, value, bank, index, phyaddr);
    regval = readl(phyaddr);
    if (wiringPiDebug)
      printf("befor write reg val: 0x%x,index:%d\n", regval, index);
    if (0 == value) {
      regval &= ~(1 << index);
      writel(regval, phyaddr);
      regval = readl(phyaddr);
      if (wiringPiDebug) 
      printf("LOW val set over reg val: 0x%x\n", regval);
        } else {
            regval |= (1 << index);
            writel(regval, phyaddr);
            regval = readl(phyaddr);
            if (wiringPiDebug)
                printf("HIGH val set over reg val: 0x%x\n", regval);
        }
    

    return;
}

int sunxi_digitalRead(int pin) {
    uint32_t regval = 0;
    uint32_t reval = 0;
    uint32_t phyaddr;
    int bank = pin >> 5;
    int index = pin - (bank << 5);
    if (BP_PIN_MASK[bank][index] == -1) {
      printf("line:%d pin(=%d) number error\n", __LINE__, pin);
      return regval;
    }

    if (BoardID == NanoPC_T3) {
      phyaddr = T3_GPIO_BASE + (bank * 4096) + POS_GPIO_FAD;
      if (wiringPiDebug)
        printf("func:%s pin:%d,  bank:%d index:%d phyaddr:0x%x\n", __func__,
               pin, bank, index, phyaddr);
      regval = *(uint32_t *)((uint32_t)gpio + phyaddr);
      if (wiringPiDebug)
        printf("read reg val: 0x%x  pin:%d  return: %d\n", regval, pin,
               reval);
      reval = (regval >> index) & 1;
      if (wiringPiDebug)
        printf("read reg val: 0x%x  pin:%d  return: %d\n", regval, pin,
               reval);
      return reval;
    }

    phyaddr = SUNXI_GPIO_BASE + (bank * 36) + 0x10; // +0x10 -> data reg
    if (wiringPiDebug)
        printf("func:%s pin:%d,bank:%d index:%d phyaddr:0x%x\n", __func__, pin, bank, index, phyaddr);
    if (BP_PIN_MASK[bank][index] != -1) {
        regval = readl(phyaddr);
        regval = regval >> index;
        regval &= 1;
        if (wiringPiDebug)
            printf("***** read reg val: 0x%x,bank:%d,index:%d,line:%d\n", regval, bank, index, __LINE__);
        return regval;
    } else {
        printf("pin number error\n");
        return regval;
    }
}

void sunxi_pullUpDnControl(int pin, int pud) {
    uint32_t regval = 0;
    int bank = pin >> 5;
    int index = pin - (bank << 5);
    int sub = index >> 4;
    int sub_index = index - 16 * sub;
    uint32_t phyaddr = SUNXI_GPIO_BASE + (bank * 36) + 0x1c + sub * 4; // +0x10 -> pullUpDn reg
    if (wiringPiDebug)
        printf("func:%s pin:%d,bank:%d index:%d sub:%d phyaddr:0x%x\n", __func__, pin, bank, index, sub, phyaddr);
    if (BP_PIN_MASK[bank][index] != -1) { //PI13~PI21 need check again
        regval = readl(phyaddr);
        if (wiringPiDebug)
            printf("pullUpDn reg:0x%x, pud:0x%x sub_index:%d\n", regval, pud, sub_index);
        regval &= ~(3 << (sub_index << 1));
        regval |= (pud << (sub_index << 1));
        if (wiringPiDebug)
            printf("pullUpDn val ready to set:0x%x\n", regval);
        writel(regval, phyaddr);
        regval = readl(phyaddr);
        if (wiringPiDebug)
            printf("pullUpDn reg after set:0x%x  addr:0x%x\n", regval, phyaddr);
    } else {
        printf("pin number error\n");
    }
    delay(1);
    return;
}
/*end 2014.09.18*/

/*
 * wiringPiFailure:
 *	Fail. Or not.
 *********************************************************************************
 */

int wiringPiFailure(int fatal, const char *message, ...) {
    va_list argp;
    char buffer [1024];

    if (!fatal && wiringPiReturnCodes)
        return -1;

    va_start(argp, message);
    vsnprintf(buffer, 1023, message, argp);
    va_end(argp);

    fprintf(stderr, "%s", buffer);
    exit(EXIT_FAILURE);

    return 0;
}

/*
 * piBoardRev:
 *	Return a number representing the hardware revision of the board.
 *
 *	Revision 1 really means the early Model B's.
 *	Revision 2 is everything else - it covers the B, B+ and CM.
 *
 *	Seems there are some boards with 0000 in them (mistake in manufacture)
 *	So the distinction between boards that I can see is:
 *	0000 - Error
 *	0001 - Not used 
 *	0002 - Model B,  Rev 1,   256MB, Egoman
 *	0003 - Model B,  Rev 1.1, 256MB, Egoman, Fuses/D14 removed.
 *	0004 - Model B,  Rev 2,   256MB, Sony
 *	0005 - Model B,  Rev 2,   256MB, Qisda
 *	0006 - Model B,  Rev 2,   256MB, Egoman
 *	0007 - Model A,  Rev 2,   256MB, Egoman
 *	0008 - Model A,  Rev 2,   256MB, Sony
 *	0009 - Model A,  Rev 2,   256MB, Qisda
 *	000d - Model B,  Rev 2,   512MB, Egoman
 *	000e - Model B,  Rev 2,   512MB, Sony
 *	000f - Model B,  Rev 2,   512MB, Qisda
 *	0010 - Model B+, Rev 1.2, 512MB, Sony
 *	0011 - Pi CM,    Rev 1.2, 512MB, Sony
 *
 *	A small thorn is the olde style overvolting - that will add in
 *		1000000
 *
 *	The Pi compute module has an revision of 0011 - since we only check the
 *	last digit, then it's 1, therefore it'll default to not 2 or 3 for a
 *	Rev 1, so will appear as a Rev 2. This is fine for the most part, but
 *	we'll properly detect the Compute Module later and adjust accordingly.
 *
 *********************************************************************************
 */

static void piBoardRevOops(const char *why) {
    fprintf(stderr, "piBoardRev: Unable to determine board revision from /proc/cpuinfo\n");
    fprintf(stderr, " -> %s\n", why);
    fprintf(stderr, " ->  You may want to check:\n");
    fprintf(stderr, " ->  http://www.lemaker.org/\n"); /*modify for BananaPro by LeMmaker team*/
    exit(EXIT_FAILURE);
}

/*add for BananaPro by LeMaker team*/
int isA20(void) {
    FILE *cpuFd;
    char line [120];
    char *d;
    if ((cpuFd = fopen("/proc/cpuinfo", "r")) == NULL)
        piBoardRevOops("Unable to open /proc/cpuinfo");
    while (fgets(line, 120, cpuFd) != NULL) {
        if (strncmp(line, "Hardware", 8) == 0)
            break;
    }

    fclose(cpuFd);
    if (strncmp(line, "Hardware", 8) != 0)
        piBoardRevOops("No \"Hardware\" line");

    for (d = &line [strlen(line) - 1]; (*d == '\n') || (*d == '\r'); --d)
        *d = 0;
    if (wiringPiDebug)
        printf("piboardRev: Hardware string: %s\n", line);

    if (strstr(line, "sun7i") != NULL) {
        if (wiringPiDebug)
            printf("Hardware:%s\n", line);
        return 1;
    } else {
        if (wiringPiDebug)
            printf("Hardware:%s\n", line);
        return 0;
    }
}

int piBoardRev(void) {
    BoardHardwareInfo* retBoardInfo;
    int ret = getBoardType(&retBoardInfo);
    if (ret >= 0) {
      if (retBoardInfo->boardTypeId > ALLWINNER_BASE && retBoardInfo->boardTypeId <= ALLWINNER_MAX 
                && retBoardInfo->boardTypeId != NanoPi_A64) {
        version = BPRVER;
        if (wiringPiDebug)
            printf("piBoardRev:  %d\n", version);
        return BPRVER;
      } else {
        if (retBoardInfo->boardTypeId == NanoPC_T3) {
          printf("if use NanoPC-T3, only support GPIO input and GPIO out!!\n");
        } else {
          printf("This NanoPi model is currently not supported. \n");
        }
      }
    } else {
        piBoardRevOops("Is not NanoPi based board. ");
    }
    return 0;
}

/*
 * piBoardId:
 *	Do more digging into the board revision string as above, but return
 *	as much details as we can.
 *	This is undocumented and really only intended for the GPIO command.
 *	Use at your own risk!
 *********************************************************************************
 */

void piBoardId(int *model, int *rev, int *mem, int *maker, int *overVolted) {
    (void) piBoardRev(); // Call this first to make sure all's OK. Don't care about the result.

    BoardHardwareInfo* retBoardInfo;
    int ret = getBoardType(&retBoardInfo);
    if (ret >= 0) {
        if ((retBoardInfo->boardTypeId > ALLWINNER_BASE && retBoardInfo->boardTypeId <= ALLWINNER_MAX 
                && retBoardInfo->boardTypeId != NanoPi_A64)||retBoardInfo->boardTypeId == NanoPC_T3) {
            *model = retBoardInfo->boardTypeId;
            *rev = PI_VERSION_1_2;
            *mem = 1024;
            *maker = PI_MAKER_FRIENDLYELEC;
        } else {
            *model = 0;
            *rev = 0;
            *mem = 0;
            *maker = 0; 
        }
    } else {
        *model = 0;
        *rev = 0;
        *mem = 0;
        *maker = 0;
    }
    *overVolted = 0;
}

/*
 * wpiPinToGpio:
 *	Translate a wiringPi Pin number to native GPIO pin number.
 *	Provided for external support.
 *********************************************************************************
 */

int wpiPinToGpio(int wpiPin) {
    if (pinToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return -1;
    }
	if (wpiPin >= MAX_PIN_COUNT || wpiPin < 0) {
        printf("Invalid pin index.\n");
        return -1;
	}
    return pinToGpio [wpiPin];
}

/*
 * physPinToGpio:
 *	Translate a physical Pin number to native GPIO pin number.
 *	Provided for external support.
 *********************************************************************************
 */

int physPinToGpio(int physPin) {
    if (physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return -1;
    }
	if (physPin >= MAX_PIN_COUNT || physPin < 0) {
        printf("Invalid pin index.\n");
        return -1;
	}
    return physToGpio [physPin];
}

/*
 * physPinToGpio:
 *	Translate a physical Pin number to wiringPi  pin number. add by lemaker team for BananaPi
 *	Provided for external support.
 *********************************************************************************
 */
int physPinToPin(int physPin) {
    if (physToPin == 0) {
        printf("please call wiringPiSetup first.\n");
        return -1;
    }
	if (physPin >= MAX_PIN_COUNT || physPin < 0) {
        printf("Invalid pin index.\n");
        return -1;
	}
    return physToPin [physPin];
}

/*
 * setPadDrive:
 *	Set the PAD driver value
 *********************************************************************************
 */

void setPadDrive(int group, int value) {
    return;
}

/*
 * getAlt:
 *	Returns the ALT bits for a given port. Only really of-use
 *	for the gpio readall command (I think)
 *********************************************************************************
 */

int getAlt(int pin) {
    int alt;

	if (pin >= MAX_PIN_COUNT || pin < 0) {
        printf("Invalid pin index.\n");
        return -1;
	}
	
    if (pinToGpio == 0 || physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return -1;
    }

    //printf("[%s:L%d] the pin:%d  mode: %d is invaild,please check it over!\n", __func__,  __LINE__, pin, wiringPiMode);
    if (wiringPiMode == WPI_MODE_PINS)
        pin = pinToGpio [pin];
    else if (wiringPiMode == WPI_MODE_PHYS)
        pin = physToGpio[pin];
    else if (wiringPiMode == WPI_MODE_GPIO) {
        //pin = pinTobcm[pin];
        // pin = pin;
        // Nothing
    } else {
        return 0;
    }
    if (-1 == pin) {
        printf("[%s:L%d] the pin:%d  mode: %d is invaild,please check it over!\n", __func__, __LINE__, pin, wiringPiMode);
        return -1;
    }
    alt = sunxi_get_gpio_mode(pin);
    return alt;
}

int getAltSilence(int pin) {
    int alt;

    if (pin >= MAX_PIN_COUNT || pin < 0) {
        return -1;
    }

    if (pinToGpio == 0 || physToGpio == 0) {
        return -1;
    }

    //printf("[%s:L%d] the pin:%d  mode: %d is invaild,please check it over!\n", __func__,  __LINE__, pin, wiringPiMode);
    if (wiringPiMode == WPI_MODE_PINS)
        pin = pinToGpio [pin];
    else if (wiringPiMode == WPI_MODE_PHYS)
        pin = physToGpio[pin];
    else if (wiringPiMode == WPI_MODE_GPIO) {
        // pin = pinTobcm[pin];
        // pin = pin;
        // Nothing
    } else {
        return 0;
    }

    if (-1 == pin) {
        return -1;
    }
    alt = sunxi_get_gpio_mode(pin);
    return alt;
}

/*
 * pwmSetMode:
 *	Select the native "balanced" mode, or standard mark:space mode
 *********************************************************************************
 */

void pwmSetMode(int mode) {
    sunxi_pwm_set_mode(mode);
    return;
}

/*
 * pwmSetRange:
 *	Set the PWM range register. We set both range registers to the same
 *	value. If you want different in your own code, then write your own.
 *********************************************************************************
 */

void pwmSetRange(unsigned int range) {
    sunxi_pwm_set_period(range);
    return;
}

/*
 * pwmSetClock:
 *	Set/Change the PWM clock. Originally my code, but changed
 *	(for the better!) by Chris Hall, <chris@kchall.plus.com>
 *	after further study of the manual and testing with a 'scope
 *********************************************************************************
 */

void pwmSetClock(int divisor) {
    sunxi_pwm_set_clk(divisor);
    sunxi_pwm_set_enable(1);
    return;
}

/*
 * gpioClockSet:
 *	Set the freuency on a GPIO clock pin
 *********************************************************************************
 */

void gpioClockSet(int pin, int freq) {
    return;
}

/*
 * wiringPiFindNode:
 *      Locate our device node
 *********************************************************************************
 */

struct wiringPiNodeStruct *wiringPiFindNode(int pin) {
    struct wiringPiNodeStruct *node = wiringPiNodes;

    while (node != NULL)
        if ((pin >= node->pinBase) && (pin <= node->pinMax))
            return node;
        else
            node = node->next;

    return NULL;
}

/*
 * wiringPiNewNode:
 *	Create a new GPIO node into the wiringPi handling system
 *********************************************************************************
 */

static void pinModeDummy(struct wiringPiNodeStruct *node, int pin, int mode) {
    return;
}

static void pullUpDnControlDummy(struct wiringPiNodeStruct *node, int pin, int pud) {
    return;
}

static int digitalReadDummy(struct wiringPiNodeStruct *node, int pin) {
    return LOW;
}

static void digitalWriteDummy(struct wiringPiNodeStruct *node, int pin, int value) {
    return;
}

static void pwmWriteDummy(struct wiringPiNodeStruct *node, int pin, int value) {
    return;
}

static int analogReadDummy(struct wiringPiNodeStruct *node, int pin) {
    return 0;
}

static void analogWriteDummy(struct wiringPiNodeStruct *node, int pin, int value) {
    return;
}

struct wiringPiNodeStruct *wiringPiNewNode(int pinBase, int numPins) {
    int pin;
    struct wiringPiNodeStruct *node;

    // Minimum pin base is 64
    if (pinBase < 64)
        (void)wiringPiFailure(WPI_FATAL, "wiringPiNewNode: pinBase of %d is < 64\n", pinBase);

    // Check all pins in-case there is overlap:
    for (pin = pinBase; pin < (pinBase + numPins); ++pin)
        if (wiringPiFindNode(pin) != NULL)
            (void)wiringPiFailure(WPI_FATAL, "wiringPiNewNode: Pin %d overlaps with existing definition\n", pin);

    node = (struct wiringPiNodeStruct *) calloc(sizeof (struct wiringPiNodeStruct), 1); // calloc zeros
    if (node == NULL) {
        (void)wiringPiFailure(WPI_FATAL, "wiringPiNewNode: Unable to allocate memory: %s\n", strerror(errno));
    } else {
        node->pinBase = pinBase;
        node->pinMax = pinBase + numPins - 1;
        node->pinMode = pinModeDummy;
        node->pullUpDnControl = pullUpDnControlDummy;
        node->digitalRead = digitalReadDummy;
        node->digitalWrite = digitalWriteDummy;
        node->pwmWrite = pwmWriteDummy;
        node->analogRead = analogReadDummy;
        node->analogWrite = analogWriteDummy;
        node->next = wiringPiNodes;
        wiringPiNodes = node;
    }
    return node;
}


/*
 *********************************************************************************
 * Core Functions
 *********************************************************************************
 */

/*
 * pinModeAlt:
 *	This is an un-documented special to let you set any pin to any mode
 *********************************************************************************
 */

void pinModeAlt(int pin, int mode) {
    return;
}

/*
 * pinMode:
 *	Sets the mode of a pin to be input, output or PWM output
 *********************************************************************************
 */

void pinMode(int pin, int mode) {
    struct wiringPiNodeStruct *node = wiringPiNodes;

    if (wiringPiDebug)
        printf("Func: %s, Line: %d,pin:%d,mode:%d\n", __func__, __LINE__, pin, mode);

    if (pinToGpio == 0 || physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return;
    }

    // On-board pin
    if (pin >= 0 && pin < MAX_PIN_COUNT) {
        if (wiringPiMode == WPI_MODE_PINS) {
            pin = pinToGpio[pin];
            if (wiringPiDebug) {
                printf(">>> pinToGpio[pin] ret %d\n", pin);
            }
        } else if (wiringPiMode == WPI_MODE_PHYS) {
            pin = physToGpio[pin];
            if (wiringPiDebug) {
                printf(">>> physToGpio[pin] ret %d\n", pin);
            }
        } else if (wiringPiMode == WPI_MODE_GPIO) {                 // pin = pinTobcm[pin];
            // pin = pin;
            if (wiringPiDebug) {
                printf(">>> pinTobcm[pin] ret %d\n", pin);
            }
        } else {
            if (wiringPiDebug) {
                printf(">>> unknow wiringPiMode\n");
            }
            return;
        }
        /*VCC or GND return directly*/
        if (-1 == pin) {
            //printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__,  __LINE__, pin);
            return;
        }

        if (mode == INPUT) {
            sunxi_set_gpio_mode(pin, INPUT);
            wiringPinMode = INPUT;
            return;
        } else if (mode == OUTPUT) {
            sunxi_set_gpio_mode(pin, OUTPUT); //gootoomoon_set_mode
            wiringPinMode = OUTPUT;
            return;
        } else if (mode == PWM_OUTPUT) {
            if (pin != 5) {
                printf("the pin you choose doesn't support hardware PWM\n");
                printf("you can select wiringPi pin %d for PWM pin\n", 1);
                printf("or you can use it in softPwm mode\n");
                return;
            }
            //printf("you choose the hardware PWM:%d\n", 1);
            sunxi_set_gpio_mode(pin, PWM_OUTPUT);
            wiringPinMode = PWM_OUTPUT;
            return;
        } else {
            return;
        }
    } else {
        if ((node = wiringPiFindNode(pin)) != NULL)
            node->pinMode(node, pin, mode);
        return;
    }
}

/*
 * pullUpDownCtrl:
 *	Control the internal pull-up/down resistors on a GPIO pin
 *	The Arduino only has pull-ups and these are enabled by writing 1
 *	to a port when in input mode - this paradigm doesn't quite apply
 *	here though.
 *********************************************************************************
 */

void pullUpDnControl(int pin, int pud) {
    struct wiringPiNodeStruct *node = wiringPiNodes;

    if (pinToGpio == 0 || physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return;
    }

    pud = upDnConvert[pud];

    if (pin >= 0 && pin < MAX_PIN_COUNT) {
        if (wiringPiMode == WPI_MODE_PINS)
            pin = pinToGpio [pin];
        else if (wiringPiMode == WPI_MODE_PHYS)
            pin = physToGpio[pin];
        else if (wiringPiMode == WPI_MODE_GPIO) {
            // pin = pinTobcm[pin]; 
            // pin = pin;
            // Nothing
        } else {
            return;
        }
        if (wiringPiDebug)
            printf("%s,%d,pin:%d\n", __func__, __LINE__, pin);

        if (-1 == pin) {
            printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__, __LINE__, pin);
            return;
        }
        pud &= 3;
        sunxi_pullUpDnControl(pin, pud);
        return;
    } else { // Extension module
        if ((node = wiringPiFindNode(pin)) != NULL)
            node->pullUpDnControl(node, pin, pud);
        return;
    }
}

/*
 * digitalRead:
 *	Read the value of a given Pin, returning HIGH or LOW
 *********************************************************************************
 */

int digitalRead(int pin) {
    char c;
    struct wiringPiNodeStruct *node = wiringPiNodes;
    if (wiringPiDebug)
      printf("Func: %s, Line: %d,pin:%d\n", __func__, __LINE__, pin);
    if (pinToGpio == 0 || physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return 0;
    }

    if (pin == 0 && wiringPiMode == WPI_MODE_GPIO_SYS) {
        //printf("%d %s,%d invalid pin,please check it over.\n",pin,__func__, __LINE__);
        return 0;
    }
    if (pin >= 0 && pin < MAX_PIN_COUNT) {
        if (wiringPiMode == WPI_MODE_GPIO_SYS) { // Sys mode
		    if (wiringPiDebug) {
			    printf("in digitalRead, wiringPiMode == WPI_MODE_GPIO_SYS\n");
		    }
            //TODO: fix me
            /*
            if (syspin[pin] == -1) {
                //printf("%d %s,%d invalid pin,please check it over.\n",pin,__func__, __LINE__);
                return 0;
            }
            */
            if (sysFds [pin] == -1) {
			    if (wiringPiDebug)
				    printf("pin %d sysFds -1.%s,%d\n", pin, __func__, __LINE__);
			    return LOW;
		    }
		    if (wiringPiDebug)
			    printf("pin %d :%d.%s,%d\n", pin, sysFds [pin], __func__, __LINE__);
		    lseek(sysFds [pin], 0L, SEEK_SET);
		    read(sysFds [pin], &c, 1);
		    return (c == '0') ? LOW : HIGH;
	    } else if (wiringPiMode == WPI_MODE_PINS) {
		    pin = pinToGpio [pin];
		    if (wiringPiDebug) {
			    printf(">>> pinToGpio[pin] ret %d\n", pin);
		    }
	    } else if (wiringPiMode == WPI_MODE_PHYS) {
		    pin = physToGpio[pin];
		    if (wiringPiDebug) {
			    printf(">>> physToGpio[pin] ret %d\n", pin);
		    }
	    } else if (wiringPiMode == WPI_MODE_GPIO) {
		    // pin = pinTobcm[pin]; 
            // pin = pin;

		    if (wiringPiDebug) {
			    printf(">>> pinTobcm[pin] ret %d\n", pin);
		    }
	    } else {
		    return LOW;
	    }
	    if (-1 == pin) {
            printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__, __LINE__, pin);
		    return LOW;
	    }
	    return sunxi_digitalRead(pin);
    } else {
        if ((node = wiringPiFindNode(pin)) == NULL)
            return LOW;
        return node->digitalRead(node, pin);
    }
}

int digitalReadSilence(int pin) {
    char c;
    struct wiringPiNodeStruct *node = wiringPiNodes;

    if (pinToGpio == 0 || physToGpio == 0) {
        return 0;
    }

    if (pin == 0 && wiringPiMode == WPI_MODE_GPIO_SYS) {
        return 0;
    }

    if (pin >= 0 && pin < MAX_PIN_COUNT) {
        if (wiringPiMode == WPI_MODE_GPIO_SYS) { // Sys mode
            if (sysFds [pin] == -1) {
                return LOW;
            }
            lseek(sysFds [pin], 0L, SEEK_SET);
            read(sysFds [pin], &c, 1);
            return (c == '0') ? LOW : HIGH;
        } else if (wiringPiMode == WPI_MODE_PINS) {
            pin = pinToGpio [pin];
        } else if (wiringPiMode == WPI_MODE_PHYS) {
            pin = physToGpio[pin];
        } else if (wiringPiMode == WPI_MODE_GPIO) {
            // pin = pinTobcm[pin];
            // pin = pin;
            // Nothing
        } else {
            return LOW;
        }
        if (-1 == pin) {
            return LOW;
        }
        return sunxi_digitalRead(pin);
    } else {
        if ((node = wiringPiFindNode(pin)) == NULL)
            return LOW;
        return node->digitalRead(node, pin);
    }
}

/*
 * digitalWrite:
 *	Set an output bit
 *********************************************************************************
 */

void digitalWrite(int pin, int value) {
    struct wiringPiNodeStruct *node = wiringPiNodes;

    if (wiringPiDebug)
        printf("%s,%d\n", __func__, __LINE__);

    if (pin >= 0 && pin < MAX_PIN_COUNT) {
        /**/ if (wiringPiMode == WPI_MODE_GPIO_SYS) // Sys mode
        {
            if (wiringPiDebug) {
                printf("%d %s,%d invalid pin,please check it over.\n", pin, __func__, __LINE__);
            }
            if (pin == 0) {
                printf("%d %s,%d invalid pin,please check it over.\n",pin,__func__, __LINE__);
                return;
            }

            //TODO: fix me
            /*
            if (syspin[pin] == -1) {
                printf("%d %s,%d invalid pin,please check it over.\n",pin,__func__, __LINE__);
                return;
            }
            */

            if (sysFds [pin] == -1) {
                if (wiringPiDebug)
                    printf("pin %d sysFds -1.%s,%d\n", pin, __func__, __LINE__);
            }

            if (sysFds [pin] != -1) {
                if (wiringPiDebug)
                    printf("pin %d :%d.%s,%d\n", pin, sysFds [pin], __func__, __LINE__);
                if (value == LOW)
                    write(sysFds [pin], "0\n", 2);
                else
                    write(sysFds [pin], "1\n", 2);
            }
            return;
        } else if (wiringPiMode == WPI_MODE_PINS)
            pin = pinToGpio [pin];
        else if (wiringPiMode == WPI_MODE_PHYS)
            pin = physToGpio[pin];
        else if (wiringPiMode == WPI_MODE_GPIO) {
            // pin = pinTobcm[pin];
            // pin = pin;
            // Nothing
        }
        else return;
        if (-1 == pin) {
            //printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__,  __LINE__, pin);
            return;
        }
        sunxi_digitalWrite(pin, value);
    } else {
        if ((node = wiringPiFindNode(pin)) != NULL)
            node->digitalWrite(node, pin, value);
    }
    return;
}

/*
 * pwmWrite:
 *	Set an output PWM value
 *********************************************************************************
 */
void pwmWrite(int pin, int value) {
    struct wiringPiNodeStruct *node = wiringPiNodes;

    if (pinToGpio == 0 || physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return;
    }

    uint32_t a_val = 0;
    if (pwmmode == 1) { // cycle
        sunxi_pwm_set_mode(1);
    } else {
        // sunxi_pwm_set_mode(0);
    }
    if (pin < MAX_PIN_NUM) { // On-Board Pin needto fix me Jim
        if (wiringPiMode == WPI_MODE_PINS)
            pin = pinToGpio [pin];
        else if (wiringPiMode == WPI_MODE_PHYS) {
            pin = physToGpio[pin];
        } else if (wiringPiMode == WPI_MODE_GPIO)
            // pin = pinTobcm[pin];
            pin = pin;
        else
            return;
        if (-1 == pin) {
            printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__, __LINE__, pin);
            return;
        }
        if (pin != 5) {
            printf("please use soft pwmmode or choose PWM pin\n");
            return;
        }
        a_val = sunxi_pwm_get_period();
        if (wiringPiDebug)
            printf("==> no:%d period now is :%d,act_val to be set:%d\n", __LINE__, a_val, value);
        if (value > a_val) {
            printf("val pwmWrite 0 <= X <= 1024\n");
            printf("Or you can set new range by yourself by pwmSetRange(range\n");
            return;
        }
        //if value changed chang it
        sunxi_pwm_set_enable(0);
        sunxi_pwm_set_act(value);
        sunxi_pwm_set_enable(1);
    } else {
        printf("not on board :%s,%d\n", __func__, __LINE__);
        if ((node = wiringPiFindNode(pin)) != NULL) {
            if (wiringPiDebug)
                printf("Jim find node%s,%d\n", __func__, __LINE__);
            node->digitalWrite(node, pin, value);
        }
    }
    if (wiringPiDebug)
        printf("this fun is ok now %s,%d\n", __func__, __LINE__);

    return;

}

/*
 * analogRead:
 *	Read the analog value of a given Pin. 
 *	There is no on-board Pi analog hardware,
 *	so this needs to go to a new node.
 *********************************************************************************
 */

int analogRead(int pin) {
    struct wiringPiNodeStruct *node = wiringPiNodes;

    if ((node = wiringPiFindNode(pin)) == NULL)
        return 0;
    else
        return node->analogRead(node, pin);
}

/*
 * analogWrite:
 *	Write the analog value to the given Pin. 
 *	There is no on-board Pi analog hardware,
 *	so this needs to go to a new node.
 *********************************************************************************
 */

void analogWrite(int pin, int value) {
    struct wiringPiNodeStruct *node = wiringPiNodes;

    if ((node = wiringPiFindNode(pin)) == NULL)
        return;

    node->analogWrite(node, pin, value);
}

/*
 * pwmToneWrite:
 *	Pi Specific.
 *      Output the given frequency on the Pi's PWM pin
 *********************************************************************************
 */

void pwmToneWrite(int pin, int freq) {
    int range;

    if (freq == 0)
        pwmWrite(pin, 0); // Off
    else {
        range = 600000 / freq;
        pwmSetRange(range);
        pwmWrite(pin, freq / 2);
    }
}



/*
 * digitalWriteByte:
 *	Pi Specific
 *	Write an 8-bit byte to the first 8 GPIO pins - try to do it as
 *	fast as possible.
 *	However it still needs 2 operations to set the bits, so any external
 *	hardware must not rely on seeing a change as there will be a change 
 *	to set the outputs bits to zero, then another change to set the 1's
 *********************************************************************************
 */
static int head2win[8] = {11, 12, 13, 15, 16, 18, 22, 7}; /*add for BananaPro by lemaker team*/

void digitalWriteByte(int value) {
    int mask = 1;
    int pin;

    if (pinToGpio == 0 || physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return;
    }

    if (wiringPiMode == WPI_MODE_GPIO_SYS || wiringPiMode == WPI_MODE_GPIO) {
        for (pin = 0; pin < 8; ++pin) {
            pinMode(pin, OUTPUT);
            delay(1);
            digitalWrite(pinToGpio [pin], value & mask);
            mask <<= 1;
        }
    } else if (wiringPiMode == WPI_MODE_PINS) {
        for (pin = 0; pin < 8; ++pin) {
            pinMode(pin, OUTPUT);
            delay(1);
            digitalWrite(pin, value & mask);
            mask <<= 1;
        }
    } else {
        for (pin = 0; pin < 8; ++pin) {
            pinMode(head2win[pin], OUTPUT);
            delay(1);
            digitalWrite(head2win[pin], value & mask);
            mask <<= 1;
        }
    }
    return;
}

/*
 * waitForInterrupt:
 *	Pi Specific.
 *	Wait for Interrupt on a GPIO pin.
 *	This is actually done via the /sys/class/gpio interface regardless of
 *	the wiringPi access mode in-use. Maybe sometime it might get a better
 *	way for a bit more efficiency.
 *********************************************************************************
 */

int waitForInterrupt(int pin, int mS) {
    int fd, x;
    uint8_t c;
    struct pollfd polls;

    if (pinToGpio == 0 || physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return;
    }

    if (wiringPiMode == WPI_MODE_PINS) {
        pin = pinToGpio [pin];
    } else if (wiringPiMode == WPI_MODE_PHYS) {
        pin = physToGpio [pin];
    }

    if ((fd = sysFds [pin]) == -1)
        return -2;

    // Setup poll structure

    polls.fd = fd;
    polls.events = POLLPRI; // Urgent data!

    // Wait for it ...

    x = poll(&polls, 1, mS);

    // Do a dummy read to clear the interrupt
    //	A one character read appars to be enough.

    (void) read(fd, &c, 1);

    return x;
}

/*
 * interruptHandler:
 *	This is a thread and gets started to wait for the interrupt we're
 *	hoping to catch. It will call the user-function when the interrupt
 *	fires.
 *********************************************************************************
 */

/*
static void *interruptHandler(void *arg) {
    int myPin;

    (void) piHiPri(55); // Only effective if we run as root

    myPin = pinPass;
    pinPass = -1;

    for (;;)
        if (waitForInterrupt(myPin, -1) > 0)
            isrFunctions [myPin] ();

    return NULL;
}
*/

/*
 * wiringPiISR:
 *	Pi Specific.
 *	Take the details and create an interrupt handler that will do a call-
 *	back to the user supplied function.
 *********************************************************************************
 */

int wiringPiISR(int pin, int mode, void (*function)(void)) {
    int bcmGpioPin;

    if (pinToGpio == 0 || physToGpio == 0) {
        printf("please call wiringPiSetup first.\n");
        return -1;
    }

    return wiringPiFailure(WPI_FATAL, "wiringPiISR: Not implemented");
    
    if ((pin < 0) || (pin >= MAX_PIN_COUNT))
        return wiringPiFailure(WPI_FATAL, "wiringPiISR: pin must be 0-%d (%d)\n", MAX_PIN_COUNT-1, pin);

    /**/ if (wiringPiMode == WPI_MODE_UNINITIALISED)
        return wiringPiFailure(WPI_FATAL, "wiringPiISR: wiringPi has not been initialised. Unable to continue.\n");
    else if (wiringPiMode == WPI_MODE_PINS)
        bcmGpioPin = pinToGpio [pin];
    else if (wiringPiMode == WPI_MODE_PHYS)
        bcmGpioPin = physToGpio [pin];
    else
        bcmGpioPin = pin;

    if (-1 == bcmGpioPin) /**/ {
        printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__, __LINE__, pin);
        return -1;
    }

    //if (edge[bcmGpioPin] == -1)
    return wiringPiFailure(WPI_FATAL, "wiringPiISR: pin not sunpprt on Nano PI M1 (%d,%d)\n", pin, bcmGpioPin);
}

/*
 * initialiseEpoch:
 *	Initialise our start-of-time variable to be the current unix
 *	time in milliseconds and microseconds.
 *********************************************************************************
 */

static void initialiseEpoch(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    epochMilli = (uint64_t) tv.tv_sec * (uint64_t) 1000 + (uint64_t) (tv.tv_usec / 1000);
    epochMicro = (uint64_t) tv.tv_sec * (uint64_t) 1000000 + (uint64_t) (tv.tv_usec);
}

/*
 * delay:
 *	Wait for some number of milliseconds
 *********************************************************************************
 */

void delay(unsigned int howLong) {
    struct timespec sleeper, dummy;

    sleeper.tv_sec = (time_t) (howLong / 1000);
    sleeper.tv_nsec = (long) (howLong % 1000) * 1000000;

    nanosleep(&sleeper, &dummy);
}

/*
 * delayMicroseconds:
 *	This is somewhat intersting. It seems that on the Pi, a single call
 *	to nanosleep takes some 80 to 130 microseconds anyway, so while
 *	obeying the standards (may take longer), it's not always what we
 *	want!
 *
 *	So what I'll do now is if the delay is less than 100uS we'll do it
 *	in a hard loop, watching a built-in counter on the ARM chip. This is
 *	somewhat sub-optimal in that it uses 100% CPU, something not an issue
 *	in a microcontroller, but under a multi-tasking, multi-user OS, it's
 *	wastefull, however we've no real choice )-:
 *
 *      Plan B: It seems all might not be well with that plan, so changing it
 *      to use gettimeofday () and poll on that instead...
 *********************************************************************************
 */

void delayMicrosecondsHard(unsigned int howLong) {
    struct timeval tNow, tLong, tEnd;

    gettimeofday(&tNow, NULL);
    tLong.tv_sec = howLong / 1000000;
    tLong.tv_usec = howLong % 1000000;
    timeradd(&tNow, &tLong, &tEnd);

    while (timercmp(&tNow, &tEnd, <))
        gettimeofday(&tNow, NULL);
}

void delayMicroseconds(unsigned int howLong) {
    struct timespec sleeper;
    unsigned int uSecs = howLong % 1000000;
    unsigned int wSecs = howLong / 1000000;

    /**/ if (howLong == 0)
        return;
    else if (howLong < 100)
        delayMicrosecondsHard(howLong);
    else {
        sleeper.tv_sec = wSecs;
        sleeper.tv_nsec = (long) (uSecs * 1000L);
        nanosleep(&sleeper, NULL);
    }
}

/*
 * millis:
 *	Return a number of milliseconds as an unsigned int.
 *********************************************************************************
 */

unsigned int millis(void) {
    struct timeval tv;
    uint64_t now;

    gettimeofday(&tv, NULL);
    now = (uint64_t) tv.tv_sec * (uint64_t) 1000 + (uint64_t) (tv.tv_usec / 1000);

    return (uint32_t) (now - epochMilli);
}

/*
 * micros:
 *	Return a number of microseconds as an unsigned int.
 *********************************************************************************
 */

unsigned int micros(void) {
    struct timeval tv;
    uint64_t now;

    gettimeofday(&tv, NULL);
    now = (uint64_t) tv.tv_sec * (uint64_t) 1000000 + (uint64_t) tv.tv_usec;

    return (uint32_t) (now - epochMicro);
}

/*
 * wiringPiSetup:
 *	Must be called once at the start of your program execution.
 *
 * Default setup: Initialises the system into wiringPi Pin mode and uses the
 *	memory mapped hardware directly.
 *
 * Changed now to revert to "gpio" mode if we're running on a Compute Module.
 *********************************************************************************
 */

int wiringPiSetup(void) {
    int fd;
    //    int boardRev;
    int model, rev, mem, maker, overVolted;

    if (getenv(ENV_DEBUG) != NULL)
        wiringPiDebug = TRUE;

    if (getenv(ENV_CODES) != NULL)
        wiringPiReturnCodes = TRUE;

    if (geteuid() != 0)
        (void)wiringPiFailure(WPI_FATAL, "wiringPiSetup: Must be root. (Did you forget sudo?)\n");

    if (wiringPiDebug)
        printf("wiringPi: wiringPiSetup called\n");

    //    boardRev = piBoardRev();
    // If we're running on a compute module, then wiringPi pin numbers don't really many anything...
    piBoardId(&model, &rev, &mem, &maker, &overVolted);
    wiringPiMode = WPI_MODE_PINS;

    int faBoardId = model;
    BoardID = model;

    // Open the master /dev/memory device
    if ((fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC)) < 0)
        return wiringPiFailure(WPI_ALMOST, "wiringPiSetup: Unable to open /dev/mem: %s\n", strerror(errno));

    int BASE_Address = 0;
    if (BoardID == NanoPC_T3) {
      BASE_Address = T3_BP_BASE;

    } else {
      BASE_Address = GPIO_BASE_BP;
    }

    if (wiringPiDebug)
        printf(" *gpio base_address is %X . \n", BASE_Address);
    // GPIO:
    // BLOCK SIZE * 2 increases range to include pwm addresses
    gpio = (uint32_t *) mmap(0, BLOCK_SIZE*10, PROT_READ | PROT_WRITE, MAP_SHARED, fd, BASE_Address);
    if ((int32_t) gpio == -1)
        return wiringPiFailure(WPI_ALMOST, "wiringPiSetup: mmap (GPIO) failed: %s\n", strerror(errno));

    // PWM
    pwm = (uint32_t *) mmap(0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_PWM_BP);
    if ((int32_t) pwm == -1)
        return wiringPiFailure(WPI_ALMOST, "wiringPiSetup: mmap (PWM) failed: %s\n", strerror(errno));

    // Clock control (needed for PWM)
    clk = (uint32_t *) mmap(0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CLOCK_BASE_BP);
    if ((int32_t) clk == -1)
        return wiringPiFailure(WPI_ALMOST, "wiringPiSetup: mmap (CLOCK) failed: %s\n", strerror(errno));

    // The drive pads
    pads = (uint32_t *) mmap(0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_PADS_BP);
    if ((int32_t) pads == -1)
        return wiringPiFailure(WPI_ALMOST, "wiringPiSetup: mmap (PADS) failed: %s\n", strerror(errno));

    initialiseEpoch();


    if (faBoardId == NanoPi_M1
            || faBoardId == NanoPi_M1_Plus
            || faBoardId == NanoPi_M1_Plus2
            || faBoardId == NanoPi_K1_Plus) {
	    pinToGpio = pinToGpio_m1;
	    physToGpio = physToGpio_m1;
	    physToPin = physToPin_m1;
        syspin = syspin_m1;
    } else if (faBoardId == NanoPi_NEO
               || faBoardId == NanoPi_NEO_Air
               || faBoardId == NanoPi_NEO2
               || faBoardId == NanoPi_NEO_Plus2) {
	    pinToGpio = pinToGpio_neo;
	    physToGpio = physToGpio_neo;
	    physToPin = physToPin_neo;
        syspin = syspin_neo;
    } else if (faBoardId == NanoPi_Duo) {
	    pinToGpio = pinToGpio_duo;
	    physToGpio = physToGpio_duo;
	    physToPin = physToPin_duo;
        syspin = syspin_duo;
    } else if (faBoardId == NanoPi_Duo2) {
        pinToGpio = pinToGpio_duo2;
        physToGpio = physToGpio_duo2;
        physToPin = physToPin_duo2;
        syspin = syspin_duo2;
    } else if (faBoardId == NanoPC_T3) {
        pinToGpio = pinToGpio_t3;
        physToGpio = physToGpio_t3;
        physToPin = physToPin_t3;
        syspin = syspin_t3;
    } else if (faBoardId == NanoPi_NEO_Core || faBoardId == NanoPi_NEO_Core2) {
	    pinToGpio = pinToGpio_neocore;
	    physToGpio = physToGpio_neocore;
	    physToPin = physToPin_neocore;
	    syspin = syspin_neocore;
    } else {
            if (wiringPiDebug)
        printf("wiringPi: wiringPiSetup fail!\n");
	    return -1;
    }

    if (wiringPiDebug)
      printf("wiringPi: wiringPiSetup success!\n");

    return 0;
}

/*
 * wiringPiSetupGpio:
 *	Must be called once at the start of your program execution.
 *
 * GPIO setup: Initialises the system into GPIO Pin mode and uses the
 *	memory mapped hardware directly.
 *********************************************************************************
 */

int wiringPiSetupGpio(void) {
    (void) wiringPiSetup();

    if (wiringPiDebug)
        printf("wiringPi: wiringPiSetupGpio called\n");

    wiringPiMode = WPI_MODE_GPIO;

    return 0;
}

/*
 * wiringPiSetupPhys:
 *	Must be called once at the start of your program execution.
 *
 * Phys setup: Initialises the system into Physical Pin mode and uses the
 *	memory mapped hardware directly.
 *********************************************************************************
 */

int wiringPiSetupPhys(void) {
    (void) wiringPiSetup();

    if (wiringPiDebug)
        printf("wiringPi: wiringPiSetupPhys called\n");

    wiringPiMode = WPI_MODE_PHYS;

    return 0;
}

/*
 * wiringPiSetupSys:
 *	Must be called once at the start of your program execution.
 *
 * Initialisation (again), however this time we are using the /sys/class/gpio
 *	interface to the GPIO systems - slightly slower, but always usable as
 *	a non-root user, assuming the devices are already exported and setup correctly.
 */

int wiringPiSetupSys(void) {
    //int boardRev;
    int pin;
    char fName [128];
    int model, rev, mem, maker, overVolted;

    if (getenv(ENV_DEBUG) != NULL)
        wiringPiDebug = TRUE;

    if (getenv(ENV_CODES) != NULL)
        wiringPiReturnCodes = TRUE;

    if (wiringPiDebug)
        printf("wiringPi: wiringPiSetupSys called\n");

    piBoardId(&model, &rev, &mem, &maker, &overVolted);
    int faBoardId = model;
    if (faBoardId == NanoPi_M1
            || faBoardId == NanoPi_M1_Plus
            || faBoardId == NanoPi_M1_Plus2
            || faBoardId == NanoPi_K1_Plus) {
        pinToGpio = pinToGpio_m1;
        physToGpio = physToGpio_m1;
        physToPin = physToPin_m1;
    } else if (faBoardId == NanoPi_NEO
               || faBoardId == NanoPi_NEO_Air
               || faBoardId == NanoPi_NEO2
               || faBoardId == NanoPi_NEO_Plus2) {
        pinToGpio = pinToGpio_neo;
        physToGpio = physToGpio_neo;
        physToPin = physToPin_neo;
    } else if (faBoardId == NanoPi_Duo) {
        pinToGpio = pinToGpio_duo;
        physToGpio = physToGpio_duo;
        physToPin = physToPin_duo;
    } else if (faBoardId == NanoPi_Duo2) {
        pinToGpio = pinToGpio_duo2;
        physToGpio = physToGpio_duo2;
        physToPin = physToPin_duo2;
    } else if (faBoardId == NanoPi_NEO_Core || faBoardId == NanoPi_NEO_Core2) {
        pinToGpio = pinToGpio_neocore;
        physToGpio = physToGpio_neocore;
        physToPin = physToPin_neocore;
    }

    for (pin = 1; pin < MAX_PIN_COUNT; ++pin) {
        if (physToGpio[pin] != -1) {
          sprintf(fName, "/sys/class/gpio/gpio%d/value", physToGpio[pin]);
          sysFds [pin] = open(fName, O_RDWR);
        }
    }

    initialiseEpoch();
    wiringPiMode = WPI_MODE_GPIO_SYS;
    return 0;
}

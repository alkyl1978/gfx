/*
    ChibiOS/GFX - Copyright (C) 2012, 2013
                 Joel Bodenmann aka Tectu <joel@unormal.org>

    This file is part of ChibiOS/GFX.

    ChibiOS/GFX is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    ChibiOS/GFX is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file    drivers/ginput/touch/STMPE811/ginput_lld_mouse.c
 * @brief   GINPUT Touch low level driver source for the STMPE811.
 *
 * @defgroup Mouse Mouse
 * @ingroup GINPUT
 * @{
 */

#include "ch.h"
#include "hal.h"
#include "gfx.h"

#include "stmpe811.h"

#if (GFX_USE_GINPUT && GINPUT_NEED_MOUSE) /*|| defined(__DOXYGEN__)*/

#include "ginput/lld/mouse.h"

#if defined(GINPUT_MOUSE_USE_CUSTOM_BOARD) && GINPUT_MOUSE_USE_CUSTOM_BOARD
	#include "ginput_lld_mouse_board.h"
#elif defined(BOARD_EMBEST_DMSTF4BB)
	#include "ginput_lld_mouse_board_embest_dmstf4bb.h"
#else
	#include "ginput_lld_mouse_board_example.h"
#endif

#ifndef STMP811_NO_GPIO_IRQPIN
	#define STMP811_NO_GPIO_IRQPIN	FALSE
#endif
#ifndef STMP811_SLOW_CPU
	#define STMP811_SLOW_CPU	FALSE
#endif

static coord_t x, y, z;
static uint8_t touched;

/* set the active window of the stmpe811. bl is bottom left, tr is top right */
static void setActiveWindow(uint16_t bl_x, uint16_t bl_y, uint16_t tr_x, uint16_t tr_y)
{
	write_reg(STMPE811_REG_WDW_TR_X, 2, tr_x);
	write_reg(STMPE811_REG_WDW_TR_Y, 2, tr_y);
	write_reg(STMPE811_REG_WDW_BL_X, 2, bl_x);
	write_reg(STMPE811_REG_WDW_BL_Y, 2, bl_y);
}

/**
 * @brief   Initialise the mouse/touch.
 *
 * @notapi
 */
void ginput_lld_mouse_init(void)
{
	init_board();

	write_reg(STMPE811_REG_SYS_CTRL1,		1, 0x02);	// Software chip reset
	chThdSleepMilliseconds(10);

	write_reg(STMPE811_REG_SYS_CTRL2,		1, 0x0C);	// Temperature sensor clock off, GPIO clock off, touch clock on, ADC clock on
#if STMP811_NO_GPIO_IRQPIN
	write_reg(STMPE811_REG_INT_EN,			1, 0x00);	// Interrupt on INT pin when touch is detected
#else
	write_reg(STMPE811_REG_INT_EN,			1, 0x01);	// Interrupt on INT pin when touch is detected
#endif
	write_reg(STMPE811_REG_ADC_CTRL1,		1, 0x48);	// ADC conversion time = 80 clock ticks, 12-bit ADC, internal voltage refernce
	chThdSleepMilliseconds(2);

	write_reg(STMPE811_REG_ADC_CTRL2,		1, 0x01);	// ADC speed 3.25MHz
	write_reg(STMPE811_REG_GPIO_AF,			1, 0x00);	// GPIO alternate function - OFF
	write_reg(STMPE811_REG_TSC_CFG,			1, 0x9A);	// Averaging 4, touch detect delay 500 us, panel driver settling time 500 us
	write_reg(STMPE811_REG_FIFO_TH,			1, 0x40);	// FIFO threshold = 64
	write_reg(STMPE811_REG_FIFO_STA,		1, 0x01);	// FIFO reset enable
	write_reg(STMPE811_REG_FIFO_STA,		1, 0x00);	// FIFO reset disable
	write_reg(STMPE811_REG_TSC_FRACT_XYZ,	1, 0x07);	// Z axis data format
	write_reg(STMPE811_REG_TSC_I_DRIVE,		1, 0x01);	// 50mA touchscreen line current
	write_reg(STMPE811_REG_TSC_CTRL,		1, 0x00);	// X&Y&Z
	write_reg(STMPE811_REG_TSC_CTRL,		1, 0x01);	// X&Y&Z, TSC enable
	write_reg(STMPE811_REG_INT_STA,			1, 0xFF);	// Clear all interrupts
#if !STMP811_NO_GPIO_IRQPIN
	touched = (uint8_t)read_reg(STMPE811_REG_TSC_CTRL, 1) & 0x80;
#endif
	write_reg(STMPE811_REG_INT_CTRL,		1, 0x01);	// Level interrupt, enable intrrupts
}

/**
 * @brief   Read the mouse/touch position.
 *
 * @param[in] pt	A pointer to the structure to fill
 *
 * @note			For drivers that don't support returning a position
 *					when the touch is up (most touch devices), it should
 *					return the previous position with the new Z value.
 *					The z value is the pressure for those touch devices
 *					that support it (-100 to 100 where > 0 is touched)
 *					or, 0 or 100 for those drivers that don't.
 *
 * @notapi
 */
void ginput_lld_mouse_get_reading(MouseReading *pt)
{
	bool_t	clearfifo;		// Do we need to clear the FIFO

#if STMP811_NO_GPIO_IRQPIN
	// Poll to get the touched status
	uint8_t		last_touched;
	
	last_touched = touched;
	touched = (uint8_t)read_reg(STMPE811_REG_TSC_CTRL, 1) & 0x80;
	clearfifo = (touched != last_touched);
#else
	// Check if the touch controller IRQ pin has gone off
	clearfifo = false;
	if(getpin_irq()) {							// please rename this to getpin_irq
		write_reg(STMPE811_REG_INT_STA, 1, 0xFF);	// clear all interrupts
		touched = (uint8_t)read_reg(STMPE811_REG_TSC_CTRL, 1) & 0x80;	// set the new touched status
		clearfifo = true;							// only take the last FIFO reading
	}
#endif

	// If not touched, return the previous results
	if (!touched) {
		pt->x = x;
		pt->y = y;
		pt->z = 0;
		pt->buttons = 0;
		return;
	}

#if !STMP811_SLOW_CPU
	if (!clearfifo && (read_reg(STMPE811_REG_FIFO_STA, 1) & 0xD0))
#endif
		clearfifo = true;

	do {
		/* Get the X, Y, Z values */
		/* This could be done in a single 4 byte read to STMPE811_REG_TSC_DATA_XYZ (incr or non-incr) */
		x = (coord_t)read_reg(STMPE811_REG_TSC_DATA_X, 2);
		y = (coord_t)read_reg(STMPE811_REG_TSC_DATA_Y, 2);
		z = (coord_t)read_reg(STMPE811_REG_TSC_DATA_Z, 1);
	} while(clearfifo && !(read_reg(STMPE811_REG_FIFO_STA, 1) & 0x20));

	// Rescale X,Y,Z - X & Y don't need scaling when you are using calibration!
#if !GINPUT_MOUSE_NEED_CALIBRATION
	x = gdispGetWidth() - x / (4096/gdispGetWidth());
	y = y / (4096/gdispGetHeight());
#endif
	z = (((z&0xFF) * 100)>>8) + 1;
	
	// Return the results. ADC gives values from 0 to 2^12 (4096)
	pt->x = x;
	pt->y = y;
	pt->z = z;
	pt->buttons = GINPUT_TOUCH_PRESSED;

	/* Force another read if we have more results */
	if (!clearfifo && !(read_reg(STMPE811_REG_FIFO_STA, 1) & 0x20))
		ginputMouseWakeup();

}

#endif /* GFX_USE_GINPUT && GINPUT_NEED_MOUSE */
/** @} */


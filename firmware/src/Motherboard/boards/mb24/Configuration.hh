/*
 * Copyright 2010 by Adam Mayer	 <adam@makerbot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef BOARDS_MB24_CONFIGURATION_HH_
#define BOARDS_MB24_CONFIGURATION_HH_


/// This file details the pin assignments and features of the
/// Makerbot Motherboard v2.x

// --- Secure Digital Card configuration ---
// NOTE: If SD support is enabled, it is implicitly assumed that the
// following pins are connected:
//  AVR    |   SD header
//---------|--------------
//  MISO   |   DATA_OUT
//  MOSI   |   DATA_IN
//  SCK    |   CLK

// Define as 1 if and SD card slot is present; 0 if not.
#define HAS_SD                  1
// The pin that connects to the write protect line on the SD header.
#define SD_WRITE_PIN            Pin(PortD,0)
// The pin that connects to the card detect line on the SD header.
#define SD_DETECT_PIN           Pin(PortD,1)
// The pin that connects to the chip select line on the SD header.
#define SD_SELECT_PIN           Pin(PortB,0)


// --- Slave UART configuration ---
// The slave UART is presumed to be an RS485 connection through a sn75176 chip.
// Define as 1 if the slave UART is present; 0 if not.
#define HAS_SLAVE_UART          1
// The pin that connects to the driver enable line on the RS485 chip.
#define TX_ENABLE_PIN           Pin(PortC,5)
// The pin that connects to the active-low recieve enable line on the RS485 chip.
#define RX_ENABLE_PIN           Pin(PortC,7)


// --- Host UART configuration ---
// The host UART is presumed to always be present on the RX/TX lines.


// --- Piezo Buzzer configuration ---
// Define as 1 if the piezo buzzer is present, 0 if not.
#define HAS_BUZZER              1
// The pin that drives the buzzer
#define BUZZER_PIN              Pin(PortC,6)


// --- Emergency Stop configuration ---
// Define as 1 if the estop is present, 0 if not.
#define HAS_ESTOP               1
// The pin connected to the emergency stop
#define ESTOP_PIN               Pin(PortE,4)
// Macros for enabling interrupts on the the pin. In this case, INT4.
#define ESTOP_ENABLE_RISING_INT { EICRB = 0x03; EIMSK |= 0x10; }
#define ESTOP_ENABLE_FALLING_INT { EICRB = 0x02; EIMSK |= 0x10; }
#define ESTOP_vect INT4_vect


// --- Axis configuration ---
// Define the number of stepper axes supported by the board.  The axes are
// denoted by X, Y, Z, A and B.
#define STEPPER_COUNT           5
#define EXTRUDERS		2

// microstepping is 1 / (1 << MICROSTEPPING)
//  0 for 1/1
//  1 for 1/2
//  2 for 1/4
//  3 for 1/8
//  4 for 1/16
//  5 for 1/32
//  etc.
#define MICROSTEPPING   3

// --- Stepper and endstop configuration ---
// Pins should be defined for each axis present on the board.  They are denoted
// X, Y, Z, A and B respectively.

// This indicates the default interpretation of the endstop values.
// If your endstops are based on the H21LOB, they are inverted;
// if they are based on the H21LOI, they are not.
#define DEFAULT_INVERTED_ENDSTOPS 1

//Stepper Ports
#define X_STEPPER_STEP          STEPPER_PORT(A,6)       //active rising edge
#define X_STEPPER_DIR           STEPPER_PORT(A,5)       //forward on high
#define X_STEPPER_ENABLE        STEPPER_PORT(A,4)       //active low
#define X_STEPPER_MIN           STEPPER_PORT(B,6)       //active high
#define X_STEPPER_MAX           STEPPER_PORT(B,5)       //active high

// P-Stop is X_STEPPER_MAX = PB5 = PCINT5
#define PSTOP_PORT  Pin(PortB,5)
#define PSTOP_MSK   PCMSK0
#define PSTOP_PCINT PCINT5
#define PSTOP_PCIE  PCIE0
#define PSTOP_VECT  PCINT0_vect

#define Y_STEPPER_STEP          STEPPER_PORT(A,3)       //active rising edge
#define Y_STEPPER_DIR           STEPPER_PORT(A,2)       //forward on high
#define Y_STEPPER_ENABLE        STEPPER_PORT(A,1)       //active low
#define Y_STEPPER_MIN           STEPPER_PORT(B,4)       //active high
#define Y_STEPPER_MAX           STEPPER_PORT(H,6)       //active high

#define Z_STEPPER_STEP          STEPPER_PORT(A,0)       //active rising edge
#define Z_STEPPER_DIR           STEPPER_PORT(H,0)       //forward on high
#define Z_STEPPER_ENABLE        STEPPER_PORT(H,1)       //active low
#define Z_STEPPER_MIN           STEPPER_PORT(H,5)       //active high
#define Z_STEPPER_MAX           STEPPER_PORT(H,4)       //active high

#define A_STEPPER_STEP          STEPPER_PORT(J,0)       //active rising edge
#define A_STEPPER_DIR           STEPPER_PORT(J,1)       //forward on high
#define A_STEPPER_ENABLE        STEPPER_PORT(E,5)       //active low

#define B_STEPPER_STEP          STEPPER_PORT(G,5)       //active rising edge
#define B_STEPPER_DIR           STEPPER_PORT(E,3)       //forward on high
#define B_STEPPER_ENABLE        STEPPER_PORT(H,3)       //active low


// --- Debugging configuration ---
// The pin which controls the debug LED (active high)
#define DEBUG_PIN               Pin(PortB,7)
// By default, debugging packets should be honored; this is made
// configurable if we're short on cycles or EEPROM.
// Define as 1 if debugging packets are honored; 0 if not.
#define HONOR_DEBUG_PACKETS     0

#define HAS_INTERFACE_BOARD     1

#ifndef SIMULATOR
// Enable the P-Stop (pause stop) support
#define PSTOP_SUPPORT
#endif

/// Pin mappings for the LCD connection.
#define LCD_RS_PIN		Pin(PortC,4)
#define LCD_ENABLE_PIN          Pin(PortC,3)
#define LCD_D0_PIN		Pin(PortD,7)
#define LCD_D1_PIN		Pin(PortG,2)
#define LCD_D2_PIN		Pin(PortG,1)
#define LCD_D3_PIN		Pin(PortG,0)

/// This is the pin mapping for the interface board. Because of the relatively
/// high cost of using the pins in a direct manner, we will instead read the
/// buttons directly by scanning their ports. If any of these definitions are
/// modified, the #scanButtons() function _must_ be updated to reflect this.
///
/// TLDR: These are here for decoration only, actual pins defined in #scanButtons()
//#define INTERFACE_X+_PIN        Pin(PortL,7)
//#define INTERFACE_X-_PIN        Pin(PortL,6)
//#define INTERFACE_Y+_PIN        Pin(PortL,5)
//#define INTERFACE_Y-_PIN        Pin(PortL,4)
//#define INTERFACE_Z+_PIN        Pin(PortL,3)
//#define INTERFACE_Z-_PIN        Pin(PortL,2)
//#define INTERFACE_ZERO_PIN      Pin(PortL,1)
//
//#define INTERFACE_OK_PIN        Pin(PortC,2)
//#define INTERFACE_CANCEL_PIN    Pin(PortC,1)

#ifndef SIMULATOR

#define INTERFACE_FOO_PIN       Pin(PortC,0)
#define INTERFACE_BAR_PIN       Pin(PortL,0)
#define INTERFACE_DEBUG_PIN     Pin(PortB,7)

#endif

//Pin mapping for Software I2C communication using analog pins
//as digital pins.
//This is primrarily for BlinkM MaxM, may not work for other I2C.
#define HAS_MOOD_LIGHT		1
#define SOFTWARE_I2C_SDA_PIN	Pin(PortK,0)	//Pin d on the BlinkM
#define SOFTWARE_I2C_SCL_PIN	Pin(PortK,1)	//Pin c on the BlinkM

//ATX Power Good
#define HAS_ATX_POWER_GOOD	1
#define ATX_POWER_GOOD		Pin(PortK,2)	//Pin ATX 8 connected to Analog 10

//Build estimation for the lcd
#define HAS_BUILD_ESTIMATION		1

//Pause@ZPos functionality for the LCD
#define PAUSEATZPOS

//Filament counter
#define HAS_FILAMENT_COUNTER

//If defined, erase the eeprom area on every boot, useful for diagnostics
//#define ERASE_EEPROM_ON_EVERY_BOOT

//If defined, enable an additional menu that allows erasing, saving and loading
//of eeprom data
#define EEPROM_MENU_ENABLE

//If defined, the planner is constrained to a pipeline size of 1,
//this means that acceleration still happens, but only on a per block basis,
//there's no speeding up between blocks.
//#define PLANNER_OFF

//If defined provides 2 debugging variables for on screen display during build
//Variables are floats:  debug_onscreen1, debug_onscreen2 and can be found in Steppers.hh
//#define DEBUG_ONSCREEN

//If defined, the stack is painted with a value and the free sram reported in
//in the Version menu.  This enables debugging to see if the SRAM was ever exhausted
//which would lead to stack corruption.
#define STACK_PAINT

//Definitions for the timer / counter  to use for the stepper interrupt
//Change this to a different 16 bit interrupt if you need to
#define STEPPER_OCRnA			OCR3A
#define STEPPER_TIMSKn			TIMSK3
#define STEPPER_OCIEnA			OCIE3A
#define STEPPER_TCCRnA			TCCR3A
#define STEPPER_TCCRnB			TCCR3B
#define STEPPER_TCCRnC			TCCR3C
#define STEPPER_TCNTn			TCNT3
#define STEPPER_TIMERn_COMPA_vect	TIMER3_COMPA_vect

//Oversample the dda to provide less jitter.
//To switch off oversampling, comment out
//2 is the number of bits, as in a bit shift.  So << 2 = multiply by 4
//= 4 times oversampling
//Obviously because of this oversampling is always a power of 2.
//Don't make it too large, as it will kill performance and can overflow int32_t
//#define OVERSAMPLED_DDA 2

#define JKN_ADVANCE

//Minimum time in seconds that a movement needs to take if the planning pipeline command buffer is
//emptied. Increase this number if you see blobs while printing high speed & high detail. It will
//slowdown on the detailed stuff.
#define ACCELERATION_MIN_SEGMENT_TIME 0.0200

//Minimum planner junction speed (mm/sec). Sets the default minimum speed the planner plans for at
//the end of the buffer and all stops. This should not be much greater than zero and should only be
//changed if unwanted behavior is observed on a user's machine when running at very slow speeds.
//2mm/sec is the recommended value.
#define ACCELERATION_MIN_PLANNER_SPEED 2

//Slowdown limit specifies what to do when the pipeline command buffer starts to empty.
//The pipeline command buffer is 16 commands in length, and Slowdown Limit can be set
//between 0 - 8 (half the buffer size).
//
//When Commands Left <= Slowdown Limit, the feed rate is progressively slowed down as the buffer
//becomes more empty.
//
//By slowing down the feed rate, you reduce the possibility of running out of commands, and creating
//a blob due to the stopped movement.
//
//Possible values are:
//
//0 - Disabled - Never Slowdown
//1 - DON'T USE
//2 - DON'T USE
//3,4,5,6,7,8 - The higher the number, the earlier the start of the slowdown
#define ACCELERATION_SLOWDOWN_LIMIT 4

//ACCELERATION_EXTRUDER_WHEN_NEGATIVE specifies the direction of extruder.
//If negative steps cause an extruder to extrude material, then set this to true.
//If positive steps cause an extruder to extrude material, then set this to false.
//Note: Although a Replicator can have 2 extruders rotating in opposite directions,
//both extruders require negative steps to extrude material.
//This setting effects "Advance" and "Extruder Deprime".
#define ACCELERATION_EXTRUDE_WHEN_NEGATIVE_A true
#define ACCELERATION_EXTRUDE_WHEN_NEGATIVE_B true

// If defined, overlapping stepper interrupts don't cause clunking
// The ideal solution it to adjust calc_timer, but this is just a safeguard
#define ANTI_CLUNK_PROTECTION

//If defined, speed is drastically reducing to crawling
//Very useful for watching acceleration and locating any bugs visually
//Only slows down when acceleration is also set on.
//#define DEBUG_SLOW_MOTION

//If defined, the toolhead and hbp are not heated, and there's
//no waiting.
//This is useful to test movement without extruding any plastic.
//HIGHLY ADVISABLE TO HAVE NO FILAMENT LOADED WHEN YOU DO THIS
//#define DEBUG_NO_HEAT_NO_WAIT

//If defined (and STACK_PAINT is defined), SRAM is monitored occasionally for
//corruption, signalling and 6 repeat error tone on the buzzer if it occurs.
//#define DEBUG_SRAM_MONITOR

//When a build is cancelled or paused, we clear the nozzle
//from the build volume.  This denotes the X/Y/Z position we should
//move to.  max/min_axis_steps_limit can be used for the limits of an axis.
//If you're moving to a position that's an end stop, it's advisable to
//clear the end stop by a few steps as you don't want the endstop to
//be hit due to positioning accuracy and the possibility of an endstop triggering
//a few steps around where it should be.
//If the value isn't defined, the axis is moved
#define BUILD_CLEAR_MARGIN 5.0
//#define BUILD_CLEAR_X (stepperAxis[X_AXIS].max_axis_steps_limit)
//#define BUILD_CLEAR_Y (stepperAxis[Y_AXIS].max_axis_steps_limit)
#define BUILD_CLEAR_Z (stepperAxis[Z_AXIS].max_axis_steps_limit - (int32_t)(BUILD_CLEAR_MARGIN * stepperAxisStepsPerMM(Z_AXIS)))

//When pausing, filament is retracted to stop stringing / blobbing.
//This sets the amount of filament in mm's to be retracted
#define PAUSE_RETRACT_FILAMENT_AMOUNT_MM        2.0

//When defined, the Ditto Printing setting is added to General Settings
#define DITTO_PRINT

//When defined, the Z axis is clipped to it's maximum limit
//Applicable to Replicator.  Probably not applicable to ToM/Cupcake due to incorrect length
//in the various .xml's out there
//#define CLIP_Z_AXIS

//When defined, acceleration stats are displayed on the LCD screen
#define ACCEL_STATS

// Our software variant id for the advanced version command
#define SOFTWARE_VARIANT_ID 0x80

// Disabled SD card folder support owing to a broken SD card detect switch
//#define BROKEN_SD

// Maximum temperature which temps can be set to (bypassed by gcode)
#define MAX_TEMP 280

#endif // BOARDS_RRMBV12_CONFIGURATION_HH_

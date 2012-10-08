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

#ifndef BOARDS_RRMBV12_CONFIGURATION_HH_
#define BOARDS_RRMBV12_CONFIGURATION_HH_

// This file details the pin assignments and features of the RepRap Motherboard
// version 1.2 for the ordinary use case.

// The pin that connects to the /PS_ON pin on the PSU header.  This pin switches
// on the PSU when pulled low.
#define PSU_PIN                 Pin(PortD,6)

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
#define SD_WRITE_PIN            Pin(PortB,2)
// The pin that connects to the card detect line on the SD header.
#define SD_DETECT_PIN           Pin(PortB,3)
// The pin that connects to the chip select line on the SD header.
#define SD_SELECT_PIN           Pin(PortB,4)

// --- Slave UART configuration ---
// The slave UART is presumed to be an RS485 connection through a sn75176 chip.
// Define as 1 if the slave UART is present; 0 if not.
#define HAS_SLAVE_UART          1
// The pin that connects to the driver enable line on the RS485 chip.
#define TX_ENABLE_PIN           Pin(PortD,4)
// The pin that connects to the active-low recieve enable line on the RS485 chip.
#define RX_ENABLE_PIN           Pin(PortD,5)

// --- Host UART configuration ---
// The host UART is presumed to always be present on the RX/TX lines.

// --- Axis configuration ---
// Define the number of stepper axes supported by the board.  The axes are
// denoted by X, Y, Z, A and B.
#ifndef FOURTH_STEPPER
  // Ordinary G3 motherboards have three stepper terminals.
  #define STEPPER_COUNT           3
#else
  // Rob G's hacked G3 motherboard supports four steppers.
  #define STEPPER_COUNT           4
#endif // FOURTH_STEPPER

#define EXTRUDERS               1

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

#ifdef FOURTH_STEPPER
  // If both ends of the endstops will trigger the same pin, set this to one.
  // The hacked G3 four-stepper shield does this to conserve pins.
  #define SINGLE_SWITCH_ENDSTOPS 1
#endif

//Stepper Ports
#define X_STEPPER_STEP          STEPPER_PORT(D,7)       //active rising edge
#define X_STEPPER_DIR           STEPPER_PORT(C,2)       //forward on high
#define X_STEPPER_ENABLE        STEPPER_PORT(C,3)       //active low
#define X_STEPPER_MIN           STEPPER_PORT(C,4)       //active high
#if defined(SINGLE_SWITCH_ENDSTOPS) && (SINGLE_SWITCH_ENDSTOPS == 1)
  #define X_STEPPER_MAX         STEPPER_PORT(C,4)       //active high
#else
  #define X_STEPPER_MAX         STEPPER_PORT(C,5)       //active high
#endif

#define Y_STEPPER_STEP          STEPPER_PORT(C,7)       //active rising edge
#define Y_STEPPER_DIR           STEPPER_PORT(C,6)       //forward on high
#define Y_STEPPER_ENABLE        STEPPER_PORT(A,7)       //active low
#define Y_STEPPER_MIN           STEPPER_PORT(A,6)       //active high
#if defined(SINGLE_SWITCH_ENDSTOPS) && (SINGLE_SWITCH_ENDSTOPS == 1)
  #define Y_STEPPER_MAX         STEPPER_PORT(A,6)       //active high
#else
  #define Y_STEPPER_MAX         STEPPER_PORT(A,5)       //active high
#endif

#define Z_STEPPER_STEP          STEPPER_PORT(A,4)       //active rising edge
#define Z_STEPPER_DIR           STEPPER_PORT(A,3)       //forward on high
#define Z_STEPPER_ENABLE        STEPPER_PORT(A,2)       //active low
#define Z_STEPPER_MIN           STEPPER_PORT(A,1)       //active high
#if defined(SINGLE_SWITCH_ENDSTOPS) && (SINGLE_SWITCH_ENDSTOPS == 1)
  #define Z_STEPPER_MAX         STEPPER_PORT(A,1)       //active high
#else
  #define Z_STEPPER_MAX         STEPPER_PORT(A,0)       //active high
#endif

#ifdef FOURTH_STEPPER
  #define A_STEPPER_STEP        STEPPER_PORT(C,5)       //active rising edge
  #define A_STEPPER_DIR         STEPPER_PORT(A,5)       //forward on high
  #define A_STEPPER_ENABLE      STEPPER_PORT(A,0)       //active low
#endif // FOURTH_STEPPER


// --- Debugging configuration ---
// The pin which controls the debug LED (active high)
#define DEBUG_PIN               Pin(PortB,0)

// By default, debugging packets should be honored; this is made
// configurable if we're short on cycles or EEPROM.
// Define as 1 if debugging packets are honored; 0 if not.
#define HONOR_DEBUG_PACKETS     0

//If defined, erase the eeprom area on every boot, useful for diagnostics
//#define ERASE_EEPROM_ON_EVERY_BOOT

//Defined on Atmega644p, indicates a 4K instead of 8K ram
#define SMALL_4K_RAM

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
#define STEPPER_OCRnA			OCR1A
#define STEPPER_TIMSKn			TIMSK1
#define STEPPER_OCIEnA			OCIE1A
#define STEPPER_TCCRnA			TCCR1A
#define STEPPER_TCCRnB			TCCR1B
#define STEPPER_TCCRnC			TCCR1C
#define STEPPER_TCNTn			TCNT1
#define STEPPER_TIMERn_COMPA_vect	TIMER1_COMPA_vect

//Oversample the dda to provide less jitter.
//To switch off oversampling, comment out
//2 is the number of bits, as in a bit shift.  So << 2 = multiply by 4
//= 4 times oversampling
//Obviously because of this oversampling is always a power of 2.
//Don't make it too large, as it will kill performance and can overflow int32_t
//#define OVERSAMPLED_DDA 2

#define JKN_ADVANCE

// Firmware deprime by default happens when the A or B axis is disabled in disableMotor
// in RepG, which sends a disable axis command to the firmware.
// In this situation, the following define should be commented out.
// If disabling the A/B axis is not being used during a travel move, then the following
// must be uncommented so that depriming happens when A steps = 0 and B steps = 0, otherwise
// deprime will not happen at the end of a travel move and at the beginning of the next extruded
// move.
//#define DEPRIME_ON_NO_EXTRUSION

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
#define DEBUG_SRAM_MONITOR

//When a build is cancelled or paused, we clear the nozzle
//from the build volume.  This denotes the X/Y/Z position we should
//move to.  max/min_axis_steps_limit can be used for the limits of an axis.
//If you're moving to a position that's an end stop, it's advisable to
//clear the end stop by a few steps as you don't want the endstop to
//be hit due to positioning accuracy and the possibility of an endstop triggering
//a few steps around where it should be.
//If the value isn't defined, the axis is moved
//#define BUILD_CLEAR_X (stepperAxis[X_AXIS].max_axis_steps_limit)
//#define BUILD_CLEAR_Y (stepperAxis[Y_AXIS].max_axis_steps_limit)
#define BUILD_CLEAR_Z (stepperAxis[Z_AXIS].max_axis_steps_limit)

//When pausing, filament is retracted to stop stringing / blobbing.
//This sets the amount of filament in mm's to be retracted
#define PAUSE_RETRACT_FILAMENT_AMOUNT_MM        2.0

//When defined, the Ditto Printing setting is added to General Settings
//#define DITTO_PRINT

//When defined, the Z axis is clipped to it's maximum limit
//Applicable to Replicator.  Probably not applicable to ToM/Cupcake due to incorrect length
//in the various .xml's out there
//#define CLIP_Z_AXIS

#endif // BOARDS_RRMBV12_CONFIGURATION_HH_

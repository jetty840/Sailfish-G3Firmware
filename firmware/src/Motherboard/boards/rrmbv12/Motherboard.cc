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

#include <stdint.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/atomic.h>
#include "Motherboard.hh"
#include "Configuration.hh"
#include "Steppers.hh"
#include "Command.hh"
#include "Eeprom.hh"
#include "EepromMap.hh"
#include "EepromDefaults.hh"
#include "StepperAccelPlanner.hh"
#include "SDCard.hh"

//Warnings to remind us that certain things should be switched off for release

#ifdef ERASE_EEPROM_ON_EVERY_BOOT
	#warning "Release: ERASE_EEPROM_ON_EVERY_BOOT enabled in Configuration.hh"
#endif

#ifdef DEBUG_VALUE
	#warning "Release: DEBUG_VALUE enabled in Configuration.hh"
#endif

#if defined(HONOR_DEBUG_PACKETS) && (HONOR_DEBUG_PACKETS == 1)
	#warning "Release: HONOR_DEBUG_PACKETS enabled in Configuration.hh"
#endif

#ifdef DEBUG_ONSCREEN
	#warning "Release: DEBUG_ONSCREEN enabled in Configuration.hh"
#endif

#ifndef JKN_ADVANCE
	#warning "Release: JKN_ADVANCE disabled in Configuration.hh"
#endif

#ifdef DEBUG_SLOW_MOTION
	#warning "Release: DEBUG_SLOW_MOTION enabled in Configuration.hh"
#endif

#ifdef DEBUG_NO_HEAT_NO_WAIT
	#warning "Release: DEBUG_NO_HEAT_NO_WAIT enabled in Configuration.hh"
#endif

#ifdef DEBUG_SRAM_MONITOR
	#warning "Release: DEBUG_SRAM_MONITOR enabled in Configuration.hh"
#endif


/// Instantiate static motherboard instance
Motherboard Motherboard::motherboard(PSU_PIN);

/// Create motherboard object
Motherboard::Motherboard(const Pin& psu_pin) :
        psu(psu_pin)
{
}

void Motherboard::setupAccelStepperTimer() {
	STEPPER_TCCRnA = 0x00;
	STEPPER_TCCRnB = 0x0A; //CTC1 + / 8 = 2Mhz.
	STEPPER_TCCRnC = 0x00;
	STEPPER_OCRnA  = 0x2000; //1KHz
	STEPPER_TIMSKn = 0x02; // turn on OCR1A match interrupt
}

#define ENABLE_TIMER_INTERRUPTS		TIMSK2		|= (1<<OCIE2A); \
					STEPPER_TIMSKn	|= (1<<STEPPER_OCIEnA)

#define DISABLE_TIMER_INTERRUPTS	TIMSK2		&= ~(1<<OCIE2A); \
					STEPPER_TIMSKn	&= ~(1<<STEPPER_OCIEnA)

// Initialize Timers
//      0 = UNUSED
//      1 = Stepper
//      2 = Microsecond timer, debug LED flasher timer and Advance timer
//
//      Timer 0 = 8 bit with PWM
//      Timer 1 = 16 bit with PWM
//      Timer 2 = 8 bit with PWM

void Motherboard::initClocks(){
        // Reset and configure timer 1, the stepper interrupt timer.
        // ISR(TIMER1_COMPA_vect)
        setupAccelStepperTimer();

        // Reset and configure timer 2, the microsecond timer, debug LED flasher timer and Advance timer.
        // Timer 2 is 8 bit
        TCCR2A = 0x02;  // CTC
        TCCR2B = 0x04;  // prescaler at 1/64
        OCR2A = 25;     //Generate interrupts 16MHz / 64 / 25 = 10KHz
        TIMSK2 = 0x02; // turn on OCR2A match interrupt

#ifndef BROKEN_SD
	// Pin Change Interrupt 11 (PB3) is tied to the SD card's card-detect switch.
	// That switch is wired to go HIGH when no card is inserted and LOW when
	// a card is present.

	// We wish to note when the card is inserted and removed so that we
	// know when the SD card reading state needs to be reinitialized

	PCMSK1 &= ~( 1 << PCINT11 );  // Disable PCINT11 temporarily
	DDRB   &= ~( 1 << PB3 );      // Port B3 is read
	PCICR  |=  ( 1 << PCIE1 );    // Enable PCIE1 (PC interrupts 8 - 15)
	PCMSK1 |=  ( 1 << PCINT11 );  // Re-enable PCINT11
#endif
}

/// Reset the motherboard to its initial state.
/// This only resets the board, and does not send a reset
/// to any attached toolheads.
void Motherboard::reset(bool hard_reset) {
	indicateError(0); // turn off blinker

	// Init and turn on power supply
        psu.init();
        psu.turnOn(true);

	// Init steppers
	uint8_t axis_invert = eeprom::getEeprom8(eeprom::AXIS_INVERSION, EEPROM_DEFAULT_AXIS_INVERSION);
	// Z holding indicates that when the Z axis is not in
	// motion, the machine should continue to power the stepper
	// coil to ensure that the Z stage does not shift.
	// Bit 7 of the AXIS_INVERSION eeprom setting
	// indicates whether or not to use z holding; 
	// the bit is active low. (0 means use z holding,
	// 1 means turn it off.)
	bool hold_z = (axis_invert & (1<<7)) == 0;
	steppers::setHoldZ(hold_z);

	// Initialize the host and slave UARTs
        UART::getHostUART().enable(true);
        UART::getHostUART().in.reset();

        // TODO: These aren't done on other platforms, are they necessary?
        UART::getHostUART().reset();
        UART::getHostUART().out.reset();


        UART::getSlaveUART().enable(true);
        UART::getSlaveUART().in.reset();

        // TODO: These aren't done on other platforms, are they necessary?
        UART::getSlaveUART().reset();
        UART::getSlaveUART().out.reset();

	initClocks();

	// Configure the debug pin.
	DEBUG_PIN.setDirection(true);
}

/// Get the number of microseconds that have passed since
/// the board was booted.
micros_t Motherboard::getCurrentMicros() {
	micros_t micros_snapshot;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		micros_snapshot = micros;
	}
	return micros_snapshot;
}

void Motherboard::runMotherboardSlice() {
}

/// Do nothing, need this to stop compile error as it called from SDCard
/// for firmware mb24
void Motherboard::resetCurrentSeconds() {
}

/// Run the motherboard interrupt
void Motherboard::doStepperInterrupt() {
        //We never ignore interrupts on pause, because when paused, we might
        //extrude filament to change it or fix jams

        DISABLE_TIMER_INTERRUPTS;
        sei();

        steppers::doStepperInterrupt();

        cli();
        ENABLE_TIMER_INTERRUPTS;

#ifdef ANTI_CLUNK_PROTECTION
        //Because it's possible another stepper interrupt became due whilst
        //we were processing the last interrupt, and had stepper interrupts
        //disabled, we compare the counter to the requested interrupt time
        //to see if it overflowed.  If it did, then we reset the counter, and
        //schedule another interrupt for very shortly into the future.
        if ( STEPPER_TCNTn >= STEPPER_OCRnA ) {
                STEPPER_OCRnA = 0x01;   //We set the next interrupt to 1 interval, because this will cause the
					//interrupt to  fire again on the next chance it has after exiting this
					//interrupt, i.e. it gets queued.

                STEPPER_TCNTn = 0;      //Reset the timer counter

                //debug_onscreen1 ++;
        }
#endif
}


//Frequency of Timer 2
//100 = (1.0 / ( 16MHz / 64 / 25 = 10KHz)) * 1000000
#define MICROS_INTERVAL 100

void Motherboard::UpdateMicros() {
	micros += MICROS_INTERVAL;	//_IN_MICROSECONDS;
}

/// Timer one comparator match interrupt
ISR(STEPPER_TIMERn_COMPA_vect) {
	Motherboard::getBoard().doStepperInterrupt();
}

#ifndef BROKEN_SD
ISR(PCINT1_vect) {
	sdcard::mustReinit = true;
}
#endif

/// Number of times to blink the debug LED on each cycle
volatile uint8_t blink_count = 0;

/// The current state of the debug LED
enum blinkState {
	BLINK_NONE,
	BLINK_ON,
	BLINK_OFF,
	BLINK_PAUSE
} blink_state = BLINK_NONE;

/// Write an error code to the debug pin.
void Motherboard::indicateError(int error_code) {
	if (error_code == 0) {
		blink_state = BLINK_NONE;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winline"
		DEBUG_PIN.setValue(false);
#pragma GCC diagnostic pop
	}
	else if (blink_count != error_code) {
		blink_state = BLINK_OFF;
	}
	blink_count = error_code;
}

/// Get the current error code.
uint8_t Motherboard::getCurrentError() {
	return blink_count;
}

/// Timer2 overflow cycles that the LED remains on while blinking
#define OVFS_ON 18
/// Timer2 overflow cycles that the LED remains off while blinking
#define OVFS_OFF 18
/// Timer2 overflow cycles between flash cycles
#define OVFS_PAUSE 80

/// Number of overflows remaining on the current blink cycle
int blink_ovfs_remaining = 0;
/// Number of blinks performed in the current cycle
int blinked_so_far = 0;

int debug_light_interrupt_divisor = 0;
#define MAX_DEBUG_LIGHT_INTERRUPT_DIVISOR	164	//Timer interrupt frequency / (16MHz / 1026 / 256)

/// Timer 2 comparator match interrupt
ISR(TIMER2_COMPA_vect) {
	Motherboard::getBoard().UpdateMicros();

#ifdef JKN_ADVANCE
	steppers::doExtruderInterrupt();
#endif

	debug_light_interrupt_divisor ++;
	if ( debug_light_interrupt_divisor < MAX_DEBUG_LIGHT_INTERRUPT_DIVISOR )
		return;

	debug_light_interrupt_divisor = 0;

	if (blink_ovfs_remaining > 0) {
		blink_ovfs_remaining--;
	} else {
		if (blink_state == BLINK_ON) {
			blinked_so_far++;
			blink_state = BLINK_OFF;
			blink_ovfs_remaining = OVFS_OFF;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winline"
			DEBUG_PIN.setValue(false);
#pragma GCC diagnostic pop
		} else if (blink_state == BLINK_OFF) {
			if (blinked_so_far >= blink_count) {
				blink_state = BLINK_PAUSE;
				blink_ovfs_remaining = OVFS_PAUSE;
			} else {
				blink_state = BLINK_ON;
				blink_ovfs_remaining = OVFS_ON;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winline"
				DEBUG_PIN.setValue(true);
#pragma GCC diagnostic pop
			}
		} else if (blink_state == BLINK_PAUSE) {
			blinked_so_far = 0;
			blink_state = BLINK_ON;
			blink_ovfs_remaining = OVFS_ON;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winline"
			DEBUG_PIN.setValue(true);
#pragma GCC diagnostic pop
		}
	}
}

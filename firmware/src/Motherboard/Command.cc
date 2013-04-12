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

#include <math.h>
#include "Configuration.hh"
#include "Command.hh"
#include "Steppers.hh"
#include "Commands.hh"
#include "Host.hh"
#include "Tool.hh"
#include "Timeout.hh"
#include "CircularBuffer.hh"
#include <util/atomic.h>
#include <avr/eeprom.h>
#include "EepromMap.hh"
#include "Eeprom.hh"
#include "EepromDefaults.hh"
#include "SDCard.hh"
#include "ExtruderControl.hh"
#include "StepperAccel.hh"
#include "Errors.hh"

extern int8_t autoPause;  // from Menu.cc

namespace command {

#ifdef SMALL_4K_RAM
	#define COMMAND_BUFFER_SIZE 256
#else
	#define COMMAND_BUFFER_SIZE 512
#endif
uint8_t buffer_data[COMMAND_BUFFER_SIZE];
CircularBuffer command_buffer(COMMAND_BUFFER_SIZE, buffer_data);
uint8_t currentToolIndex = 0;

uint32_t line_number;

bool outstanding_tool_command = false;

enum PauseState paused = PAUSE_STATE_NONE;
#ifdef HAS_INTERFACE_BOARD
static const prog_uchar *pauseErrorMessage;
static bool sdCardError;
#endif
uint32_t sd_count = 0;

#ifdef PSTOP_SUPPORT
// When non-zero, a P-Stop has been requested
uint8_t pstop_triggered = 0;

// We don't want to execute a Pause until after the coordinate system
// has been established by either recalling home offsets or a G92 X Y Z A B
// command.  Some people use the G92 approach so that RepG will generate
// an accelerated move command for the very first move.  This lets them
// have a fast platform move along the Z axis.  Unfortunately, S3G's
// G92-like command is botched and ALL coordinates are set.  That makes
// it impossible to tell if the gcode actually intended to set all the
// coordinates or if it was simply a G92 Z0.
bool pstop_okay = false;

// One way to tell if it's okay to allow a pstop is to assume it's
// okay after a few G1 commands.
uint8_t pstop_move_count = 0;

#endif

uint16_t statusDivisor = 0;
volatile uint32_t recentCommandClock = 0;
volatile uint32_t recentCommandTime = 0;

static Point pausedPosition;

#ifdef PAUSEATZPOS
volatile int32_t  pauseZPos = 0;
bool pauseAtZPosActivated = false;
#endif

#ifdef HAS_FILAMENT_COUNTER
int64_t filamentLength[2] = {0, 0};	//This maybe pos or neg, but ABS it and all is good (in steps)
int64_t lastFilamentLength[2] = {0, 0};
static int32_t lastFilamentPosition[2];
#endif

bool pauseUnRetract = false;

int16_t pausedPlatformTemp;
int16_t pausedExtruderTemp[2];
volatile uint8_t pauseNoHeat = PAUSE_HEAT_ON;

uint8_t buildPercentage = 101;
float startingBuildTimeSeconds;
uint8_t startingBuildTimePercentage;
float elapsedSecondsSinceBuildStart;

#ifdef DITTO_PRINT
bool dittoPrinting = false;
#endif
bool deleteAfterUse = true;

#ifdef HAS_INTERFACE_BOARD
uint16_t altTemp[EXTRUDERS];
#endif

uint16_t getRemainingCapacity() {
	uint16_t sz;
	ATOMIC_BLOCK(ATOMIC_FORCEON) {
		sz = command_buffer.getRemainingCapacity();
	}
	return sz;
}

/// Returns the build percentage (0-100).  This is 101 is the build percentage hasn't been set yet

uint8_t getBuildPercentage(void) {
	return buildPercentage;
}

//Called when filament is extracted via the filament menu during a pause.
//It prevents noodle from being primed into the extruder on resume

void pauseUnRetractClear(void) {
	pauseUnRetract = false;
}

void pause(bool pause, uint8_t heaterControl) {
	//We only store this on a pause, because an unpause needs to
	// unpause based on the setting before
	if ( pause ) {
		paused = (enum PauseState)PAUSE_STATE_ENTER_COMMAND;
		pauseNoHeat = heaterControl;
	}
	else
		paused = (enum PauseState)PAUSE_STATE_EXIT_COMMAND;
#ifdef PSTOP_SUPPORT
	pstop_triggered = 0;
#endif
}

// Returns the pausing intent
bool isPaused() {
	//If we're not paused, or we in an exiting state, then we are not
	//paused, or we are in the process of unpausing.
	if ( paused == PAUSE_STATE_NONE || paused & PAUSE_STATE_EXIT_COMMAND )
		return false;
	return true;
}

#ifdef HAS_INTERFACE_BOARD
const prog_uchar *pauseGetErrorMessage() {
    return pauseErrorMessage;
}
#endif

void pauseClearError() {
    if ( paused == PAUSE_STATE_ERROR )
	paused = PAUSE_STATE_NONE;
#ifdef HAS_INTERFACE_BOARD
    pauseErrorMessage = 0;
    sdCardError = false;
#endif
}

// Returns the paused state
enum PauseState pauseState() {
	return paused;
}

//Returns true if we're transitioning between fully paused, or fully unpaused
bool pauseIntermediateState() {
	if (( paused == PAUSE_STATE_NONE ) || ( paused == PAUSE_STATE_PAUSED )) return false;
	return true;
}


//Only valid when paused == PAUSE_STATE_PAUSED

Point getPausedPosition(void) {
	return pausedPosition;
}

#ifdef PAUSEATZPOS

void pauseAtZPos(int32_t zpos) {
        pauseZPos = zpos;

	//If we're already past the pause position, we might be paused, 
	//or homing, so we activate the pauseZPos later, when the Z position drops
	//below pauseZPos
	if ( steppers::getPlannerPosition()[2] >= pauseZPos )
		pauseAtZPosActivated = false;
	else	pauseAtZPosActivated = true;
}

int32_t getPauseAtZPos() {
	return pauseZPos;
}

#endif


//Returns the estimated time left for the build in seconds
//If we can't complete the calculation due to a lack of information, then we return 0

int32_t estimatedTimeLeftInSeconds(void) {
	//Safety guard against insufficient information, we return 0 if this is the case
	if (( buildPercentage == 101 ) | ( buildPercentage == 0 ) || ( buildPercentage == startingBuildTimePercentage ) ||
	    ( startingBuildTimeSeconds == 0.0 ) || (startingBuildTimePercentage == 0 ) || (elapsedSecondsSinceBuildStart == 0.0))
		return 0;

	//The build time is not calculated from the start of the build, it's calculated from the first non zero build
	//percentage update sent in the .s3g or from the host
	float timeLeft = (elapsedSecondsSinceBuildStart / (float)(buildPercentage - startingBuildTimePercentage)) * (100.0 - (float)buildPercentage); 
	
	//Safe guard against negative results
	if ( timeLeft < 0.0 )	timeLeft = 0.0;

	return (int32_t)timeLeft;
}

bool isEmpty() {
	return command_buffer.isEmpty();
}

void push(uint8_t byte) {
	command_buffer.push(byte);
}

uint8_t pop8() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winline"
	return command_buffer.pop();
#pragma GCC diagnostic pop
}

int16_t pop16() {
	union {
		// AVR is little-endian
		int16_t a;
		struct {
			uint8_t data[2];
		} b;
	} shared;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winline"
	shared.b.data[0] = command_buffer.pop();
	shared.b.data[1] = command_buffer.pop();
#pragma GCC diagnostic pop
	return shared.a;
}

int32_t pop32() {
	union {
		// AVR is little-endian
		int32_t a;
		struct {
			uint8_t data[4];
		} b;
	} shared;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winline"
	shared.b.data[0] = command_buffer.pop();
	shared.b.data[1] = command_buffer.pop();
	shared.b.data[2] = command_buffer.pop();
	shared.b.data[3] = command_buffer.pop();
#pragma GCC diagnostic pop
	return shared.a;
}

enum ModeState {
	READY=0,
	MOVING,
	DELAY,
	HOMING,
	WAIT_ON_TOOL,
	WAIT_ON_PLATFORM,
#ifdef HAS_INTERFACE_BOARD
	WAIT_ON_BUTTON
#endif
};

#ifdef HAS_INTERFACE_BOARD
static bool hasInterfaceBoard = false;
#endif

enum ModeState mode = READY;
uint8_t copiesToPrint, copiesPrinted;

Timeout delay_timeout;
Timeout homing_timeout;
Timeout tool_wait_timeout;
Timeout button_wait_timeout;
/// Bitmap of button pushes to wait for
uint16_t button_mask;
enum {
        BUTTON_TIMEOUT_CONTINUE = 0,
        BUTTON_TIMEOUT_ABORT = 1,
        BUTTON_CLEAR_SCREEN = 2
};
/// Action to take when button times out
uint8_t button_timeout_behavior;

void reset() {
	buildPercentage = 101;
	startingBuildTimeSeconds = 0.0;
	startingBuildTimePercentage = 0;
	deleteAfterUse = true;
	elapsedSecondsSinceBuildStart = 0.0;

#ifdef PAUSEATZPOS
        pauseAtZPos(0);
	pauseAtZPosActivated = false;
#endif

	command_buffer.reset();
	line_number = 0;
	paused = PAUSE_STATE_NONE;
#ifdef PSTOP_SUPPORT
	pstop_triggered = 0;
	pstop_move_count = 0;
	pstop_okay = false;
#endif
#ifdef HAS_INTERFACE_BOARD
	pauseErrorMessage = 0;
	sdCardError = false;
#endif
	sd_count = 0;
#ifdef HAS_FILAMENT_COUNTER
        filamentLength[0] = filamentLength[1] = 0;
        lastFilamentLength[0] = lastFilamentLength[1] = 0;
	lastFilamentPosition[0] = lastFilamentPosition[1] = 0;
#endif

#ifdef DITTO_PRINT
	if (( eeprom::getEeprom8(eeprom::TOOL_COUNT, 1) == 2 ) && ( eeprom::getEeprom8(eeprom::DITTO_PRINT_ENABLED, EEPROM_DEFAULT_DITTO_PRINT_ENABLED) ))
		dittoPrinting = true;
	else	dittoPrinting = false;
#endif

#ifdef HAS_INTERFACE_BOARD
	altTemp[0] = 0;
#if EXTRUDERS > 1
	altTemp[1] = 0;
#endif
#endif

#ifdef HAS_INTERFACE_BOARD
	hasInterfaceBoard = Motherboard::getBoard().hasInterface();
#endif

	copiesToPrint = 0;
	copiesPrinted = 0;

	mode = READY;
}

#ifdef HAS_INTERFACE_BOARD

bool isWaiting(){
        return (mode == WAIT_ON_BUTTON);
}

#endif

bool isReady() {
    return (mode == READY);
}

uint32_t getLineNumber() {
	return line_number;	
}

void clearLineNumber() {
	line_number = 0;
}

//If retract is true, the filament is retracted by 1mm,
//if it's false, it's pushed back out by 1mm
void retractFilament(bool retract) {
	//Handle the unretract cancel
	if	( retract )		pauseUnRetract = true;
	else if ( ! pauseUnRetract )	return;

	Point targetPosition = steppers::getPlannerPosition();

#if EXTRUDERS > 1
	bool extrude_direction[EXTRUDERS] = {ACCELERATION_EXTRUDE_WHEN_NEGATIVE_A, ACCELERATION_EXTRUDE_WHEN_NEGATIVE_B};
#else
	bool extrude_direction[EXTRUDERS] = {ACCELERATION_EXTRUDE_WHEN_NEGATIVE_A};
#endif

	for ( uint8_t e = 0; e < EXTRUDERS; e ++ ) {
		targetPosition[A_AXIS + e] += (int32_t)(( extrude_direction[e] ^ retract ) ? -1 : 1) *
					      stepperAxisMMToSteps(PAUSE_RETRACT_FILAMENT_AMOUNT_MM, A_AXIS + e);
	}

	//Calculate the dda interval, we'll calculate for A and assume B is the same
	//Get the feedrate for A, we'll use the max_speed_change feedrate for A
	float retractFeedRateA = (float)eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_X + sizeof(uint32_t) * A_AXIS, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_A) / 10.0;

	int32_t dda_interval = (int32_t)(1000000.0 / (retractFeedRateA * (float)stepperAxis[A_AXIS].steps_per_mm));

#ifdef DITTO_PRINT
	if ( dittoPrinting ) {
		if ( currentToolIndex == 0 )	targetPosition[B_AXIS] = targetPosition[A_AXIS];
		else				targetPosition[A_AXIS] = targetPosition[B_AXIS];
	}
#endif

	steppers::setTarget(targetPosition, dda_interval);
}

// Moves the Z platform to the bottom
// so that it clears the print if clearPlatform is true,
// otherwise restores the last position before platformAccess(true) was called

void platformAccess(bool clearPlatform) {
   Point currentPosition, targetPosition;

   if ( clearPlatform ) {
	//if we haven't defined a position or we haven't homed, then we
	//don't know our position, so it's unwise to attempt to clear the build
	//so we return doing nothing.
	for ( uint8_t i = 0; i <= Z_AXIS; i ++ ) {
                if (( ! stepperAxis[i].hasDefinePosition ) || ( ! stepperAxis[i].hasHomed ))
			return;
	}

        targetPosition = pausedPosition;

	//Position to clear the build area
#ifdef BUILD_CLEAR_X
        targetPosition[0] = BUILD_CLEAR_X;
#endif

#ifdef BUILD_CLEAR_Y
        targetPosition[1] = BUILD_CLEAR_Y;
#endif

#ifdef BUILD_CLEAR_Z
        targetPosition[2] = BUILD_CLEAR_Z;
#endif

   } else {
        targetPosition = pausedPosition;

	//Extruders may have moved, so we use the current position
	//for them and define it 
	currentPosition = steppers::getPlannerPosition();

	steppers::definePosition(Point(currentPosition[0], currentPosition[1], currentPosition[2],
				       targetPosition[3], targetPosition[4]), false);
   }

   //Calculate the dda speed.  Somewhat crude but effective.  Use the Z
   //axis, it generally has the slowest feed rate
   int32_t dda_rate = (int32_t)(FPTOF(stepperAxis[Z_AXIS].max_feedrate) * (float)stepperAxis[Z_AXIS].steps_per_mm);

   // Calculate the distance
   currentPosition = steppers::getPlannerPosition();
   float dx = (float)(currentPosition[0] - targetPosition[0]) / (float)stepperAxis[X_AXIS].steps_per_mm;
   float dy = (float)(currentPosition[1] - targetPosition[1]) / (float)stepperAxis[Y_AXIS].steps_per_mm;
   float dz = (float)(currentPosition[2] - targetPosition[2]) / (float)stepperAxis[Z_AXIS].steps_per_mm;
   float distance = sqrtf(dx*dx + dy*dy + dz*dz);

#ifdef DITTO_PRINT
   if ( dittoPrinting ) {
	if ( currentToolIndex == 0 )	targetPosition[B_AXIS] = targetPosition[A_AXIS];
	else				targetPosition[A_AXIS] = targetPosition[B_AXIS];
   }
#endif

#ifdef HAS_INTERFACE_BOARD
   // Don't let the platform clearing be sped up, otherwise Z steps may be skipped
   //   and then the resume after pause will be at the wrong height
   uint8_t as = steppers::alterSpeed;
   steppers::alterSpeed = 0;
#endif

   steppers::setTargetNewExt(targetPosition, dda_rate, (uint8_t)0, distance, FPTOI16(stepperAxis[Z_AXIS].max_feedrate << 6));

#ifdef HAS_INTERFACE_BOARD
   // Restore use of speed control
   steppers::alterSpeed = as;
#endif
}


#ifdef HAS_FILAMENT_COUNTER

//Adds the filament used during this build for a particular extruder
void addFilamentUsedForExtruder(uint8_t extruder) {
        //Need to do this to get the absolute amount
        int64_t fl = getFilamentLength(extruder);

        if ( fl > 0 ) {
		int16_t offset = (extruder == 0 ) ? eeprom::FILAMENT_LIFETIME_A : eeprom::FILAMENT_LIFETIME_B;
                int64_t filamentUsed = eeprom::getEepromInt64(offset, EEPROM_DEFAULT_FILAMENT_LIFETIME);
                filamentUsed += fl;
                eeprom::putEepromInt64(offset, filamentUsed);

                //We've used it up, so reset it
                lastFilamentLength[extruder] = filamentLength[extruder];
                filamentLength[extruder] = 0;
        }
}

//Adds the filament used during this build
void addFilamentUsed() {
	addFilamentUsedForExtruder(0);	//A
	addFilamentUsedForExtruder(1);	//B
}

int64_t getFilamentLength(uint8_t extruder) {
        if ( filamentLength[extruder] < 0 )       return -filamentLength[extruder];
        return filamentLength[extruder];
}

int64_t getLastFilamentLength(uint8_t extruder) {
        if ( lastFilamentLength[extruder] < 0 )   return -lastFilamentLength[extruder];
        return lastFilamentLength[extruder];
}

#endif


bool areToolsReady() {
	uint8_t result;
	OutPacket responsePacket;

#ifdef DITTO_PRINT
	if ( dittoPrinting )
	{
		if (extruderControl((currentToolIndex == 1) ? 0 : 1, SLAVE_CMD_GET_TOOL_STATUS, EXTDR_CMD_GET, responsePacket, 0)) {
			result = responsePacket.read8(1);
			if (! ( result & 0x01 ))	return false;
		}
	}
#endif

	if (extruderControl(currentToolIndex, SLAVE_CMD_GET_TOOL_STATUS, EXTDR_CMD_GET, responsePacket, 0)) {
		result = responsePacket.read8(1);
		if (( result & 0x01 ))	return true;
	}

	return false;
}


bool isPlatformReady() {
	uint8_t result;
	OutPacket responsePacket;

	if (extruderControl(0, SLAVE_CMD_IS_PLATFORM_READY, EXTDR_CMD_GET, responsePacket, 0)) {
		result = responsePacket.read8(1);
		if (result != 0)  return true;
	}

	return false;
}


void buildAnotherCopy() {
	recentCommandClock = 0;
	recentCommandTime  = 0;
	command_buffer.reset();

#ifdef HAS_FILAMENT_COUNTER
	addFilamentUsed();
	lastFilamentLength[0] = 0;
	lastFilamentLength[1] = 0;
#endif
	sdcard::playbackRestart();
}


//Store the current heater set points for restoration later
void storeHeaterTemperatures(void) {
	OutPacket responsePacket;

	pausedExtruderTemp[0] = 0;
	if (extruderControl(0, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0))
		pausedExtruderTemp[0] = responsePacket.read16(1);

	pausedExtruderTemp[1] = 0;
	if (( eeprom::getEeprom8(eeprom::TOOL_COUNT, 1) == 2 ) && (extruderControl(1, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0)))
		pausedExtruderTemp[1] = responsePacket.read16(1);

	pausedPlatformTemp = 0;
	if (extruderControl(0, SLAVE_CMD_GET_PLATFORM_SP, EXTDR_CMD_GET, responsePacket, 0));
		pausedPlatformTemp = responsePacket.read16(1);
}


//Switch the heaters off
void pauseHeaters(uint8_t which) {
	OutPacket responsePacket;

	if ( which & PAUSE_EXT_OFF ) {
		extruderControl(0, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, (uint16_t)0);
		if ( eeprom::getEeprom8(eeprom::TOOL_COUNT, 1) == 2 )
			extruderControl(1, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, (uint16_t)0);
	}
	if ( which & PAUSE_HBP_OFF )
		extruderControl(0, SLAVE_CMD_SET_PLATFORM_TEMP, EXTDR_CMD_SET, responsePacket, (uint16_t)0);
}


//Switch the heaters back on to their previous set poings
void unPauseHeaters(void) {
	OutPacket responsePacket;

	if ( pausedExtruderTemp[0] > 0 ) {
		extruderControl(0, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, pausedExtruderTemp[0]);
	}

	if (( eeprom::getEeprom8(eeprom::TOOL_COUNT, 1) == 2 ) && ( pausedExtruderTemp[1] > 0 )) {
		extruderControl(1, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, pausedExtruderTemp[1]);
	}

	if ( pausedPlatformTemp > 0 ) {
		extruderControl(0, SLAVE_CMD_SET_PLATFORM_TEMP, EXTDR_CMD_SET, responsePacket, pausedPlatformTemp);
	}
}


//Returns true if the heaters are at the correct temperature (near their set points),
//false if not
bool areHeatersAtTemperature(void) {
	uint8_t ready;
	OutPacket responsePacket;

	if ( pausedExtruderTemp[0] > 0 ) {
		if (extruderControl(0, SLAVE_CMD_IS_TOOL_READY, EXTDR_CMD_GET, responsePacket, 0)) {
			ready = responsePacket.read8(1);
			if ( ! ready ) return false;
		}
	}

	if (( eeprom::getEeprom8(eeprom::TOOL_COUNT, 1) == 2 ) && ( pausedExtruderTemp[1] > 0 ))  {
		if (extruderControl(1, SLAVE_CMD_IS_TOOL_READY, EXTDR_CMD_GET, responsePacket, 0)) {
			ready = responsePacket.read8(1);
			if ( ! ready ) return false;
		}
	}

	if ( pausedPlatformTemp > 0 ) {
		if (extruderControl(0, SLAVE_CMD_IS_PLATFORM_READY, EXTDR_CMD_GET, responsePacket, 0)) {
			ready = responsePacket.read8(1);
			if ( !  ready ) return false;
		}
	}

	return true;
}


// Handle movement comands -- called from a few places
static void handleMovementCommand(const uint8_t &command) {
	if (command == HOST_CMD_QUEUE_POINT_EXT) {
		// check for completion
		if (command_buffer.getLength() >= 25) {
			pop8(); // remove the command code
			mode = MOVING;

			int32_t x = pop32();
			int32_t y = pop32();
			int32_t z = pop32();
			int32_t a = pop32();
			int32_t b = pop32();
			int32_t dda = pop32();

#ifdef DITTO_PRINT
   			if ( dittoPrinting ) {
				if ( currentToolIndex == 0 )	b = a;
				else				a = b;
			}
#endif

#ifdef HAS_FILAMENT_COUNTER
	                filamentLength[0] += (int64_t)(a - lastFilamentPosition[0]);
			filamentLength[1] += (int64_t)(b - lastFilamentPosition[1]);
			lastFilamentPosition[0] = a;
			lastFilamentPosition[1] = b;
#endif

			line_number++;
#ifdef PSTOP_SUPPORT
			if ( !pstop_okay && ++pstop_move_count > 4 ) pstop_okay = true;
#endif
			steppers::setTarget(Point(x,y,z,a,b), dda);
		}
	}
	 else if (command == HOST_CMD_QUEUE_POINT_NEW) {
		// check for completion
		if (command_buffer.getLength() >= 26) {
			pop8(); // remove the command code
			mode = MOVING;

			int32_t x = pop32();
			int32_t y = pop32();
			int32_t z = pop32();
			int32_t a = pop32();
			int32_t b = pop32();
			int32_t us = pop32();
			uint8_t relative = pop8();

#ifdef DITTO_PRINT
   			if ( dittoPrinting ) {
				if ( currentToolIndex == 0 ) {
					b = a;
				
					//Set B to be the same as A
					relative &= ~(_BV(B_AXIS));
					if ( relative & _BV(A_AXIS) )	relative |= _BV(B_AXIS);
				} else {
					a = b;

					//Set A to be the same as B
					relative &= ~(_BV(A_AXIS));
					if ( relative & _BV(B_AXIS) )	relative |= _BV(A_AXIS);
				}
			}
#endif

#ifdef HAS_FILAMENT_COUNTER
			int32_t ab[2] = {a,b};

			for ( int i = 0; i < 2; i ++ ) {
				if ( relative & (1 << (A_AXIS + i))) {
					filamentLength[i] += (int64_t)ab[i];
					lastFilamentPosition[i] += ab[i];
				} else {
					filamentLength[i] += (int64_t)(ab[i] - lastFilamentPosition[i]);
					lastFilamentPosition[i] = ab[i];
				}
			}
#endif

			line_number++;
#ifdef PSTOP_SUPPORT
			if ( !pstop_okay && ++pstop_move_count > 4 ) pstop_okay = true;
#endif
			steppers::setTargetNew(Point(x,y,z,a,b), us, relative);
		}
	}
	else if (command == HOST_CMD_QUEUE_POINT_NEW_EXT ) {
		// check for completion
		if (command_buffer.getLength() >= 32) {
			pop8(); // remove the command code
			mode = MOVING;

			int32_t x = pop32();
			int32_t y = pop32();
			int32_t z = pop32();
			int32_t a = pop32();
			int32_t b = pop32();
			int32_t dda_rate = pop32();
			uint8_t relative = pop8() & 0x7F; // make sure that the high bit is clear
			int32_t distanceInt32 = pop32();
			float *distance = (float *)&distanceInt32;
			int16_t feedrateMult64 = pop16();

#ifdef DITTO_PRINT
   			if ( dittoPrinting ) {
				if ( currentToolIndex == 0 ) {
					b = a;
				
					//Set B to be the same as A
					relative &= ~(_BV(B_AXIS));
					if ( relative & _BV(A_AXIS) )	relative |= _BV(B_AXIS);
				} else {
					a = b;

					//Set A to be the same as B
					relative &= ~(_BV(A_AXIS));
					if ( relative & _BV(B_AXIS) )	relative |= _BV(A_AXIS);
				}
			}
#endif

#ifdef HAS_FILAMENT_COUNTER
			int32_t ab[2] = {a,b};

			for ( int i = 0; i < 2; i ++ ) {
				if ( relative & (1 << (A_AXIS + i))) {
					filamentLength[i] += (int64_t)ab[i];
					lastFilamentPosition[i] += ab[i];
				} else {
					filamentLength[i] += (int64_t)(ab[i] - lastFilamentPosition[i]);
					lastFilamentPosition[i] = ab[i];
				}
			}
#endif
			line_number++;
#ifdef PSTOP_SUPPORT
			// Positions must be known at this point; okay to do a pstop and
			// its attendant platform clearing
			if ( !pstop_okay && ++pstop_move_count > 4 ) pstop_okay = true;
#endif
			steppers::setTargetNewExt(Point(x,y,z,a,b), dda_rate,
#ifdef HAS_INTERFACE_BOARD
						  relative | steppers::alterSpeed,
#else
						  relative,
#endif
						  *distance, feedrateMult64);
		}
	}
}


//If deleteAfterUse is true, the packet is deleted from the command buffer
//after it has been processed.
//If overrideToolIndex = -1, the toolIndex specified in the packet is used, otherwise
//the toolIndex specified by overrideToolIndex is used

bool processExtruderCommandPacket(bool deleteAfterUse, int8_t overrideToolIndex) {
	// command is ready
	if (tool::getLock()) {
		OutPacket& out = tool::getOutPacket();
		out.reset();

		//command_buffer[0] is HOST_CMD_TOOL_COMMAND, we ignore it here,
		//but we don't remove it in runCommandSlice because tool::getLock
		//may not succeed, and we're lose bytes

		//Handle the tool index and override it if we need to
		uint8_t toolIndex = command_buffer[1];
		if ( overrideToolIndex != -1 )	toolIndex = (uint8_t)overrideToolIndex;
		out.append8(toolIndex); // copy tool index

		uint8_t commandCode = command_buffer[2];
		out.append8(commandCode); // copy command code

		uint8_t payload_len = command_buffer[3]; // get payload length

		if ( deleteAfterUse )	line_number++;

		//These commands aren't used with 5D, so we turf them as RepG still sends them
		if (( commandCode == SLAVE_CMD_TOGGLE_MOTOR_1 ) || 
		    ( commandCode == SLAVE_CMD_TOGGLE_MOTOR_2 ) || 
		    ( commandCode == SLAVE_CMD_SET_MOTOR_1_PWM ) || 
		    ( commandCode == SLAVE_CMD_SET_MOTOR_2_PWM ) || 
		    ( commandCode == SLAVE_CMD_SET_MOTOR_1_DIR ) || 
		    ( commandCode == SLAVE_CMD_SET_MOTOR_2_DIR ) || 
		    ( commandCode == SLAVE_CMD_SET_MOTOR_1_RPM ) || 
		    ( commandCode == SLAVE_CMD_SET_MOTOR_2_RPM ) || 
		    ( commandCode == SLAVE_CMD_SET_SERVO_1_POS ) || 
		    ( commandCode == SLAVE_CMD_SET_SERVO_2_POS )) {

			if ( deleteAfterUse ) {
				for ( uint8_t i = 0; i < (4U + payload_len); i ++ )
					pop8();
			}

			tool::releaseLock();
			return true;
		}

#ifdef HAS_FILAMENT_COUNTER
		if (( commandCode == SLAVE_CMD_SET_TEMP ) && ( ! sdcard::isPlaying()) ) {
			uint16_t *temp = (uint16_t *)&command_buffer[4];
			if ( *temp == 0 ) addFilamentUsed();
		}
#endif

#ifdef EEPROM_DEFAULT_OVERRIDE_GCODE_TEMP
		//Override the gcode temperature if set for an extruder or platform
		if (( commandCode == SLAVE_CMD_SET_TEMP ) || ( commandCode == SLAVE_CMD_SET_PLATFORM_TEMP )) {
			uint16_t *temp = (uint16_t *)&command_buffer[4];
			if ( (*temp != 0) && (
#ifdef HAS_INTERFACE_BOARD
				 (altTemp[toolIndex] != 0) ||
#endif
				 (eeprom::getEeprom8(eeprom::OVERRIDE_GCODE_TEMP, EEPROM_DEFAULT_OVERRIDE_GCODE_TEMP))) ) {
				uint16_t overrideTemp = 0;

				switch ( commandCode ) {
				case SLAVE_CMD_SET_TEMP:
				    overrideTemp =
#ifdef HAS_INTERFACE_BOARD
					(altTemp[toolIndex] > 0) ? altTemp[toolIndex] :
#endif
					(uint16_t)eeprom::getEeprom8((toolIndex == 0 ) ? eeprom::TOOL0_TEMP : eeprom::TOOL1_TEMP,
								     (toolIndex == 0 ) ? EEPROM_DEFAULT_TOOL0_TEMP : EEPROM_DEFAULT_TOOL1_TEMP);
				    break;
				case SLAVE_CMD_SET_PLATFORM_TEMP:
				    overrideTemp = (uint16_t)eeprom::getEeprom8(eeprom::PLATFORM_TEMP, EEPROM_DEFAULT_PLATFORM_TEMP);
				    break;
				default:
				    break;
				}
				if (overrideTemp > MAX_TEMP) overrideTemp = MAX_TEMP;
				*temp = overrideTemp;
			}
		}
#endif

		for ( uint8_t i = 0; i < payload_len; i ++ )
			out.append8(command_buffer[4U + i]);

		tool::startTransaction();

		if ( deleteAfterUse ) {
			for ( uint8_t i = 0; i < (4U + payload_len); i ++ )
				pop8();
		}

		// we don't care about the response, so we can release
		// the lock after we initiate the transfer
		tool::releaseLock();
	
		return true;
	}

	return false;
}


//Handle the pause state

void handlePauseState(void) {
    OutPacket responsePacket;

    switch ( paused ) {

    case PAUSE_STATE_ENTER_START_PIPELINE_DRAIN:
	//We've entered a pause, start draining the pipeline
	paused = PAUSE_STATE_ENTER_WAIT_PIPELINE_DRAIN;
	break;

    case PAUSE_STATE_ENTER_WAIT_PIPELINE_DRAIN:
	//Wait for the pipeline to drain
	if ( movesplanned() == 0 )
	    paused = PAUSE_STATE_ENTER_START_RETRACT_FILAMENT;
	break;

    case PAUSE_STATE_ENTER_START_RETRACT_FILAMENT:
	//Store the current position so we can return later
	pausedPosition = steppers::getPlannerPosition();

	//Retract the filament by 1mm to prevent blobbing
	retractFilament(true);
	paused = PAUSE_STATE_ENTER_WAIT_RETRACT_FILAMENT;
	break;

    case PAUSE_STATE_ENTER_WAIT_RETRACT_FILAMENT:
	//Wait for the filament retraction to complete
	if ( movesplanned() == 0 )
	    paused = PAUSE_STATE_ENTER_START_CLEARING_PLATFORM;
	break;

    case PAUSE_STATE_ENTER_START_CLEARING_PLATFORM:
    {
	//Bracket to stop compiler complaining
	//Clear the platform
	platformAccess(true);
	paused = PAUSE_STATE_ENTER_WAIT_CLEARING_PLATFORM;
	bool cancelling = false;
	if ( host::getBuildState() == host::BUILD_CANCELLING || host::getBuildState() == host::BUILD_CANCELED )
	    cancelling = true;

	//Store the current heater temperatures for restoring later
	if ( pauseNoHeat != PAUSE_HEAT_ON )
	    storeHeaterTemperatures();

	//If we're pausing, and we have HEAT_DURING_PAUSE switched off, switch off the heaters
	if ( ( ! cancelling ) && ( pauseNoHeat != PAUSE_HEAT_ON ) )
	    pauseHeaters(pauseNoHeat);

       //Switch off the extruder fan
	extruderControl(0, SLAVE_CMD_TOGGLE_FAN, EXTDR_CMD_SET, responsePacket, 0);
    }
    break;

    case PAUSE_STATE_ENTER_WAIT_CLEARING_PLATFORM:
	//We finished the last command, now we wait for the platform to reach the bottom
	//before entering the pause
	if ( movesplanned() == 0 ) {
#ifdef HAS_INTERFACE_BOARD
	    paused = ( sdCardError && hasInterfaceBoard ) ? PAUSE_STATE_ERROR : PAUSE_STATE_PAUSED;
#else
	    paused = PAUSE_STATE_PAUSED;
#endif
	}
	break;

    case PAUSE_STATE_EXIT_START_HEATERS:
	//We've begun to exit the pause, instruct the heaters to resume their set points
	if ( pauseNoHeat != PAUSE_HEAT_ON )
	    unPauseHeaters();

	//Switch on the extruder fan if we're noheat pausing
	if ( pauseNoHeat != PAUSE_HEAT_ON )
	    extruderControl(0, SLAVE_CMD_TOGGLE_FAN, EXTDR_CMD_SET, responsePacket, 1);
      
	paused = PAUSE_STATE_EXIT_WAIT_FOR_HEATERS;
	break;

    case PAUSE_STATE_EXIT_WAIT_FOR_HEATERS:
	//Waiting for the heaters to reach their set points
	if ( ( pauseNoHeat != PAUSE_HEAT_ON ) && ( ! areHeatersAtTemperature() ) )
	    break;
	paused = PAUSE_STATE_EXIT_START_RETURNING_PLATFORM;
	break;

   case PAUSE_STATE_EXIT_START_RETURNING_PLATFORM:
       //Instruct the platform to return to it's pre pause position
       platformAccess(false);
       paused = PAUSE_STATE_EXIT_WAIT_RETURNING_PLATFORM;
       break;

   case PAUSE_STATE_EXIT_WAIT_RETURNING_PLATFORM:
       //Wait for the platform to finish moving to it's prepause position
       if ( movesplanned() == 0 ) {
	   paused = PAUSE_STATE_EXIT_START_UNRETRACT_FILAMENT;
       }
       break;

    case PAUSE_STATE_EXIT_START_UNRETRACT_FILAMENT:
	retractFilament(false);
	paused = PAUSE_STATE_EXIT_WAIT_UNRETRACT_FILAMENT;
	break;

    case PAUSE_STATE_EXIT_WAIT_UNRETRACT_FILAMENT:
	//Wait for the filament unretraction to finish
	//then resume processing commands
	if ( movesplanned() == 0 ) {
	    paused = PAUSE_STATE_NONE;
#ifdef HAS_INTERFACE_BOARD
	    pauseErrorMessage = 0;
	    sdCardError = false;
#endif
#ifdef PSTOP_SUPPORT
	    autoPause = 0;
	    pstop_triggered = 0;
#endif
	}
	break;

    case PAUSE_STATE_ERROR:
    default:
	break;
    }
}



// A fast slice for processing commands and refilling the stepper queue, etc.
void runCommandSlice() {
	recentCommandClock ++;

#ifdef HAS_MOOD_LIGHT
	updateMoodStatus();
#endif

	// get command from SD card if building from SD
	if ( sdcard::isPlaying() ) {

	    while (command_buffer.getRemainingCapacity() > 0 && sdcard::playbackHasNext()) {
		sd_count++;
		command_buffer.push(sdcard::playbackNext());
	    }

	    // Deal with any end of file conditions
	    if ( !sdcard::playbackHasNext() ) {

		// SD card file is finished.  Was it a normal finish or an error?
		//  Check the pause state; otherwise, we can hit this code once
		//  and start a pause with host::stopBuild() and then re-enter
		//  this code again at which point ho<st::stopBuild() will then
		//  do an immediate cancel.  Alternatively, call finishPlayback()
		//  so that sdcard::isPlaying() is then false.

		if ( sdcard::sdAvailable != sdcard::SD_SUCCESS && paused == PAUSE_STATE_NONE ) {

		    // SD card error of some sort

		    // Do a DEBUG light blink pattern
		    int err;
		    if ( sdcard::sdAvailable == sdcard::SD_ERR_CRC ) err = ERR_SD_CRC;
		    else if (sdcard::sdAvailable == sdcard::SD_ERR_NO_CARD_PRESENT ) err = ERR_SD_NOCARD;
		    else err = ERR_SD_READ;
		    Motherboard::getBoard().indicateError(err);

#ifdef HAS_FILAMENT_COUNTER
		    // Save the used filament info
		    addFilamentUsed();
#endif
		    // Ensure that the heaters are turned off
		    pauseHeaters(0xff);

		    // Wind down the steppers
		    for ( uint8_t j = 0; j < STEPPER_COUNT; j++ )
			steppers::enableAxis(j, false);

		    // There's likely some command data still in the command buffer
		    // If we don't flush it, it'll get executed causing the build
		    // platform to "unclear" itself.
		    command_buffer.reset();

#ifdef HAS_INTERFACE_BOARD
		    // Establish an error message to display while cancelling the build
		    sdCardError = true;
		    if ( hasInterfaceBoard ) {
			const static PROGMEM prog_uchar crc_err[]    = "SD CRC error";
			const static PROGMEM prog_uchar nocard_err[] = "SD card removed";
			const static PROGMEM prog_uchar read_err[]   = "SD read error";
			if ( sdcard::sdAvailable == sdcard::SD_ERR_CRC ) pauseErrorMessage = crc_err;
			else if (sdcard::sdAvailable == sdcard::SD_ERR_NO_CARD_PRESENT ) pauseErrorMessage = nocard_err;
			else pauseErrorMessage = read_err;
		    }
#endif
		    // And finally cancel the build
		    host::stopBuild();
		}
		else {
#ifdef HAS_INTERFACE_BOARD
		    if ( !hasInterfaceBoard )
#endif
			// sdcard::isPlaying() will continue to return true until we finish the playback
			//   so finish the playback so that RepG will be told that the build is done
			sdcard::finishPlayback();
		}
	    }
	}

#ifdef PAUSEATZPOS
	//If we were previously past the z pause position (e.g. homing, entering a value during a pause)
	//then we need to activate the z pause when the z position falls below it's value
	if (( pauseZPos ) && ( ! pauseAtZPosActivated ) && ( steppers::getPlannerPosition()[2] < pauseZPos)) {
		pauseAtZPosActivated = true;
	}

        //If we've reached Pause @ ZPos, then pause
        if ((( pauseZPos ) && ( pauseAtZPosActivated ) && ( ! isPaused() ) && ( steppers::getPlannerPosition()[2]) >= pauseZPos )) {
		pauseAtZPos(0);		//Clear the pause at zpos
                host::pauseBuild(true, PAUSE_HEAT_ON);
#ifdef HAS_BUZZER
		Motherboard::getBoard().buzz(4, 3, eeprom::getEeprom8(eeprom::BUZZER_REPEATS, EEPROM_DEFAULT_BUZZER_REPEATS));
#endif
		return;
	}
#endif

	if (( paused != PAUSE_STATE_NONE && paused != PAUSE_STATE_PAUSED )) {
		handlePauseState();
		return;	
	}

	// don't execute commands if paused or shutdown because of heater failure
	if (paused == PAUSE_STATE_PAUSED) { return; }

	if (mode == HOMING) {
		if (!steppers::isRunning()) {
			mode = READY;
		} else if (homing_timeout.hasElapsed()) {
			steppers::abort();
			mode = READY;
		}
	}

#ifdef PSTOP_SUPPORT
	// We don't act on the PSTOP when we are homing or are paused
	if ( pstop_triggered && pstop_okay && mode != HOMING && paused == PAUSE_STATE_NONE ) {
		if ( !isPaused() )
		{
			const static PROGMEM prog_uchar pstop_msg[] = "P-Stop triggered";
			pauseErrorMessage = pstop_msg;
			host::pauseBuild(true, PAUSE_EXT_OFF | PAUSE_HBP_OFF);
		}
		pstop_triggered = 0;
	}
#endif

	if (mode == MOVING) {
		if (!steppers::isRunning()) { mode = READY; }
	}
	if (mode == DELAY) {
		// check timers
		if (delay_timeout.hasElapsed()) {
			mode = READY;
		}
	}
	if (mode == WAIT_ON_TOOL) {
		if (tool_wait_timeout.hasElapsed()) {
			mode = READY;
		} else if (areToolsReady()) {
			mode = READY;
		}
	}
	if (mode == WAIT_ON_PLATFORM) {
		if (tool_wait_timeout.hasElapsed()) {
			mode = READY;
		} else if (isPlatformReady()) {
			mode = READY;
		}
	}

#ifdef HAS_INTERFACE_BOARD
	if (mode == WAIT_ON_BUTTON) {
		if (button_wait_timeout.hasElapsed()) {
			if ((button_timeout_behavior & (1 << BUTTON_TIMEOUT_ABORT))) {
				// Abort build!
				// We'll interpret this as a catastrophic situation
				// and do a full reset of the machine.
				Motherboard::getBoard().reset(false);
			} else {
				mode = READY;
				//      Motherboard::getBoard().interfaceBlink(0,0);
			}
		} else {
			// Check buttons
			InterfaceBoard& ib = Motherboard::getBoard().getInterfaceBoard();
			if (ib.buttonPushed()) {
				if((button_timeout_behavior & (1 << BUTTON_CLEAR_SCREEN)))
					ib.hideMessageScreen();
				//Motherboard::getBoard().interfaceBlink(0,0);
				//RGB_LED::setDefaultColor();
				mode = READY;
			}
		}
	}
#endif

	if (mode == READY) {
		// process next command on the queue.
		if (command_buffer.getLength() > 0) {
			uint8_t command = command_buffer[0];

			//If we're running acceleration, we want to populate the pipeline buffer,
			//but we also need to sync (wait for the pipeline buffer to clear) on certain
			//commands, we do that here
			//If we're not pipeline'able command, then we sync here,
			//by waiting for the pipeline buffer to empty before continuing
			if ((command != HOST_CMD_QUEUE_POINT_EXT) &&
 			    (command != HOST_CMD_QUEUE_POINT_NEW) &&
			    (command != HOST_CMD_QUEUE_POINT_NEW_EXT ) &&
			    (command != HOST_CMD_ENABLE_AXES ) &&
			    (command != HOST_CMD_CHANGE_TOOL ) &&
			    (command != HOST_CMD_SET_POSITION_EXT) &&
			    (command != HOST_CMD_SET_ACCELERATION_TOGGLE) &&
			    (command != HOST_CMD_RECALL_HOME_POSITION) &&
			    (command != HOST_CMD_FIND_AXES_MINIMUM) && 
			    (command != HOST_CMD_FIND_AXES_MAXIMUM) &&
			    (command != HOST_CMD_TOOL_COMMAND) &&
			    (command != HOST_CMD_PAUSE_FOR_BUTTON ) ) {
       	                         if ( ! st_empty() )     return;
       	                 }

			if (command == HOST_CMD_QUEUE_POINT_EXT || command == HOST_CMD_QUEUE_POINT_NEW ||
			    command == HOST_CMD_QUEUE_POINT_NEW_EXT ) {
				handleMovementCommand(command);
			} else if (command == HOST_CMD_CHANGE_TOOL) {
				if (command_buffer.getLength() >= 2) {
					pop8(); // remove the command code
					currentToolIndex = pop8();
					line_number++;
                    
					steppers::changeToolIndex(currentToolIndex);
                                        tool::setCurrentToolheadIndex(currentToolIndex);
				}
			} else if (command == HOST_CMD_ENABLE_AXES) {
				recentCommandTime = recentCommandClock;
				if (command_buffer.getLength() >= 2) {
					pop8(); // remove the command code
					uint8_t axes = pop8();
					line_number ++;

#ifdef DITTO_PRINT
					if ( dittoPrinting ) {
						if ( currentToolIndex == 0 ) {
							//Set B to be the same as A
							axes &= ~(_BV(B_AXIS));
							if ( axes & _BV(A_AXIS) )   axes |= _BV(B_AXIS);
						} else {
							//Set A to be the same as B
							axes &= ~(_BV(A_AXIS));
							if ( axes & _BV(B_AXIS) )   axes |= _BV(A_AXIS);
						}
					}
#endif

					bool enable = (axes & 0x80) != 0;
					for (int i = 0; i < STEPPER_COUNT; i++) {
						if ((axes & _BV(i)) != 0) {
							steppers::enableAxis(i, enable);
						}
					}
				}
			} else if (command == HOST_CMD_SET_POSITION_EXT) {
				// check for completion
				if (command_buffer.getLength() >= 21) {
					pop8(); // remove the command code
					int32_t x = pop32();
					int32_t y = pop32();
					int32_t z = pop32();
					int32_t a = pop32();
					int32_t b = pop32();

#ifdef DITTO_PRINT
					if ( dittoPrinting ) {
						if ( currentToolIndex == 0 )	b = a;
						else				a = b;
					}
#endif

#ifdef HAS_FILAMENT_COUNTER
					lastFilamentPosition[0] = a;
					lastFilamentPosition[1] = b;
#endif
					line_number++;
					
					steppers::definePosition(Point(x,y,z,a,b), false);
				}
			} else if (command == HOST_CMD_DELAY) {
				if (command_buffer.getLength() >= 5) {
					mode = DELAY;
					pop8(); // remove the command code
					// parameter is in milliseconds; timeouts need microseconds
					uint32_t microseconds = pop32() * 1000L;
					line_number ++;

					delay_timeout.start(microseconds);
				}
			} else if (command == HOST_CMD_PAUSE_FOR_BUTTON) {
				if (command_buffer.getLength() >= 5) {
					pop8(); // remove the command code
#ifdef HAS_INTERFACE_BOARD
					button_mask = pop8();
					uint16_t timeout_seconds = pop16();
					button_timeout_behavior = pop8();

					if (timeout_seconds != 0) {
						button_wait_timeout.start((micros_t)timeout_seconds * (micros_t)1000L * (micros_t)1000L);
					} else {
						button_wait_timeout = Timeout();
					}
					// set button wait via interface board
					//Motherboard::getBoard().interfaceBlink(25,15);
					InterfaceBoard& ib = Motherboard::getBoard().getInterfaceBoard();
					ib.waitForButton(button_mask);
					mode = WAIT_ON_BUTTON;
#else
					pop8();		//mask
					pop16();	//timeout_seconds
					pop8();		//button_timeout_behavior
#endif
					line_number++;
				}
			} else if (command == HOST_CMD_DISPLAY_MESSAGE) {
				if (command_buffer.getLength() >= 6) {
					pop8(); // remove the command code
#ifdef HAS_INTERFACE_BOARD
					uint8_t options = pop8();
					uint8_t xpos = pop8();
					uint8_t ypos = pop8();
					uint8_t timeout_seconds = pop8();

					MessageScreen* scr = Motherboard::getBoard().getMessageScreen();

					// check message clear bit
					if ( (options & (1 << 0)) == 0 ) { scr->clearMessage(); }
					// set position and add message
					scr->setXY(xpos,ypos);
					scr->addMessage(command_buffer);

					// push message screen if the full message has been recieved
					if((options & (1 << 1))){
						InterfaceBoard& ib = Motherboard::getBoard().getInterfaceBoard();
						if (! ib.isMessageScreenVisible()) {
							ib.showMessageScreen();
						} else {
							scr->refreshScreen();
						}
						// set message timeout if not a buttonWait call
						if ((timeout_seconds != 0) && (!(options & (1 <<2)))) {
							scr->setTimeout(timeout_seconds);//, true);
						}

						if (options & (1 << 2)) { // button wait bit --> start button wait
							if (timeout_seconds != 0) {
								button_wait_timeout.start(timeout_seconds * 1000L * 1000L);
							} else {
								button_wait_timeout = Timeout();
							}
							button_mask =  (1 << ButtonArray::ZERO) | (1 << ButtonArray::ZMINUS) |
							    (1 << ButtonArray::ZPLUS) | (1 << ButtonArray::YMINUS) |
							    (1 << ButtonArray::YPLUS) | (1 << ButtonArray::XMINUS) |
							    (1 << ButtonArray::XPLUS) | (1 << ButtonArray::CANCEL) |
							    (1 << ButtonArray::OK);
							button_timeout_behavior = 1 << BUTTON_CLEAR_SCREEN;
							//Motherboard::getBoard().interfaceBlink(25,15);
							InterfaceBoard& ib = Motherboard::getBoard().getInterfaceBoard();
							ib.waitForButton(button_mask);
							mode = WAIT_ON_BUTTON;
						}
					}
#else
					pop8();	//options
					pop8();	//xpos
					pop8();	//ypos
					pop8();	//timeout_seconds
					while (pop8() != '\0') 	;
#endif
					line_number++;
				}
			} else if (command == HOST_CMD_FIND_AXES_MINIMUM ||
					command == HOST_CMD_FIND_AXES_MAXIMUM) {
				if (command_buffer.getLength() >= 8) {
					pop8(); // remove the command
					uint8_t flags = pop8();
					uint32_t feedrate = pop32(); // feedrate in us per step
					uint16_t timeout_s = pop16();
					line_number ++;

					bool direction = (command == HOST_CMD_FIND_AXES_MAXIMUM);
					mode = HOMING;
					homing_timeout.start(timeout_s * 1000L * 1000L);
					steppers::startHoming(direction,
							      flags,
							      feedrate);
				}
			} else if (command == HOST_CMD_WAIT_FOR_TOOL) {
				if (command_buffer.getLength() >= 6) {
#ifdef DEBUG_NO_HEAT_NO_WAIT
					mode = READY;
#else
					mode = WAIT_ON_TOOL;
#endif
#ifdef PSTOP_SUPPORT
					// Assume that by now coordinates are set
					pstop_okay = true;
#endif
					pop8();
					currentToolIndex = pop8();
					pop16();	//uint16_t toolPingDelay
					uint16_t toolTimeout = (uint16_t)pop16();
					line_number++;
					
					// if we re-add handling of toolTimeout, we need to make sure
					// that values that overflow our counter will not be passed)
					tool_wait_timeout.start(toolTimeout*1000000L);
				}
			} else if (command == HOST_CMD_WAIT_FOR_PLATFORM) {
        			// FIXME: Almost equivalent to WAIT_FOR_TOOL
				if (command_buffer.getLength() >= 6) {
#ifdef DEBUG_NO_HEAT_NO_WAIT
					mode = READY;
#else
					mode = WAIT_ON_PLATFORM;
#endif
#ifdef PSTOP_SUPPORT
					// Assume that by now coordinates are set
					pstop_okay = true;
#endif
					pop8();
					pop8();	//uint8_t currentToolIndex
					pop16(); //uint16_t toolPingDelay
					uint16_t toolTimeout = (uint16_t)pop16();
					line_number++;
					
					// if we re-add handling of toolTimeout, we need to make sure
					// that values that overflow our counter will not be passed)
					tool_wait_timeout.start(toolTimeout*1000000L);
				}
			} else if (command == HOST_CMD_STORE_HOME_POSITION) {
				// check for completion
				if (command_buffer.getLength() >= 2) {
					pop8();
					uint8_t axes = pop8();
					line_number++;

					// Go through each axis, and if that axis is specified, read it's value,
					// then record it to the eeprom.
					for (uint8_t i = 0; i < STEPPER_COUNT; i++) {
						if ( axes & (1 << i) ) {
							uint16_t offset = eeprom::AXIS_HOME_POSITIONS + 4*i;
							uint32_t position = steppers::getPlannerPosition()[i];
							cli();
							eeprom_write_block(&position, (void*) offset, 4);
							sei();
						}
					}
				}
			} else if (command == HOST_CMD_RECALL_HOME_POSITION) {
				// check for completion
				if (command_buffer.getLength() >= 2) {
					pop8();
					uint8_t axes = pop8();
					line_number++;
		
					Point newPoint = steppers::getPlannerPosition();

					for (uint8_t i = 0; i < STEPPER_COUNT; i++) {
						if ( axes & (1 << i) ) {
							uint16_t offset = eeprom::AXIS_HOME_POSITIONS + 4*i;
							cli();
							eeprom_read_block(&(newPoint[i]), (void*) offset, 4);
							sei();
						}
					}

#ifdef HAS_FILAMENT_COUNTER
					lastFilamentPosition[0] = newPoint[3];
					lastFilamentPosition[1] = newPoint[4];
#endif
					steppers::definePosition(newPoint, true);
				}
			}else if (command == HOST_CMD_SET_RGB_LED){
				//Not currently implemented in RepG as an MCode, although the driver supports the command
				if (command_buffer.getLength() >= 6) {
					pop8(); // remove the command code

#ifdef HAS_MOOD_LIGHT
					uint8_t red = pop8();
					uint8_t green = pop8();
					uint8_t blue = pop8();
					uint8_t fadeSpeed = pop8();	//blink_rate on MightyBoard

                    			pop8();	//uint8_t effect	//Unused

					Motherboard::getBoard().MoodLightSetRGBColor(red, green, blue, fadeSpeed, 0);
#else
					pop8();	//red
					pop8();	//green
					pop8();	//blue
					pop8();	//fadeSpeed
					pop8();	//effect
#endif

                    			line_number++;
				}
			}else if (command == HOST_CMD_SET_BEEP){
				//Not currently implemented in RepG as an MCode, although the driver supports the command
				if (command_buffer.getLength() >= 6) {
					pop8(); // remove the command code
#ifdef HAS_BUZZER
					uint8_t buzzes   = (uint8_t)pop16();	//Equivalent to frequency on Mightyboard
					uint8_t duration = (uint8_t)pop16();	//Equivalent to beep_length on Mightyboard
					uint8_t repeats  = pop8();	//Equivalent to effect on Mightyboard
					if ( buzzes == 0 )	Motherboard::getBoard().stopBuzzer();
					else 			Motherboard::getBoard().buzz(buzzes, duration, repeats);
#else
					pop16();	//buzzes
					pop16();	//duration
					pop8();		//repeats
#endif
					line_number++;
				}			
			} else if (command == HOST_CMD_TOOL_COMMAND) {
				if (command_buffer.getLength() >= 4) { // needs a payload

					// Need to empty the pipeline before running the ABP
					if ( command_buffer[2] == SLAVE_CMD_TOGGLE_ABP && !st_empty() )
						return;

					uint8_t payload_length = command_buffer[3];
					if (command_buffer.getLength() >= (uint8_t)(4U+payload_length)) {
						//Backup the value, in case processExtruderCommandPacket fails due to getLock failing
						bool deleteAfterUseOnEntry = deleteAfterUse;

#ifdef DITTO_PRINT
						if ( dittoPrinting ) {
							//Delete after use toggles, so that
							//when deleteAfterUse = false, it's the 1st call of the extruder command
							//and we copy to the other extruder.  When true, it's the 2nd call if the 
							//extruder command, and we use the tool index specified in the command
							if ( deleteAfterUse )	deleteAfterUse = false;
							else			deleteAfterUse = true;
						} else
#endif
							deleteAfterUse = true;	//ELSE

						//If we're not setting a temperature, or toggling a fan, then we don't
						//"ditto print" the command, so we delete after use
						if (( command_buffer[2] != SLAVE_CMD_SET_TEMP ) && ( command_buffer[2] != SLAVE_CMD_TOGGLE_FAN ))
							deleteAfterUse = true;

						//If we're copying this command due to ditto printing, then we need to switch
						//the extruder controller by switching toolindex to the other extruder
						int8_t overrideToolIndex = -1;
						if ( ! deleteAfterUse ) {
							if ( command_buffer[1] == 0 )	overrideToolIndex = 1;
							else				overrideToolIndex = 0;
						}

						//If we can't process the packet, restore our original deleteAfterUse
						//for trying again
						if ( ! processExtruderCommandPacket(deleteAfterUse, overrideToolIndex) )
							deleteAfterUse = deleteAfterUseOnEntry;
					}
				}

			} else if (command == HOST_CMD_SET_BUILD_PERCENT){
				if (command_buffer.getLength() >= 3){
					pop8(); // remove the command code

#ifdef HAS_INTERFACE_BOARD
					buildPercentage = pop8();

					//Set the starting time / percent on the first HOST_CMD_SET_BUILD_PERCENT
					//with a non zero value sent near the start of the build
					//We use this to calculate the build time
					if (( buildPercentage > 0 ) && ( startingBuildTimeSeconds == 0.0) && ( startingBuildTimePercentage == 0 )) {
						startingBuildTimeSeconds = Motherboard::getBoard().getCurrentSeconds();
						startingBuildTimePercentage = buildPercentage;
					}
					if ( buildPercentage > 0 ) {
						elapsedSecondsSinceBuildStart = (Motherboard::getBoard().getCurrentSeconds() - startingBuildTimeSeconds);
					}

					pop8();	// uint8_t ignore; // remove the reserved byte
#else
					pop8();	//buildPercentage
					pop8();	//reserved byte
#endif
					line_number++;
				}

			} else if (command == HOST_CMD_QUEUE_SONG ) { //queue a song for playing
				/// Error tone is 0,
				/// End tone is 1,
				/// all other tones user-defined (defaults to end-tone)
				if (command_buffer.getLength() >= 2){
					pop8(); // remove the command code

#ifdef HAS_BUZZER
					uint8_t songId = pop8();

					if(songId == 0)
						//Error code
						Motherboard::getBoard().buzz(6, 1, eeprom::getEeprom8(eeprom::BUZZER_REPEATS, EEPROM_DEFAULT_BUZZER_REPEATS));
					else if (songId == 1 )
						//Print done
						Motherboard::getBoard().buzz(4, 3, eeprom::getEeprom8(eeprom::BUZZER_REPEATS, EEPROM_DEFAULT_BUZZER_REPEATS));
					else
						//Other Error
						Motherboard::getBoard().buzz(8, 1, eeprom::getEeprom8(eeprom::BUZZER_REPEATS, EEPROM_DEFAULT_BUZZER_REPEATS));
#else
					pop8();	//songId
#endif
					line_number++;
                                }
			} else if ( command == HOST_CMD_RESET_TO_FACTORY) {
				/// reset EEPROM settings to the factory value. Reboot bot.
				if (command_buffer.getLength() >= 2){
				pop8(); // remove the command code
				pop8();	//uint8_t options
				line_number++;
				cli();
				eeprom::setDefaults(true);
				sei();
				Motherboard::getBoard().reset(false);
				}
			} else if ( command == HOST_CMD_BUILD_START_NOTIFICATION) {
				if (command_buffer.getLength() >= 5){
					pop8(); // remove the command code
					pop32();	//int buildSteps
					line_number++;
					host::handleBuildStartNotification(command_buffer);		
				}
			} else if ( command == HOST_CMD_BUILD_END_NOTIFICATION) {
				if (command_buffer.getLength() >= 2){
					pop8(); // remove the command code
					uint8_t flags = pop8();
					line_number++;
					host::handleBuildStopNotification(flags);
				}
			} else if ( command == HOST_CMD_SET_ACCELERATION_TOGGLE) {
				if (command_buffer.getLength() >= 2){
					pop8(); // remove the command code
					line_number++;
					uint8_t status = pop8();
					steppers::setSegmentAccelState(status == 1);
				}
			} else if ( command == HOST_CMD_STREAM_VERSION ) {

				if (command_buffer.getLength() >= 21){
					pop8(); // remove the command code
					line_number++;
					pop8(); // version high
					pop8(); // version low
					pop8(); // reserved;
					pop32(); // reserved;
					pop16(); // bot type pid
					pop16(); // reserved
					pop32(); // reserved;
					pop32(); // reserved;
					pop8(); // reserved;
				}
			} else {
			}
		}
	}

        /// we're not handling overflows in the line counter.  Possibly implement this later.
        if(line_number > MAX_LINE_COUNT){
                line_number = MAX_LINE_COUNT + 1;
        }
}


#ifdef HAS_MOOD_LIGHT

#define STOCHASTIC_PERCENT(v, a, b)		(((v - a) / (b - a)) * 100.0)
#define MAX2(a,b)				((a >= b)?a:b)
#define STATUS_DIVISOR_TIME_PER_STATUS_CHANGE	4000
#define RECENT_COMMAND_TIMEOUT			4000 * 200

void updateMoodStatus() {
	//Implement a divisor so we don't get called on every turn, we don't
	//want to overload the interrupt loop when we don't need frequent changes	
	statusDivisor ++;

	if ( statusDivisor < STATUS_DIVISOR_TIME_PER_STATUS_CHANGE )	return;
	statusDivisor = 0;

	MoodLightController moodLight = Motherboard::getBoard().getMoodLightController();

	//If we're not set to the Bot Status Script, then there's no need to check anything
	if ( moodLight.getLastScriptPlayed() != 0 ) return;


	//Certain states don't require us to check as often,
	//save some CPU cycles

	enum moodLightStatus lastMlStatus = moodLight.getLastStatus();

	//If printing and recent commands, do nothing
	if ((lastMlStatus == MOOD_LIGHT_STATUS_PRINTING) &&
	    ( recentCommandTime >= (recentCommandClock - (uint32_t)RECENT_COMMAND_TIMEOUT )))	return;
	
 	enum moodLightStatus mlStatus = MOOD_LIGHT_STATUS_IDLE;

	//Get the status of the tool head and platform

	//Figure out how hot or cold we are
	bool toolReady     = areToolsReady();
	bool platformReady = isPlatformReady();

	OutPacket responsePacket;
	uint16_t toolTemp=0, toolTempSetPoint=0, platformTemp=0, platformTempSetPoint=0;

	if (extruderControl(0, SLAVE_CMD_GET_TEMP, EXTDR_CMD_GET, responsePacket, 0))
		toolTemp = responsePacket.read16(1);

	if (extruderControl(0, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0))
		toolTempSetPoint = responsePacket.read16(1);

	if (extruderControl(0, SLAVE_CMD_GET_PLATFORM_TEMP, EXTDR_CMD_GET, responsePacket, 0))
		platformTemp = responsePacket.read16(1);

	if (extruderControl(0, SLAVE_CMD_GET_PLATFORM_SP, EXTDR_CMD_GET, responsePacket, 0))
		platformTempSetPoint = responsePacket.read16(1);
	

	float percentHotTool, percentHotPlatform;

	if ( toolTempSetPoint == 0 ) {
		//We're cooling.  0% = 48C  100% = 240C
		percentHotTool = STOCHASTIC_PERCENT((float)toolTemp, 48.0, 240.0);
		if ( percentHotTool > 100.0 )	percentHotTool = 100.0;
		if ( percentHotTool < 0.0 )	percentHotTool = 0.0;
	} else {
		//We're heating.  0% = 18C  100% = Set Point 
		percentHotTool = STOCHASTIC_PERCENT((float)toolTemp, 18.0, (float)toolTempSetPoint);
		if ( percentHotTool > 100.0 )	percentHotTool = 100.0;
		if ( percentHotTool < 0.0 )	percentHotTool = 0.0;

		if ( toolReady )			percentHotTool = 100.0;
		else if ( percentHotTool >= 100.0 )	percentHotTool = 99.0;
	}

	if ( platformTempSetPoint == 0 ) {
		//We're cooling.  0% = 48C  100% = 120C
		percentHotPlatform = STOCHASTIC_PERCENT((float)platformTemp, 48.0, 120.0);
		if ( percentHotPlatform > 100.0 )	percentHotPlatform = 100.0;
		if ( percentHotPlatform < 0.0 )		percentHotPlatform = 0.0;
	} else {
		//We're heating.  0% = 18C  100% = Set Point 
		percentHotPlatform = STOCHASTIC_PERCENT((float)platformTemp, 18.0, (float)platformTempSetPoint);
		if ( percentHotPlatform > 100.0 )	percentHotPlatform = 100.0;
		if ( percentHotPlatform < 0.0 )		percentHotPlatform = 0.0;

		if ( platformReady )			percentHotPlatform = 100.0;
		else if ( percentHotPlatform >= 100.0 )	percentHotPlatform = 99.0;
	}

	//Are we heating or cooling
	bool heating = false;
	if (( toolTempSetPoint != 0 ) || ( platformTempSetPoint != 0 ))	heating = true;

	if ( heating ) {
		//If we're heating and tool and platform are 100%, then we're not cooling anymore, we're printing
		if (( percentHotTool >= 100.0 ) && ( percentHotPlatform >= 100.0 ) &&
	    	    ( recentCommandTime >= (recentCommandClock - (uint32_t)RECENT_COMMAND_TIMEOUT ))) {
			mlStatus = MOOD_LIGHT_STATUS_PRINTING;
		} else {
			//We can't go from printing back to heating
			if ( lastMlStatus != MOOD_LIGHT_STATUS_PRINTING ) mlStatus = MOOD_LIGHT_STATUS_HEATING;
			else						  mlStatus = MOOD_LIGHT_STATUS_PRINTING;
		}
	} else {
		//If we're cooling and tool and platform are 0%, then we're not cooling anymore
		if  (( percentHotTool <= 0.0 ) && ( percentHotPlatform <= 0.0 )) {
			mlStatus = MOOD_LIGHT_STATUS_IDLE;
		} else {
			//We can't go from idle back to cooling
			if ( lastMlStatus != MOOD_LIGHT_STATUS_IDLE ) mlStatus = MOOD_LIGHT_STATUS_COOLING;
			else					      mlStatus = MOOD_LIGHT_STATUS_IDLE;
		}
	}


	float percentHot = MAX2(percentHotTool, percentHotPlatform);

	Motherboard::getBoard().getMoodLightController().displayStatus(mlStatus, percentHot);

	lastMlStatus = mlStatus;
}

#endif

}

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
 *
 * Modifications for Jetty Marlin compatability, authored by Dan Newman and Jetty.
 */

#ifndef SIMULATOR

#define __STDC_LIMIT_MACROS
#include "Steppers.hh"
#include "StepperAxis.hh"
#include <stdint.h>
#include <util/delay.h>
#include <util/atomic.h>
#include "Eeprom.hh"
#include "EepromMap.hh"
#include "EepromDefaults.hh"
#include "stdio.h"

#else

#define __STDC_LIMIT_MACROS
#include "Steppers.hh"
#include "StepperAxis.hh"
#include <stdint.h>
#include "Eeprom.hh"
#include "EepromMap.hh"
#include "EepromDefaults.hh"

#ifdef ATOMIC_BLOCK
#undef ATOMIC_BLOCK
#endif
#define ATOMIC_BLOCK(x)

#ifdef labs
#undef labs
#endif
#define labs(x) abs(x)

#define st_init()
#define st_interrupt() false
#define st_extruder_interrupt()
#define quickStop()
#define DEBUG_TIMER_TCTIMER_USI 0
#define DEBUG_TIMER_START
#define DEBUG_TIMER_FINISH

#endif

#ifdef DEBUG_ONSCREEN
	volatile float debug_onscreen1 = 0.0, debug_onscreen2 = 0.0;
#endif

namespace steppers {

volatile bool is_running;
volatile bool is_homing;
bool acceleration = true;
uint8_t plannerMaxBufferSize;
FPTYPE axis_steps_per_unit_inverse[STEPPER_COUNT];

#ifdef SPEED_CONTROL
FPTYPE speedFactor = KCONSTANT_1;
uint8_t alterSpeed = 0x00;
#endif

// Some gcode is loaded with enable/disable extruder commands. E.g., before each travel-only move.
// This seems okay for 1.75 mm filament extruders.  However, it is problematic for 3mm filament
// extruders: when the stepper motor is disabled, too much filament backs out owing to the high
// melt chamber pressure and the free-wheeling pinch gear.  To combat this, the firmware has an
// option to leave the extruder stepper motors engaged throughout an entire build, ignoring any
// gcode / s3g command to disable the extruder stepper motors.
bool extruder_hold[EXTRUDERS]; // True if the extruders should not be disabled during printing

// Segments are accelerated when segmentAccelState is true; unaccelerated otherwise
static bool segmentAccelState = true;

bool holdZ = false;

Point tolerance_offset_T0;
Point tolerance_offset_T1;
Point *tool_offsets;
uint8_t toolIndex = 0;

//Also requires DEBUG_ONSCREEN to be defined in StepperAccel.h
//#define TIME_STEPPER_INTERRUPT

#if defined(DEBUG_ONSCREEN) && defined(TIME_STEPPER_INTERRUPT)
uint16_t debugTimer = 0;
#endif


bool isRunning() {
	return is_running || is_homing;
}

bool isHoming() {
        return is_homing;
}

// The following simple routine loads the toolhead offsets.  The systems
// used for the offsets have changed over time much to the confusion and
// consternation of users.  And, to compound the problem it appears that
// an auto-conversion of the firmware's 5.x to 6.x format was done
// incorrectly.
//
// Changes such as this unfortunately make it difficult for third-party
// tools (e.g., slicers) to support MBI's hardware: who wants to keep on
// chasing a moving target?
//
// Brief history of the toolhead offsets.
//
// RepG 29 (11 December 2011; MBI 5.x firmwares)
// ---------------------------------------------
//
//   1. Each toolhead moved ½ the ideal offset in gcode
//
//      G10 P1 X-16.5  ( X coordinates += -16.5 mm after a G54 command issued )
//      G10 P2 X+16.5  ( X coordinates += +16.5 mm after a G55 command issued )
//
//   2. No extra handling in the firmware
//
//   This was likely done to kick start experimentation of dualstrusion.
//   Folks using Thing-o-Matics with no modifications to the 3.1 firmware.
//
// RepG 33, 34 (27 February, 13 March 2012; MBI 5.x firmwares)
// -----------------------------------------------------------
//
//   1. Each tool head moved ½ the ideal offset in gcode
//
//      G10 P1 X-16.5  ( X coordinates += -16.5 mm after a G54 command issued )
//      G10 P2 X+16.5  ( X coordinates += +16.5 mm after a G55 command issued )
//
//   2. In firmware, deviations from ideal are stored in EEPROM
//
//   3. In firmware, deviations split between each toolhead
//
//      Tool 0 in use: X coordinates += + x-deviation / 2
//      Tool 1 in use: X coordinates += - x-deviation / 2
//
//   So, when moving to X = 0, the actual position is
//
//      Tool 0 in use:  -16.5 mm + x-deviation / 2
//      Tool 1 in use:  +16.5 mm - x-deviation / 2
//
//   Difference is then
//
//      total-X-offset = 33.0 mm - x-deviation
//
//   Consequently,
//
//      x-deviation > 0 ==> nozzles are closer together than the ideal 33.0 mm
//      x-deviation < 0 ==> nozzles are farther appart than the ideal 33.0 mm
//
// RepG 37, 39 (22 June, 3 October 2012; MBI 5.5 firmware)
// -------------------------------------------------------
//
//   1. Tool 1 moved the entire ideal offset in gcode
//
//      G10 P1 X0     ( X coordinates unchanged after a G54 command issued )
//      G10 P2 X33.0  ( X coordinates += 33.0 mm after a G55 command issued )
//
//   2. In firmware, deviations from ideal are stored in EEPROM
//
//   3. In firmware, deviations split between each toolhead
//
//      Tool 0 in use: X coordinates += + x-deviation / 2
//      Tool 1 in use: X coordinates += - x-deviation / 2
//
//   So, when moving to X = 0, the actual position is
//
//      Tool 0 in use:    0.0 mm + x-deviation / 2
//      Tool 1 in use:  +33.0 mm - x-deviation / 2
//
//   Difference is then
//
//      total-X-offset = 33.0 mm - x-deviation
//
//   Consequently,
//
//      x-deviation > 0 ==> nozzles are closer together than the ideal 33.0 mm
//      x-deviation < 0 ==> nozzles are farther appart than the ideal 33.0 mm
//
// RepG 40 (9 November 2012; MBI 6.0 firmware)
// -------------------------------------------
// *** MBI introduces an error in their 6.x firmware for handling of old
// *** EEPROM toolhead offsets.
//
//   1. Gcode no longer tracks toolhead offsets: the G10, G54, and G55
//      commands are no longer used.
//
//   2. In firmware, the total offsets are stored in EEPROM
//
//   3. In firmware, the total offset is put onto Tool 1
//
//      Tool 0 in use: X coordinates remain unchanged
//      Tool 1 in use: X coordinates += total-X-offset'
//
//   So, when moving to X = 0, the actual position is
//
//      Tool 0 in use:  0.0 mm
//      Tool 1 in use:  0.0 mm + total-X-offset'
//
//   The difference is the total-X-offset'.
//
//   Fine so far.  However, MBI included in the 6.x (and now 7.x) firmware
//   code to automatically convert the 5.x "x-deviation" stored in EEPROM
//   to a "total-X-offset'" stored in EEPROM.  They used this formula,
//
//      total-X-offset' = 33.0 mm + x-deviation
//
//   Unfortunately, that's wrong the wrong formula!  As per the analyses
//   above of what the x-deviation stored in EEPROM means,
//
//      x-deviation > 0 ==> nozzles are closer than 33.0 mm
//      x-deviation < 0 ==> nozzles are farther appart than 33.0 mm
//
//   Thus, the correct translation to a total-X-offset is
//
//      total-X-offset' = 33.0 mm - x-deviation
//
//   And that's exactly the form we arrived at in the prior analysis.
//   Additionally, this can be seen when doing dualstrusion prints with
//   MBI's 5.5 and then 6.2 firmware.  Dan happens to have nozzles with
//   significant offsets in both X and Y ( ~0.6 mm for both ).  A very
//   nicely aligned print done with 5.5 then comes out with visible
//   misalignment with 6.2 and its incorrect auto-conversion of the tool
//   head offsets.  The sign error in the conversion gives a result off
//   by twice the offset or ~1.2 mm.  That's visually significant.
//
//   Minor correction of the 6.2 firmware to compute the total-X-offset'
//   correctly resulted in correctly aligned prints.
//
// Sailfish therefore implements the correct conversion.  Also, unlike
// the MBI firmware, it doesn't change the offsets stored in EEPROM.  It
// just converts the offsets at run time as necessary.

void loadToleranceOffsets() {

#define TOOLHEAD_OFFSET_X 33.0

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		// Force all toolhead offsets to 0
		// Note that when the bot's tool count is set to 1, RepG won't display the
		//   toolhead offsets.  It then becomes possible for a bot operator to not
		//   realize that s/he has non-zero toolhead offsets set.  RepG won't show
		//   them but they're there and effect behavior unless we force the offsets
		//   to zero when the tool count is != 2.

		for (uint8_t i = 0; i < STEPPER_COUNT; i++) {
			tolerance_offset_T0[i] = 0;
			tolerance_offset_T1[i] = 0;
		}
	}

#ifndef SIMULATOR
	// get toolhead offsets for dual extruder units
	if ( eeprom::getEeprom8(eeprom::TOOL_COUNT, 1) == 2 ) {

		// ~4 mm expressed in units of steps
		int32_t fourMM = ((int32_t)stepperAxisStepsPerMM(0)) << 2;

		// The X Toolhead offset in units of stepps
		int32_t xToolheadOffset = (int32_t)(eeprom::getEepromUInt32(eeprom::TOOLHEAD_OFFSET_SETTINGS + 0, 0));
		int32_t yToolheadOffset = (int32_t)(eeprom::getEepromUInt32(eeprom::TOOLHEAD_OFFSET_SETTINGS + 4, 0));

		// See which toolhead offset system is used
		uint8_t toolhead_system =
			eeprom::getEeprom8(eeprom::TOOLHEAD_OFFSET_SYSTEM, EEPROM_DEFAULT_TOOLHEAD_OFFSET_SYSTEM);

		if ( toolhead_system == 0 ) {

			// OLD SYSTEM: stored offset is the deviation from the
			//    ideal offset of 33.0 or 35.0 mm.
			
			// See if the stored offset is > 4 mm.  If so, then it's
			//    likely meant for the new system.  If so, convert it
			//    to the old system.

			if ( abs(xToolheadOffset) > fourMM ) {
				xToolheadOffset =
					(int32_t)(0.5 + TOOLHEAD_OFFSET_X * stepperAxisStepsPerMM(0)) - xToolheadOffset;
				yToolheadOffset = -yToolheadOffset;
			}

			ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
				// In the RepG 39 and earlier system
				//
				//    T0[i] = +1/2 offset[i]
				//    T1[i] = -1/2 offset[i]
				//
				// MOREOVER, the offset is the deviation from the ideal offset
				tolerance_offset_T0[0] =  xToolheadOffset / 2;
				tolerance_offset_T1[0] = -tolerance_offset_T0[0];
				tolerance_offset_T0[1] =  yToolheadOffset / 2;
				tolerance_offset_T1[1] = -tolerance_offset_T0[1];
			}
		}
		else {

			// NEW SYSTEM: stored offset is the actual offset (33 or 35 mm)
			
			// See if the stored offset is < 4.0 mm.  If so, then it's
			//    likely meant for the old system.  If so, convert it
			//    to the new system.

			if ( abs(xToolheadOffset) <= fourMM ) {
				xToolheadOffset =
					(int32_t)(0.5 + TOOLHEAD_OFFSET_X * stepperAxisStepsPerMM(0)) - xToolheadOffset;
				yToolheadOffset = -yToolheadOffset;
			}

			ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
				// In the RepG 40 and later system
				//
				//    T0[i] = 0
				//    T1[i] = offset[i]
				//
				// The offset is the full offset, not the deviation from the offset
				tolerance_offset_T1[0] = xToolheadOffset;
				tolerance_offset_T1[1] = yToolheadOffset;
			}
		}
	}
#endif
}


//Returns true if the eeprom contains sane values
//We check Jog Mode is 0, 1 or 2 and STEPS_PER_MM_B is STEPS_PER_MM_B_DEFAULT
//Note that STEPS_PER_MM_B is currently unused.  If we ever use STEPS_PER_MM_B, we will want to find
//another method or just rely on JogMode.
//This is mainly a solution that gets around issues with RobG's firmware conflicting with this one.

bool eepromIsSane() {
	uint8_t jogModeSettings = eeprom::getEeprom8(eeprom::JOG_MODE_SETTINGS);
	jogModeSettings = jogModeSettings >> 1;

	//Valid values are 0,1 or 2, anything else means we have corruption
	if ( jogModeSettings > 2 )	return false;

	return true;
}

void reset() {
	if ( ! eepromIsSane() ) eeprom::setJettyFirmwareDefaults();

	stepperAxisInit(false);

	// must be after stepperAxisInit() so that stepperAxisStepsPerMM() functions correctly
	loadToleranceOffsets();

	// must be after loadToleranceOffsets() so that the toolhead offsets are at hand
	changeToolIndex(0);

        //Get the acceleration settings
	uint8_t accel = eeprom::getEeprom8(eeprom::ACCELERATION_ON, EEPROM_DEFAULT_ACCELERATION_ON) & 0x01;
        acceleration = accel & 0x01;

	setSegmentAccelState(acceleration);
	deprimeEnable(true);

	//Here's more documentation on the various settings / features
	//http://wiki.ultimaker.com/Marlin_firmware_for_the_Ultimaker
	//https://github.com/ErikZalm/Marlin/commits/Marlin_v1
	//http://forums.reprap.org/read.php?147,94689,94689
	//http://reprap.org/pipermail/reprap-dev/2011-May/003323.html
	//http://www.brokentoaster.com/blog/?p=358

	for ( uint8_t i = 0; i < STEPPER_COUNT; i ++ )
		axis_steps_per_unit_inverse[i] = FTOFP(1.0 / stepperAxisStepsPerMM(i));

	// Set max acceleration in units/s^2 for print moves
	// X,Y,Z,A,B maximum start speed for accelerated moves.
	// A,B default values are good for skeinforge 40+, for older versions raise them a lot.
	max_acceleration_units_per_sq_second[X_AXIS] = eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_X, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_X);
	max_acceleration_units_per_sq_second[Y_AXIS] = eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_Y, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_Y);
	max_acceleration_units_per_sq_second[Z_AXIS] = eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_Z, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_Z);
	max_acceleration_units_per_sq_second[A_AXIS] = eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_A, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_A);
	#if EXTRUDERS > 1
	max_acceleration_units_per_sq_second[B_AXIS] = eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_B, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_B);
	#endif

	for (uint8_t i = 0; i < STEPPER_COUNT; i ++) {
		// Limit the max accelerations so that the calculation of block->acceleration & JKN Advance K2
		// can be performed without overflow issues
		if (max_acceleration_units_per_sq_second[i] > (uint32_t)((float)0xFFFFF / stepperAxisStepsPerMM(i)))
		     max_acceleration_units_per_sq_second[i] = (uint32_t)((float)0xFFFFF / stepperAxisStepsPerMM(i));
		axis_steps_per_sqr_second[i] = (uint32_t)((float)max_acceleration_units_per_sq_second[i] * stepperAxisStepsPerMM(i));
		axis_accel_step_cutoff[i] = (uint32_t)0xffffffff / axis_steps_per_sqr_second[i];
	}

	//Set default acceleration for "Normal Moves (acceleration)" and "filament only moves (retraction)" in mm/sec^2

	// X,Y,Z,A,B max acceleration in mm/s^2 for printing moves
	p_acceleration = (uint32_t)eeprom::getEepromUInt32(eeprom::ACCEL_MAX_EXTRUDER_NORM, EEPROM_DEFAULT_ACCEL_MAX_EXTRUDER_NORM);
	if (p_acceleration > 10000)
	     // 10,000 limit is actually a little smaller than 0xFFFFF / 96 steps/mm
	     p_acceleration = 10000;

#ifdef DEBUG_SLOW_MOTION
	p_acceleration = (uint32_t)20;
#endif

	// X,Y,Z,A,B max acceleration in mm/s^2 for retracts
	p_retract_acceleration  = (uint32_t)eeprom::getEepromUInt32(eeprom::ACCEL_MAX_EXTRUDER_RETRACT, EEPROM_DEFAULT_ACCEL_MAX_EXTRUDER_RETRACT);
	if (p_retract_acceleration > 10000)
	     // 10,000 limit is actually a little smaller than 0xFFFFF / 96 steps/mm
	     p_retract_acceleration = 10000;

#ifdef DEBUG_SLOW_MOTION
	p_retract_acceleration	= (uint32_t)20;
#endif

	//Number of steps when priming or deprime the extruder
	extruder_deprime_steps[0]    = (int16_t)eeprom::getEepromUInt32(eeprom::ACCEL_EXTRUDER_DEPRIME_A, EEPROM_DEFAULT_ACCEL_EXTRUDER_DEPRIME_A);
	#if EXTRUDERS > 1
	extruder_deprime_steps[1]    = (int16_t)eeprom::getEepromUInt32(eeprom::ACCEL_EXTRUDER_DEPRIME_B, EEPROM_DEFAULT_ACCEL_EXTRUDER_DEPRIME_B);
	#endif

	//Maximum speed change
	max_speed_change[X_AXIS]  = FTOFP((float)eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_X, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_X) / 10.0);
	max_speed_change[Y_AXIS]  = FTOFP((float)eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_Y, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_Y) / 10.0);
	max_speed_change[Z_AXIS]  = FTOFP((float)eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_Z, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_Z) / 10.0);
	max_speed_change[A_AXIS]  = FTOFP((float)eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_A, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_A) / 10.0);
	#if EXTRUDERS > 1
	max_speed_change[B_AXIS]  = FTOFP((float)eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_B, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_B) / 10.0);
	#endif

#ifdef DEBUG_SLOW_MOTION
	max_speed_change[X_AXIS]  = FTOFP((float)1);
	max_speed_change[Y_AXIS]  = FTOFP((float)1);
	max_speed_change[Z_AXIS]  = FTOFP((float)0.15);
	max_speed_change[A_AXIS]  = FTOFP((float)1);
	#if EXTRUDERS > 1
	max_speed_change[B_AXIS]  = FTOFP((float)1);
	#endif
#endif

#ifdef FIXED
	smallest_max_speed_change = max_speed_change[Z_AXIS];
	for (uint8_t i = 0; i < STEPPER_COUNT; i++) {
		if ( max_speed_change[i] < smallest_max_speed_change )
			smallest_max_speed_change = max_speed_change[i];
	}
#endif

	FPTYPE advanceK         = FTOFP((float)eeprom::getEepromUInt32(eeprom::ACCEL_ADVANCE_K,  EEPROM_DEFAULT_ACCEL_ADVANCE_K)         / 100000.0);
	FPTYPE advanceK2        = FTOFP((float)eeprom::getEepromUInt32(eeprom::ACCEL_ADVANCE_K2, EEPROM_DEFAULT_ACCEL_ADVANCE_K2)        / 100000.0);

	minimumSegmentTime = FTOFP((float)ACCELERATION_MIN_SEGMENT_TIME);

	// Minimum planner junction speed. Sets the default minimum speed the planner plans for at the end
	// of the buffer and all stops. This should not be much greater than zero and should only be changed
	// if unwanted behavior is observed on a user's machine when running at very slow speeds.
	minimumPlannerSpeed = FTOFP((float)ACCELERATION_MIN_PLANNER_SPEED);

	if ( eeprom::getEeprom8(eeprom::ACCEL_SLOWDOWN_FLAG, EEPROM_DEFAULT_ACCEL_SLOWDOWN_FLAG) ) {
		slowdown_limit = (int)ACCELERATION_SLOWDOWN_LIMIT;
		if ( slowdown_limit > (BLOCK_BUFFER_SIZE / 2))  slowdown_limit = 0;
	}
	else	slowdown_limit = 0;	

	//Clockwise extruder
	extrude_when_negative[0] = ACCELERATION_EXTRUDE_WHEN_NEGATIVE_A;
	#if EXTRUDERS > 1
	extrude_when_negative[1] = ACCELERATION_EXTRUDE_WHEN_NEGATIVE_B;
	#endif

	//These max feedrates limit the speed the extruder can move at when
	//it's been advanced, primed/deprimed and depressurized
	//It acts as an overall speed governer for the A/B axis
	//The values are obtained via the RepG xml and are updated on connection
	//with RepG if they're different than stored.  These values are in mm per
	//min, we divide by 60 here to get mm / sec.
	extruder_only_max_feedrate[0] = FPTOF(stepperAxis[A_AXIS].max_feedrate);
	#if EXTRUDERS > 1
	extruder_only_max_feedrate[1] = FPTOF(stepperAxis[B_AXIS].max_feedrate);
	#endif

	// Some gcode is loaded with enable/disable extruder commands. E.g., before each travel-only move.
	// This seems okay for 1.75 mm filament extruders.  However, it is problematic for 3mm filament
	// extruders: when the stepper motor is disabled, too much filament backs out owing to the high
	// melt chamber pressure and the free-wheeling pinch gear.  To combat this, the firmware has an
	// option to leave the extruder stepper motors engaged throughout an entire build, ignoring any
	// gcode / s3g command to disable the extruder stepper motors.
	extruder_hold[0] = ((eeprom::getEeprom8(eeprom::EXTRUDER_HOLD, EEPROM_DEFAULT_EXTRUDER_HOLD)) != 0);
	#if EXTRUDERS > 1
	extruder_hold[1] = extruder_hold[0];
	#endif

	int mscale = (int)(0.5 + stepperAxisStepsPerMM(A_AXIS) / 50.0);
	switch (mscale) {
	case 0: microstepping = 2; break;
	case 1: microstepping = 3; break;
	case 2: microstepping = 4; break;
	case 3:
	case 4: microstepping = 5; break;
	case 5:
	case 6:
	case 7:
	case 8: microstepping = 6; break;
	default: microstepping = 7; break;
	}

#ifdef PLANNER_OFF
	plannerMaxBufferSize = 1;
#else
	plannerMaxBufferSize = BLOCK_BUFFER_SIZE - 1;
#endif

#ifdef SPEED_CONTROL
	alterSpeed  = 0x00;
	speedFactor = KCONSTANT_1;
#endif

	plan_init(advanceK, advanceK2, holdZ);		//Initialize planner
	st_init();					//Initialize stepper accel
}

//public:
void init() {
	is_running = false;
	is_homing = false;

	stepperAxisInit(true);

        /// if eeprom has not been initialized. store default values
	if (eeprom::getEepromUInt32(eeprom::TOOLHEAD_OFFSET_SETTINGS, 0xFFFFFFFF) == 0xFFFFFFFF) {
		eeprom::storeToolheadToleranceDefaults();
	}
}


void abort() {
	//Stop the stepper subsystem and get the current position
	//after stopping
	quickStop();

        is_running = false;
        is_homing = false;
	
	stepperAxisInit(false);

	setSegmentAccelState(acceleration);
	deprimeEnable(true);
}

/// Define current position as given point
void definePosition(const Point& position_in, bool home) {
	Point position_offset = position_in;

	for ( uint8_t i = 0; i < STEPPER_COUNT; i ++ ) {
		stepperAxis[i].hasDefinePosition = true;

		//Add the toolhead offset
		if ( !home ) position_offset[i] += (*tool_offsets)[i];
	}

	plan_set_position(position_offset[X_AXIS], position_offset[Y_AXIS], position_offset[Z_AXIS], position_offset[A_AXIS], position_offset[B_AXIS]);
}


/// Get the last position of the planner
/// This is also the target position of the last command that was sent with
/// setTarget / setTargetNew / setTargetNewExt
/// Note this isn't the position of the hardware right now, use getStepperPosition for that.
/// If the pipeline buffer is empty, then getPlannerPosition == getStepperPosition
const Point getPlannerPosition() {
	Point p;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
		#if EXTRUDERS > 1
			p = Point(planner_position[X_AXIS], planner_position[Y_AXIS], planner_position[Z_AXIS],
				  planner_position[A_AXIS], planner_position[B_AXIS] );
		#else
			p = Point(planner_position[X_AXIS], planner_position[Y_AXIS], planner_position[Z_AXIS],
				  planner_position[A_AXIS], 0 );
		#endif

		//Subtract out the toolhead offset
		for ( uint8_t i = 0; i < STEPPER_COUNT; i ++ )
			p[i] -= (*tool_offsets)[i];
	}
	return p;
}


/// Get current position

#ifndef SIMULATOR

const Point getStepperPosition(uint8_t *toolhead) {
	uint8_t active_toolhead;
	int32_t position[STEPPER_COUNT];

	st_get_position(&position[X_AXIS], &position[Y_AXIS], &position[Z_AXIS], &position[A_AXIS], &position[B_AXIS], &active_toolhead);

	active_toolhead %= 2;	//Safeguard, shouldn't be needed

	//Because all targets have a toolhead offset added to them, we need to undo that here.
	//Also, because the toollhead can change, we need to use the active_toolhead from the hardware position
	//and can't use toolIndex
	Point *gp_tool_offsets;

	if ( active_toolhead == 1 )	gp_tool_offsets = &tolerance_offset_T1;
	else				gp_tool_offsets = &tolerance_offset_T0;

	//Subtract out the toolhead offset
	for ( uint8_t i = 0; i < STEPPER_COUNT; i ++ )
		position[i] -= (*gp_tool_offsets)[i];

	#if EXTRUDERS > 1
		Point p = Point(position[X_AXIS], position[Y_AXIS], position[Z_AXIS], position[A_AXIS], position[B_AXIS]);
	#else
		Point p = Point(position[X_AXIS], position[Y_AXIS], position[Z_AXIS], position[A_AXIS], 0);
	#endif
	if ( toolhead ) *toolhead = active_toolhead;
	return p;
}

#else

const Point getStepperPosition(uint8_t *toolhead) {
	Point p = Point(0,0,0,0,0);
	if ( toolhead ) *toolhead = 0;
	return p;
}

#endif


void setHoldZ(bool holdZ_in) {
	holdZ = holdZ_in;
}


void setTarget(const Point& target, int32_t dda_interval) {
	//Add on the tool offsets
	for ( uint8_t i = 0; i < STEPPER_COUNT; i ++ )
		planner_target[i] = target[i] + (*tool_offsets)[i];

#ifdef CLIP_Z_AXIS
	//Clip the Z axis so that it can't move outside the build area.
	//Addresses a specific issue with old start.gcode for the replicator.
	//It has a G1 Z155 command that was slamming the platform into the floor.  
	planner_target[Z_AXIS] = stepperAxis_clip_to_max(Z_AXIS, planner_target[Z_AXIS]);
#endif

	//Calculate the maximum steps of any axis and store in planner_master_steps
	//Also calculate the step deltas (planner_steps[i]) at the same time.
	int32_t max_delta = 0;
	planner_master_steps_index = 0;
	for (int i = 0; i < STEPPER_COUNT; i++) {
		planner_steps[i] = labs(planner_target[i] - planner_position[i]);

		if ( planner_steps[i] > max_delta) {
			planner_master_steps_index = i;
			max_delta = planner_steps[i];
		}
	}
	planner_master_steps = (uint32_t)max_delta;

	if ( planner_master_steps == 0 ) {
#ifdef DEBUG_BLOCK_BY_MOVE_INDEX
		//To keep in sync with the simulator
		current_move_index ++;
#endif
		return;
	}

	//dda_rate is the number of dda steps per second for the master axis
	uint32_t dda_rate = (uint32_t)(1000000 / dda_interval);

	plan_buffer_line(0, dda_rate, toolIndex, false, toolIndex);

	if ( movesplanned() >=  plannerMaxBufferSize) is_running = true;
	else                                          is_running = false;
}


void setTargetNew(const Point& target, int32_t us, uint8_t relative) {
	//Add on the tool offsets and convert relative moves into absolute moves
	for ( uint8_t i = 0; i < STEPPER_COUNT; i ++ ) {
		planner_target[i] = target[i] + (*tool_offsets)[i];

		if ((relative & (1 << i)) != 0) {
			planner_target[i] = planner_position[i] + planner_target[i];
		}
	}

#ifdef CLIP_Z_AXIS
	//Clip the Z axis so that it can't move outside the build area.
	//Addresses a specific issue with old start.gcode for the replicator.
	//It has a G1 Z155 command that was slamming the platform into the floor.  
	planner_target[Z_AXIS] = stepperAxis_clip_to_max(Z_AXIS, planner_target[Z_AXIS]);
#endif

        //Calculate the maximum steps of any axis and store in planner_master_steps
        //Also calculate the step deltas (planner_steps[i]) at the same time.
        int32_t max_delta = 0;
	planner_master_steps_index = 0;
        for (int i = 0; i < STEPPER_COUNT; i++) {
                planner_steps[i] = labs(planner_target[i] - planner_position[i]);

                if ( planner_steps[i] > max_delta) {
			planner_master_steps_index = i;
                        max_delta = planner_steps[i];
		}
        }
        planner_master_steps = (uint32_t)max_delta;

	if ( planner_master_steps == 0 ) {
#ifdef DEBUG_BLOCK_BY_MOVE_INDEX
		//To keep in sync with the simulator
		current_move_index ++;
#endif
		return;
	}

	int32_t  dda_interval	= us / max_delta;

	//dda_rate is the number of dda steps per second for the master axis
	uint32_t dda_rate	= (uint32_t)(1000000 / dda_interval);

	plan_buffer_line(0, dda_rate, toolIndex, false, toolIndex);

	if ( movesplanned() >=  plannerMaxBufferSize)      is_running = true;
	else                                               is_running = false;
}


//Dda_rate is the number of dda steps per second for the master axis

void setTargetNewExt(const Point& target, int32_t dda_rate, uint8_t relative, float distance, int16_t feedrateMult64) {

	//Add on the tool offsets and convert relative moves into absolute moves
	for ( uint8_t i = 0; i < STEPPER_COUNT; i ++ ) {
		planner_target[i] = target[i] + (*tool_offsets)[i];

		if ((relative & (1 << i)) != 0) {
			planner_target[i] = planner_position[i] + planner_target[i];
		}
	}

#ifdef CLIP_Z_AXIS
	//Clip the Z axis so that it can't move outside the build area.
	//Addresses a specific issue with old start.gcode for the replicator.
	//It has a G1 Z155 command that was slamming the platform into the floor.  
	planner_target[Z_AXIS] = stepperAxis_clip_to_max(Z_AXIS, planner_target[Z_AXIS]);
#endif

        //Calculate the maximum steps of any axis and store in planner_master_steps
        //Also calculate the step deltas (planner_steps[i]) at the same time.
        int32_t max_delta = 0;
        planner_master_steps_index = 0;
        for (int i = 0; i < STEPPER_COUNT; i++) {
                planner_steps[i] = planner_target[i] - planner_position[i];
		int32_t abs_planner_steps = labs(planner_steps[i]);
		if (abs_planner_steps <= 0x7fff)
		     delta_mm[i] = FPMULT2(ITOFP(planner_steps[i]), axis_steps_per_unit_inverse[i]);
		else
		      // This typically only happens for LONG Z axis moves
		      // As such it typically happens three times per print
		     delta_mm[i] = FTOFP((float)planner_steps[i] * FPTOF(axis_steps_per_unit_inverse[i]));
                planner_steps[i] = abs_planner_steps;

                if ( planner_steps[i] > max_delta) {
			planner_master_steps_index = i;
                        max_delta = planner_steps[i];
		}
        }
        planner_master_steps = (uint32_t)max_delta;

	if (( planner_master_steps == 0 ) || ( distance == 0.0 )) {
#ifdef DEBUG_BLOCK_BY_MOVE_INDEX
		//To keep in sync with the simulator
		current_move_index ++;
#endif
		return;
	}

	//Handle distance
	planner_distance = FTOFP(distance);

	//Handle feedrate
	FPTYPE feedrate = 0;

	if ( acceleration ) {
		feedrate = ITOFP((int32_t)feedrateMult64);

		//Feed rate was multiplied by 64 before it was sent, undo
#ifdef FIXED
		feedrate >>= 6;
#else
		feedrate /= 64.0;
#endif

#if defined(HAS_INTERFACE_BOARD) && defined(SPEED_CONTROL)
		if ( relative & 0x80 ) {
			feedrate = FPMULT2(feedrate, speedFactor);
			dda_rate = (int32_t)((float)dda_rate * FPTOF(speedFactor));
		}
#endif
	}

	plan_buffer_line(feedrate, dda_rate, toolIndex, acceleration && segmentAccelState, toolIndex);

	if ( movesplanned() >=  plannerMaxBufferSize)      is_running = true;
	else                                               is_running = false;
}


//Step positions for homing.  We shift by >> 1 so that we can add
//tool_offsets without overflow
#define POSITIVE_HOME_POSITION ((INT32_MAX - 1) >> 1)
#define NEGATIVE_HOME_POSITION ((INT32_MIN + 1) >> 1)

/// Start homing

void startHoming(const bool maximums, const uint8_t axes_enabled, const uint32_t us_per_step) {
	setSegmentAccelState(false);

	Point target = getStepperPosition();

        for (uint8_t i = 0; i < STEPPER_COUNT; i++) {
                if ((axes_enabled & (1<<i)) == 0) {
			axis_homing[i] = false;
		} else {
	 		target[i] = (maximums) ? POSITIVE_HOME_POSITION : NEGATIVE_HOME_POSITION;
			axis_homing[i] = true;
			stepperAxis[i].hasHomed = true;
                }
        }

	setTarget(target, us_per_step);

        is_homing = true;
}


/// Enable/disable the given axis.
void enableAxis(uint8_t index, bool enable) {
        if (index < STEPPER_COUNT) {
		stepperAxisSetEnabled(index, enable);
        }
}


/// Returns a bit mask for all axes enabled
uint8_t allAxesEnabled(void) {
	return axesEnabled;
}


/// Toggle segment acceleration on or off
/// Note this is also off if acceleration variable is not set
void setSegmentAccelState(bool state) {
     segmentAccelState = state;
}


void changeToolIndex(uint8_t tool) {
	toolIndex = tool;

	//Just in case as toolIndex is used to index into arrays
	//so we need a sane value here.
	toolIndex %= 2;

	if(toolIndex == 1)	tool_offsets = &tolerance_offset_T1;
	else			tool_offsets = &tolerance_offset_T0;
}


// endstop status bits: (7-0) : | N/A | N/A | z max | z min | y max | y min | x max | x min |

uint8_t getEndstopStatus() {
	uint8_t status = 0;

	status |= stepperAxisIsAtMaximum(Z_AXIS) << 5;
	status |= stepperAxisIsAtMinimum(Z_AXIS) << 4;
	status |= stepperAxisIsAtMaximum(Y_AXIS) << 3;
	status |= stepperAxisIsAtMinimum(Y_AXIS) << 2;
	status |= stepperAxisIsAtMaximum(X_AXIS) << 1;
	status |= stepperAxisIsAtMinimum(X_AXIS) << 0;

	return status;
}


// Enables and disables deprime
// Deprime is always disabled if acceleration is switch off in the eeprom

void deprimeEnable(bool enable) {
	st_deprime_enable(acceleration && enable);
}


void runSteppersSlice() {
#if 0
#ifdef DEBUG_VALUE
	uint8_t bufferUsed = movesplanned();

	//Set bit 1 of the debug leds to the amount of items left in the acceleration pipeline
	//2 or less items left, and we switch foo off, otherwise on
	if ( bufferUsed < 3 ) DEBUG_VALUE(0x00);
	else                  DEBUG_VALUE(0x01);
#endif
#endif

        //Set the stepper interrupt timer
#if defined(DEBUG_ONSCREEN) && defined(TIME_STEPPER_INTERRUPT)
        debug_onscreen2 = debugTimer;
#endif

#ifdef INTERFACE_BAR_PIN
	//Indicate acceleration on/off
        INTERFACE_BAR_PIN.setValue(segmentAccelState && acceleration);
#endif
			
#ifdef INTERFACE_FOO_PIN
	//Indicate near empty acceleration pipeline
	INTERFACE_FOO_PIN.setValue((movesplanned() >= 3));
#endif
}


void doStepperInterrupt() {
#if defined(DEBUG_ONSCREEN) && defined(TIME_STEPPER_INTERRUPT)
                DEBUG_TIMER_START;
#endif

	//is_running is determined when a buffer item is added, however
	//if st_interrupt deletes a buffer item, then is_running must have changed
	//and now be false, so we set it here
	if ( st_interrupt() ) is_running = false;

	//If we're homing, there's a few possibilities:
	//1. The homing is still running on one of the axis
	//2. The homing on all axis has stopped running, in which case, the block
	//is dead, so we delete it and sync up the planner position to the stepper position
	//Homing blocks aren't automatically deleted by st_interrupt because they are set to
	//positions of INT32_MAX/MIN.
	if ( is_homing ) {
		is_homing = false;
	
		//Are we still homing on one of the axis?
		for (uint8_t i = 0; i <= Z_AXIS; i++)
			is_homing |= axis_homing[i];

		//If we've finished homing, stop the stepper subsystem
		//and sync
		if ( ! is_homing ) {
			//Delete all blocks (should only be 1 homing block) and sync
			//planner position to stepper position
			quickStop();

			setSegmentAccelState(acceleration);
		}
	}

#if defined(DEBUG_ONSCREEN) && defined(TIME_STEPPER_INTERRUPT)
        DEBUG_TIMER_FINISH;
        debugTimer = DEBUG_TIMER_TCTIMER_USI;
#endif
}


void doExtruderInterrupt() {
	st_extruder_interrupt();
}

}

/*
 * Copyright 2010 by Adam Mayer <adam@makerbot.com>
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

#include "EepromMap.hh"
#include "Eeprom.hh"
#include "EepromDefaults.hh"
#include <avr/eeprom.h>

namespace eeprom {

void setJettyFirmwareDefaults() {
#ifdef ERASE_EEPROM_ON_EVERY_BOOT
	return;
#endif

#ifdef EEPROM_DEFAULT_TOOL0_TEMP
    eeprom_write_byte((uint8_t*)eeprom::TOOL0_TEMP,			EEPROM_DEFAULT_TOOL0_TEMP);
#endif

#ifdef EEPROM_DEFAULT_TOOL1_TEMP
    eeprom_write_byte((uint8_t*)eeprom::TOOL1_TEMP,			EEPROM_DEFAULT_TOOL1_TEMP);
#endif

#ifdef EEPROM_DEFAULT_PLATFORM_TEMP
    eeprom_write_byte((uint8_t*)eeprom::PLATFORM_TEMP,			EEPROM_DEFAULT_PLATFORM_TEMP);
#endif

#ifdef EEPROM_DEFAULT_EXTRUDE_DURATION
    eeprom_write_byte((uint8_t*)eeprom::EXTRUDE_DURATION,		EEPROM_DEFAULT_EXTRUDE_DURATION);
#endif

#ifdef EEPROM_DEFAULT_EXTRUDE_MMS
    eeprom_write_byte((uint8_t*)eeprom::EXTRUDE_MMS,			EEPROM_DEFAULT_EXTRUDE_MMS);
#endif

#ifdef EEPROM_DEFAULT_MOOD_LIGHT_SCRIPT
    eeprom_write_byte((uint8_t*)eeprom::MOOD_LIGHT_SCRIPT,		EEPROM_DEFAULT_MOOD_LIGHT_SCRIPT);
#endif

#ifdef EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_RED
    eeprom_write_byte((uint8_t*)eeprom::MOOD_LIGHT_CUSTOM_RED,		EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_RED);
#endif

#ifdef EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_GREEN
    eeprom_write_byte((uint8_t*)eeprom::MOOD_LIGHT_CUSTOM_GREEN,	EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_GREEN);
#endif

#ifdef EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_BLUE
    eeprom_write_byte((uint8_t*)eeprom::MOOD_LIGHT_CUSTOM_BLUE,		EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_BLUE);
#endif

    eeprom_write_byte((uint8_t*)eeprom::JOG_MODE_SETTINGS,		EEPROM_DEFAULT_JOG_MODE_SETTINGS);

#ifdef EEPROM_DEFAULT_BUZZER_REPEATS
    eeprom_write_byte((uint8_t*)eeprom::BUZZER_REPEATS,			EEPROM_DEFAULT_BUZZER_REPEATS);
#endif

    putEepromInt64(eeprom::STEPS_PER_MM_X,				EEPROM_DEFAULT_STEPS_PER_MM_X);
    putEepromInt64(eeprom::STEPS_PER_MM_Y,				EEPROM_DEFAULT_STEPS_PER_MM_Y);
    putEepromInt64(eeprom::STEPS_PER_MM_Z,				EEPROM_DEFAULT_STEPS_PER_MM_Z);
    putEepromInt64(eeprom::STEPS_PER_MM_A,				EEPROM_DEFAULT_STEPS_PER_MM_A);
    putEepromInt64(eeprom::STEPS_PER_MM_B,				EEPROM_DEFAULT_STEPS_PER_MM_B);

    eeprom_write_byte((uint8_t*)eeprom::ABP_COPIES,			EEPROM_DEFAULT_ABP_COPIES);

#ifdef EEPROM_DEFAULT_OVERRIDE_GCODE_TEMP
    eeprom_write_byte((uint8_t*)eeprom::OVERRIDE_GCODE_TEMP,		EEPROM_DEFAULT_OVERRIDE_GCODE_TEMP);
#endif

#ifdef EEPROM_DEFAULT_SD_USE_CRC
    eeprom_write_byte((uint8_t*)eeprom::SD_USE_CRC,                     EEPROM_DEFAULT_SD_USE_CRC);
#endif

    eeprom_write_byte((uint8_t*)eeprom::ACCELERATION_ON,		EEPROM_DEFAULT_ACCELERATION_ON);
    putEepromUInt32(eeprom::ACCEL_MAX_FEEDRATE_X,			EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_X);
    putEepromUInt32(eeprom::ACCEL_MAX_FEEDRATE_Y,			EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_Y);
    putEepromUInt32(eeprom::ACCEL_MAX_FEEDRATE_Z,			EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_Z);
    putEepromUInt32(eeprom::ACCEL_MAX_FEEDRATE_A,			EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_A);
    putEepromUInt32(eeprom::ACCEL_MAX_FEEDRATE_B,			EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_B);
    putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_X,			EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_X);
    putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_Y,			EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_Y);
    putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_Z,			EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_Z);
    putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_A,			EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_A);
    putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_B,			EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_B);
    putEepromUInt32(eeprom::ACCEL_MAX_EXTRUDER_NORM,			EEPROM_DEFAULT_ACCEL_MAX_EXTRUDER_NORM);
    putEepromUInt32(eeprom::ACCEL_MAX_EXTRUDER_RETRACT,			EEPROM_DEFAULT_ACCEL_MAX_EXTRUDER_RETRACT);
    putEepromUInt32(eeprom::ACCEL_ADVANCE_K2,				EEPROM_DEFAULT_ACCEL_ADVANCE_K2);
    putEepromUInt32(eeprom::ACCEL_ADVANCE_K,				EEPROM_DEFAULT_ACCEL_ADVANCE_K);

#ifdef EEPROM_DEFAULT_LCD_TYPE
    eeprom_write_byte((uint8_t*)eeprom::LCD_TYPE,			EEPROM_DEFAULT_LCD_TYPE);
#endif

#ifdef EEPROM_DEFAULT_ENDSTOPS_USED
    eeprom_write_byte((uint8_t*)eeprom::ENDSTOPS_USED,			EEPROM_DEFAULT_ENDSTOPS_USED);
#endif

#ifdef EEPROM_DEFAULT_HOMING_FEED_RATE_X
    putEepromUInt32(eeprom::HOMING_FEED_RATE_X,				EEPROM_DEFAULT_HOMING_FEED_RATE_X);
#endif

#ifdef EEPROM_DEFAULT_HOMING_FEED_RATE_Y
    putEepromUInt32(eeprom::HOMING_FEED_RATE_Y,				EEPROM_DEFAULT_HOMING_FEED_RATE_Y);
#endif

#ifdef EEPROM_DEFAULT_HOMING_FEED_RATE_Z
    putEepromUInt32(eeprom::HOMING_FEED_RATE_Z,				EEPROM_DEFAULT_HOMING_FEED_RATE_Z);
#endif

    putEepromUInt32(eeprom::ACCEL_EXTRUDER_DEPRIME_A,			EEPROM_DEFAULT_ACCEL_EXTRUDER_DEPRIME_A);
    putEepromUInt32(eeprom::ACCEL_EXTRUDER_DEPRIME_B,			EEPROM_DEFAULT_ACCEL_EXTRUDER_DEPRIME_B);
    eeprom_write_byte((uint8_t *)eeprom::EXTRUDER_DEPRIME_ON_TRAVEL,    EEPROM_DEFAULT_DEPRIME_ON_TRAVEL);
    eeprom_write_byte((uint8_t *)eeprom::ACCEL_SLOWDOWN_FLAG,		EEPROM_DEFAULT_ACCEL_SLOWDOWN_FLAG);

    putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_X,			EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_X);
    putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_Y,			EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_Y);
    putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_Z,			EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_Z);
    putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_A,			EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_A);
    putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_B,			EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_B);

    eeprom_write_byte((uint8_t*)eeprom::DITTO_PRINT_ENABLED,		EEPROM_DEFAULT_DITTO_PRINT_ENABLED);
    eeprom_write_byte((uint8_t*)eeprom::EXTRUDER_HOLD,			EEPROM_DEFAULT_EXTRUDER_HOLD);
    eeprom_write_byte((uint8_t*)eeprom::TOOLHEAD_OFFSET_SYSTEM,		EEPROM_DEFAULT_TOOLHEAD_OFFSET_SYSTEM);

    eeprom_write_byte((uint8_t*)eeprom::CLEAR_FOR_ESTOP, 0);

    verifyAndFixVidPid();
}

// TODO: Shouldn't this just reset everything to an uninitialized state?
void setDefaults(bool retainCounters) {
#ifdef ERASE_EEPROM_ON_EVERY_BOOT
	return;
#endif

    // Initialize eeprom map
    eeprom_write_byte((uint8_t*)eeprom::AXIS_INVERSION,		EEPROM_DEFAULT_AXIS_INVERSION);
    eeprom_write_byte((uint8_t*)eeprom::ENDSTOP_INVERSION,	EEPROM_DEFAULT_ENDSTOP_INVERSION);
    eeprom_write_byte((uint8_t*)eeprom::MACHINE_NAME,		EEPROM_DEFAULT_MACHINE_NAME);

#if defined(EEPROM_DEFAULT_ESTOP_CONFIGURATION) && defined(HAS_ESTOP)
    eeprom_write_byte((uint8_t*)eeprom::ESTOP_CONFIGURATION,	EEPROM_DEFAULT_ESTOP_CONFIGURATION);
#endif

    eeprom_write_byte((uint8_t*)eeprom::TOOL_COUNT,		EEPROM_DEFAULT_TOOL_COUNT);

    setJettyFirmwareDefaults();

#ifdef CUPCAKE_3G5D_COMBINED_ENDSTOPS
    eeprom_write_byte((uint8_t*)eeprom::ENDSTOP_Z_MIN, 0);
#endif

    if (retainCounters)
	    return;

    //These settings aren't set in Jetty Firmware defaults, as we never want them to be accidently overwritten
    putEepromInt64(eeprom::FILAMENT_LIFETIME_A,			EEPROM_DEFAULT_FILAMENT_LIFETIME);
    putEepromInt64(eeprom::FILAMENT_LIFETIME_B,			EEPROM_DEFAULT_FILAMENT_LIFETIME);
    putEepromInt64(eeprom::FILAMENT_TRIP_A,			EEPROM_DEFAULT_FILAMENT_TRIP);
    putEepromInt64(eeprom::FILAMENT_TRIP_B,			EEPROM_DEFAULT_FILAMENT_TRIP);
}

void storeToolheadToleranceDefaults(){

        // assume t0 to t1 distance is in specifications (0 steps tolerance error)
        uint32_t offsets[3] = {0,0,0};
        eeprom_write_block((uint8_t*)&(offsets[0]),(uint8_t*)(eeprom::TOOLHEAD_OFFSET_SETTINGS), 12 );

}

void verifyAndFixVidPid() {
    /// write Sailfish VID/PID.
    uint16_t vidPid[] = {0x23C1, 0xACDC};           /// PID/VID for the Sailfish!
    uint16_t currentVidPid[2];

    eeprom_read_block(&(currentVidPid[0]), (uint8_t*)eeprom::VID_PID_INFO, 4);
    
    if (( currentVidPid[0] != vidPid[0] ) || ( currentVidPid[1] != vidPid[1] ))
	eeprom_write_block(&(vidPid[0]),(uint8_t*)eeprom::VID_PID_INFO,4);
}

}

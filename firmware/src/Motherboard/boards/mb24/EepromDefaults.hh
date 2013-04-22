//
// MBI Firmware Defaults
//
#define EEPROM_DEFAULT_AXIS_INVERSION			(uint8_t)(1<<1)		// Y axis = 1
#define EEPROM_DEFAULT_ENDSTOP_INVERSION		(uint8_t)0b00011111	// All endstops inverted

#define EEPROM_DEFAULT_MACHINE_NAME			0			// name is null

#define EEPROM_DEFAULT_ESTOP_CONFIGURATION		ESTOP_CONF_NONE

// Not strictly MBI defaults, but we but them here as we never want these values to be overwritten
#define EEPROM_DEFAULT_FILAMENT_LIFETIME		0
#define EEPROM_DEFAULT_FILAMENT_TRIP			0

//
// Jetty Firmware Defaults
//
#define EEPROM_DEFAULT_TOOL0_TEMP			220
#define EEPROM_DEFAULT_TOOL1_TEMP			220
#define EEPROM_DEFAULT_PLATFORM_TEMP			110

#define EEPROM_DEFAULT_EXTRUDE_DURATION			1
#define EEPROM_DEFAULT_EXTRUDE_MMS			1

#define EEPROM_DEFAULT_MOOD_LIGHT_SCRIPT		0
#define EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_RED		255
#define EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_GREEN		255
#define EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_BLUE		255

#define EEPROM_DEFAULT_JOG_MODE_SETTINGS		0

#define EEPROM_DEFAULT_BUZZER_REPEATS			3

// The next 4 entries, aren't really the defaults, but they're used in EEPROM_DEFAULT_STEPS_PER_MM_?, so we include
// them here so we don't miss them if we change scaling
#define STEPS_PER_MM_PADDING     5
#define STEPS_PER_MM_PRECISION   10
#define STEPS_PER_MM_LOWER_LIMIT 10000000
#define STEPS_PER_MM_UPPER_LIMIT 200000000000000

#define EEPROM_DEFAULT_STEPS_PER_MM_X			470698520000	// 47.069852
#define EEPROM_DEFAULT_STEPS_PER_MM_Y			470698520000	// 47.069852
#define EEPROM_DEFAULT_STEPS_PER_MM_Z			2000000000000	// 200.0
#define EEPROM_DEFAULT_STEPS_PER_MM_A			502354788069	// 50.2354788069
#define EEPROM_DEFAULT_STEPS_PER_MM_B			502354788069	// 50.2354788069

#define EEPROM_DEFAULT_ABP_COPIES			1

#define EEPROM_DEFAULT_OVERRIDE_GCODE_TEMP		0

#define EEPROM_DEFAULT_ACCELERATION_ON			0x01

#define EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_X		(100*60)
#define EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_Y		(100*60)
#define EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_Z		(16*60)
#define EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_A		(100*60)
#define EEPROM_DEFAULT_ACCEL_MAX_FEEDRATE_B		(100*60)

#define EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_X		500
#define EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_Y		500
#define EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_Z		150
#define EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_A		10000
#define EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_B		10000

#define EEPROM_DEFAULT_ACCEL_MAX_EXTRUDER_NORM		2000
#define EEPROM_DEFAULT_ACCEL_MAX_EXTRUDER_RETRACT	4000

#define EEPROM_DEFAULT_ACCEL_ADVANCE_K			700		// 0.00850 Multiplied by 100000
#define EEPROM_DEFAULT_ACCEL_ADVANCE_K2			400		// 0.00900 Multiplied by 100000

#define LCD_TYPE_16x4   0
#define LCD_TYPE_20x4   50
#define LCD_TYPE_24x4   51
#define EEPROM_DEFAULT_LCD_TYPE				LCD_TYPE_16x4	// 20x4 = 50, 24x4 = 51, anything else = 16x4 (done that way to make it unlike anything other than
									// 16x4 will get chosen due to corrupted eeprom
#define EEPROM_DEFAULT_ENDSTOPS_USED			(32+4+1)	// ZMax + YMin + XMin

#define EEPROM_DEFAULT_HOMING_FEED_RATE_X		500
#define EEPROM_DEFAULT_HOMING_FEED_RATE_Y		500
#define EEPROM_DEFAULT_HOMING_FEED_RATE_Z		500

#define EEPROM_DEFAULT_ACCEL_EXTRUDER_DEPRIME_A		8		// Steps
#define EEPROM_DEFAULT_ACCEL_EXTRUDER_DEPRIME_B		8		// Steps

#define EEPROM_DEFAULT_ACCEL_SLOWDOWN_FLAG		1

#define EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_X		300		// mm/s Multiplied by 10
#define EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_Y		300		// mm/s Multiplied by 10
#define EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_Z		100		// mm/s Multiplied by 10
#define EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_A		300		// mm/s Multiplied by 10
#define EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_B		300		// mm/s Multiplied by 10

#define EEPROM_DEFAULT_DITTO_PRINT_ENABLED		0

#define EEPROM_DEFAULT_AXIS_LENGTH			1000		//Steps

#define EEPROM_DEFAULT_TOOL_COUNT			1

#define EEPROM_DEFAULT_EXTRUDER_HOLD			0

#define EEPROM_DEFAULT_TOOLHEAD_OFFSET_SYSTEM           1

#define EEPROM_DEFAULT_SD_USE_CRC                       0

#define EEPROM_DEFAULT_PSTOP_ENABLE                     0

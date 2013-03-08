// Future things that could be consolidated into 1 to save code space when required:
//
// Combined lcd.clear() and lcd.setCursor(0, 0) -> lcd.clearHomeCursor(): savings 184 bytes
// lcd.setCursor(0, r) --> lcd.setRow(r): savings 162 bytes
//
// ValueSetScreen
// BuzzerSetRepeatsMode
// ABPCopiesSetScreen

#include "Configuration.hh"
#include "Menu.hh"

// TODO: Kill this, should be hanlded by build system.
#ifdef HAS_INTERFACE_BOARD

#include "StepperAccel.hh"
#include "Steppers.hh"
#include "Commands.hh"
#include "Errors.hh"
#include "Tool.hh"
#include "Host.hh"
#include "Timeout.hh"
#include "InterfaceBoard.hh"
#include "Interface.hh"
#include "Motherboard.hh"
#include "Version.hh"
#include <util/delay.h>
#include <stdlib.h>
#include "SDCard.hh"
#include "EepromMap.hh"
#include "Eeprom.hh"
#include "EepromDefaults.hh"
#include <avr/eeprom.h>
#include "ExtruderControl.hh"
#include "Main.hh"
#include "locale.h"
#include "lib_sd/sd_raw_err.h"

// Maximum length of an SD card file
#define SD_MAXFILELEN 64

#define HOST_PACKET_TIMEOUT_MS 20
#define HOST_PACKET_TIMEOUT_MICROS (1000L*HOST_PACKET_TIMEOUT_MS)

#define HOST_TOOL_RESPONSE_TIMEOUT_MS 50
#define HOST_TOOL_RESPONSE_TIMEOUT_MICROS (1000L*HOST_TOOL_RESPONSE_TIMEOUT_MS)

#define MAX_ITEMS_PER_SCREEN 4

#define LCD_TYPE_CHANGE_BUTTON_HOLD_TIME 10.0

int16_t overrideExtrudeSeconds = 0;

Point homePosition;

static uint16_t genericOnOff_offset;
static uint8_t genericOnOff_default;
static const prog_uchar *genericOnOff_msg1;
static const prog_uchar *genericOnOff_msg2;
static const prog_uchar *genericOnOff_msg3;
static const prog_uchar *genericOnOff_msg4;

static const PROGMEM prog_uchar eof_msg1[]     = "Extruder Hold:";
static const PROGMEM prog_uchar generic_off[]  = "Off";
static const PROGMEM prog_uchar generic_on[]   = "On";
static const PROGMEM prog_uchar updnset_msg[]  = "Up/Dn/Ent to Set";
static const PROGMEM prog_uchar unknown_temp[] = "XXX";

static const PROGMEM prog_uchar aof_msg1[] = "Accelerated";
static const PROGMEM prog_uchar aof_msg2[] = "Printing:";
static const PROGMEM prog_uchar ts_msg1[] = "Toolhead offset";
static const PROGMEM prog_uchar ts_msg2[] = "system:";
static const PROGMEM prog_uchar ts_old[]  = "Old";
static const PROGMEM prog_uchar ts_new[]  = "New";
static const PROGMEM prog_uchar dp_msg1[]   = "Ditto Printing:";
static const PROGMEM prog_uchar ogct_msg1[] = "Override GCode";
static const PROGMEM prog_uchar ogct_msg2[] = "Temperature:";
static const PROGMEM prog_uchar sdcrc_msg1[] = "Perform SD card";
static const PROGMEM prog_uchar sdcrc_msg2[] = "error checking:";


//Macros to expand SVN revision macro into a str
#define STR_EXPAND(x) #x	//Surround the supplied macro by double quotes
#define STR(x) STR_EXPAND(x)

const static PROGMEM prog_uchar units_mm[] = "mm";
static const char dumpFilename[] = "eeprom_dump.bin";
static void timedMessage(LiquidCrystal& lcd, uint8_t which);

void VersionMode::reset() {
}


//  Assumes room for up to 7 + NUL
static void formatTime(char *buf, uint32_t val)
{
	bool hasdigit = false;
	uint8_t idx = 0;

	uint8_t radidx = 0;
	const uint8_t radixcount = 5;
	const uint8_t houridx = 2;
	const uint8_t minuteidx = 4;
	uint32_t radixes[radixcount] = {360000, 36000, 3600, 600, 60};
	if (val >= 3600000)
		val %= 3600000;

	for (radidx = 0; radidx < radixcount; radidx++) {
		char digit = '0';
		uint8_t bit = 8;
		uint32_t radshift = radixes[radidx] << 3;
		for (; bit > 0; bit >>= 1, radshift >>= 1) {
			if (val > radshift) {
				val -= radshift;
				digit += bit;
			}
		}
		if (hasdigit || digit != '0' || radidx >= houridx) {
			buf[idx++] = digit;
			hasdigit = true;
		} else
			buf[idx++] = ' ';
		if (radidx == houridx)
			buf[idx++] = 'h';
		else if (radidx == minuteidx)
			buf[idx++] = 'm';
	}

	buf[idx] = '\0';
}


//  Assumes at least 3 spare bytes
static void digits3(char *buf, uint8_t val)
{
	uint8_t v;

	if ( val >= 100 )
	{
		v = val / 100;
		buf[0] = v + '0';
		val -= v * 100;
	}
	else
		buf[0] = ' ';

	if ( val >= 10 || buf[0] != ' ')
	{
		v = val / 10;
		buf[1] = v + '0';
		val -= v * 10;
	}
	else
		buf[1] = ' ';

	buf[2] = val + '0';
	buf[3] = '\0';
}



void SplashScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar splash1[] = "  Sailfish FW   ";
	const static PROGMEM prog_uchar splash2[] = " -------------- ";
	const static PROGMEM prog_uchar splash3[] = "Thing 32084 4.4 ";
	//    static PROGMEM prog_uchar splash4[] = " Revision 00000 ";
	const static PROGMEM prog_uchar splash4[] = " Revision " SVN_VERSION_STR;

	if (forceRedraw) {
		lcd.homeCursor();
		lcd.writeFromPgmspace(LOCALIZE(splash1));

		lcd.setRow(1);
		lcd.writeFromPgmspace(LOCALIZE(splash2));

		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(splash3));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(splash4));
#ifdef MENU_L10N_H_
		lcd.setCursor(9,3);
                lcd.writeString((char *)SVN_VERSION_STR);
#endif
	}
	else {
		// The machine has started, so we're done!
                interface::popScreen();
        }
}

void SplashScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	// We can't really do anything, since the machine is still loading, so ignore.
}

void SplashScreen::reset() {
}

UserViewMenu::UserViewMenu() {
	itemCount = 4;
	reset();
}

void UserViewMenu::resetState() {
        uint8_t jogModeSettings = eeprom::getEeprom8(eeprom::JOG_MODE_SETTINGS, EEPROM_DEFAULT_JOG_MODE_SETTINGS);

	if ( jogModeSettings & 0x01 )	itemIndex = 3;
	else				itemIndex = 2;

	firstItemIndex = 2;
}

void UserViewMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar uv_msg[]  = "X/Y Direction:";
	const static PROGMEM prog_uchar uv_model[]= "Model View";
	const static PROGMEM prog_uchar uv_user[] = "User View";

	switch (index) {
	case 0:
	        lcd.writeFromPgmspace(LOCALIZE(uv_msg));
		break;
	case 1:
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(uv_model));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(uv_user));
		break;
	}
}

void UserViewMenu::handleSelect(uint8_t index) {
	uint8_t jogModeSettings = eeprom::getEeprom8(eeprom::JOG_MODE_SETTINGS, EEPROM_DEFAULT_JOG_MODE_SETTINGS);

	switch (index) {
	default:
		return;
	case 2:
		// Model View
		jogModeSettings &= (uint8_t)0xFE;
		break;
	case 3:
		// User View
		jogModeSettings |= (uint8_t)0x01;
		break;
	}
	eeprom_write_byte((uint8_t *)eeprom::JOG_MODE_SETTINGS,
			  jogModeSettings);
	interface::popScreen();
}

void JoggerMenu::jog(ButtonArray::ButtonName direction, bool pauseModeJog) {
	int32_t interval = 1000;

	float speed = 1.5;	//In mm's

	if ( pauseModeJog ) 	jogDistance = DISTANCE_CONT;
	else {
		switch(jogDistance) {
		case DISTANCE_0_1MM:
			speed = 0.1;   //0.1mm
			break;
		case DISTANCE_1MM:
			speed = 1.0;   //1mm
			break;
		case DISTANCE_CONT:
			speed = 1.5;   //1.5mm
			break;
		}
	}

	//Reverse direction of X and Y if we're in User View Mode and
	//not model mode
	int32_t vMode = 1;
	if ( userViewMode ) vMode = -1;

	float stepsPerSecond;
	enum AxisEnum axisIndex = X_AXIS;
	uint16_t eepromLocation = eeprom::HOMING_FEED_RATE_X;

	uint8_t activeToolhead;
	Point position = steppers::getStepperPosition(&activeToolhead);

	switch(direction) {
        	case ButtonArray::XMINUS:
			position[0] -= vMode * stepperAxisMMToSteps(speed,X_AXIS);
			eepromLocation = eeprom::HOMING_FEED_RATE_X; axisIndex = X_AXIS;
			break;
       		case ButtonArray::XPLUS:
			position[0] += vMode * stepperAxisMMToSteps(speed,X_AXIS);
			eepromLocation = eeprom::HOMING_FEED_RATE_X; axisIndex = X_AXIS;
			break;
        	case ButtonArray::YMINUS:
			position[1] -= vMode * stepperAxisMMToSteps(speed,Y_AXIS);
			eepromLocation = eeprom::HOMING_FEED_RATE_Y; axisIndex = Y_AXIS;
			break;
        	case ButtonArray::YPLUS:
			position[1] += vMode * stepperAxisMMToSteps(speed,Y_AXIS);
			eepromLocation = eeprom::HOMING_FEED_RATE_Y; axisIndex = Y_AXIS;
			break;
		case ButtonArray::ZMINUS:
			position[2] -= stepperAxisMMToSteps(speed,Z_AXIS);
			eepromLocation = eeprom::HOMING_FEED_RATE_Z; axisIndex = Z_AXIS;
			break;
		case ButtonArray::ZPLUS:
			position[2] += stepperAxisMMToSteps(speed,Z_AXIS);
			eepromLocation = eeprom::HOMING_FEED_RATE_Z; axisIndex = Z_AXIS;
			break;
		case ButtonArray::CANCEL:
			break;
		case ButtonArray::OK:
		case ButtonArray::ZERO:
			if ( ! pauseModeJog ) break;
			eepromLocation = 0;
			float mms = (float)eeprom::getEeprom8(eeprom::EXTRUDE_MMS, EEPROM_DEFAULT_EXTRUDE_MMS);
			stepsPerSecond = mms * stepperAxisStepsPerMM(A_AXIS);
			interval = (int32_t)(1000000.0 / stepsPerSecond);

			//Handle reverse
			if ( direction == ButtonArray::OK )	stepsPerSecond *= -1;

			if ( ! ACCELERATION_EXTRUDE_WHEN_NEGATIVE_A )	stepsPerSecond *= -1;

			//Extrude for 0.5 seconds
			position[activeToolhead + A_AXIS] += (int32_t)(0.5 * stepsPerSecond);
			break;
	}

	if ( jogDistance == DISTANCE_CONT )	lastDirectionButtonPressed = direction;
	else					lastDirectionButtonPressed = (ButtonArray::ButtonName)0;

	if ( eepromLocation != 0 ) {
		//60.0, because feed rate is in mm/min units, we convert to seconds
		float feedRate = (float)eeprom::getEepromUInt32(eepromLocation, 500) / 60.0;
		stepsPerSecond = feedRate * (float)stepperAxisStepsPerMM(axisIndex);
		interval = (int32_t)(1000000.0 / stepsPerSecond);
	}

	steppers::setTarget(position, interval);
}

bool MessageScreen::screenWaiting(void){
	return (timeout.isActive() || incomplete);
}

void MessageScreen::addMessage(CircularBuffer& buf) {
	char c = buf.pop();
	while (c != '\0' && cursor < BUF_SIZE && buf.getLength() > 0) {
		message[cursor++] = c;
		c = buf.pop();
	}
	// ensure that message is always null-terminated
	if (cursor >= BUF_SIZE) {
		message[BUF_SIZE-1] = '\0';
	} else {
		message[cursor] = '\0';
	}
}


void MessageScreen::addMessage(const prog_uchar msg[]) {

	if ( cursor >= BUF_SIZE )
		return;

	cursor += strlcpy_P(message + cursor, (const prog_char *)msg,
			    BUF_SIZE - cursor);

	// ensure that message is always null-terminated
	if (cursor >= BUF_SIZE) {
		message[BUF_SIZE-1] = '\0';
	} else {
		message[cursor] = '\0';
	}
}

void MessageScreen::clearMessage() {
	x = y = 0;
	message[0] = '\0';
	cursor = 0;
	needsRedraw = false;
	timeout = Timeout();
	incomplete = false;
}

void MessageScreen::setTimeout(uint8_t seconds) {
	timeout.start((micros_t)seconds * (micros_t)1000 * (micros_t)1000);
}
void MessageScreen::refreshScreen(){
	needsRedraw = true;
}

void MessageScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	char* b = message;
	int ycursor = y, xcursor = x;
	if (timeout.hasElapsed()) {
		InterfaceBoard& ib = Motherboard::getBoard().getInterfaceBoard();
		ib.hideMessageScreen();
		return;
	}
	if (forceRedraw || needsRedraw) {
		needsRedraw = false;
		lcd.clear();

		lcd.setCursor(xcursor, ycursor);
		while (*b != '\0') {
			if (( *b == '\n' ) || ( xcursor >= lcd.getDisplayWidth() )) {
				xcursor = 0;
				ycursor++;
				lcd.setCursor(xcursor, ycursor);
			}

			if ( *b != '\n' ) {
				lcd.write(*b);
				xcursor ++;
			}

			b ++;
		}
	}
}

void MessageScreen::reset() {
	timeout = Timeout();
	buttonsDisabled = false;
}

void MessageScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	if ( buttonsDisabled )	return;
}

void JogMode::reset() {
	uint8_t jogModeSettings = eeprom::getEeprom8(eeprom::JOG_MODE_SETTINGS, EEPROM_DEFAULT_JOG_MODE_SETTINGS);

	jogDistance = (enum distance_t)((jogModeSettings >> 1 ) & 0x07);
	if ( jogDistance > DISTANCE_CONT ) jogDistance = DISTANCE_0_1MM;

	distanceChanged = false;
	lastDirectionButtonPressed = (ButtonArray::ButtonName)0;

	userViewMode = jogModeSettings & 0x01;
	userViewModeChanged = false;

	steppers::setSegmentAccelState(true);
}

void JogMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar j_jog1[]      = "Jog mode: ";
	const static PROGMEM prog_uchar j_jog2[]      = "   Y+         Z+";
	const static PROGMEM prog_uchar j_jog3[]      = "X- V  X+  (mode)";
	const static PROGMEM prog_uchar j_jog4[]      = "   Y-         Z-";
	const static PROGMEM prog_uchar j_jog2_user[] = "  Y           Z+";
	const static PROGMEM prog_uchar j_jog3_user[] = "X V X     (mode)";
	const static PROGMEM prog_uchar j_jog4_user[] = "  Y           Z-";

	const static PROGMEM prog_uchar j_distance0_1mm[] = ".1mm";
	const static PROGMEM prog_uchar j_distance1mm[] = "1mm";
	const static PROGMEM prog_uchar j_distanceCont[] = "Cont..";

	if ( userViewModeChanged ) userViewMode = eeprom::getEeprom8(eeprom::JOG_MODE_SETTINGS, EEPROM_DEFAULT_JOG_MODE_SETTINGS) & 0x01;

	if (forceRedraw || distanceChanged || userViewModeChanged) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(j_jog1));

		switch (jogDistance) {
		case DISTANCE_0_1MM:
			lcd.write(0xF3);	//Write tilde
			lcd.writeFromPgmspace(LOCALIZE(j_distance0_1mm));
			break;
		case DISTANCE_1MM:
			lcd.write(0xF3);	//Write tilde
			lcd.writeFromPgmspace(LOCALIZE(j_distance1mm));
			break;
		case DISTANCE_CONT:
			lcd.writeFromPgmspace(LOCALIZE(j_distanceCont));
			break;
		}

		lcd.setRow(1);
		lcd.writeFromPgmspace(userViewMode ? LOCALIZE(j_jog2_user) : LOCALIZE(j_jog2));

		lcd.setRow(2);
		lcd.writeFromPgmspace(userViewMode ? LOCALIZE(j_jog3_user) : LOCALIZE(j_jog3));

		lcd.setRow(3);
		lcd.writeFromPgmspace(userViewMode ? LOCALIZE(j_jog4_user) : LOCALIZE(j_jog4));

		distanceChanged = false;
		userViewModeChanged = false;
	}

	if ( jogDistance == DISTANCE_CONT ) {
		if ( lastDirectionButtonPressed ) {
			if (!interface::isButtonPressed(lastDirectionButtonPressed)) {
				lastDirectionButtonPressed = (ButtonArray::ButtonName)0;
				steppers::abort();
			}
		}
	}
}

void JogMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
        case ButtonArray::ZERO:
		userViewModeChanged = true;
		interface::pushScreen(&userViewMenu);
		break;
        case ButtonArray::OK:
		switch(jogDistance)
		{
			case DISTANCE_0_1MM:
				jogDistance = DISTANCE_1MM;
				break;
			case DISTANCE_1MM:
				jogDistance = DISTANCE_CONT;
				break;
			case DISTANCE_CONT:
				jogDistance = DISTANCE_0_1MM;
				break;
		}
		distanceChanged = true;
		eeprom_write_byte((uint8_t *)eeprom::JOG_MODE_SETTINGS, userViewMode | (jogDistance << 1));
		break;
        default:
		if (( lastDirectionButtonPressed ) && (lastDirectionButtonPressed != button ))
			steppers::abort();
		jog(button, false);
		break;
        case ButtonArray::CANCEL:
		steppers::abort();
		steppers::enableAxis(0, false);
		steppers::enableAxis(1, false);
		steppers::enableAxis(2, false);
                interface::popScreen();
		steppers::setSegmentAccelState(true);
		break;
	}
}

void ExtruderMode::reset() {
	extrudeSeconds = (enum extrudeSeconds)eeprom::getEeprom8(eeprom::EXTRUDE_DURATION, EEPROM_DEFAULT_EXTRUDE_DURATION);
	updatePhase = 0;
	timeChanged = false;
	lastDirection = 1;
	overrideExtrudeSeconds = 0;
	steppers::setSegmentAccelState(false);
}

void ExtruderMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar e_extrude1[] = "Extrude: ";
	const static PROGMEM prog_uchar e_extrude2[] = "(set mm/s)   Fwd";
	const static PROGMEM prog_uchar e_extrude3[] = " (stop)    (dur)";
	const static PROGMEM prog_uchar e_extrude4[] = "---/---C     Rev";
	const static PROGMEM prog_uchar e_secs[]     = "SECS";
	const static PROGMEM prog_uchar e_blank[]    = "       ";

	if (overrideExtrudeSeconds)	extrude((int32_t)overrideExtrudeSeconds, true);

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(e_extrude1));

		lcd.setRow(1);
		lcd.writeFromPgmspace(LOCALIZE(e_extrude2));

		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(e_extrude3));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(e_extrude4));
	}

	if ((forceRedraw) || (timeChanged)) {
		lcd.setCursor(9,0);
		lcd.writeFromPgmspace(LOCALIZE(e_blank));
		lcd.setCursor(9,0);
		lcd.writeFloat((float)extrudeSeconds, 0);
		lcd.writeFromPgmspace(LOCALIZE(e_secs));
		timeChanged = false;
	}

	OutPacket responsePacket;
	Point position;
	uint8_t activeToolhead;

	// Redraw tool info
	switch (updatePhase) {
	case 0:
	        steppers::getStepperPosition(&activeToolhead);
		lcd.setRow(3);
		if (extruderControl(activeToolhead, SLAVE_CMD_GET_TEMP, EXTDR_CMD_GET, responsePacket, 0)) {
			uint16_t data = responsePacket.read16(1);
			lcd.writeInt(data, 3);
		} else {
			lcd.writeFromPgmspace(unknown_temp);
		}
		break;

	case 1:
		lcd.setCursor(4,3);
		if (extruderControl(activeToolhead, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0)) {
			uint16_t data = responsePacket.read16(1);
			lcd.writeInt(data, 3);
		} else {
			lcd.writeFromPgmspace(unknown_temp);
		}
		break;
	}

	updatePhase++;
	if (updatePhase > 1) {
		updatePhase = 0;
	}
}

void ExtruderMode::extrude(int32_t seconds, bool overrideTempCheck) {
	uint8_t activeToolhead;
	Point position = steppers::getStepperPosition(&activeToolhead);

	//Check we're hot enough
	if ( ! overrideTempCheck )
	{
		OutPacket responsePacket;
		if (extruderControl(activeToolhead, SLAVE_CMD_IS_TOOL_READY, EXTDR_CMD_GET, responsePacket, 0)) {
			uint8_t data = responsePacket.read8(1);
			if ( ! data )
			{
				overrideExtrudeSeconds = seconds;
				interface::pushScreen(&extruderTooColdMenu);
				return;
			}
		}
	}

	float mms = (float)eeprom::getEeprom8(eeprom::EXTRUDE_MMS, EEPROM_DEFAULT_EXTRUDE_MMS);
	float stepsPerSecond = mms * stepperAxisStepsPerMM(A_AXIS);
	int32_t interval = (int32_t)(1000000.0 / stepsPerSecond);

	//Handle 5D
	float direction = 1.0;
	if ( ACCELERATION_EXTRUDE_WHEN_NEGATIVE_A )	direction = -1.0;

	if ( seconds == 0 )	steppers::abort();
	else {
		position[A_AXIS + activeToolhead] += direction * seconds * stepsPerSecond;
		steppers::setTarget(position, interval);
	}

	if (overrideTempCheck)	overrideExtrudeSeconds = 0;
}

void ExtruderMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	static const PROGMEM prog_uchar e_message1[] = "Extruder speed:";
	static const PROGMEM prog_uchar e_units[]    = " mm/s ";
	switch (button) {
        	case ButtonArray::OK:
			switch(extrudeSeconds) {
                		case EXTRUDE_SECS_1S:
					extrudeSeconds = EXTRUDE_SECS_2S;
					break;
                		case EXTRUDE_SECS_2S:
					extrudeSeconds = EXTRUDE_SECS_5S;
					break;
                		case EXTRUDE_SECS_5S:
					extrudeSeconds = EXTRUDE_SECS_10S;
					break;
				case EXTRUDE_SECS_10S:
					extrudeSeconds = EXTRUDE_SECS_30S;
					break;
				case EXTRUDE_SECS_30S:
					extrudeSeconds = EXTRUDE_SECS_60S;
					break;
				case EXTRUDE_SECS_60S:
					extrudeSeconds = EXTRUDE_SECS_90S;
					break;
				case EXTRUDE_SECS_90S:
					extrudeSeconds = EXTRUDE_SECS_120S;
					break;
                		case EXTRUDE_SECS_120S:
					extrudeSeconds = EXTRUDE_SECS_240S;
					break;
                		case EXTRUDE_SECS_240S:
					extrudeSeconds = EXTRUDE_SECS_1S;
					break;
				default:
					extrudeSeconds = EXTRUDE_SECS_1S;
					break;
			}

			eeprom_write_byte((uint8_t *)eeprom::EXTRUDE_DURATION, (uint8_t)extrudeSeconds);

			//If we're already extruding, change the time running
			if (steppers::isRunning())
				extrude((int32_t)(lastDirection * extrudeSeconds), false);

			timeChanged = true;
			break;
        	case ButtonArray::YPLUS:
			// Show Extruder MMS Setting Screen
			extruderSetMMSScreen.location = eeprom::EXTRUDE_MMS;
			extruderSetMMSScreen.default_value = EEPROM_DEFAULT_EXTRUDE_MMS;
			extruderSetMMSScreen.message1 = LOCALIZE(e_message1);
			extruderSetMMSScreen.units = LOCALIZE(e_units);
                        interface::pushScreen(&extruderSetMMSScreen);
			break;
        	case ButtonArray::ZERO:
        	case ButtonArray::YMINUS:
        	case ButtonArray::XMINUS:
        	case ButtonArray::XPLUS:
			extrude((int32_t)EXTRUDE_SECS_CANCEL, true);
        		break;
        	case ButtonArray::ZMINUS:
        	case ButtonArray::ZPLUS:
			if ( button == ButtonArray::ZPLUS )	lastDirection = 1;
			else					lastDirection = -1;
			
			extrude((int32_t)(lastDirection * extrudeSeconds), false);
			break;
       	 	case ButtonArray::CANCEL:
			steppers::abort();
			steppers::enableAxis(3, false);
               		interface::popScreen();
			steppers::setSegmentAccelState(true);
			steppers::enableAxis(3, false);
			break;
	}
}



ExtruderTooColdMenu::ExtruderTooColdMenu() {
	itemCount = 4;
	reset();
}

void ExtruderTooColdMenu::resetState() {
	itemIndex = 2;
	firstItemIndex = 2;
}

void ExtruderTooColdMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar etc_warning[]  = "Tool0 too cold!";
	const static PROGMEM prog_uchar etc_cancel[]   = "Cancel";
	const static PROGMEM prog_uchar etc_override[] = "Override";
	const prog_uchar *msg;

	switch (index) {
	case 0:
		msg = LOCALIZE(etc_warning);
		break;
	default:
		return;
	case 2:
		msg = LOCALIZE(etc_cancel);
		break;
	case 3:
		msg = LOCALIZE(etc_override);
		break;
	}
	lcd.writeFromPgmspace(msg);
}

void ExtruderTooColdMenu::handleCancel() {
	overrideExtrudeSeconds = 0;
	interface::popScreen();
}

void ExtruderTooColdMenu::handleSelect(uint8_t index) {
	switch (index) {
	default :
		return;
	case 2:
		// Cancel extrude
		overrideExtrudeSeconds = 0;
		break;
	case 3:
		// Override and extrude
		break;
	}
	interface::popScreen();
}

void MoodLightMode::reset() {
	updatePhase = 0;
	scriptId = eeprom_read_byte((uint8_t *)eeprom::MOOD_LIGHT_SCRIPT);
}

void MoodLightMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar ml_mood1[]   = "Mood: ";
	const static PROGMEM prog_uchar ml_mood3_1[] = "(set RGB)";
	const static PROGMEM prog_uchar ml_blank[]   = "          ";
	const static PROGMEM prog_uchar ml_moodNotPresent1[] = "Mood Light not";
	const static PROGMEM prog_uchar ml_moodNotPresent2[] = "present!!";
	const static PROGMEM prog_uchar ml_moodNotPresent3[] = "See Thingiverse";
	const static PROGMEM prog_uchar ml_moodNotPresent4[] = "   thing:15347";

	//If we have no mood light, point to thingiverse to make one
	if ( ! interface::moodLightController().blinkM.blinkMIsPresent ) {
		//Try once more to restart the mood light controller
		if ( ! interface::moodLightController().start() ) {
			if ( forceRedraw ) {
				lcd.clearHomeCursor();
				lcd.writeFromPgmspace(LOCALIZE(ml_moodNotPresent1));
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(ml_moodNotPresent2));
				lcd.setRow(2);
				lcd.writeFromPgmspace(LOCALIZE(ml_moodNotPresent3));
				lcd.setRow(3);
				lcd.writeFromPgmspace(LOCALIZE(ml_moodNotPresent4));
			}
		
			return;
		}
	}

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(ml_mood1));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

 	//Redraw tool info

	switch (updatePhase) {
	case 0:
		lcd.setCursor(6, 0);
		lcd.writeFromPgmspace(LOCALIZE(ml_blank));	
		lcd.setCursor(6, 0);
		lcd.writeFromPgmspace(interface::moodLightController().scriptIdToStr(scriptId));
		break;

	case 1:
		lcd.setRow(2);
		lcd.writeFromPgmspace((scriptId == 1) ? LOCALIZE(ml_mood3_1) : LOCALIZE(ml_blank));
		break;
	}

	if (++updatePhase > 1)
		updatePhase = 0;
}



void MoodLightMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	if ( ! interface::moodLightController().blinkM.blinkMIsPresent )
		interface::popScreen();

	uint8_t i;

	switch (button) {
	case ButtonArray::OK:
		eeprom_write_byte((uint8_t *)eeprom::MOOD_LIGHT_SCRIPT,
				  scriptId);
		interface::popScreen();
		break;

	case ButtonArray::ZERO:
		if ( scriptId == 1 )
			//Set RGB Values
			interface::pushScreen(&moodLightSetRGBScreen);
		break;

	case ButtonArray::ZPLUS:
		// increment more
		for ( i = 0; i < 5; i ++ )
			scriptId = interface::moodLightController().nextScriptId(scriptId);
		interface::moodLightController().playScript(scriptId);
		break;

	case ButtonArray::ZMINUS:
		// decrement more
		for ( i = 0; i < 5; i ++ )
			scriptId = interface::moodLightController().prevScriptId(scriptId);
		interface::moodLightController().playScript(scriptId);
		break;

	case ButtonArray::YPLUS:
		// increment less
		scriptId = interface::moodLightController().nextScriptId(scriptId);
		interface::moodLightController().playScript(scriptId);
		break;

	case ButtonArray::YMINUS:
		// decrement less
		scriptId = interface::moodLightController().prevScriptId(scriptId);
		interface::moodLightController().playScript(scriptId);
		break;

	default:
		break;

	case ButtonArray::CANCEL:
		scriptId = eeprom_read_byte((uint8_t *)eeprom::MOOD_LIGHT_SCRIPT);
		interface::moodLightController().playScript(scriptId);
		interface::popScreen();
		break;
	}
}


void MoodLightSetRGBScreen::reset() {
	inputMode = 0;	//Red
	redrawScreen = false;

	red   = eeprom::getEeprom8(eeprom::MOOD_LIGHT_CUSTOM_RED,   EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_RED);;
	green = eeprom::getEeprom8(eeprom::MOOD_LIGHT_CUSTOM_GREEN, EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_GREEN);;
	blue  = eeprom::getEeprom8(eeprom::MOOD_LIGHT_CUSTOM_BLUE,  EEPROM_DEFAULT_MOOD_LIGHT_CUSTOM_BLUE);;
}

void MoodLightSetRGBScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar mlsrgb_message1_red[]   = "Red:";
	const static PROGMEM prog_uchar mlsrgb_message1_green[] = "Green:";
	const static PROGMEM prog_uchar mlsrgb_message1_blue[]  = "Blue:";

	if ((forceRedraw) || (redrawScreen)) {
		lcd.clearHomeCursor();
		if      ( inputMode == 0 ) lcd.writeFromPgmspace(LOCALIZE(mlsrgb_message1_red));
		else if ( inputMode == 1 ) lcd.writeFromPgmspace(LOCALIZE(mlsrgb_message1_green));
		else if ( inputMode == 2 ) lcd.writeFromPgmspace(LOCALIZE(mlsrgb_message1_blue));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));

		redrawScreen = false;
	}


	// Redraw tool info
	lcd.setRow(1);
	if      ( inputMode == 0 ) lcd.writeInt(red,  3);
	else if ( inputMode == 1 ) lcd.writeInt(green,3);
	else if ( inputMode == 2 ) lcd.writeInt(blue, 3);
}

void MoodLightSetRGBScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	uint8_t *value = &red;

	if 	( inputMode == 1 )	value = &green;
	else if ( inputMode == 2 )	value = &blue;

	switch (button) {
        case ButtonArray::CANCEL:
		interface::popScreen();
		break;
        case ButtonArray::ZERO:
		break;
        case ButtonArray::OK:
		if ( inputMode < 2 ) {
			inputMode ++;
			redrawScreen = true;
		} else {
			eeprom_write_byte((uint8_t*)eeprom::MOOD_LIGHT_CUSTOM_RED,  red);
			eeprom_write_byte((uint8_t*)eeprom::MOOD_LIGHT_CUSTOM_GREEN,green);
			eeprom_write_byte((uint8_t*)eeprom::MOOD_LIGHT_CUSTOM_BLUE, blue);

			//Set the color
			interface::moodLightController().playScript(1);

			interface::popScreen();
		}
		break;
        case ButtonArray::ZPLUS:
		// increment more
		if (*value <= 245) *value += 10;
		break;
        case ButtonArray::ZMINUS:
		// decrement more
		if (*value >= 10) *value -= 10;
		break;
        case ButtonArray::YPLUS:
		// increment less
		if (*value <= 254) *value += 1;
		break;
        case ButtonArray::YMINUS:
		// decrement less
		if (*value >= 1) *value -= 1;
		break;

        case ButtonArray::XMINUS:
        case ButtonArray::XPLUS:
		break;
	}
}

void MonitorMode::reset() {
	updatePhase =  UPDATE_PHASE_FIRST;
	buildTimePhase = BUILD_TIME_PHASE_FIRST;
	lastBuildTimePhase = BUILD_TIME_PHASE_FIRST;
	lastElapsedSeconds = 0.0;
	pausePushLockout = false;
	pauseMode.autoPause = 0;
	buildCompleteBuzzPlayed = false;
	overrideForceRedraw = false;
	copiesPrinted = 0;
	flashingTool = false;
	flashingPlatform = false;
}


void MonitorMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar mon_extruder_temp[]      =   "Tool   ---/---\001";
	const static PROGMEM prog_uchar mon_platform_temp[]      =   "Bed    ---/---\001";
	const static PROGMEM prog_uchar mon_elapsed_time[]       =   "Elapsed:   0h00m";
	const static PROGMEM prog_uchar mon_completed_percent[]  =   "Completed:   0% ";
	const static PROGMEM prog_uchar mon_time_left[]          =   "TimeLeft:  0h00m";
	const static PROGMEM prog_uchar mon_time_left_secs[]     =   "secs";
	const static PROGMEM prog_uchar mon_time_left_none[]     =   "   none";
	const static PROGMEM prog_uchar mon_zpos[] 		 =   "ZPos:           ";
	const static PROGMEM prog_uchar mon_speed[] 		 =   "Acc:            ";
	const static PROGMEM prog_uchar mon_filament[]           =   "Filament:0.00m  ";
	const static PROGMEM prog_uchar mon_copies[]		 =   "Copy:           ";
	const static PROGMEM prog_uchar mon_of[]		 =   " of ";
	const static PROGMEM prog_uchar mon_error[]		 =   "error!";
	char buf[17];

	if ( command::isPaused() ) {
		if ( ! pausePushLockout ) {
			pausePushLockout = true;
			pauseMode.autoPause = 1;
			interface::pushScreen(&pauseMode);
			return;
		}
	} else pausePushLockout = false;

	//Check for a build complete, and if we have more than one copy
	//to print, setup another one
	if ( host::isBuildComplete() ) {
		uint8_t copiesToPrint = eeprom::getEeprom8(eeprom::ABP_COPIES, EEPROM_DEFAULT_ABP_COPIES);
		if ( copiesToPrint > 1 ) {
			if ( copiesPrinted < (copiesToPrint - 1)) {
				copiesPrinted ++;
				overrideForceRedraw = true;
				command::buildAnotherCopy();
			}
		}
	}

	if ((forceRedraw) || (overrideForceRedraw)) {
		lcd.clearHomeCursor();
		switch(host::getHostState()) {
		case host::HOST_STATE_READY:
			lcd.writeString(host::getMachineName());
			break;
		case host::HOST_STATE_BUILDING:
		case host::HOST_STATE_BUILDING_FROM_SD:
			lcd.writeString(host::getBuildName());
			lcd.setRow(1);
			lcd.writeFromPgmspace(LOCALIZE(mon_completed_percent));
			break;
		case host::HOST_STATE_ERROR:
			lcd.writeFromPgmspace(LOCALIZE(mon_error));
			break;
		case host::HOST_STATE_CANCEL_BUILD :
			break;
		}

		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(mon_extruder_temp));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(mon_platform_temp));

		lcd.setCursor(15,3);
		lcd.write((command::getPauseAtZPos() == 0) ? ' ' : '*');
	}

	overrideForceRedraw = false;

	//Flash the temperature indicators
	toggleHeating ^= true;

	if ( flashingTool ) {
		lcd.setCursor(5,2);
		lcd.write(toggleHeating ? LCD_CUSTOM_CHAR_EXTRUDER_NORMAL : LCD_CUSTOM_CHAR_EXTRUDER_HEATING);
	}
	if ( flashingPlatform ) {
		lcd.setCursor(5,3);
		lcd.write(toggleHeating ? LCD_CUSTOM_CHAR_PLATFORM_NORMAL : LCD_CUSTOM_CHAR_PLATFORM_HEATING);
	}

	OutPacket responsePacket;
	uint8_t activeToolhead;
	steppers::getStepperPosition(&activeToolhead);

	// Redraw tool info
	switch (updatePhase) {
	case UPDATE_PHASE_TOOL_TEMP:
		lcd.setCursor(7,2);
		if (extruderControl(activeToolhead, SLAVE_CMD_GET_TEMP, EXTDR_CMD_GET, responsePacket, 0)) {
			uint16_t data = responsePacket.read16(1);
			lcd.writeInt(data, 3);
		} else {
			lcd.writeFromPgmspace(unknown_temp);
		}
		break;

	case UPDATE_PHASE_TOOL_TEMP_SET_POINT:
		lcd.setCursor(11,2);
		uint16_t data;
		data = 0;
		if (extruderControl(activeToolhead, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0)) {
			data = responsePacket.read16(1);
			lcd.writeInt(data, 3);
		} else {
			lcd.writeFromPgmspace(unknown_temp);
		}

		lcd.setCursor(5,2);
		if (extruderControl(activeToolhead, SLAVE_CMD_IS_TOOL_READY, EXTDR_CMD_GET, responsePacket, 0)) {
			flashingTool = false;
			uint8_t ready = responsePacket.read8(1);
			if ( data != 0 ) {
				if ( ready ) lcd.write(LCD_CUSTOM_CHAR_EXTRUDER_HEATING);
				else	     flashingTool = true;
			}
			else	lcd.write(LCD_CUSTOM_CHAR_EXTRUDER_NORMAL);
		}
		break;

	case UPDATE_PHASE_PLATFORM_TEMP:
		lcd.setCursor(7,3);
		if (extruderControl(0, SLAVE_CMD_GET_PLATFORM_TEMP, EXTDR_CMD_GET, responsePacket, 0)) {
			uint16_t data = responsePacket.read16(1);
			lcd.writeInt(data, 3);
		} else {
			lcd.writeFromPgmspace(unknown_temp);
		}
		break;

	case UPDATE_PHASE_PLATFORM_SET_POINT:
		lcd.setCursor(11,3);
		data = 0;
		if (extruderControl(0, SLAVE_CMD_GET_PLATFORM_SP, EXTDR_CMD_GET, responsePacket, 0)) {
			data = responsePacket.read16(1);
			lcd.writeInt(data, 3);
		} else {
			lcd.writeFromPgmspace(unknown_temp);
		}

		lcd.setCursor(5,3);
		if (extruderControl(0, SLAVE_CMD_IS_PLATFORM_READY, EXTDR_CMD_GET, responsePacket, 0)) {
			flashingPlatform = false;
			uint8_t ready = responsePacket.read8(1);
			if ( data != 0 ) {
				if ( ready ) lcd.write(LCD_CUSTOM_CHAR_PLATFORM_HEATING);
				else	     flashingPlatform = true;
			}
			else	lcd.write(LCD_CUSTOM_CHAR_PLATFORM_NORMAL);
		}

		lcd.setCursor(15,3);
		lcd.write((command::getPauseAtZPos() == 0) ? ' ' : '*');
		break;
	case UPDATE_PHASE_LAST:
		break;
	case UPDATE_PHASE_BUILD_PHASE_SCROLLER:
		enum host::HostState hostState = host::getHostState();
		
		if ( (hostState != host::HOST_STATE_BUILDING ) && ( hostState != host::HOST_STATE_BUILDING_FROM_SD )) break;

		//Signal buzzer if we're complete
		if (( ! buildCompleteBuzzPlayed ) && ( sdcard::getPercentPlayed() >= 100.0 )) {
			buildCompleteBuzzPlayed = true;
       			Motherboard::getBoard().buzz(2, 3, eeprom::getEeprom8(eeprom::BUZZER_REPEATS, EEPROM_DEFAULT_BUZZER_REPEATS));
		}

		bool okButtonHeld = interface::isButtonPressed(ButtonArray::OK);

		//Holding the ok button stops rotation
        	if ( okButtonHeld )	buildTimePhase = lastBuildTimePhase;

		float secs;
		int32_t tsecs;
		Point position;
		uint8_t precision;
		float completedPercent;
		float filamentUsed, lastFilamentUsed;

		switch (buildTimePhase) {
			case BUILD_TIME_PHASE_COMPLETED_PERCENT:
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(mon_completed_percent));
				lcd.setCursor(11,1);
				completedPercent = (float)command::getBuildPercentage();
				digits3(buf, (uint8_t)completedPercent);
				lcd.writeString(buf);
				lcd.write('%');
				break;
			case BUILD_TIME_PHASE_ELAPSED_TIME:
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(mon_elapsed_time));
				lcd.setCursor(9,1);
				if ( host::isBuildComplete() ) secs = lastElapsedSeconds; //We stop counting elapsed seconds when we are done
				else {
					lastElapsedSeconds = Motherboard::getBoard().getCurrentSeconds();
					secs = lastElapsedSeconds;
				}
				formatTime(buf, (uint32_t)secs);
				lcd.writeString(buf);
				break;
			case BUILD_TIME_PHASE_TIME_LEFT:
				tsecs = command::estimatedTimeLeftInSeconds();
				if ( tsecs > 0 ) {
					lcd.setRow(1);
					lcd.writeFromPgmspace(LOCALIZE(mon_time_left));
					lcd.setCursor(9,1);
					if ((tsecs > 0 ) && (tsecs < 60) && ( host::isBuildComplete() ) ) {
						digits3(buf, (uint8_t)tsecs);
						lcd.writeString(buf);
						lcd.writeFromPgmspace(LOCALIZE(mon_time_left_secs));	
					} else if (( tsecs <= 0) || ( host::isBuildComplete()) ) {
#ifdef HAS_FILAMENT_COUNTER
						command::addFilamentUsed();
#endif
						lcd.writeFromPgmspace(LOCALIZE(mon_time_left_none));
					} else {
						formatTime(buf, (uint32_t)tsecs);
						lcd.writeString(buf);
					}
					break;
				}
				//We can't display the time left, so we drop into ZPosition instead
				else	buildTimePhase = (enum BuildTimePhase)((uint8_t)buildTimePhase + 1);

			case BUILD_TIME_PHASE_ZPOS:
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(mon_zpos));
				lcd.setCursor(6,1);
				position = steppers::getStepperPosition();
			
				//Divide by the axis steps to mm's
				lcd.writeFloat(stepperAxisStepsToMM(position[2], Z_AXIS), 3);

				lcd.writeFromPgmspace(LOCALIZE(units_mm));
				break;
			case BUILD_TIME_PHASE_FILAMENT:
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(mon_filament));
				lcd.setCursor(9,1);
				lastFilamentUsed = stepperAxisStepsToMM(command::getLastFilamentLength(0) + command::getLastFilamentLength(1), A_AXIS);
				if ( lastFilamentUsed != 0.0 )	filamentUsed = lastFilamentUsed;
				else				filamentUsed = stepperAxisStepsToMM((command::getFilamentLength(0) + command::getFilamentLength(1)), A_AXIS);
				filamentUsed /= 1000.0;	//convert to meters
				if	( filamentUsed < 0.1 )	{
					 filamentUsed *= 1000.0;	//Back to mm's
					precision = 1;
				}
				else if ( filamentUsed < 10.0 )	 precision = 4;
				else if ( filamentUsed < 100.0 ) precision = 3;
				else				 precision = 2;
				lcd.writeFloat(filamentUsed, precision);
				if ( precision == 1 ) lcd.write('m');
				lcd.write('m');
				break;
			case BUILD_TIME_PHASE_COPIES_PRINTED:
				{
				uint8_t totalCopies = eeprom::getEeprom8(eeprom::ABP_COPIES, EEPROM_DEFAULT_ABP_COPIES);
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(mon_copies));
				lcd.setCursor(7,1);
				lcd.writeFloat((float)(copiesPrinted + 1), 0);
				lcd.writeFromPgmspace(LOCALIZE(mon_of));
				lcd.writeFloat((float)totalCopies, 0);
				}
				break;
			case BUILD_TIME_PHASE_LAST:
				break;
			case BUILD_TIME_PHASE_ACCEL_STATS:
				float minSpeed, avgSpeed, maxSpeed;
				accelStatsGet(&minSpeed, &avgSpeed, &maxSpeed);
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(mon_speed));
				lcd.setCursor(4,1);
				if ( minSpeed < 100.0 )	lcd.write(' ');	//If we have space, pad out a bit
				lcd.writeFloat(minSpeed,0);
				lcd.write('/');
				lcd.writeFloat(avgSpeed,0);
				lcd.write('/');
				lcd.writeFloat(maxSpeed,0);
				lcd.write(' ');
				break;
		}

        	if ( ! okButtonHeld ) {
			//Advance buildTimePhase and wrap around
			lastBuildTimePhase = buildTimePhase;
			buildTimePhase = (enum BuildTimePhase)((uint8_t)buildTimePhase + 1);

			//If we're setup to print more than one copy, then show that build phase,
			//otherwise skip it
			if ( buildTimePhase == BUILD_TIME_PHASE_COPIES_PRINTED ) {
				uint8_t totalCopies = eeprom::getEeprom8(eeprom::ABP_COPIES, EEPROM_DEFAULT_ABP_COPIES);
				if ( totalCopies <= 1 )	
					buildTimePhase = (enum BuildTimePhase)((uint8_t)buildTimePhase + 1);
			}

			if ( buildTimePhase >= BUILD_TIME_PHASE_LAST )
				buildTimePhase = BUILD_TIME_PHASE_FIRST;
		}
		break;
	}

	//Advance updatePhase and wrap around
	updatePhase = (enum UpdatePhase)((uint8_t)updatePhase + 1);
	if (updatePhase >= UPDATE_PHASE_LAST)
		updatePhase = UPDATE_PHASE_FIRST;

#ifdef DEBUG_ONSCREEN
        lcd.homeCursor();
        lcd.writeString((char *)"DOS1: ");
        lcd.writeFloat(debug_onscreen1, 3);
        lcd.writeString((char *)" ");

        lcd.setRow(1);
        lcd.writeString((char *)"DOS2: ");
        lcd.writeFloat(debug_onscreen2, 3);
        lcd.writeString((char *)" ");
#endif
}

void MonitorMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
        case ButtonArray::CANCEL:
		switch(host::getHostState()) {
		case host::HOST_STATE_BUILDING:
		case host::HOST_STATE_BUILDING_FROM_SD:
                        interface::pushScreen(&cancelBuildMenu);
			break;
		default:
                        interface::popScreen();
			break;
		}
        case ButtonArray::ZPLUS:
		if ( host::getHostState() == host::HOST_STATE_BUILDING_FROM_SD )
			updatePhase = UPDATE_PHASE_BUILD_PHASE_SCROLLER;
		break;
	default:
		break;
	}
}



void VersionMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar v_version1[] = "Motherboard  _._";
	const static PROGMEM prog_uchar v_version2[] = "   Extruder  _._";
	const static PROGMEM prog_uchar v_version3[] = " Revision " SVN_VERSION_STR;
	const static PROGMEM prog_uchar v_version4[] = "Free SRAM ";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(v_version1));

		lcd.setRow(1);
		lcd.writeFromPgmspace(LOCALIZE(v_version2));

		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(v_version3));

		//Display the motherboard version
		lcd.setCursor(13, 0);
		lcd.writeInt(firmware_version / 100, 1);

		lcd.setCursor(15, 0);
		lcd.writeInt(firmware_version % 100, 1);

		//Display the extruder version
		OutPacket responsePacket;

		if (extruderControl(0, SLAVE_CMD_VERSION, EXTDR_CMD_GET, responsePacket, 0)) {
			uint16_t extruderVersion = responsePacket.read16(1);

			lcd.setCursor(13, 1);
			lcd.writeInt(extruderVersion / 100, 1);

			lcd.setCursor(15, 1);
			lcd.writeInt(extruderVersion % 100, 1);
		} else {
			lcd.setCursor(13, 1);
			lcd.writeString((char *)"X.X");
		}

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(v_version4));
		lcd.writeFloat((float)StackCount(),0);
	} else {
	}
}

void VersionMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	interface::popScreen();
}

void Menu::update(LiquidCrystal& lcd, bool forceRedraw) {
	uint8_t height = lcd.getDisplayHeight();

	// Do we need to redraw the whole menu?
	if ((itemIndex/height) != (lastDrawIndex/height)
			|| forceRedraw ) {
		// Redraw the whole menu
		lcd.clear();

		for (uint8_t i = 0; i < height; i++) {
			// Instead of using lcd.clear(), clear one line at a time so there
			// is less screen flickr.

			if (i+(itemIndex/height)*height +1 > itemCount) {
				break;
			}

			lcd.setCursor(1,i);
			// Draw one page of items at a time
			drawItem(i+(itemIndex/height)*height, lcd);
		}
	}
	else {
		// Only need to clear the previous cursor
		lcd.setRow(lastDrawIndex%height);
		lcd.write(' ');
	}

	lcd.setRow(itemIndex%height);
	lcd.write(LCD_CUSTOM_CHAR_ARROW);
	lastDrawIndex = itemIndex;
}

void Menu::reset() {
	firstItemIndex = 0;
	itemIndex = 0;
	lastDrawIndex = 255;

	resetState();
}

void Menu::resetState() {
}

void Menu::handleSelect(uint8_t index) {
}

void Menu::handleCancel() {
	// Remove ourselves from the menu list
        interface::popScreen();
}

void Menu::notifyButtonPressed(ButtonArray::ButtonName button) {
	uint8_t steps = MAX_ITEMS_PER_SCREEN;
	switch (button) {
        case ButtonArray::ZERO:
        case ButtonArray::OK:
		handleSelect(itemIndex);
		break;
        case ButtonArray::CANCEL:
		handleCancel();
		break;
        case ButtonArray::YMINUS:
		steps = 1;
        case ButtonArray::ZMINUS:
		// increment index
		if      (itemIndex < itemCount - steps) 
			itemIndex+=steps;
		else if (itemIndex==itemCount-1)
			itemIndex=firstItemIndex;
		else	itemIndex=itemCount-1;
		break;
        case ButtonArray::YPLUS:
		steps = 1;
        case ButtonArray::ZPLUS:
		// decrement index
		if      (itemIndex-steps > firstItemIndex)
			itemIndex-=steps;
		else if (itemIndex==firstItemIndex)
			itemIndex=itemCount - 1;
		else	itemIndex=firstItemIndex;
		break;

        case ButtonArray::XMINUS:
        case ButtonArray::XPLUS:
		break;
	}
}


CancelBuildMenu::CancelBuildMenu() :
	pauseDisabled(false) {
	itemCount = 7;
	pauseMode.autoPause = 0;
	reset();
	if (( steppers::isHoming() ) || (sdcard::getPercentPlayed() >= 100.0))	pauseDisabled = true;

	if ( host::isBuildComplete() )
		printAnotherEnabled = true;
	else	printAnotherEnabled = false;

}

void CancelBuildMenu::resetState() {
        pauseMode.autoPause = 0;
	pauseDisabled = false;
	if (( steppers::isHoming() ) || (sdcard::getPercentPlayed() >= 100.0))	pauseDisabled = true;

	if ( host::isBuildComplete() )
		printAnotherEnabled = true;
	else	printAnotherEnabled = false;

	if ( pauseDisabled )	{
		itemIndex = 2;
		itemCount = 3;
	} else {
		itemIndex = 1;
		itemCount = 9;
	}
	if ( printAnotherEnabled )
	    itemCount++;
#if 0
	if ( printAnotherEnabled ) {
		itemIndex = 1;
	}
#endif

	firstItemIndex = 1;
}


void CancelBuildMenu::update(LiquidCrystal& lcd, bool forceRedraw) {
	if ( command::isPaused() )	interface::popScreen();
	else				Menu::update(lcd, forceRedraw);
}


void CancelBuildMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
        // --- first screen ---
	const static PROGMEM prog_uchar cb_choose[]		= "Please Choose:";
	const static PROGMEM prog_uchar cb_abort[]		= "Abort Print";
	const static PROGMEM prog_uchar cb_pause[]		= "Pause";
	const static PROGMEM prog_uchar cb_pauseZ[]		= "Pause at ZPos";

        // --- second screen ---
	const static PROGMEM prog_uchar cb_pauseHBPHeat[]	= "Pause, HBP on";
	const static PROGMEM prog_uchar cb_pauseNoHeat[]	= "Pause, No Heat";
	const static PROGMEM prog_uchar cb_speed[]              = "Change Speed";
	const static PROGMEM prog_uchar cb_temp[]               = "Change Temp";

	// --- third screen ---
	const static PROGMEM prog_uchar cb_printAnother[]	= "Print Another";
	const static PROGMEM prog_uchar cb_cont[]		= "Continue Build";
	const static PROGMEM prog_uchar cb_back[]               = "Return to Menu";

	if (( steppers::isHoming() ) || (sdcard::getPercentPlayed() >= 100.0))	pauseDisabled = true;

	//Implement variable length menu
	uint8_t lind = 0;

	// ---- first screen ----

	if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_choose));
	lind ++;

	// skip abort if paused...
	if (( pauseDisabled ) && ( ! printAnotherEnabled )) lind ++;

	if ( index == lind)	lcd.writeFromPgmspace(LOCALIZE(cb_abort));
	lind ++;

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_pause));
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_pauseZ));
		lind ++;
	}

	// ---- second screen ----

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_pauseHBPHeat));
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_pauseNoHeat));
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_speed));
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_temp));
		lind ++;	    
	}

	// ---- third screen ----

	if ( printAnotherEnabled ) {
		if ( index == lind ) lcd.writeFromPgmspace(LOCALIZE(cb_printAnother));
		lind ++;
	}

	if ( index == lind ) {
		if ( printAnotherEnabled ) lcd.writeFromPgmspace(LOCALIZE(cb_back));
		else lcd.writeFromPgmspace(LOCALIZE(cb_cont));
	}
}

void CancelBuildMenu::handleSelect(uint8_t index) {
	//Implement variable length menu
	uint8_t lind = 0;

	if (( pauseDisabled ) && ( ! printAnotherEnabled )) lind ++;

	lind ++;

	if ( index == lind ) {
#ifdef HAS_FILAMENT_COUNTER
		command::addFilamentUsed();
#endif
		// Cancel build, returning to whatever menu came before monitor mode.
		// TODO: Cancel build.
		interface::popScreen();
		host::stopBuild();
		return;
	}
	lind ++;

	if ( ! pauseDisabled ) {
		if ( index == lind ) {
			command::pause(true, PAUSE_HEAT_ON);  // pause, all heaters left on
			pauseMode.autoPause = 0;
			interface::pushScreen(&pauseMode);
			return;
		}
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind ) {
			interface::pushScreen(&pauseAtZPosScreen);
			return;
		}
		lind ++;
	}

	// ---- second screen ----

	if ( ! pauseDisabled ) {
		if ( index == lind ) {
			command::pause(true, PAUSE_EXT_OFF);  // pause, HBP left on
			pauseMode.autoPause = 0;
			interface::pushScreen(&pauseMode);
			return;
		}
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind ) {
			command::pause(true, PAUSE_EXT_OFF | PAUSE_HBP_OFF);  // pause no heat
			pauseMode.autoPause = 0;
			interface::pushScreen(&pauseMode);
		}
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )
		{
			interface::pushScreen(&changeSpeedScreen);
			return;
		}
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )
		{
			interface::pushScreen(&changeTempScreen);
			return;
		}
		lind ++;
	}

	// ---- third screen ----
	if ( printAnotherEnabled ) {
		if ( index == lind ) {
			command::buildAnotherCopy();
			interface::popScreen();
			return;
		}
		lind ++;
	}

	if ( index == lind ) {
		// Don't cancel print, just close dialog.
                interface::popScreen();
	}
}

MainMenu::MainMenu() {
	itemCount = 20;
#ifdef EEPROM_MENU_ENABLE
	itemCount ++;
#endif
	reset();

	lcdTypeChangeTimer = 0.0;
}

void MainMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar main_monitor[]		= "Monitor";
	const static PROGMEM prog_uchar main_build[]		= "Build from SD";
	const static PROGMEM prog_uchar main_jog[]		= "Jog";
	const static PROGMEM prog_uchar main_preheat[]		= "Preheat";
	const static PROGMEM prog_uchar main_extruder[]		= "Extrude";
	const static PROGMEM prog_uchar main_homeAxis[]		= "Home Axis";
	const static PROGMEM prog_uchar main_advanceABP[]	= "Advance ABP";
	const static PROGMEM prog_uchar main_steppersS[]	= "Steppers";
	const static PROGMEM prog_uchar main_moodlight[]	= "Mood Light";
	const static PROGMEM prog_uchar main_buzzer[]		= "Buzzer";
	const static PROGMEM prog_uchar main_buildSettings[]	= "Build Settings";
	const static PROGMEM prog_uchar main_profiles[]		= "Profiles";
	const static PROGMEM prog_uchar main_extruderFan[]	= "Extruder Fan";
	const static PROGMEM prog_uchar main_calibrate[]	= "Calibrate";
	const static PROGMEM prog_uchar main_homeOffsets[]	= "Home Offsets";
	const static PROGMEM prog_uchar main_filamentUsed[]	= "Filament Used";
	const static PROGMEM prog_uchar main_currentPosition[]	= "Position";
	const static PROGMEM prog_uchar main_endStops[]		= "Test End Stops";
	const static PROGMEM prog_uchar main_homingRates[]	= "Homing Rates";
	const static PROGMEM prog_uchar main_versions[]		= "Version";
#ifdef EEPROM_MENU_ENABLE
	const static PROGMEM prog_uchar main_eeprom[]		= "Eeprom";
#endif
	const static prog_uchar *messages[20
#ifdef EEPROM_MENU_ENABLE
					  +1
#endif
		] = { LOCALIZE(main_monitor),         //  0
		      LOCALIZE(main_build),           //  1
		      LOCALIZE(main_preheat),         //  2
		      LOCALIZE(main_extruder),        //  3

		      LOCALIZE(main_buildSettings),   //  4
		      LOCALIZE(main_homeAxis),        //  5
		      LOCALIZE(main_jog),             //  6
		      LOCALIZE(main_filamentUsed),    //  7

		      LOCALIZE(main_advanceABP),      //  8
		      LOCALIZE(main_steppersS),       //  9
		      LOCALIZE(main_moodlight),       // 10
		      LOCALIZE(main_buzzer),          // 11

		      LOCALIZE(main_profiles),        // 12
		      LOCALIZE(main_calibrate),       // 13
		      LOCALIZE(main_homeOffsets),     // 14
		      LOCALIZE(main_homingRates),     // 15

		      LOCALIZE(main_extruderFan),     // 16
		      LOCALIZE(main_endStops),        // 17
		      LOCALIZE(main_currentPosition), // 18
		      LOCALIZE(main_versions),        // 19

#ifdef EEPROM_MENU_ENABLE
		      LOCALIZE(main_eeprom),          // 20
#endif
	};

	if ( index < sizeof(messages)/sizeof(prog_uchar *) )
		lcd.writeFromPgmspace(messages[index]);
}


void MainMenu::handleSelect(uint8_t index) {
	switch (index) {
	case 0:
	    interface::pushScreen(&monitorMode);
	    break;
	case 1:
	    interface::pushScreen(&sdMenu);
	    break;
	case 2:
	    interface::pushScreen(&preheatMenu);
	    preheatMenu.fetchTargetTemps();
	    break;
	case 3:    
	    interface::pushScreen(&extruderMenu);
	    break;
	case 4:
	    interface::pushScreen(&buildSettingsMenu);
	    break;
	case 5:
	    interface::pushScreen(&homeAxisMode);
	    break;
	case 6:
	    interface::pushScreen(&jogger);
	    break;
	case 7:
	    interface::pushScreen(&filamentUsedMode);
	    break;
	case 8:
	    interface::pushScreen(&advanceABPMode);
	    break;
	case 9:
	    interface::pushScreen(&steppersMenu);
	    break;
	case 10:
	    interface::pushScreen(&moodLightMode);
	    break;
	case 11:
	    interface::pushScreen(&buzzerSetRepeats);
	    break;
	case 12:
	    interface::pushScreen(&profilesMenu);
	    break;
	case 13:
	    interface::pushScreen(&calibrateMode);
	    break;
	case 14:
	    interface::pushScreen(&homeOffsetsMode);
	    break;
	case 15:
	    interface::pushScreen(&homingFeedRatesMode);
	    break;
	case 16:
	    interface::pushScreen(&extruderFanMenu);
	    break;
	case 17:
	    interface::pushScreen(&testEndStopsMode);
	    break;
	case 18:
	    interface::pushScreen(&currentPositionMode);
	    break;
	case 19:
	    interface::pushScreen(&versionMode);
	    break;
#ifdef EEPROM_MENU_ENABLE
	case 20:
	    interface::pushScreen(&eepromMenu);
	    break;
#endif
	}
}

void MainMenu::update(LiquidCrystal& lcd, bool forceRedraw) {
	Menu::update(lcd, forceRedraw);

	if (interface::isButtonPressed(ButtonArray::XMINUS)) {
		if (( lcdTypeChangeTimer != -1.0 ) && ( lcdTypeChangeTimer + LCD_TYPE_CHANGE_BUTTON_HOLD_TIME ) <= Motherboard::getBoard().getCurrentSeconds()) {
			Motherboard::getBoard().buzz(1, 1, 1);
			lcdTypeChangeTimer = -1.0;
			lcd.nextLcdType();
			lcd.reloadDisplayType();
			host::stopBuildNow();
		}
	} else lcdTypeChangeTimer = Motherboard::getBoard().getCurrentSeconds();
}

SDMenu::SDMenu() : 
	updatePhase(0),
	drawItemLockout(false),
	selectable(false),
	folderStackIndex(-1) {
	reset();
}

void SDMenu::resetState() {
	itemCount = countFiles();
	if ( !itemCount ) {
		folderStackIndex = -1;
		itemCount  = 1;
		selectable = false;
	}
	else selectable = true;
	updatePhase = 0;
	lastItemIndex = 0;
	drawItemLockout = false;
}

// Count the number of files on the SD card
uint8_t SDMenu::countFiles() {
	uint8_t count = 0;

	// First, reset the directory index
	if ( sdcard::directoryReset() != sdcard::SD_SUCCESS )
		// TODO: Report 
		return 0;

	char fnbuf[3];

	// Count the files
	do {
		bool isdir;
		sdcard::directoryNextEntry(fnbuf,sizeof(fnbuf),&isdir);
		if ( fnbuf[0] == 0 )
			return count;
		// Count .. and anyfile which doesn't begin with .
		else if ( (fnbuf[0] != '.') ||
			  ( isdir && fnbuf[1] == '.' && fnbuf[2] == 0) )
			count++;
	} while (true);

	// Never reached
	return count;
}

bool SDMenu::getFilename(uint8_t index, char buffer[], uint8_t buffer_size, bool *isdir) {

	*isdir = false;

	// First, reset the directory list
	if ( sdcard::directoryReset() != sdcard::SD_SUCCESS )
                return false;

	bool my_isdir;
	for(uint8_t i = 0; i < index+1; i++) {
		do {
			sdcard::directoryNextEntry(buffer, buffer_size, &my_isdir);
			if ( buffer[0] == 0 )
				// No more files
				return false;
			// Count only .. and any file not beginning with .
			if ( (buffer[0] != '.' ) ||
			     ( my_isdir && buffer[1] == '.' && buffer[2] == 0) )
				break;
		} while (true);
	}
	*isdir = my_isdir;
        return true;
}

void SDMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	uint8_t idx, filenameLength;
	uint8_t longFilenameOffset = 0;
	uint8_t displayWidth = lcd.getDisplayWidth() - 1;
	uint8_t offset = 1;
	char fnbuf[SD_MAXFILELEN+2]; // extra +1 as we may precede the name with a folder indicator
	bool isdir;

	if ( !selectable || index > itemCount - 1 )
		return;

        if ( !getFilename(index, fnbuf + 1, sizeof(fnbuf) - 1, &isdir) ) {
		selectable = false;
		return;
	}

	if ( isdir )
	{
		fnbuf[0] = ( fnbuf[1] == '.' && fnbuf[2] == '.' ) ? LCD_CUSTOM_CHAR_RETURN : LCD_CUSTOM_CHAR_FOLDER;
		offset = 0;
	}

	//Figure out length of filename
	for (filenameLength = 0; (filenameLength < sizeof(fnbuf)) && (fnbuf[offset+filenameLength] != 0); filenameLength++) ;

	//Support scrolling filenames that are longer than the lcd screen
	if (filenameLength >= displayWidth) longFilenameOffset = updatePhase % (filenameLength - displayWidth + 1);

	for (idx = 0; (idx < displayWidth) && ((longFilenameOffset + idx) < sizeof(fnbuf)) &&
                        (fnbuf[offset+longFilenameOffset + idx] != 0); idx++)
		lcd.write(fnbuf[offset+longFilenameOffset + idx]);

	//Clear out the rest of the line
	while ( idx < displayWidth ) {
		lcd.write(' ');
		idx ++;
	}
	return;
}

void SDMenu::update(LiquidCrystal& lcd, bool forceRedraw) {
	uint8_t height = lcd.getDisplayHeight();
	
	if (( ! forceRedraw ) && ( ! drawItemLockout )) {
		//Redraw the last item if we have changed
		if (((itemIndex/height) == (lastDrawIndex/height)) &&
		     ( itemIndex != lastItemIndex ))  {
			lcd.setCursor(1,lastItemIndex % height);
			drawItem(lastItemIndex, lcd);
		}
		lastItemIndex = itemIndex;

		lcd.setCursor(1,itemIndex % height);
		drawItem(itemIndex, lcd);
	}

	if ( selectable ) {
		Menu::update(lcd, forceRedraw);
		updatePhase ++;
	}
	else {
		// This was actually triggered in drawItem() but popping a screen
		// from there is not a good idea
		timedMessage(lcd, 0);
		interface::popScreen();
	}
}

void SDMenu::notifyButtonPressed(ButtonArray::ButtonName button) {
	updatePhase = 0;
	Menu::notifyButtonPressed(button);
}

void SDMenu::handleSelect(uint8_t index) {

	if (host::getHostState() != host::HOST_STATE_READY || !selectable)
		// TODO: report error
		return;

	drawItemLockout = true;

	char fnbuf[SD_MAXFILELEN+1];
	bool isdir;
        if ( !getFilename(index, fnbuf, sizeof(fnbuf), &isdir) )
		goto badness;

	if ( isdir ) {

		// Attempt to change the directory
		if ( !sdcard::changeDirectory(fnbuf) )
			goto badness;

		// Find our way around this folder
		//  Doing a resetState() will determine the new itemCount
		resetState();

		itemIndex = 0;

		// If we're not selectable, don't bother
		if ( selectable ) {
			// Recall last itemIndex in this folder if we popped up
			if ( fnbuf[0] != '.' || fnbuf[1] != '.' || fnbuf[2] != 0 ) {
				// We've moved down into a child folder
				if ( folderStackIndex < (int8_t)(sizeof(folderStack) - 1) )
					folderStack[++folderStackIndex] = index;
			}
			else {
				// We've moved up into our parent folder
				if ( folderStackIndex >= 0 ) {
					itemIndex = folderStack[folderStackIndex--];
					if (itemIndex >= itemCount) {
						// Something is wrong; invalidate the entire stack
						itemIndex = 0;
						folderStackIndex = -1;
					}
				}
			}
		}

		// Repaint the display
		// Really ensure that the entire screen is wiped
		lastDrawIndex = index; // so that the old cursor can be cleared
		Menu::update(Motherboard::getBoard().getInterfaceBoard().lcd, true);

		return;
	}

	if ( host::startBuildFromSD(fnbuf) == sdcard::SD_SUCCESS )
		return;

badness:
	// TODO: report error
	interface::pushScreen(&unableToOpenFileMenu);
}

void ValueSetScreen::reset() {
	value = eeprom::getEeprom8(location, default_value);
}

void ValueSetScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(message1);

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	// Redraw tool info
	lcd.setRow(1);
	lcd.writeInt(value,3);
	if ( units )	lcd.writeFromPgmspace(units);
}

void ValueSetScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
        case ButtonArray::CANCEL:
		interface::popScreen();
		break;
        case ButtonArray::ZERO:
		break;
        case ButtonArray::OK:
		eeprom_write_byte((uint8_t*)location,value);
		interface::popScreen();
		break;
        case ButtonArray::ZPLUS:
		// increment more
		if (value <= 249) {
			value += 5;
		}
		break;
        case ButtonArray::ZMINUS:
		// decrement more
		if (value >= 6) {
			value -= 5;
		}
		break;
        case ButtonArray::YPLUS:
		// increment less
		if (value <= 253) {
			value += 1;
		}
		break;
        case ButtonArray::YMINUS:
		// decrement less
		if (value >= 2) {
			value -= 1;
		}
		break;

        case ButtonArray::XMINUS:
        case ButtonArray::XPLUS:
		break;
	}
}

PreheatMenu::PreheatMenu() {
	itemCount = 4;
	reset();
}

void PreheatMenu::fetchTargetTemps() {
	OutPacket responsePacket;
	uint8_t activeToolhead;
	steppers::getStepperPosition(&activeToolhead);
	if (extruderControl(activeToolhead, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0)) {
		tool0Temp = responsePacket.read16(1);
	}
	if (extruderControl(activeToolhead, SLAVE_CMD_GET_PLATFORM_SP, EXTDR_CMD_GET, responsePacket, 0)) {
		platformTemp = responsePacket.read16(1);
	}
}

void PreheatMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar ph_heat[]     = "Heat ";
	const static PROGMEM prog_uchar ph_cool[]     = "Cool ";
	const static PROGMEM prog_uchar ph_tool0[]    = "Tool0";
	const static PROGMEM prog_uchar ph_platform[] = "Bed";
	const static PROGMEM prog_uchar ph_tool0set[] = "Set Tool0 Temp";
	const static PROGMEM prog_uchar ph_platset[]  = "Set Bed Temp";

	switch (index) {
	case 0:
		fetchTargetTemps();
		if (tool0Temp > 0) {
			lcd.writeFromPgmspace(LOCALIZE(ph_cool));
		} else {
			lcd.writeFromPgmspace(LOCALIZE(ph_heat));
		}
		lcd.writeFromPgmspace(LOCALIZE(ph_tool0));
		break;
	case 1:
		if (platformTemp > 0) {
			lcd.writeFromPgmspace(LOCALIZE(ph_cool));
		} else {
			lcd.writeFromPgmspace(LOCALIZE(ph_heat));
		}
		lcd.writeFromPgmspace(LOCALIZE(ph_platform));
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(ph_tool0set));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(ph_platset));
		break;
	}
}

void PreheatMenu::handleSelect(uint8_t index) {
	static const PROGMEM prog_uchar ph_message1[] = "Tool0 Targ Temp:";
	const static PROGMEM prog_uchar ph_message2[] = "Bed Target Temp:";
	uint8_t activeToolhead;
	steppers::getStepperPosition(&activeToolhead);

	OutPacket responsePacket;
	switch (index) {
		case 0:
			// Toggle Extruder heater on/off
			if (tool0Temp > 0) {
				extruderControl(activeToolhead, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, 0);
			} else {
				uint8_t value = eeprom::getEeprom8(eeprom::TOOL0_TEMP, EEPROM_DEFAULT_TOOL0_TEMP);
				extruderControl(activeToolhead, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, (uint16_t)value);
			}
			fetchTargetTemps();
			lastDrawIndex = 255; // forces redraw.
			break;
		case 1:
			// Toggle Platform heater on/off
			if (platformTemp > 0) {
				extruderControl(0, SLAVE_CMD_SET_PLATFORM_TEMP, EXTDR_CMD_SET, responsePacket, 0);
			} else {
				uint8_t value = eeprom::getEeprom8(eeprom::PLATFORM_TEMP, EEPROM_DEFAULT_PLATFORM_TEMP);
				extruderControl(0, SLAVE_CMD_SET_PLATFORM_TEMP, EXTDR_CMD_SET, responsePacket, (uint16_t)value);
			}
			fetchTargetTemps();
			lastDrawIndex = 255; // forces redraw.
			break;
		case 2:
			// Show Extruder Temperature Setting Screen
			heaterTempSetScreen.location = eeprom::TOOL0_TEMP;
			heaterTempSetScreen.default_value = EEPROM_DEFAULT_TOOL0_TEMP;
			heaterTempSetScreen.message1 = LOCALIZE(ph_message1);
			heaterTempSetScreen.units = NULL;
                        interface::pushScreen(&heaterTempSetScreen);
			break;
		case 3:
			// Show Platform Temperature Setting Screen
			heaterTempSetScreen.location = eeprom::PLATFORM_TEMP;
			heaterTempSetScreen.default_value = EEPROM_DEFAULT_PLATFORM_TEMP;
			heaterTempSetScreen.message1 = LOCALIZE(ph_message2);
			heaterTempSetScreen.units = NULL;
                        interface::pushScreen(&heaterTempSetScreen);
			break;
		}
}

void HomeAxisMode::reset() {
}

void HomeAxisMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar ha_home1[] = "Home Axis: ";
	const static PROGMEM prog_uchar ha_home2[] = "  Y            Z";
	const static PROGMEM prog_uchar ha_home3[] = "X   X (endstops)";
	const static PROGMEM prog_uchar ha_home4[] = "  Y            Z";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(ha_home1));

		lcd.setRow(1);
		lcd.writeFromPgmspace(LOCALIZE(ha_home2));

		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(ha_home3));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(ha_home4));
	}
}

void HomeAxisMode::home(ButtonArray::ButtonName direction) {
	uint8_t axis = 0, axisIndex = 0;
	bool 	maximums = false;

	uint8_t endstops = eeprom::getEeprom8(eeprom::ENDSTOPS_USED, EEPROM_DEFAULT_ENDSTOPS_USED);

	switch(direction) {
	        case ButtonArray::XMINUS:
      		case ButtonArray::XPLUS:
			axis 	 = 0x01;
			if ( endstops & 0x02 )	maximums = true;
			if ( endstops & 0x01 )	maximums = false;
			axisIndex = 0;
			break;
        	case ButtonArray::YMINUS:
        	case ButtonArray::YPLUS:
			axis 	 = 0x02;
			if ( endstops & 0x08 )	maximums = true;
			if ( endstops & 0x04 )	maximums = false;
			axisIndex = 1;
			break;
        	case ButtonArray::ZMINUS:
        	case ButtonArray::ZPLUS:
			axis 	 = 0x04;
			if ( endstops & 0x20 )	maximums = true;
			if ( endstops & 0x10 )	maximums = false;
			axisIndex = 2;
			break;
		default:
			break;
	}

	//60.0, because feed rate is in mm/min units, we convert to seconds
	float feedRate = (float)eeprom::getEepromUInt32(eeprom::HOMING_FEED_RATE_X + (axisIndex * sizeof(uint32_t)), 500) / 60.0;
	float stepsPerSecond = feedRate * (float)stepperAxisMMToSteps(1.0, (enum AxisEnum)axisIndex);
	int32_t interval = (int32_t)(1000000.0 / stepsPerSecond);

	steppers::startHoming(maximums, axis, (uint32_t)interval);
}

void HomeAxisMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
#if 0
        	case ButtonArray::YMINUS:
        	case ButtonArray::ZMINUS:
        	case ButtonArray::YPLUS:
        	case ButtonArray::ZPLUS:
        	case ButtonArray::XMINUS:
        	case ButtonArray::XPLUS:
#else
	        default:
#endif
			home(button);
			break;
        	case ButtonArray::ZERO:
        	case ButtonArray::OK:
			interface::pushScreen(&endStopConfigScreen);
			break;
        	case ButtonArray::CANCEL:
			steppers::abort();
			steppers::enableAxis(0, false);
			steppers::enableAxis(1, false);
			steppers::enableAxis(2, false);
               		interface::popScreen();
			break;
	}
}


EnabledDisabledMenu::EnabledDisabledMenu() {
	itemCount = 4;
	reset();
}

void EnabledDisabledMenu::resetState() {
	setupTitle();

	if ( isEnabled() ) itemIndex = 3;
	else		   itemIndex = 2;
	firstItemIndex = 2;
}

void EnabledDisabledMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const prog_uchar *msg;

	switch (index) {
	default: return;
	case 0: msg = msg1; break;
	case 1: msg = msg2; break;
	case 2: msg = LOCALIZE(generic_off); break;
	case 3: msg = LOCALIZE(generic_on); break;
	}
	if (msg) lcd.writeFromPgmspace(msg);
}

void EnabledDisabledMenu::handleSelect(uint8_t index) {
	if ( index == 2 ) enable(false);
	if ( index == 3 ) enable(true);
	interface::popScreen();
}

bool SteppersMenu::isEnabled() {
    for ( uint8_t j = 0; j < STEPPER_COUNT; j++ )
	if ( stepperAxisIsEnabled(j) )
	    return true;
	return false;
}

void SteppersMenu::enable(bool enabled) {
    for ( uint8_t j = 0; j < STEPPER_COUNT; j++ )
	steppers::enableAxis(j, enabled);
}

void SteppersMenu::setupTitle() {
	const static PROGMEM prog_uchar step_msg1[] = "Stepper Motors:";
	msg1 = LOCALIZE(step_msg1);
	msg2 = 0;
}

void TestEndStopsMode::reset() {
}

void TestEndStopsMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar tes_test1[] = "Test End Stops: ";
	const static PROGMEM prog_uchar tes_test2[] = "XMin:N    XMax:N";
	const static PROGMEM prog_uchar tes_test3[] = "YMin:N    YMax:N";
	const static PROGMEM prog_uchar tes_test4[] = "ZMin:N    ZMax:N";
	const static PROGMEM prog_uchar tes_strY[]  = "Y";
	const static PROGMEM prog_uchar tes_strN[]  = "N";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(tes_test1));

		lcd.setRow(1);
		lcd.writeFromPgmspace(LOCALIZE(tes_test2));

		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(tes_test3));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(tes_test4));
	}

	lcd.setCursor(5, 1);
	if ( stepperAxisIsAtMinimum(0) ) lcd.writeFromPgmspace(LOCALIZE(tes_strY));
	else				 lcd.writeFromPgmspace(LOCALIZE(tes_strN));
	lcd.setCursor(15, 1);
	if ( stepperAxisIsAtMaximum(0) ) lcd.writeFromPgmspace(LOCALIZE(tes_strY));
	else				 lcd.writeFromPgmspace(LOCALIZE(tes_strN));

	lcd.setCursor(5, 2);
	if ( stepperAxisIsAtMinimum(1) ) lcd.writeFromPgmspace(LOCALIZE(tes_strY));
	else				 lcd.writeFromPgmspace(LOCALIZE(tes_strN));
	lcd.setCursor(15, 2);
	if ( stepperAxisIsAtMaximum(1) ) lcd.writeFromPgmspace(LOCALIZE(tes_strY));
	else				 lcd.writeFromPgmspace(LOCALIZE(tes_strN));

	lcd.setCursor(5, 3);
	if ( stepperAxisIsAtMinimum(2) ) lcd.writeFromPgmspace(LOCALIZE(tes_strY));
	else				 lcd.writeFromPgmspace(LOCALIZE(tes_strN));
	lcd.setCursor(15, 3);
	if ( stepperAxisIsAtMaximum(2) ) lcd.writeFromPgmspace(LOCALIZE(tes_strY));
	else				 lcd.writeFromPgmspace(LOCALIZE(tes_strN));
}

void TestEndStopsMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	// That above list is exhaustive
	interface::popScreen();
}

void PauseMode::reset() {
	lastDirectionButtonPressed = (ButtonArray::ButtonName)0;
	lastPauseState = PAUSE_STATE_NONE;
	userViewMode = eeprom::getEeprom8(eeprom::JOG_MODE_SETTINGS, EEPROM_DEFAULT_JOG_MODE_SETTINGS) & 0x01;
}

void PauseMode::update(LiquidCrystal& lcd, bool forceRedraw) {
    const static PROGMEM prog_uchar p_waitForCurrentCommand[] = "Entering pause..";
    const static PROGMEM prog_uchar p_retractFilament[]       = "Retract filament";
    const static PROGMEM prog_uchar p_clearingBuild[]         = "Clearing build..";
    const static PROGMEM prog_uchar p_heating[]               = "Heating...      ";
    const static PROGMEM prog_uchar p_leavingPaused[]         = "Leaving pause...";
    const static PROGMEM prog_uchar p_paused1[]               = "Paused(";
    const static PROGMEM prog_uchar p_paused2[]               = "   Y+         Z+";
    const static PROGMEM prog_uchar p_paused3[]               = "X- Rev X+  (Fwd)";
    const static PROGMEM prog_uchar p_paused4[]               = "   Y-         Z-";
    const static PROGMEM prog_uchar p_close[]                 = "):";
    const static PROGMEM prog_uchar p_cancel1[]               = "SD card error";
    const static PROGMEM prog_uchar p_cancel2[]               = "Build cancelled";
    const static PROGMEM prog_uchar p_cancel3[]               = "Press any button";
    const static PROGMEM prog_uchar p_cancel4[]               = "to continue ";

    enum PauseState pauseState = command::pauseState();

    if ( forceRedraw || ( lastPauseState != pauseState) )
	lcd.clear();

    OutPacket responsePacket;

    lcd.homeCursor();

    switch ( pauseState ) {
    case PAUSE_STATE_ENTER_START_PIPELINE_DRAIN:
    case PAUSE_STATE_ENTER_WAIT_PIPELINE_DRAIN:
	lcd.writeFromPgmspace(LOCALIZE(p_waitForCurrentCommand));
	break;
    case PAUSE_STATE_ENTER_START_RETRACT_FILAMENT:
    case PAUSE_STATE_ENTER_WAIT_RETRACT_FILAMENT:
	lcd.writeFromPgmspace(LOCALIZE(p_retractFilament));
	break;
    case PAUSE_STATE_ENTER_START_CLEARING_PLATFORM:
    case PAUSE_STATE_ENTER_WAIT_CLEARING_PLATFORM:
	lcd.writeFromPgmspace(LOCALIZE(p_clearingBuild));
	{
	    const prog_uchar *msg = command::pauseGetErrorMessage();
	    if ( msg ) {
		lcd.setRow(1);
		lcd.writeFromPgmspace(msg);
	    }
	}
	break;
    case PAUSE_STATE_PAUSED:
	lcd.writeFromPgmspace(LOCALIZE(p_paused1));
	lcd.writeFloat(stepperAxisStepsToMM(command::getPausedPosition()[Z_AXIS], Z_AXIS), 3);
	lcd.writeFromPgmspace(LOCALIZE(p_close));

	lcd.setRow(1);
	lcd.writeFromPgmspace(LOCALIZE(p_paused2));
	lcd.setRow(2);
	lcd.writeFromPgmspace(LOCALIZE(p_paused3));
	lcd.setRow(3);
	lcd.writeFromPgmspace(LOCALIZE(p_paused4));
	break;
    case PAUSE_STATE_EXIT_START_HEATERS:
    case PAUSE_STATE_EXIT_WAIT_FOR_HEATERS:
	lcd.writeFromPgmspace(LOCALIZE(p_heating));
	break;
    case PAUSE_STATE_EXIT_START_RETURNING_PLATFORM:
    case PAUSE_STATE_EXIT_WAIT_RETURNING_PLATFORM:
    case PAUSE_STATE_EXIT_START_UNRETRACT_FILAMENT:
    case PAUSE_STATE_EXIT_WAIT_UNRETRACT_FILAMENT:
	lcd.writeFromPgmspace(LOCALIZE(p_leavingPaused));
	break;
    case PAUSE_STATE_ERROR:
    {
	const prog_uchar *msg = command::pauseGetErrorMessage();
	if ( !msg ) msg = LOCALIZE(p_cancel1);
	lcd.writeFromPgmspace(msg);
	lcd.setRow(1);
	lcd.writeFromPgmspace(LOCALIZE(p_cancel2));
	lcd.setRow(2);
	lcd.writeFromPgmspace(LOCALIZE(p_cancel3));
	lcd.setRow(3);
	lcd.writeFromPgmspace(LOCALIZE(p_cancel4));
	break;
    }
    case PAUSE_STATE_NONE:
	//Pop off the pause screen and the menu underneath to return to the monitor screen
	interface::popScreen();
	if ( autoPause == 1 ) interface::popScreen();
	if ( autoPause == 2 ) interface::popScreen();
	break;
    }

    if ( lastDirectionButtonPressed ) {
	if ( interface::isButtonPressed(lastDirectionButtonPressed) )
	    jog(lastDirectionButtonPressed, true);
	else {
	    lastDirectionButtonPressed = (ButtonArray::ButtonName)0;
	    steppers::abort();
	}
    }

    lastPauseState = pauseState;
}

void PauseMode::notifyButtonPressed(ButtonArray::ButtonName button) {
    enum PauseState ps = command::pauseState();
    if ( ps == PAUSE_STATE_PAUSED ) {
	if ( button == ButtonArray::CANCEL )
	    host::pauseBuild(false);
	else
	    jog(button, true);
    }
    else if ( ps == PAUSE_STATE_ERROR ) {
	autoPause = 2;
	command::pauseClearError(); // changes pause state to PAUSE_STATE_NONE
    }
}

void PauseAtZPosScreen::reset() {
	int32_t currentPause = command::getPauseAtZPos();
	if ( currentPause == 0 ) {
		Point position = steppers::getPlannerPosition();
		pauseAtZPos = stepperAxisStepsToMM(position[2], Z_AXIS);
	} else  pauseAtZPos = stepperAxisStepsToMM(currentPause, Z_AXIS);
}

void PauseAtZPosScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar pz_message1[] = "Pause at ZPos:";
	const static PROGMEM prog_uchar pz_mm[]       = "mm   ";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(pz_message1));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	// Redraw tool info
	lcd.setRow(1);
	lcd.writeFloat((float)pauseAtZPos, 3);
	lcd.writeFromPgmspace(LOCALIZE(pz_mm));
}

void PauseAtZPosScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
	default :
		return;
	case ButtonArray::OK:
		//Set the pause
		command::pauseAtZPos(stepperAxisMMToSteps(pauseAtZPos, Z_AXIS));
		// FALL THROUGH
	case ButtonArray::CANCEL:
		interface::popScreen();
		interface::popScreen();
		break;
	case ButtonArray::ZPLUS:
		// increment more
		if (pauseAtZPos <= 250) pauseAtZPos += 1.0;
		break;
	case ButtonArray::ZMINUS:
		// decrement more
		if (pauseAtZPos >= 1.0) pauseAtZPos -= 1.0;
		else			pauseAtZPos = 0.0;
		break;
	case ButtonArray::YPLUS:
		// increment less
		if (pauseAtZPos <= 254) pauseAtZPos += 0.05;
		break;
	case ButtonArray::YMINUS:
		// decrement less
		if (pauseAtZPos >= 0.05) pauseAtZPos -= 0.05;
		else			 pauseAtZPos = 0.0;
		break;
	}

	if ( pauseAtZPos < 0.001 )	pauseAtZPos = 0.0;
}

void ChangeTempScreen::reset() {
    // Make getTemp() think that a toolhead change has occurred
    activeToolhead = 255;
    altTemp = 0;
    getTemp();
}

void ChangeTempScreen::getTemp() {
    uint8_t at;
    steppers::getStepperPosition(&at);
    if ( at != activeToolhead ) {
	activeToolhead = at;
	altTemp = command::altTemp[activeToolhead];
	if ( altTemp == 0 ) {
	    // Get the current set point
	    OutPacket responsePacket;
	    if ( extruderControl(activeToolhead, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0) ) {
		altTemp = responsePacket.read16(1);
	    }
	    else {
		// Cannot get the current set point....  Use pre-heat temp?
		if ( activeToolhead == 0 )
		    altTemp = (uint16_t)eeprom::getEeprom8(eeprom::TOOL0_TEMP, EEPROM_DEFAULT_TOOL0_TEMP);
		else
		    altTemp = (uint16_t)eeprom::getEeprom8(eeprom::TOOL1_TEMP, EEPROM_DEFAULT_TOOL1_TEMP);
	    }
	}
	if ( altTemp > (uint16_t)MAX_TEMP ) altTemp = MAX_TEMP;
    }
}

void ChangeTempScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
    const static PROGMEM prog_uchar ct_message1[] = "Extruder temp:";

    if (forceRedraw) {
	lcd.clearHomeCursor();
	lcd.writeFromPgmspace(LOCALIZE(ct_message1));

	lcd.setRow(3);
	lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
    }

    // Since the print is still running, the active tool head may have changed
    getTemp();

    // Redraw tool info
    lcd.setRow(1);
    lcd.writeInt(altTemp, 3);
    lcd.write('C');
}

void ChangeTempScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
    // We change the actual steppers::speedFactor so that
    //   the user can see the change dynamically (after making it through
    //   the queue of planned blocks)
    int16_t temp = (int16_t)(0x7fff & altTemp);
    switch (button) {
    case ButtonArray::OK:
    {
	OutPacket responsePacket;
	// Set the temp
	command::altTemp[activeToolhead] = altTemp;
	// Only set the temp if the heater is active
	if ( extruderControl(activeToolhead, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0) ) {
	    uint16_t data = responsePacket.read16(1);
	    if ( data != 0 )
		extruderControl(activeToolhead, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, (uint16_t)altTemp);
	}
    }
        // FALL THROUGH
    case ButtonArray::CANCEL:
	interface::popScreen();
	interface::popScreen();
	return;
    case ButtonArray::ZPLUS:
	// increment more
	temp += 5;
	break;
    case ButtonArray::ZMINUS:
	// decrement more
	temp -= 5;
	break;
    case ButtonArray::YPLUS:
	// increment less
	temp += 1;
	break;
    case ButtonArray::YMINUS:
	// decrement less
	temp -= 1;
	break;
    default :
	return;
    }

    if (temp > MAX_TEMP ) altTemp = MAX_TEMP;
    else if ( temp < 0 ) altTemp = 0;
    else altTemp = (uint16_t)(0x7fff & temp);
}

void ChangeSpeedScreen::reset() {
	// So that we can restore the speed in case of a CANCEL
	speedFactor = steppers::speedFactor;
	alterSpeed  = steppers::alterSpeed;
}

void ChangeSpeedScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar cs_message1[] = "Increase speed:";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(cs_message1));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	// Redraw tool info
	lcd.setRow(1);
	lcd.write('x');
	lcd.writeFloat(FPTOF(steppers::speedFactor), 2);
}

void ChangeSpeedScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	// We change the actual steppers::speedFactor so that
	//   the user can see the change dynamically (after making it through
	//   the queue of planned blocks)
	FPTYPE sf = steppers::speedFactor;
	switch (button) {
	case ButtonArray::CANCEL:
		// restore the original
		steppers::alterSpeed  = alterSpeed;
		steppers::speedFactor = speedFactor;
		// FALL THROUGH
	case ButtonArray::OK:
		interface::popScreen();
		interface::popScreen();
		return;
	case ButtonArray::ZPLUS:
		// increment more
		sf += KCONSTANT_0_25;
		break;
	case ButtonArray::ZMINUS:
		// decrement more
		sf -= KCONSTANT_0_25;
		break;
	case ButtonArray::YPLUS:
		// increment less
		sf += KCONSTANT_0_05;
		break;
	case ButtonArray::YMINUS:
		// decrement less
		sf -= KCONSTANT_0_05;
		break;
	default :
		return;
	}

	if ( sf > KCONSTANT_5 ) sf = KCONSTANT_5;
	else if ( sf < KCONSTANT_0_1 ) sf = KCONSTANT_0_1;

	// If sf == 1 then disable speedup
	steppers::alterSpeed  = (sf == KCONSTANT_1) ? 0x00 : 0x80;
	steppers::speedFactor = sf;
}

void AdvanceABPMode::reset() {
	abpForwarding = false;
}

void AdvanceABPMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar abp_msg1[] = "Advance ABP:";
	const static PROGMEM prog_uchar abp_msg2[] = "hold key...";
	const static PROGMEM prog_uchar abp_msg3[] = "           (fwd)";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(abp_msg1));

		lcd.setRow(1);
		lcd.writeFromPgmspace(LOCALIZE(abp_msg2));

		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(abp_msg3));
	}

	if (( abpForwarding ) && ( ! interface::isButtonPressed(ButtonArray::OK) )) {
		OutPacket responsePacket;

		abpForwarding = false;
		extruderControl(0, SLAVE_CMD_TOGGLE_ABP, EXTDR_CMD_SET8, responsePacket, (uint16_t)0);
	}
}

void AdvanceABPMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	OutPacket responsePacket;

	switch (button) {
        	case ButtonArray::OK:
			abpForwarding = true;
			extruderControl(0, SLAVE_CMD_TOGGLE_ABP, EXTDR_CMD_SET8, responsePacket, (uint16_t)1);
			break;
	        default :
               		interface::popScreen();
			break;
	}
}

void CalibrateMode::reset() {
	//Disable stepps on axis 0, 1, 2, 3, 4
	steppers::enableAxis(0, false);
	steppers::enableAxis(1, false);
	steppers::enableAxis(2, false);
	steppers::enableAxis(3, false);
	steppers::enableAxis(4, false);

	lastCalibrationState = CS_NONE;
	calibrationState = CS_START1;
}

void CalibrateMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar c_calib1[] = "Calibrate: Move ";
	const static PROGMEM prog_uchar c_calib2[] = "build platform";
	const static PROGMEM prog_uchar c_calib3[] = "until nozzle...";
	const static PROGMEM prog_uchar c_calib4[] = "          (cont)";
	const static PROGMEM prog_uchar c_calib5[] = "lies in center,";
	const static PROGMEM prog_uchar c_calib6[] = "turn threaded";
	const static PROGMEM prog_uchar c_calib7[] = "rod until...";
	const static PROGMEM prog_uchar c_calib8[] = "nozzle just";
	const static PROGMEM prog_uchar c_calib9[] = "touches.";
	const static PROGMEM prog_uchar c_homeZ[]  = "Homing Z...";
	const static PROGMEM prog_uchar c_homeY[]  = "Homing Y...";
	const static PROGMEM prog_uchar c_homeX[]  = "Homing X...";
	const static PROGMEM prog_uchar c_done[]   = "! Calibrated !";
	const static PROGMEM prog_uchar c_regen[]  = "Regenerate gcode";
	const static PROGMEM prog_uchar c_reset[]  = "         (reset)";

	if ((forceRedraw) || (calibrationState != lastCalibrationState)) {
		lcd.clearHomeCursor();
		switch(calibrationState) {
			case CS_START1:
				lcd.writeFromPgmspace(LOCALIZE(c_calib1));
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(c_calib2));
				lcd.setRow(2);
				lcd.writeFromPgmspace(LOCALIZE(c_calib3));
				lcd.setRow(3);
				lcd.writeFromPgmspace(LOCALIZE(c_calib4));
				break;
			case CS_START2:
				lcd.writeFromPgmspace(LOCALIZE(c_calib5));
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(c_calib6));
				lcd.setRow(2);
				lcd.writeFromPgmspace(LOCALIZE(c_calib7));
				lcd.setRow(3);
				lcd.writeFromPgmspace(LOCALIZE(c_calib4));
				break;
			case CS_PROMPT_MOVE:
				lcd.writeFromPgmspace(LOCALIZE(c_calib8));
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(c_calib9));
				lcd.setRow(3);
				lcd.writeFromPgmspace(LOCALIZE(c_calib4));
				break;
			case CS_HOME_Z:
			case CS_HOME_Z_WAIT:
				lcd.writeFromPgmspace(LOCALIZE(c_homeZ));
				break;
			case CS_HOME_Y:
			case CS_HOME_Y_WAIT:
				lcd.writeFromPgmspace(LOCALIZE(c_homeY));
				break;
			case CS_HOME_X:
			case CS_HOME_X_WAIT:
				lcd.writeFromPgmspace(LOCALIZE(c_homeX));
				break;
			case CS_PROMPT_CALIBRATED:
				lcd.writeFromPgmspace(LOCALIZE(c_done));
				lcd.setRow(1);
				lcd.writeFromPgmspace(LOCALIZE(c_regen));
				lcd.setRow(3);
				lcd.writeFromPgmspace(LOCALIZE(c_reset));
				break;
			default:
				break;
		}
	}

	lastCalibrationState = calibrationState;

	//Change the state
	//Some states are changed when a button is pressed via notifyButton
	//Some states are changed when something completes, in which case we do it here

	float interval = 2000.0;
	bool maximums = false;

	uint8_t endstops = eeprom::getEeprom8(eeprom::ENDSTOPS_USED, EEPROM_DEFAULT_ENDSTOPS_USED);

	float feedRate, stepsPerSecond;

	switch(calibrationState) {
		case CS_HOME_Z:
			//Declare current position to be x=0, y=0, z=0, a=0, b=0
			steppers::definePosition(Point(0,0,0,0,0), false);
			interval *= stepperAxisStepsToMM((int32_t)200.0, Z_AXIS); //Use ToM as baseline
			if ( endstops & 0x20 )	maximums = true;
			if ( endstops & 0x10 )	maximums = false;
			feedRate = (float)eeprom::getEepromUInt32(eeprom::HOMING_FEED_RATE_Z, EEPROM_DEFAULT_HOMING_FEED_RATE_Z) / 60.0;
			stepsPerSecond = feedRate * (float)stepperAxisMMToSteps(1.0, Z_AXIS);
			interval = 1000000.0 / stepsPerSecond;
			steppers::startHoming(maximums, 0x04, (uint32_t)interval);
			calibrationState = CS_HOME_Z_WAIT;
			break;
		case CS_HOME_Z_WAIT:
			if ( ! steppers::isHoming() )	calibrationState = CS_HOME_Y;
			break;
		case CS_HOME_Y:
			interval *= stepperAxisStepsToMM((int32_t)47.06, Y_AXIS); //Use ToM as baseline
			if ( endstops & 0x08 )	maximums = true;
			if ( endstops & 0x04 )	maximums = false;
			feedRate = (float)eeprom::getEepromUInt32(eeprom::HOMING_FEED_RATE_Y, EEPROM_DEFAULT_HOMING_FEED_RATE_Y) / 60.0;
			stepsPerSecond = feedRate * (float)stepperAxisMMToSteps(1.0, Y_AXIS);
			interval = 1000000.0 / stepsPerSecond;
			steppers::startHoming(maximums, 0x02, (uint32_t)interval);
			calibrationState = CS_HOME_Y_WAIT;
			break;
		case CS_HOME_Y_WAIT:
			if ( ! steppers::isHoming() )	calibrationState = CS_HOME_X;
			break;
		case CS_HOME_X:
			interval *= stepperAxisStepsToMM((int32_t)47.06, X_AXIS); //Use ToM as baseline
			if ( endstops & 0x02 )	maximums = true;
			if ( endstops & 0x01 )	maximums = false;
			feedRate = (float)eeprom::getEepromUInt32(eeprom::HOMING_FEED_RATE_X, EEPROM_DEFAULT_HOMING_FEED_RATE_X) / 60.0;
			stepsPerSecond = feedRate * (float)stepperAxisMMToSteps(1.0, X_AXIS);
			interval = 1000000.0 / stepsPerSecond;
			steppers::startHoming(maximums, 0x01, (uint32_t)interval);
			calibrationState = CS_HOME_X_WAIT;
			break;
		case CS_HOME_X_WAIT:
			if ( ! steppers::isHoming() ) {
				//Record current X, Y, Z, A, B co-ordinates to the motherboard
				for (uint8_t i = 0; i < STEPPER_COUNT; i++) {
					uint16_t offset = eeprom::AXIS_HOME_POSITIONS + 4*i;
					uint32_t position = steppers::getStepperPosition()[i];
					cli();
					eeprom_write_block(&position, (void*) offset, 4);
					sei();
				}

				//Disable stepps on axis 0, 1, 2, 3, 4
				steppers::enableAxis(0, false);
				steppers::enableAxis(1, false);
				steppers::enableAxis(2, false);
				steppers::enableAxis(3, false);
				steppers::enableAxis(4, false);

				calibrationState = CS_PROMPT_CALIBRATED;
			}
			break;
		default:
			break;
	}
}

void CalibrateMode::notifyButtonPressed(ButtonArray::ButtonName button) {

	if ( calibrationState == CS_PROMPT_CALIBRATED ) {
		host::stopBuildNow();
		return;
	}

	switch (button) {
 	        default:
			if (( calibrationState == CS_START1 ) || ( calibrationState == CS_START2 ) ||
			    (calibrationState == CS_PROMPT_MOVE ))	calibrationState = (enum calibrateState)((uint8_t)calibrationState + 1);
			break;
        	case ButtonArray::CANCEL:
               		interface::popScreen();
			break;
	}
}

void HomeOffsetsMode::reset() {
	homePosition = steppers::getStepperPosition();

	for (uint8_t i = 0; i < STEPPER_COUNT; i++) {
		uint16_t offset = eeprom::AXIS_HOME_POSITIONS + 4*i;
		cli();
		eeprom_read_block(&(homePosition[i]), (void*) offset, 4);
		sei();
	}

	lastHomeOffsetState = HOS_NONE;
	homeOffsetState	    = HOS_OFFSET_X;
}

void HomeOffsetsMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar ho_message1x[] = "X Offset:";
	const static PROGMEM prog_uchar ho_message1y[] = "Y Offset:";
	const static PROGMEM prog_uchar ho_message1z[] = "Z Offset:";

	if ( homeOffsetState != lastHomeOffsetState )	forceRedraw = true;

	if (forceRedraw) {
		lcd.clearHomeCursor();
		switch(homeOffsetState) {
			case HOS_OFFSET_X:
				lcd.writeFromPgmspace(LOCALIZE(ho_message1x));
				break;
                	case HOS_OFFSET_Y:
				lcd.writeFromPgmspace(LOCALIZE(ho_message1y));
				break;
                	case HOS_OFFSET_Z:
				lcd.writeFromPgmspace(LOCALIZE(ho_message1z));
				break;
			default:
				break;
		}

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	float position = 0.0;

	switch(homeOffsetState) {
		case HOS_OFFSET_X:
			position = stepperAxisStepsToMM(homePosition[0], X_AXIS);
			break;
		case HOS_OFFSET_Y:
			position = stepperAxisStepsToMM(homePosition[1], Y_AXIS);
			break;
		case HOS_OFFSET_Z:
			position = stepperAxisStepsToMM(homePosition[2], Z_AXIS);
			break;
		default:
			break;
	}

	lcd.setRow(1);
	lcd.writeFloat((float)position, 3);
	lcd.writeFromPgmspace(LOCALIZE(units_mm));

	lastHomeOffsetState = homeOffsetState;
}

void HomeOffsetsMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	if (( homeOffsetState == HOS_OFFSET_Z ) && (button == ButtonArray::OK )) {
		//Write the new home positions
		for (uint8_t i = 0; i < STEPPER_COUNT; i++) {
			uint16_t offset = eeprom::AXIS_HOME_POSITIONS + 4*i;
			uint32_t position = homePosition[i];
			cli();
			eeprom_write_block(&position, (void*) offset, 4);
			sei();
		}

		host::stopBuildNow();
		return;
	}

	uint8_t currentIndex = homeOffsetState - HOS_OFFSET_X;

	switch (button) {
		case ButtonArray::CANCEL:
			interface::popScreen();
			break;
		case ButtonArray::ZERO:
			break;
		case ButtonArray::OK:
			if 	( homeOffsetState == HOS_OFFSET_X )	homeOffsetState = HOS_OFFSET_Y;
			else if ( homeOffsetState == HOS_OFFSET_Y )	homeOffsetState = HOS_OFFSET_Z;
			break;
		case ButtonArray::ZPLUS:
			// increment more
			homePosition[currentIndex] += 20;
			break;
		case ButtonArray::ZMINUS:
			// decrement more
			homePosition[currentIndex] -= 20;
			break;
		case ButtonArray::YPLUS:
			// increment less
			homePosition[currentIndex] += 1;
			break;
		case ButtonArray::YMINUS:
			// decrement less
			homePosition[currentIndex] -= 1;
			break;
		case ButtonArray::XMINUS:
		case ButtonArray::XPLUS:
			break;
	}
}

void BuzzerSetRepeatsMode::reset() {
	repeats = eeprom::getEeprom8(eeprom::BUZZER_REPEATS, EEPROM_DEFAULT_BUZZER_REPEATS);
}

void BuzzerSetRepeatsMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar bsr_message1[] = "Repeat Buzzer:";
	const static PROGMEM prog_uchar bsr_message2[] = "(0=Buzzer Off)";
	const static PROGMEM prog_uchar bsr_times[]    = " times ";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(bsr_message1));

		lcd.setRow(1);
		lcd.writeFromPgmspace(LOCALIZE(bsr_message2));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	// Redraw tool info
	lcd.setRow(2);
	lcd.writeInt(repeats, 3);
	lcd.writeFromPgmspace(LOCALIZE(bsr_times));
}

void BuzzerSetRepeatsMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
		case ButtonArray::CANCEL:
			interface::popScreen();
			break;
		case ButtonArray::ZERO:
			break;
		case ButtonArray::OK:
			eeprom_write_byte((uint8_t *)eeprom::BUZZER_REPEATS, repeats);
			interface::popScreen();
			break;
		case ButtonArray::ZPLUS:
			// increment more
			if (repeats <= 249) repeats += 5;
			break;
		case ButtonArray::ZMINUS:
			// decrement more
			if (repeats >= 5) repeats -= 5;
			break;
		case ButtonArray::YPLUS:
			// increment less
			if (repeats <= 253) repeats += 1;
			break;
		case ButtonArray::YMINUS:
			// decrement less
			if (repeats >= 1) repeats -= 1;
			break;
		case ButtonArray::XMINUS:
		case ButtonArray::XPLUS:
			break;
	}
}

bool ExtruderFanMenu::isEnabled() {
	//Should really check the current status of the fan here
	return false;
}

void ExtruderFanMenu::enable(bool enabled) {
	OutPacket responsePacket;

	extruderControl(0, SLAVE_CMD_TOGGLE_FAN, EXTDR_CMD_SET, responsePacket, (uint16_t)((enabled)?1:0));
}

void ExtruderFanMenu::setupTitle() {
	static const PROGMEM prog_uchar ext_msg1[] = "Extruder Fan:";
	static const PROGMEM prog_uchar ext_msg2[] = "";
	msg1 = LOCALIZE(ext_msg1);
	msg2 = LOCALIZE(ext_msg2);
}

FilamentUsedResetMenu::FilamentUsedResetMenu() {
	itemCount = 4;
	reset();
}

void FilamentUsedResetMenu::resetState() {
	itemIndex = 2;
	firstItemIndex = 2;
}

void FilamentUsedResetMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar fur_msg[] = "Reset To Zero?";
	const static PROGMEM prog_uchar fur_no[]  = "No";
	const static PROGMEM prog_uchar fur_yes[] = "Yes";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(fur_msg));
		break;
	case 1:
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(fur_no));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(fur_yes));
		break;
	}
}

void FilamentUsedResetMenu::handleSelect(uint8_t index) {
	switch (index) {
	case 3:
		//Reset to zero
                eeprom::putEepromInt64(eeprom::FILAMENT_LIFETIME_A, EEPROM_DEFAULT_FILAMENT_LIFETIME);
                eeprom::putEepromInt64(eeprom::FILAMENT_LIFETIME_B, EEPROM_DEFAULT_FILAMENT_LIFETIME);
                eeprom::putEepromInt64(eeprom::FILAMENT_TRIP_A, EEPROM_DEFAULT_FILAMENT_TRIP);
                eeprom::putEepromInt64(eeprom::FILAMENT_TRIP_B, EEPROM_DEFAULT_FILAMENT_TRIP);
	case 2:
		interface::popScreen();
                interface::popScreen();
		break;
	}
}

void FilamentUsedMode::reset() {
	lifetimeDisplay = true;
	overrideForceRedraw = false;
}

void FilamentUsedMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar fu_lifetime[] = "Lifetime Odo.:";
	const static PROGMEM prog_uchar fu_trip[]     = "Trip Odometer:";
	const static PROGMEM prog_uchar fu_but_life[] = "(trip)   (reset)";
	const static PROGMEM prog_uchar fu_but_trip[] = "(life)   (reset)";

	if ((forceRedraw) || (overrideForceRedraw)) {
		lcd.clearHomeCursor();
		if ( lifetimeDisplay )	lcd.writeFromPgmspace(LOCALIZE(fu_lifetime));
		else			lcd.writeFromPgmspace(LOCALIZE(fu_trip));

	        int64_t filamentUsedA = eeprom::getEepromInt64(eeprom::FILAMENT_LIFETIME_A, EEPROM_DEFAULT_FILAMENT_LIFETIME);
	        int64_t filamentUsedB = eeprom::getEepromInt64(eeprom::FILAMENT_LIFETIME_B, EEPROM_DEFAULT_FILAMENT_LIFETIME);

		if ( ! lifetimeDisplay ) {
			int64_t tripA = eeprom::getEepromInt64(eeprom::FILAMENT_TRIP_A, EEPROM_DEFAULT_FILAMENT_TRIP);
			int64_t tripB = eeprom::getEepromInt64(eeprom::FILAMENT_TRIP_B, EEPROM_DEFAULT_FILAMENT_TRIP);
			filamentUsedA = filamentUsedA - tripA;	
			filamentUsedB = filamentUsedB - tripB;	
		}

		float filamentUsedMMA = stepperAxisStepsToMM(filamentUsedA, A_AXIS);
#if EXTRUDERS > 1
		float filamentUsedMMB = stepperAxisStepsToMM(filamentUsedB, B_AXIS);
#else
		float filamentUsedMMB = 0.0f;
#endif

		float filamentUsedMM = filamentUsedMMA + filamentUsedMMB;

		lcd.setRow(1);
		lcd.writeFloat(filamentUsedMM / 1000.0, 4);
		lcd.write('m');

		lcd.setRow(2);
		if ( lifetimeDisplay )	lcd.writeFromPgmspace(LOCALIZE(fu_but_life));
		else			lcd.writeFromPgmspace(LOCALIZE(fu_but_trip));

		lcd.setRow(3);
		lcd.writeFloat(((filamentUsedMM / 25.4) / 12.0), 4);
		lcd.writeString((char *)"ft");

		overrideForceRedraw = false;
	}
}

void FilamentUsedMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
		case ButtonArray::CANCEL:
			interface::popScreen();
			break;
		case ButtonArray::ZERO:
			lifetimeDisplay ^= true;
			overrideForceRedraw = true;
			break;
		case ButtonArray::OK:
			if ( lifetimeDisplay )
				interface::pushScreen(&filamentUsedResetMenu);
			else {
                		eeprom::putEepromInt64(eeprom::FILAMENT_TRIP_A, eeprom::getEepromInt64(eeprom::FILAMENT_LIFETIME_A, EEPROM_DEFAULT_FILAMENT_LIFETIME));
                		eeprom::putEepromInt64(eeprom::FILAMENT_TRIP_B, eeprom::getEepromInt64(eeprom::FILAMENT_LIFETIME_B, EEPROM_DEFAULT_FILAMENT_LIFETIME));
				interface::popScreen();
			}
			break;
	        default:
			break;
	}
}

BuildSettingsMenu::BuildSettingsMenu() {
	itemCount = 5;
	reset();
}

void BuildSettingsMenu::resetState() {
	acceleration = 1 == (eeprom::getEeprom8(eeprom::ACCELERATION_ON, EEPROM_DEFAULT_ACCELERATION_ON) & 0x01);
	itemCount = acceleration ? 8 : 7;
	itemIndex = 0;
	firstItemIndex = 0;
	genericOnOff_msg1 = 0;
	genericOnOff_msg2 = 0;
	genericOnOff_msg3 = LOCALIZE(generic_off);
	genericOnOff_msg4 = LOCALIZE(generic_on);
}

void BuildSettingsMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar bs_item0[] = "Override Temp";
	const static PROGMEM prog_uchar bs_item1[] = "Ditto Print";
	const static PROGMEM prog_uchar bs_item2[] = "Accel. On/Off";
	const static PROGMEM prog_uchar bs_item3[] = "Accel. Settings";
	const static PROGMEM prog_uchar bs_item4[] = "Extruder Hold";
	const static PROGMEM prog_uchar bs_item5[] = "Toolhead System";
	const static PROGMEM prog_uchar bs_item6[] = "SD Err Checking";
	const static PROGMEM prog_uchar bs_item7[] = "ABP Copies (SD)";

	if ( !acceleration && index > 2 ) ++index;

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(bs_item0));
		break;
	case 1:
		lcd.writeFromPgmspace(LOCALIZE(bs_item1));
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(bs_item2));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(bs_item3));
		break;
	case 4:
		lcd.writeFromPgmspace(LOCALIZE(bs_item4));
		break;
	case 5:
		lcd.writeFromPgmspace(LOCALIZE(bs_item5));
		break;
	case 6:
		lcd.writeFromPgmspace(LOCALIZE(bs_item6));
		break;
	case 7:
		lcd.writeFromPgmspace(LOCALIZE(bs_item7));
		break;
	}
}

void BuildSettingsMenu::handleSelect(uint8_t index) {
	OutPacket responsePacket;

	if ( !acceleration && index > 2 ) ++index;

	genericOnOff_msg3 = LOCALIZE(generic_off);
	genericOnOff_msg4 = LOCALIZE(generic_on);

	switch (index) {
	case 0:
	    //Override the gcode temperature
	    genericOnOff_offset  = eeprom::OVERRIDE_GCODE_TEMP;
	    genericOnOff_default = EEPROM_DEFAULT_OVERRIDE_GCODE_TEMP;
	    genericOnOff_msg1 = LOCALIZE(ogct_msg1);
	    genericOnOff_msg2 = LOCALIZE(ogct_msg2);
	    interface::pushScreen(&genericOnOffMenu);
	    break;
	case 1:
	    //Ditto Print
	    genericOnOff_offset  = eeprom::DITTO_PRINT_ENABLED;
	    genericOnOff_default = EEPROM_DEFAULT_DITTO_PRINT_ENABLED;
	    genericOnOff_msg1 = LOCALIZE(dp_msg1);
	    genericOnOff_msg2 = 0;
	    interface::pushScreen(&genericOnOffMenu);
	    break;
	case 2:
	    //Acceleraton On/Off Menu
	    genericOnOff_offset  = eeprom::ACCELERATION_ON;
	    genericOnOff_default = EEPROM_DEFAULT_ACCELERATION_ON;
	    genericOnOff_msg1 = LOCALIZE(aof_msg1);
	    genericOnOff_msg2 = LOCALIZE(aof_msg2);
	    interface::pushScreen(&genericOnOffMenu);
	    break;
	case 3:
	    interface::pushScreen(&acceleratedSettingsMode);
	    break;
	case 4:
	    //Extruder Hold
	    genericOnOff_offset  = eeprom::EXTRUDER_HOLD;
	    genericOnOff_default = EEPROM_DEFAULT_EXTRUDER_HOLD;
	    genericOnOff_msg1 = LOCALIZE(eof_msg1);
	    genericOnOff_msg2 = 0;
	    interface::pushScreen(&genericOnOffMenu);
	    break;
	case 5:
	    //Toolhead System
	    genericOnOff_offset  = eeprom::TOOLHEAD_OFFSET_SYSTEM;
	    genericOnOff_default = EEPROM_DEFAULT_TOOLHEAD_OFFSET_SYSTEM;
	    genericOnOff_msg1 = LOCALIZE(ts_msg1);
	    genericOnOff_msg2 = LOCALIZE(ts_msg2);
	    genericOnOff_msg3 = LOCALIZE(ts_old);
	    genericOnOff_msg4 = LOCALIZE(ts_new);
	    interface::pushScreen(&genericOnOffMenu);
	    break;
	case 6:
	    // SD card error checking
	    genericOnOff_offset  = eeprom::SD_USE_CRC;
	    genericOnOff_default = EEPROM_DEFAULT_SD_USE_CRC;
	    genericOnOff_msg1 = LOCALIZE(sdcrc_msg1);
	    genericOnOff_msg2 = LOCALIZE(sdcrc_msg2);
	    interface::pushScreen(&genericOnOffMenu);
	    break;
	case 7:
	    //Change number of ABP copies
	    interface::pushScreen(&abpCopiesSetScreen);
	    break;
	}
}

void ABPCopiesSetScreen::reset() {
	value = eeprom::getEeprom8(eeprom::ABP_COPIES, EEPROM_DEFAULT_ABP_COPIES);
	if ( value < 1 ) {
		eeprom_write_byte((uint8_t*)eeprom::ABP_COPIES,EEPROM_DEFAULT_ABP_COPIES);
		value = eeprom::getEeprom8(eeprom::ABP_COPIES, EEPROM_DEFAULT_ABP_COPIES); //Just in case
	}
}

void ABPCopiesSetScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar abp_message1[] = "ABP Copies (SD):";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(abp_message1));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	// Redraw tool info
	lcd.setRow(1);
	lcd.writeInt(value,3);
}

void ABPCopiesSetScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
        case ButtonArray::CANCEL:
		interface::popScreen();
		break;
        case ButtonArray::ZERO:
		break;
        case ButtonArray::OK:
		eeprom_write_byte((uint8_t*)eeprom::ABP_COPIES,value);
		interface::popScreen();
		interface::popScreen();
		break;
        case ButtonArray::ZPLUS:
		// increment more
		if (value <= 249) {
			value += 5;
		}
		break;
        case ButtonArray::ZMINUS:
		// decrement more
		if (value >= 6) {
			value -= 5;
		}
		break;
        case ButtonArray::YPLUS:
		// increment less
		if (value <= 253) {
			value += 1;
		}
		break;
        case ButtonArray::YMINUS:
		// decrement less
		if (value >= 2) {
			value -= 1;
		}
		break;
        default:
		break;
	}
}

GenericOnOffMenu::GenericOnOffMenu() {
    itemCount = 4;
    reset();
}

void GenericOnOffMenu::resetState() {
	uint8_t val = eeprom::getEeprom8(genericOnOff_offset, genericOnOff_default);
	bool state = (genericOnOff_offset == eeprom::SD_USE_CRC)  ? (val == 1) : (val != 0);
	itemIndex = state ? 3 : 2; 
	firstItemIndex = 2;
}

void GenericOnOffMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
    const prog_uchar *msg;   
    switch (index) {
    default :
	return;
    case 0:
	msg = genericOnOff_msg1;
	break;
    case 1:
	msg = genericOnOff_msg2;
	break;
    case 2:
	msg = genericOnOff_msg3;
	break;
    case 3:
	msg = genericOnOff_msg4;
	break;
    }
    if (msg) lcd.writeFromPgmspace(msg);
}

void GenericOnOffMenu::handleSelect(uint8_t index) {
    uint8_t oldValue = (eeprom::getEeprom8(genericOnOff_offset, genericOnOff_default)) != 0;
    uint8_t newValue = oldValue;

    switch (index) {
    default:
	return;
    case 2:  
	newValue = 0x00;
	break;
    case 3:
	newValue = 0x01;
	break;
    }

    interface::popScreen();
    //If the value has changed, do a reset
    if ( newValue != oldValue ) {
	cli();
	eeprom_write_byte((uint8_t*)genericOnOff_offset, newValue);
	sei();
	//Reset
#ifndef BROKEN_SD
	if ( genericOnOff_offset == eeprom::SD_USE_CRC )
	    sdcard::mustReinit = true;
	else
#endif
	    host::stopBuildNow();
    }
}


#define NUM_PROFILES 4
#define PROFILES_SAVED_AXIS 3

void writeProfileToEeprom(uint8_t pIndex, uint8_t *pName, int32_t homeX,
			  int32_t homeY, int32_t homeZ, uint8_t hbpTemp,
			  uint8_t tool0Temp, uint8_t tool1Temp, uint8_t extruderMMS) {
	uint16_t offset = eeprom::PROFILE_BASE + (uint16_t)pIndex * PROFILE_NEXT_OFFSET;

	cli();

	//Write profile name
	if ( pName )	eeprom_write_block(pName,(uint8_t*)offset, PROFILE_NAME_LENGTH);
	offset += PROFILE_NAME_LENGTH;

	//Write home axis
	eeprom_write_block(&homeX, (void*) offset, 4);		offset += 4;
	eeprom_write_block(&homeY, (void*) offset, 4);		offset += 4;
	eeprom_write_block(&homeZ, (void*) offset, 4);		offset += 4;

	//Write temps and extruder MMS
	eeprom_write_byte((uint8_t *)offset, hbpTemp);		offset += 1;
	eeprom_write_byte((uint8_t *)offset, tool0Temp);	offset += 1;
	eeprom_write_byte((uint8_t *)offset, tool1Temp);	offset += 1;
	eeprom_write_byte((uint8_t *)offset, extruderMMS);	offset += 1;
	
	sei();
}

void readProfileFromEeprom(uint8_t pIndex, uint8_t *pName, int32_t *homeX,
			   int32_t *homeY, int32_t *homeZ, uint8_t *hbpTemp,
			   uint8_t *tool0Temp, uint8_t *tool1Temp, uint8_t *extruderMMS) {
	uint16_t offset = eeprom::PROFILE_BASE + (uint16_t)pIndex * PROFILE_NEXT_OFFSET;

	cli();

	//Read profile name
	if ( pName )	eeprom_read_block(pName,(uint8_t*)offset, PROFILE_NAME_LENGTH);
	offset += PROFILE_NAME_LENGTH;

	//Write home axis
	eeprom_read_block(homeX, (void*) offset, 4);		offset += 4;
	eeprom_read_block(homeY, (void*) offset, 4);		offset += 4;
	eeprom_read_block(homeZ, (void*) offset, 4);		offset += 4;

	//Write temps and extruder MMS
	*hbpTemp	= eeprom_read_byte((uint8_t *)offset);	offset += 1;
	*tool0Temp	= eeprom_read_byte((uint8_t *)offset);	offset += 1;
	*tool1Temp	= eeprom_read_byte((uint8_t *)offset);	offset += 1;
	*extruderMMS	= eeprom_read_byte((uint8_t *)offset);	offset += 1;
	
	sei();
}

//buf should have length PROFILE_NAME_LENGTH + 1 

void getProfileName(uint8_t pIndex, uint8_t *buf) {
	uint16_t offset = eeprom::PROFILE_BASE + PROFILE_NEXT_OFFSET * (uint16_t)pIndex;

	cli();
	eeprom_read_block(buf,(void *)offset,PROFILE_NAME_LENGTH);
	sei();

	buf[PROFILE_NAME_LENGTH] = '\0';
}

#define NAME_CHAR_LOWER_LIMIT 32
#define NAME_CHAR_UPPER_LIMIT 126

bool isValidProfileName(uint8_t pIndex) {
	uint8_t buf[PROFILE_NAME_LENGTH + 1];

	getProfileName(pIndex, buf);
	for ( uint8_t i = 0; i < PROFILE_NAME_LENGTH; i ++ ) {
		if (( buf[i] < NAME_CHAR_LOWER_LIMIT ) || ( buf[i] > NAME_CHAR_UPPER_LIMIT ) || ( buf[i] == 0xff )) return false;
	}

	return true;
}

ProfilesMenu::ProfilesMenu() {
	itemCount = NUM_PROFILES;
	reset();

	//Setup defaults if required
	//If the value is 0xff, write the profile number
	uint8_t buf[PROFILE_NAME_LENGTH+1];
        const static PROGMEM prog_uchar pro_defaultProfile[] = "Profile?";

	//Get the home axis positions, we may need this to write the defaults
	homePosition = steppers::getStepperPosition();

	for (uint8_t i = 0; i < PROFILES_SAVED_AXIS; i++) {
		uint16_t offset = eeprom::AXIS_HOME_POSITIONS + 4*(uint16_t)i;
		cli();
		eeprom_read_block(&homePosition[i], (void*)offset, 4);
		sei();
	}

	for (int p = 0; p < NUM_PROFILES; p ++ ) {
		if ( ! isValidProfileName(p)) {
			//Create the default profile name
			for( uint8_t i = 0; i < PROFILE_NAME_LENGTH; i ++ )
				buf[i] = pgm_read_byte_near(pro_defaultProfile+i);
			buf[PROFILE_NAME_LENGTH - 1] = '1' + p;

			//Write the defaults
			writeProfileToEeprom(p, buf, homePosition[0], homePosition[1], homePosition[2],
					    100, 210, 210, 19);
		}
	}
}

void ProfilesMenu::resetState() {
	firstItemIndex = 0;
	itemIndex = 0;
}

void ProfilesMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	uint8_t buf[PROFILE_NAME_LENGTH + 1];

	getProfileName(index, buf);

	lcd.writeString((char *)buf);
}

void ProfilesMenu::handleSelect(uint8_t index) {
	profileSubMenu.profileIndex = index;
	interface::pushScreen(&profileSubMenu);
}

ProfileSubMenu::ProfileSubMenu() {
	itemCount = 4;
	reset();
}

void ProfileSubMenu::resetState() {
	itemIndex = 0;
	firstItemIndex = 0;
}

void ProfileSubMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar ps_msg1[]  = "Restore";
	const static PROGMEM prog_uchar ps_msg2[]  = "Display Config";
	const static PROGMEM prog_uchar ps_msg3[]  = "Change Name";
	const static PROGMEM prog_uchar ps_msg4[]  = "Save To Profile";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(ps_msg1));
		break;
	case 1:
		lcd.writeFromPgmspace(LOCALIZE(ps_msg2));
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(ps_msg3));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(ps_msg4));
		break;
	}
}

void ProfileSubMenu::handleSelect(uint8_t index) {
	uint8_t hbpTemp, tool0Temp, tool1Temp, extruderMMS;

	switch (index) {
		case 0:
			//Restore
			//Read settings from eeprom
			readProfileFromEeprom(profileIndex, NULL, &homePosition[0], &homePosition[1], &homePosition[2],
					      &hbpTemp, &tool0Temp, &tool1Temp, &extruderMMS);

			//Write out the home offsets
			for (uint8_t i = 0; i < PROFILES_SAVED_AXIS; i++) {
				uint16_t offset = eeprom::AXIS_HOME_POSITIONS + 4*(uint16_t)i;
				cli();
				eeprom_write_block(&homePosition[i], (void*)offset, 4);
				sei();
			}

			cli();
			eeprom_write_byte((uint8_t *)eeprom::PLATFORM_TEMP, hbpTemp);
			eeprom_write_byte((uint8_t *)eeprom::TOOL0_TEMP,    tool0Temp);
			eeprom_write_byte((uint8_t *)eeprom::TOOL1_TEMP,    tool1Temp);
			eeprom_write_byte((uint8_t *)eeprom::EXTRUDE_MMS,   extruderMMS);
			sei();

                	interface::popScreen();
                	interface::popScreen();

			//Reset
			host::stopBuildNow();
			break;
		case 1:
			//Display settings
			profileDisplaySettingsMenu.profileIndex = profileIndex;
			interface::pushScreen(&profileDisplaySettingsMenu);
			break;
		case 2:
			//Change Profile Name
			profileChangeNameMode.profileIndex = profileIndex;
			interface::pushScreen(&profileChangeNameMode);
			break;
		case 3: //Save To Profile 
			//Get the home axis positions
			homePosition = steppers::getStepperPosition();
			for (uint8_t i = 0; i < PROFILES_SAVED_AXIS; i++) {
				uint16_t offset = eeprom::AXIS_HOME_POSITIONS + 4*(uint16_t)i;
				cli();
				eeprom_read_block(&homePosition[i], (void*)offset, 4);
				sei();
			}

			hbpTemp		= eeprom::getEeprom8(eeprom::PLATFORM_TEMP, EEPROM_DEFAULT_PLATFORM_TEMP);
			tool0Temp	= eeprom::getEeprom8(eeprom::TOOL0_TEMP, EEPROM_DEFAULT_TOOL0_TEMP);
			tool1Temp	= eeprom::getEeprom8(eeprom::TOOL1_TEMP, EEPROM_DEFAULT_TOOL1_TEMP);
			extruderMMS	= eeprom::getEeprom8(eeprom::EXTRUDE_MMS, EEPROM_DEFAULT_EXTRUDE_MMS);

			writeProfileToEeprom(profileIndex, NULL, homePosition[0], homePosition[1], homePosition[2],
					     hbpTemp, tool0Temp, tool1Temp, extruderMMS);

                	interface::popScreen();
			break;
	}
}

void ProfileChangeNameMode::reset() {
	cursorLocation = 0;
	getProfileName(profileIndex, profileName);
}

void ProfileChangeNameMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar pcn_message1[] = "Profile Name:";
	const static PROGMEM prog_uchar pcn_blank[]    = " ";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(pcn_message1));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	lcd.setRow(1);
	lcd.writeString((char *)profileName);

	//Draw the cursor
	lcd.setCursor(cursorLocation,2);
	lcd.write('^');

	//Write a blank before and after the cursor if we're not at the ends
	if ( cursorLocation >= 1 ) {
		lcd.setCursor(cursorLocation-1, 2);
		lcd.writeFromPgmspace(LOCALIZE(pcn_blank));
	}
	if ( cursorLocation < PROFILE_NAME_LENGTH ) {
		lcd.setCursor(cursorLocation+1, 2);
		lcd.writeFromPgmspace(LOCALIZE(pcn_blank));
	}
}

void ProfileChangeNameMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	uint16_t offset;

	switch (button) {
		case ButtonArray::CANCEL:
			interface::popScreen();
			break;
		case ButtonArray::ZERO:
			break;
		case ButtonArray::OK:
			//Write the profile name
			offset = eeprom::PROFILE_BASE + (uint16_t)profileIndex * PROFILE_NEXT_OFFSET;

			cli();
			eeprom_write_block(profileName,(uint8_t*)offset, PROFILE_NAME_LENGTH);
			sei();

			interface::popScreen();
			break;
		case ButtonArray::YPLUS:
			profileName[cursorLocation] += 1;
			break;
		case ButtonArray::ZPLUS:
			profileName[cursorLocation] += 5;
			break;
		case ButtonArray::YMINUS:
			profileName[cursorLocation] -= 1;
			break;
		case ButtonArray::ZMINUS:
			profileName[cursorLocation] -= 5;
			break;
		case ButtonArray::XMINUS:
			if ( cursorLocation > 0 )			cursorLocation --;
			break;
		case ButtonArray::XPLUS:
			if ( cursorLocation < (PROFILE_NAME_LENGTH-1) )	cursorLocation ++;
			break;
	}

	//Hard limits
	if ( profileName[cursorLocation] < NAME_CHAR_LOWER_LIMIT )	profileName[cursorLocation] = NAME_CHAR_LOWER_LIMIT;
	if ( profileName[cursorLocation] > NAME_CHAR_UPPER_LIMIT )	profileName[cursorLocation] = NAME_CHAR_UPPER_LIMIT;
}

ProfileDisplaySettingsMenu::ProfileDisplaySettingsMenu() {
	itemCount = 8;
	reset();
}

void ProfileDisplaySettingsMenu::resetState() {
	readProfileFromEeprom(profileIndex, profileName, &homeX, &homeY, &homeZ,
			      &hbpTemp, &tool0Temp, &tool1Temp, &extruderMMS);
	itemIndex = 2;
	firstItemIndex = 2;
}

void ProfileDisplaySettingsMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar pds_xOffset[]     = "XOff: ";
	const static PROGMEM prog_uchar pds_yOffset[]     = "YOff: ";
	const static PROGMEM prog_uchar pds_zOffset[]     = "ZOff: ";
	const static PROGMEM prog_uchar pds_hbp[]         = "HBP Temp:   ";
	const static PROGMEM prog_uchar pds_tool0[]       = "Tool0 Temp: ";
	const static PROGMEM prog_uchar pds_extruder[]    = "ExtrdrMM/s: ";

	switch (index) {
	case 0:
		lcd.writeString((char *)profileName);
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(pds_xOffset));
		lcd.writeFloat(stepperAxisStepsToMM(homeX, X_AXIS), 3);
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(pds_yOffset));
		lcd.writeFloat(stepperAxisStepsToMM(homeY, Y_AXIS), 3);
		break;
	case 4:
		lcd.writeFromPgmspace(LOCALIZE(pds_zOffset));
		lcd.writeFloat(stepperAxisStepsToMM(homeZ, Z_AXIS), 3);
		break;
	case 5:
		lcd.writeFromPgmspace(LOCALIZE(pds_hbp));
		lcd.writeFloat((float)hbpTemp, 0);
		break;
	case 6:
		lcd.writeFromPgmspace(LOCALIZE(pds_tool0));
		lcd.writeFloat((float)tool0Temp, 0);
		break;
	case 7:
		lcd.writeFromPgmspace(LOCALIZE(pds_extruder));
		lcd.writeFloat((float)extruderMMS, 0);
		break;
	}
}

void ProfileDisplaySettingsMenu::handleSelect(uint8_t index) {
}

void CurrentPositionMode::reset() {
}

void CurrentPositionMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	uint8_t active_toolhead;
	Point position = steppers::getStepperPosition(&active_toolhead);

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.write('X');

		lcd.setRow(1);
		lcd.write('Y');

		lcd.setRow(2);
		lcd.write('Z');

		lcd.setRow(3);
		lcd.write('A' + active_toolhead);
	}
	lcd.write(':');

	lcd.setCursor(3, 0);
	lcd.writeFloat(stepperAxisStepsToMM(position[0], X_AXIS), 3);
	lcd.writeFromPgmspace(LOCALIZE(units_mm));

	lcd.setCursor(3, 1);
	lcd.writeFloat(stepperAxisStepsToMM(position[1], Y_AXIS), 3);
	lcd.writeFromPgmspace(LOCALIZE(units_mm));

	lcd.setCursor(3, 2);
	lcd.writeFloat(stepperAxisStepsToMM(position[2], Z_AXIS), 3);
	lcd.writeFromPgmspace(LOCALIZE(units_mm));

	lcd.setCursor(3, 3);
	lcd.writeFloat(stepperAxisStepsToMM(position[3 + active_toolhead], A_AXIS + active_toolhead), 3);
	lcd.writeFromPgmspace(LOCALIZE(units_mm));
}

void CurrentPositionMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	interface::popScreen();
}

		//Unable to open file, filename too long?
UnableToOpenFileMenu::UnableToOpenFileMenu() {
	itemCount = 4;
	reset();
}

void UnableToOpenFileMenu::resetState() {
	itemIndex = 3;
	firstItemIndex = 3;
}

void UnableToOpenFileMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar utof_msg1[]   = "Failed to open";
	const static PROGMEM prog_uchar utof_msg2[]   = "file or folder.";
	const static PROGMEM prog_uchar utof_msg3[]   = "Name too long?";
	const static PROGMEM prog_uchar utof_cont[]   = "Continue";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(utof_msg1));
		break;
	case 1:
		lcd.writeFromPgmspace(LOCALIZE(utof_msg2));
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(utof_msg3));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(utof_cont));
		break;
	}
}

void UnableToOpenFileMenu::handleSelect(uint8_t index) {
	interface::popScreen();
}


void AcceleratedSettingsMode::reset() {
	cli();
        values[0]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_X, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_X);
        values[1]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_Y, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_Y);
        values[2]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_Z, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_Z);
        values[3]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_A, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_A);
	values[4]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_B, EEPROM_DEFAULT_ACCEL_MAX_ACCELERATION_B);
        values[5]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_EXTRUDER_NORM, EEPROM_DEFAULT_ACCEL_MAX_EXTRUDER_NORM);
        values[6]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_EXTRUDER_RETRACT, EEPROM_DEFAULT_ACCEL_MAX_EXTRUDER_RETRACT);
	values[7]	= eeprom::getEepromUInt32(eeprom::ACCEL_ADVANCE_K, EEPROM_DEFAULT_ACCEL_ADVANCE_K);
	values[8]	= eeprom::getEepromUInt32(eeprom::ACCEL_ADVANCE_K2, EEPROM_DEFAULT_ACCEL_ADVANCE_K2);
	values[9]	= eeprom::getEepromUInt32(eeprom::ACCEL_EXTRUDER_DEPRIME_A, EEPROM_DEFAULT_ACCEL_EXTRUDER_DEPRIME_A);
	values[10]	= eeprom::getEepromUInt32(eeprom::ACCEL_EXTRUDER_DEPRIME_B, EEPROM_DEFAULT_ACCEL_EXTRUDER_DEPRIME_B);
        values[11]	= (uint32_t)(eeprom::getEeprom8(eeprom::ACCEL_SLOWDOWN_FLAG, EEPROM_DEFAULT_ACCEL_SLOWDOWN_FLAG)) ? 1 : 0;
	values[12]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_X, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_X);
	values[13]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_Y, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_Y);
	values[14]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_Z, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_Z);
	values[15]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_A, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_A);
	values[16]	= eeprom::getEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_B, EEPROM_DEFAULT_ACCEL_MAX_SPEED_CHANGE_B);

	sei();

	lastAccelerateSettingsState= AS_NONE;
	accelerateSettingsState= AS_MAX_ACCELERATION_X;
}

void AcceleratedSettingsMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar as_message1xMaxAccelRate[] 	= "X Max Accel:";
	const static PROGMEM prog_uchar as_message1yMaxAccelRate[] 	= "Y Max Accel:";
	const static PROGMEM prog_uchar as_message1zMaxAccelRate[] 	= "Z Max Accel:";
	const static PROGMEM prog_uchar as_message1aMaxAccelRate[] 	= "Right Max Accel:";
	const static PROGMEM prog_uchar as_message1bMaxAccelRate[] 	= "Left Max Accel:";
	const static PROGMEM prog_uchar as_message1ExtruderNorm[]  	= "Max Accel:";
	const static PROGMEM prog_uchar as_message1ExtruderRetract[]	= "Max Accel Extdr:";
	const static PROGMEM prog_uchar as_message1AdvanceK[]		= "JKN Advance K:";
	const static PROGMEM prog_uchar as_message1AdvanceK2[]		= "JKN Advance K2:";
	const static PROGMEM prog_uchar as_message1ExtruderDeprimeA[]	= "Extdr.DeprimeR:";
	const static PROGMEM prog_uchar as_message1ExtruderDeprimeB[]	= "Extdr.DeprimeL:";
	const static PROGMEM prog_uchar as_message1SlowdownLimit[]	= "SlowdownEnabled:";
	const static PROGMEM prog_uchar as_message1MaxSpeedChangeX[]	= "MaxSpeedChangeX:";
	const static PROGMEM prog_uchar as_message1MaxSpeedChangeY[]	= "MaxSpeedChangeY:";
	const static PROGMEM prog_uchar as_message1MaxSpeedChangeZ[]	= "MaxSpeedChangeZ:";
	const static PROGMEM prog_uchar as_message1MaxSpeedChangeA[]	= "MaxSpeedChangeR:";
	const static PROGMEM prog_uchar as_message1MaxSpeedChangeB[]	= "MaxSpeedChangeL:";
	const static PROGMEM prog_uchar as_blank[]			= "    ";

	if ( accelerateSettingsState != lastAccelerateSettingsState )	forceRedraw = true;

	if (forceRedraw) {
		lcd.clearHomeCursor();
		const prog_uchar *msg;
		switch(accelerateSettingsState) {
		case AS_MAX_ACCELERATION_X:
		    msg = LOCALIZE(as_message1xMaxAccelRate);
		    break;
		case AS_MAX_ACCELERATION_Y:
		    msg = LOCALIZE(as_message1yMaxAccelRate);
		    break;
		case AS_MAX_ACCELERATION_Z:
		    msg = LOCALIZE(as_message1zMaxAccelRate);
		    break;
		case AS_MAX_ACCELERATION_A:
		    msg = LOCALIZE(as_message1aMaxAccelRate);
		    break;
		case AS_MAX_ACCELERATION_B:
		    msg = LOCALIZE(as_message1bMaxAccelRate);
		    break;
		case AS_MAX_EXTRUDER_NORM:
		    msg = LOCALIZE(as_message1ExtruderNorm);
		    break;
		case AS_MAX_EXTRUDER_RETRACT:
		    msg = LOCALIZE(as_message1ExtruderRetract);
		    break;
		case AS_ADVANCE_K:
		    msg = LOCALIZE(as_message1AdvanceK);
		    break;
		case AS_ADVANCE_K2:
		    msg = LOCALIZE(as_message1AdvanceK2);
		    break;
		case AS_EXTRUDER_DEPRIME_A:
		    msg = LOCALIZE(as_message1ExtruderDeprimeA);
		    break;
		case AS_EXTRUDER_DEPRIME_B:
		    msg = LOCALIZE(as_message1ExtruderDeprimeB);
		    break;
		case AS_SLOWDOWN_FLAG:
		    msg = LOCALIZE(as_message1SlowdownLimit);
		    break;
		case AS_MAX_SPEED_CHANGE_X:
		    msg = LOCALIZE(as_message1MaxSpeedChangeX);
		    break;
		case AS_MAX_SPEED_CHANGE_Y:
		    msg = LOCALIZE(as_message1MaxSpeedChangeY);
		    break;
		case AS_MAX_SPEED_CHANGE_Z:
		    msg = LOCALIZE(as_message1MaxSpeedChangeZ);
		    break;
		case AS_MAX_SPEED_CHANGE_A:
		    msg = LOCALIZE(as_message1MaxSpeedChangeA);
		    break;
		case AS_MAX_SPEED_CHANGE_B:
		    msg = LOCALIZE(as_message1MaxSpeedChangeB);
		    break;
		default:
		    msg = 0;
		    break;
		}

		if ( msg ) lcd.writeFromPgmspace(msg);
		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	uint32_t value = 0;

	uint8_t currentIndex = accelerateSettingsState - AS_MAX_ACCELERATION_X;

	value = values[currentIndex];

	lcd.setRow(1);

	switch(accelerateSettingsState) {
		case AS_MAX_SPEED_CHANGE_X:
		case AS_MAX_SPEED_CHANGE_Y:
		case AS_MAX_SPEED_CHANGE_Z:
		case AS_MAX_SPEED_CHANGE_A:
		case AS_MAX_SPEED_CHANGE_B:
					lcd.writeFloat((float)value / 10.0, 1);
					break;
		case AS_ADVANCE_K:
		case AS_ADVANCE_K2:
					lcd.writeFloat((float)value / 100000.0, 5);
					break;
		default:
					lcd.writeFloat((float)value, 0);
					break;
	}
	lcd.writeFromPgmspace(LOCALIZE(as_blank));

	lastAccelerateSettingsState = accelerateSettingsState;
}

void AcceleratedSettingsMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	if (( accelerateSettingsState == AS_LAST_ENTRY ) && (button == ButtonArray::OK )) {
		//Write the data
		cli();
       		eeprom::putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_X,	 values[0]);
        	eeprom::putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_Y,	 values[1]);
        	eeprom::putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_Z,	 values[2]);
        	eeprom::putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_A,	 values[3]);
        	eeprom::putEepromUInt32(eeprom::ACCEL_MAX_ACCELERATION_B,	 values[4]);
        	eeprom::putEepromUInt32(eeprom::ACCEL_MAX_EXTRUDER_NORM,	 values[5]);
        	eeprom::putEepromUInt32(eeprom::ACCEL_MAX_EXTRUDER_RETRACT,	 values[6]);
		eeprom::putEepromUInt32(eeprom::ACCEL_ADVANCE_K,		 values[7]);
		eeprom::putEepromUInt32(eeprom::ACCEL_ADVANCE_K2,		 values[8]);
		eeprom::putEepromUInt32(eeprom::ACCEL_EXTRUDER_DEPRIME_A,	 values[9]);
		eeprom::putEepromUInt32(eeprom::ACCEL_EXTRUDER_DEPRIME_B,	 values[10]);
		eeprom_write_byte((uint8_t*)eeprom::ACCEL_SLOWDOWN_FLAG,(uint8_t)values[11]);
		eeprom::putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_X,	 values[12]);
		eeprom::putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_Y,	 values[13]);
		eeprom::putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_Z,	 values[14]);
		eeprom::putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_A,	 values[15]);
		eeprom::putEepromUInt32(eeprom::ACCEL_MAX_SPEED_CHANGE_B,	 values[16]);
		sei();

		host::stopBuildNow();
		return;
	}

	uint8_t currentIndex = accelerateSettingsState - AS_MAX_ACCELERATION_X;

	switch (button) {
		case ButtonArray::CANCEL:
			interface::popScreen();
			break;
		case ButtonArray::ZERO:
			break;
		case ButtonArray::OK:
			accelerateSettingsState = (enum accelerateSettingsState)((uint8_t)accelerateSettingsState + 1);
			return;
			break;
		case ButtonArray::ZPLUS:
			// increment more
			values[currentIndex] += 100;
			break;
		case ButtonArray::ZMINUS:
			// decrement more
			values[currentIndex] -= 100;
			break;
		case ButtonArray::YPLUS:
			// increment less
			values[currentIndex] += 1;
			break;
		case ButtonArray::YMINUS:
			// decrement less
			values[currentIndex] -= 1;
			break;
	        default:
			break;
	}

	//Settings that allow a zero value
	if (!(
	      ( accelerateSettingsState == AS_ADVANCE_K )	   || ( accelerateSettingsState == AS_ADVANCE_K2 ) || 
	      ( accelerateSettingsState == AS_EXTRUDER_DEPRIME_A ) || ( accelerateSettingsState == AS_EXTRUDER_DEPRIME_B ) ||
	      ( accelerateSettingsState == AS_SLOWDOWN_FLAG ))) {
		if ( values[currentIndex] < 1 )	values[currentIndex] = 1;
	}

	if ( values[currentIndex] > 200000 ) values[currentIndex] = 1;

	//Settings that have a maximum value
	if (( accelerateSettingsState == AS_SLOWDOWN_FLAG ) && ( values[currentIndex] > 1))
		values[currentIndex] = 1;
}

void EndStopConfigScreen::reset() {
	endstops = eeprom::getEeprom8(eeprom::ENDSTOPS_USED, EEPROM_DEFAULT_ENDSTOPS_USED);
}

void EndStopConfigScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar esc_message1[] = "EndstopsPresent:";
	const static PROGMEM prog_uchar esc_blank[]    = " ";

	if (forceRedraw) {
		lcd.clearHomeCursor();
		lcd.writeFromPgmspace(LOCALIZE(esc_message1));

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	// Redraw tool info
	lcd.setRow(1);
	lcd.writeFloat((float)endstops, 0);
	lcd.writeFromPgmspace(LOCALIZE(esc_blank));
}

void EndStopConfigScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
		case ButtonArray::CANCEL:
			interface::popScreen();
			break;
		case ButtonArray::ZERO:
			break;
		case ButtonArray::OK:
			eeprom_write_byte((uint8_t *)eeprom::ENDSTOPS_USED, endstops);
			interface::popScreen();
			break;
		case ButtonArray::ZPLUS:
			// increment more
			if (endstops <= 122) endstops += 5;
			break;
		case ButtonArray::ZMINUS:
			// decrement more
			if (endstops >= 5) endstops -= 5;
			break;
		case ButtonArray::YPLUS:
			// increment less
			if (endstops <= 126) endstops += 1;
			break;
		case ButtonArray::YMINUS:
			// decrement less
			if (endstops >= 1) endstops -= 1;
			break;
	        default:
			break;
	}
}

void HomingFeedRatesMode::reset() {
	cli();
	homingFeedRate[0] = eeprom::getEepromUInt32(eeprom::HOMING_FEED_RATE_X, EEPROM_DEFAULT_HOMING_FEED_RATE_X);
	homingFeedRate[1] = eeprom::getEepromUInt32(eeprom::HOMING_FEED_RATE_Y, EEPROM_DEFAULT_HOMING_FEED_RATE_Y);
	homingFeedRate[2] = eeprom::getEepromUInt32(eeprom::HOMING_FEED_RATE_Z, EEPROM_DEFAULT_HOMING_FEED_RATE_Z);
	sei();
	
	lastHomingFeedRateState = HFRS_NONE;
	homingFeedRateState	= HFRS_OFFSET_X;
}

void HomingFeedRatesMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar hfr_message1x[] = "X Home Feedrate:";
	const static PROGMEM prog_uchar hfr_message1y[] = "Y Home Feedrate:";
	const static PROGMEM prog_uchar hfr_message1z[] = "Z Home Feedrate:";
	const static PROGMEM prog_uchar hfr_mm[]        = "mm/min ";

	if ( homingFeedRateState != lastHomingFeedRateState )	forceRedraw = true;

	if (forceRedraw) {
		lcd.clearHomeCursor();
		switch(homingFeedRateState) {
			case HFRS_OFFSET_X:
				lcd.writeFromPgmspace(LOCALIZE(hfr_message1x));
				break;
                	case HFRS_OFFSET_Y:
				lcd.writeFromPgmspace(LOCALIZE(hfr_message1y));
				break;
                	case HFRS_OFFSET_Z:
				lcd.writeFromPgmspace(LOCALIZE(hfr_message1z));
				break;
			default:
				break;
		}

		lcd.setRow(3);
		lcd.writeFromPgmspace(LOCALIZE(updnset_msg));
	}

	float feedRate = 0.0;

	switch(homingFeedRateState) {
		case HFRS_OFFSET_X:
			feedRate = homingFeedRate[0];
			break;
		case HFRS_OFFSET_Y:
			feedRate = homingFeedRate[1];
			break;
		case HFRS_OFFSET_Z:
			feedRate = homingFeedRate[2];
			break;
		default:
			break;
	}

	lcd.setRow(1);
	lcd.writeFloat((float)feedRate, 0);
	lcd.writeFromPgmspace(LOCALIZE(hfr_mm));

	lastHomingFeedRateState = homingFeedRateState;
}

void HomingFeedRatesMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	if (( homingFeedRateState == HFRS_OFFSET_Z ) && (button == ButtonArray::OK )) {
		//Write the new homing feed rates
		cli();
		eeprom::putEepromUInt32(eeprom::HOMING_FEED_RATE_X, homingFeedRate[0]);
		eeprom::putEepromUInt32(eeprom::HOMING_FEED_RATE_Y, homingFeedRate[1]);
		eeprom::putEepromUInt32(eeprom::HOMING_FEED_RATE_Z, homingFeedRate[2]);
		sei();

		interface::popScreen();
	}

	uint8_t currentIndex = homingFeedRateState - HFRS_OFFSET_X;

	switch (button) {
		case ButtonArray::CANCEL:
			interface::popScreen();
			break;
		case ButtonArray::ZERO:
			break;
		case ButtonArray::OK:
			if 	( homingFeedRateState == HFRS_OFFSET_X )	homingFeedRateState = HFRS_OFFSET_Y;
			else if ( homingFeedRateState == HFRS_OFFSET_Y )	homingFeedRateState = HFRS_OFFSET_Z;
			break;
		case ButtonArray::ZPLUS:
			// increment more
			homingFeedRate[currentIndex] += 20;
			break;
		case ButtonArray::ZMINUS:
			// decrement more
			if ( homingFeedRate[currentIndex] >= 21 )
				homingFeedRate[currentIndex] -= 20;
			break;
		case ButtonArray::YPLUS:
			// increment less
			homingFeedRate[currentIndex] += 1;
			break;
		case ButtonArray::YMINUS:
			// decrement less
			if ( homingFeedRate[currentIndex] >= 2 )
				homingFeedRate[currentIndex] -= 1;
			break;
	        default:
			break;
	}

	if (( homingFeedRate[currentIndex] < 1 ) || ( homingFeedRate[currentIndex] > 2000 ))
		homingFeedRate[currentIndex] = 1;
}

#ifdef EEPROM_MENU_ENABLE

EepromMenu::EepromMenu() {
	itemCount = 3;
	reset();
}

void EepromMenu::resetState() {
	itemIndex = 0;
	firstItemIndex = 0;
	safetyGuard = 0;
	itemSelected = -1;
	warningScreen = true;
}

void EepromMenu::update(LiquidCrystal& lcd, bool forceRedraw) {
	if ( warningScreen ) {
		if ( forceRedraw ) {
			const static PROGMEM prog_uchar eeprom_msg1[]	= "This menu can";
			const static PROGMEM prog_uchar eeprom_msg2[]  	= "make your bot";
			const static PROGMEM prog_uchar eeprom_msg3[]  	= "inoperable.";
			const static PROGMEM prog_uchar eeprom_msg4[]  	= "Press Y+ to cont";

			lcd.homeCursor();
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg1));

			lcd.setRow(1);
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg2));

			lcd.setRow(2);
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg3));

			lcd.setRow(3);
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg4));
		}
	}
	else {
		if ( itemSelected != -1 )
			lcd.clearHomeCursor();

		const static PROGMEM prog_uchar eeprom_message_dump[]		= "Saving...";
		const static PROGMEM prog_uchar eeprom_message_restore[]	= "Restoring...";

		switch ( itemSelected ) {
			case 0:	//Dump
				//sdcard::forceReinit(); // to force return to / directory
				if ( ! sdcard::fileExists(dumpFilename) ) {
					lcd.writeFromPgmspace(LOCALIZE(eeprom_message_dump));
					if ( ! eeprom::saveToSDFile(dumpFilename) )
						timedMessage(lcd, 1);
				} else
					timedMessage(lcd, 2);
				interface::popScreen();
				break;

			case 1: //Restore
				//sdcard::forceReinit(); // to return to root
				if ( sdcard::fileExists(dumpFilename) ) {
					lcd.writeFromPgmspace(LOCALIZE(eeprom_message_restore));
					if ( ! eeprom::restoreFromSDFile(dumpFilename) )
						timedMessage(lcd, 3);
					host::stopBuildNow();
				} else {
					timedMessage(lcd, 4);
					interface::popScreen();
				}
				break;

			case 2: //Erase
				timedMessage(lcd, 5);
				eeprom::erase();
				interface::popScreen();
				host::stopBuildNow();
				break;
			default:
				Menu::update(lcd, forceRedraw);
				break;
		}

		lcd.setRow(3);
		if ( safetyGuard >= 1 ) {
			const static PROGMEM prog_uchar eeprom_msg9[]  = "* Press ";
			const static PROGMEM prog_uchar eeprom_msg10[] = "x more!";

			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg9));
			lcd.writeInt((uint16_t)(4-safetyGuard),1);
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg10));
		} else {
			const static PROGMEM prog_uchar eeprom_blank[] = "                ";
			lcd.writeFromPgmspace(LOCALIZE(eeprom_blank));
		}

		itemSelected = -1;
	}
}

void EepromMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar e_message_dump[]       	= "EEPROM -> SD";
	const static PROGMEM prog_uchar e_message_restore[]	= "SD -> EEPROM";
	const static PROGMEM prog_uchar e_message_erase[]      	= "Erase EEPROM";
	switch (index)
	{
		case 0:
			lcd.writeFromPgmspace(LOCALIZE(e_message_dump));
			break;
		case 1:
			lcd.writeFromPgmspace(LOCALIZE(e_message_restore));
			break;
		case 2:
			lcd.writeFromPgmspace(LOCALIZE(e_message_erase));
			break;
	}
}

void EepromMenu::handleSelect(uint8_t index) {
	switch (index)
	{
		case 0:
			//Dump
			safetyGuard = 0;
			itemSelected = 0;
			break;
		case 1:
			//Restore
			safetyGuard ++;
			if ( safetyGuard > 3 ) {
				safetyGuard = 0;
				itemSelected = 1;
			}
			break;
		case 2:
			//Erase
			safetyGuard ++;
			if ( safetyGuard > 3 ) {
				safetyGuard = 0;
				itemSelected = 2;
			}
			break;
	}
}

void EepromMenu::notifyButtonPressed(ButtonArray::ButtonName button) {
	if ( warningScreen ) {
		if ( button == ButtonArray::YPLUS )
			warningScreen = false;
		else
			Menu::notifyButtonPressed(ButtonArray::CANCEL);
		return;
	}

        if ( button == ButtonArray::YMINUS || button == ButtonArray::ZMINUS ||
	     button == ButtonArray::YPLUS  || button == ButtonArray::ZPLUS )
		safetyGuard = 0;

	Menu::notifyButtonPressed(button);
}


#endif

// Clear the screen, display a message, and then count down from 5 to 0 seconds
//   with a countdown timer to let folks know what's up

static void timedMessage(LiquidCrystal& lcd, uint8_t which)
{
	const static PROGMEM prog_uchar sderr_badcard[]   = "SD card error";
	const static PROGMEM prog_uchar sderr_nofiles[]   = "No files found";
	const static PROGMEM prog_uchar sderr_nocard[]    = "No SD card";
	const static PROGMEM prog_uchar sderr_initfail[]  = "Card init failed";
	const static PROGMEM prog_uchar sderr_partition[] = "Bad partition";
	const static PROGMEM prog_uchar sderr_filesys[]   = "Is it FAT-16?";
	const static PROGMEM prog_uchar sderr_noroot[]    = "No root folder";
	const static PROGMEM prog_uchar sderr_locked[]    = "Read locked";
	const static PROGMEM prog_uchar sderr_fnf[]       = "File not found";
	const static PROGMEM prog_uchar sderr_toobig[]    = "Too big";
	const static PROGMEM prog_uchar sderr_crcerr[]    = "CRC error";
	const static PROGMEM prog_uchar sderr_comms[]     = "Comms failure";

	const static PROGMEM prog_uchar eeprom_msg11[]  = "Write Failed!";
	const static PROGMEM prog_uchar eeprom_msg12[]  = "File exists!";
	const static PROGMEM prog_uchar eeprom_msg5[]   = "Read Failed!";
	const static PROGMEM prog_uchar eeprom_msg6[]   = "EEPROM may be";
	const static PROGMEM prog_uchar eeprom_msg7[]   = "corrupt";
	const static PROGMEM prog_uchar eeprom_msg8[]   = "File not found!";
	const static PROGMEM prog_uchar eeprom_message_erase[] = "Erasing...";
	const static PROGMEM prog_uchar eeprom_message_error[] = "Error";
	const static PROGMEM prog_uchar timed_message_clock[]  = "00:00";

	lcd.clearHomeCursor();

	switch(which) {
	case 0:
	{
	    lcd.writeFromPgmspace(LOCALIZE(sderr_badcard));
	    const prog_uchar *msg = 0;
	    if ( sdcard::sdErrno == SDR_ERR_BADRESPONSE ||
		 sdcard::sdErrno == SDR_ERR_COMMS ||
		 sdcard::sdErrno == SDR_ERR_PATTERN ||
		 sdcard::sdErrno == SDR_ERR_VOLTAGE )
		    msg = LOCALIZE(sderr_comms);
	    else {
		    switch (sdcard::sdAvailable) {
		    case sdcard::SD_SUCCESS:             msg = LOCALIZE(sderr_nofiles); break;
		    case sdcard::SD_ERR_NO_CARD_PRESENT: msg = LOCALIZE(sderr_nocard); break;
		    case sdcard::SD_ERR_INIT_FAILED:     msg = LOCALIZE(sderr_initfail); break;
		    case sdcard::SD_ERR_PARTITION_READ:  msg = LOCALIZE(sderr_partition); break;
		    case sdcard::SD_ERR_OPEN_FILESYSTEM: msg = LOCALIZE(sderr_filesys); break;
		    case sdcard::SD_ERR_NO_ROOT:         msg = LOCALIZE(sderr_noroot); break;
		    case sdcard::SD_ERR_CARD_LOCKED:     msg = LOCALIZE(sderr_locked); break;
		    case sdcard::SD_ERR_FILE_NOT_FOUND:  msg = LOCALIZE(sderr_fnf); break;
		    case sdcard::SD_ERR_VOLUME_TOO_BIG:  msg = LOCALIZE(sderr_toobig); break;
		    case sdcard::SD_ERR_CRC:             msg = LOCALIZE(sderr_crcerr); break;
		    default:
		    case sdcard::SD_ERR_GENERIC:
			    break;
		    }
	    }
	    if ( msg ) {
		lcd.setRow(1);
		lcd.writeFromPgmspace(msg);
	    }
	    break;
	}
	case 1:
		lcd.writeFromPgmspace(LOCALIZE(eeprom_msg11));
		break;

	case 2:
		lcd.writeFromPgmspace(LOCALIZE(eeprom_message_error));
		lcd.setRow(1);
		lcd.writeString((char *)dumpFilename);
		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(eeprom_msg12));
		break;

	case 3:
		lcd.writeFromPgmspace(LOCALIZE(eeprom_msg5));
		lcd.setRow(1);
		lcd.writeFromPgmspace(LOCALIZE(eeprom_msg6));
		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(eeprom_msg7));
		break;

	case 4:
		lcd.writeFromPgmspace(LOCALIZE(eeprom_message_error));
		lcd.setRow(1);
		lcd.writeString((char *)dumpFilename);
		lcd.setRow(2);
		lcd.writeFromPgmspace(LOCALIZE(eeprom_msg8));
		break;

	case 5:
		lcd.writeFromPgmspace(LOCALIZE(eeprom_message_erase));
		break;
	}

	lcd.setRow(3);
	lcd.writeFromPgmspace(timed_message_clock);
	for (uint8_t i = 0; i < 5; i++)
	{
		lcd.setCursor(4, 3);
		lcd.write('5' - (char)i);
		_delay_us(1000000);
	}
}
#endif

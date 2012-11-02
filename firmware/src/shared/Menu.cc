// Future things that could be consolidated into 1 to save code space when required:
//
// ValueSetScreen
// BuzzerSetRepeatsMode
// ABPCopiesSetScreen

#include "Menu.hh"
#include "Configuration.hh"

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

#define HOST_PACKET_TIMEOUT_MS 20
#define HOST_PACKET_TIMEOUT_MICROS (1000L*HOST_PACKET_TIMEOUT_MS)

#define HOST_TOOL_RESPONSE_TIMEOUT_MS 50
#define HOST_TOOL_RESPONSE_TIMEOUT_MICROS (1000L*HOST_TOOL_RESPONSE_TIMEOUT_MS)

#define MAX_ITEMS_PER_SCREEN 4

#define LCD_TYPE_CHANGE_BUTTON_HOLD_TIME 10.0

int16_t overrideExtrudeSeconds = 0;

Point homePosition;

//Macros to expand SVN revision macro into a str
#define STR_EXPAND(x) #x	//Surround the supplied macro by double quotes
#define STR(x) STR_EXPAND(x)

void VersionMode::reset() {
}

void strcat(char *buf, const char* str)
{
	char *ptr = buf;
	while (*ptr) ptr++;
	while (*str) *ptr++ = *str++;
	*ptr++ = '\0';
}


int appendTime(char *buf, uint8_t buflen, uint32_t val)
{
	bool hasdigit = false;
	uint8_t idx = 0;
	uint8_t written = 0;

	if (buflen < 1) {
		return written;
	}

	while (idx < buflen && buf[idx]) idx++;
	if (idx >= buflen-1) {
		buf[buflen-1] = '\0';
		return written;
	}

	uint8_t radidx = 0;
	const uint8_t radixcount = 5;
	const uint8_t houridx = 2;
	const uint8_t minuteidx = 4;
	uint32_t radixes[radixcount] = {360000, 36000, 3600, 600, 60};
	if (val >= 3600000) {
		val %= 3600000;
	}
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
		} else {
			buf[idx++] = ' ';
		}
		if (idx >= buflen) {
			buf[buflen-1] = '\0';
			return written;
		}
		written++;
		if (radidx == houridx) {
			buf[idx++] = 'h';
			if (idx >= buflen) {
				buf[buflen-1] = '\0';
				return written;
			}
			written++;
		}
		if (radidx == minuteidx) {
			buf[idx++] = 'm';
			if (idx >= buflen) {
				buf[buflen-1] = '\0';
				return written;
			}
			written++;
		}
	}

	if (idx < buflen) {
		buf[idx] = '\0';
	} else {
		buf[buflen-1] = '\0';
	}

	return written;
}



int appendUint8(char *buf, uint8_t buflen, uint8_t val)
{
	bool hasdigit = false;
	uint8_t written = 0;
	uint8_t idx = 0;

	if (buflen < 1) {
		return written;
	}

	while (idx < buflen && buf[idx]) idx++;
	if (idx >= buflen-1) {
		buf[buflen-1] = '\0';
		return written;
	}

	if (val >= 100) {
		uint8_t res = val / 100;
		val -= res * 100;
		buf[idx++] = '0' + res;
		if (idx >= buflen) {
			buf[buflen-1] = '\0';
			return written;
		}
		hasdigit = true;
		written++;
	} else {
		buf[idx++] = ' ';
		if (idx >= buflen) {
			buf[buflen-1] = '\0';
			return written;
		}
		written++;
	}

	if (val >= 10 || hasdigit) {
		uint8_t res = val / 10;
		val -= res * 10;
		buf[idx++] = '0' + res;
		if (idx >= buflen) {
			buf[buflen-1] = '\0';
			return written;
		}
		hasdigit = true;
		written++;
	} else {
		buf[idx++] = ' ';
		if (idx >= buflen) {
			buf[buflen-1] = '\0';
			return written;
		}
		written++;
	}

	buf[idx++] = '0' + val;
	if (idx >= buflen) {
		buf[buflen-1] = '\0';
		return written;
	}
	written++;

	if (idx < buflen) {
		buf[idx] = '\0';
	} else {
		buf[buflen-1] = '\0';
	}

	return written;
}



void SplashScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar splash1[] = "  Sailfish FW   ";
	const static PROGMEM prog_uchar splash2[] = " -------------- ";
	const static PROGMEM prog_uchar splash3[] = "Thing 32084 4.0 ";
	const static PROGMEM prog_uchar splash4[] = " Revision: ____ "; 

	if (forceRedraw) {
		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(splash1));

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(LOCALIZE(splash2));

		lcd.setCursor(0,2);
		lcd.writeFromPgmspace(LOCALIZE(splash3));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(splash4));
		lcd.setCursor(11,3);
                lcd.writeString((char *)STR(SVN_VERSION));
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
	case 2:
		// Model View
		eeprom_write_byte((uint8_t *)eeprom::JOG_MODE_SETTINGS, (jogModeSettings & (uint8_t)0xFE));
		interface::popScreen();
		break;
	case 3:
		// User View
		eeprom_write_byte((uint8_t *)eeprom::JOG_MODE_SETTINGS, (jogModeSettings | (uint8_t)0x01));
                interface::popScreen();
		break;
	}
}

void JoggerMenu::jog(ButtonArray::ButtonName direction, bool pauseModeJog) {
	Point position = steppers::getStepperPosition();
	int32_t interval = 1000;

	float	speed = 1.5;	//In mm's

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

			float mms = (float)eeprom::getEeprom8(eeprom::EXTRUDE_MMS, EEPROM_DEFAULT_EXTRUDE_MMS);
			stepsPerSecond = mms * (float)stepperAxisMMToSteps(1.0, A_AXIS);
			interval = (int32_t)(1000000.0 / stepsPerSecond);

			//Handle reverse
			if ( direction == ButtonArray::OK )	stepsPerSecond *= -1;

			if ( ! ACCELERATION_EXTRUDE_WHEN_NEGATIVE_A )	stepsPerSecond *= -1;

			//Extrude for 0.5 seconds
			position[3] += (int32_t)(0.5 * stepsPerSecond);
			break;
	}

	if ( jogDistance == DISTANCE_CONT )	lastDirectionButtonPressed = direction;
	else					lastDirectionButtonPressed = (ButtonArray::ButtonName)0;

	if ( eepromLocation != 0 ) {
		//60.0, because feed rate is in mm/min units, we convert to seconds
		float feedRate = (float)eeprom::getEepromUInt32(eepromLocation, 500) / 60.0;
		stepsPerSecond = feedRate * (float)stepperAxisMMToSteps(1.0, axisIndex);
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
	if (cursor == BUF_SIZE-1) {
		message[BUF_SIZE-1] = '\0';
	} else {
		message[cursor] = '\0';
	}
}


void MessageScreen::addMessage(const prog_uchar msg[]) {

	cursor += strlcpy_P(message + cursor, (const prog_char *)msg, BUF_SIZE - cursor);

	// ensure that message is always null-terminated
	if (cursor == BUF_SIZE) {
		message[BUF_SIZE-1] = '\0';
	} else {
		message[cursor] = '\0';
	}
}

#if 0

void MessageScreen::addMessage(char msg[]) {

	char* letter = msg;
	while (*letter != 0) {
		message[cursor++] = *letter;
		letter++;
	}

	// ensure that message is always null-terminated
	if (cursor == BUF_SIZE) {
		message[BUF_SIZE-1] = '\0';
	} else {
		message[cursor] = '\0';
	}
}

#endif

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
		lcd.clear();
		lcd.setCursor(0,0);
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

		lcd.setCursor(0,1);
		if ( userViewMode )	lcd.writeFromPgmspace(LOCALIZE(j_jog2_user));
		else			lcd.writeFromPgmspace(LOCALIZE(j_jog2));

		lcd.setCursor(0,2);
		if ( userViewMode )	lcd.writeFromPgmspace(LOCALIZE(j_jog3_user));
		else			lcd.writeFromPgmspace(LOCALIZE(j_jog3));

		lcd.setCursor(0,3);
		if ( userViewMode )	lcd.writeFromPgmspace(LOCALIZE(j_jog4_user));
		else			lcd.writeFromPgmspace(LOCALIZE(j_jog4));

		distanceChanged = false;
		userViewModeChanged    = false;
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
        case ButtonArray::YMINUS:
        case ButtonArray::ZMINUS:
        case ButtonArray::YPLUS:
        case ButtonArray::ZPLUS:
        case ButtonArray::XMINUS:
        case ButtonArray::XPLUS:
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
		lcd.clear();
		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(e_extrude1));

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(LOCALIZE(e_extrude2));

		lcd.setCursor(0,2);
		lcd.writeFromPgmspace(LOCALIZE(e_extrude3));

		lcd.setCursor(0,3);
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

	// Redraw tool info
	switch (updatePhase) {
	case 0:
		lcd.setCursor(0,3);
		if (extruderControl(0, SLAVE_CMD_GET_TEMP, EXTDR_CMD_GET, responsePacket, 0)) {
			uint16_t data = responsePacket.read16(1);
			lcd.writeInt(data,3);
		} else {
			lcd.writeString((char *)"XXX");
		}
		break;

	case 1:
		lcd.setCursor(4,3);
		if (extruderControl(0, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0)) {
			uint16_t data = responsePacket.read16(1);
			lcd.writeInt(data,3);
		} else {
			lcd.writeString((char *)"XXX");
		}
		break;
	}

	updatePhase++;
	if (updatePhase > 1) {
		updatePhase = 0;
	}
}

void ExtruderMode::extrude(int32_t seconds, bool overrideTempCheck) {
	//Check we're hot enough
	if ( ! overrideTempCheck )
	{
		OutPacket responsePacket;
		if (extruderControl(0, SLAVE_CMD_IS_TOOL_READY, EXTDR_CMD_GET, responsePacket, 0)) {
			uint8_t data = responsePacket.read8(1);
		
			if ( ! data )
			{
				overrideExtrudeSeconds = seconds;
				interface::pushScreen(&extruderTooColdMenu);
				return;
			}
		}
	}

	Point position = steppers::getStepperPosition();

	float mms = (float)eeprom::getEeprom8(eeprom::EXTRUDE_MMS, EEPROM_DEFAULT_EXTRUDE_MMS);
	float stepsPerSecond = mms * (float)stepperAxisMMToSteps(1.0, A_AXIS);
	int32_t interval = (int32_t)(1000000.0 / stepsPerSecond);

	//Handle 5D
	float direction = 1.0;
	if ( ACCELERATION_EXTRUDE_WHEN_NEGATIVE_A )	direction = -1.0;

	if ( seconds == 0 )	steppers::abort();
	else {
		position[3] += direction * seconds * stepsPerSecond;
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

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(etc_warning));
		break;
	case 1:
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(etc_cancel));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(etc_override));
		break;
	}
}

void ExtruderTooColdMenu::handleCancel() {
	overrideExtrudeSeconds = 0;
	interface::popScreen();
}

void ExtruderTooColdMenu::handleSelect(uint8_t index) {
	switch (index) {
	case 2:
		// Cancel extrude
		overrideExtrudeSeconds = 0;
		interface::popScreen();
		break;
	case 3:
		// Override and extrude
                interface::popScreen();
		break;
	}
}

void MoodLightMode::reset() {
	updatePhase = 0;
	scriptId = eeprom_read_byte((uint8_t *)eeprom::MOOD_LIGHT_SCRIPT);
}

void MoodLightMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar ml_mood1[]   = "Mood: ";
	const static PROGMEM prog_uchar ml_mood3_1[] = "(set RGB)";
	const static PROGMEM prog_uchar ml_msg4[]    = "Up/Dn/Ent to Set";
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
				lcd.clear();
				lcd.setCursor(0,0);
				lcd.writeFromPgmspace(LOCALIZE(ml_moodNotPresent1));
				lcd.setCursor(0,1);
				lcd.writeFromPgmspace(LOCALIZE(ml_moodNotPresent2));
				lcd.setCursor(0,2);
				lcd.writeFromPgmspace(LOCALIZE(ml_moodNotPresent3));
				lcd.setCursor(0,3);
				lcd.writeFromPgmspace(LOCALIZE(ml_moodNotPresent4));
			}
		
			return;
		}
	}

	if (forceRedraw) {
		lcd.clear();
		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(ml_mood1));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(ml_msg4));
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
		lcd.setCursor(0, 2);
		if ( scriptId == 1 )	lcd.writeFromPgmspace(LOCALIZE(ml_mood3_1));
		else			lcd.writeFromPgmspace(LOCALIZE(ml_blank));	
		break;
	}

	updatePhase++;
	if (updatePhase > 1) {
		updatePhase = 0;
	}
}



void MoodLightMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	if ( ! interface::moodLightController().blinkM.blinkMIsPresent )	interface::popScreen();

	uint8_t i;

	switch (button) {
        	case ButtonArray::OK:
			eeprom_write_byte((uint8_t *)eeprom::MOOD_LIGHT_SCRIPT, scriptId);
               		interface::popScreen();
			break;

        	case ButtonArray::ZERO:
			if ( scriptId == 1 )
			{
				//Set RGB Values
                        	interface::pushScreen(&moodLightSetRGBScreen);
			}
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

        	case ButtonArray::XMINUS:
        	case ButtonArray::XPLUS:
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
	const static PROGMEM prog_uchar mlsrgb_message4[] = "Up/Dn/Ent to Set";

	if ((forceRedraw) || (redrawScreen)) {
		lcd.clear();

		lcd.setCursor(0,0);
		if      ( inputMode == 0 ) lcd.writeFromPgmspace(LOCALIZE(mlsrgb_message1_red));
		else if ( inputMode == 1 ) lcd.writeFromPgmspace(LOCALIZE(mlsrgb_message1_green));
		else if ( inputMode == 2 ) lcd.writeFromPgmspace(LOCALIZE(mlsrgb_message1_blue));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(mlsrgb_message4));

		redrawScreen = false;
	}


	// Redraw tool info
	lcd.setCursor(0,1);
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
	pauseMode.autoPause = false;
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
	const static PROGMEM prog_uchar mon_zpos_mm[]	 	 =   "mm";
	const static PROGMEM prog_uchar mon_filament[]           =   "Filament:0.00m  ";
	const static PROGMEM prog_uchar mon_copies[]		 =   "Copy:           ";
	const static PROGMEM prog_uchar mon_of[]		 =   " of ";
	const static PROGMEM prog_uchar mon_error[]		 =   "error!";
	char buf[17];

	if ( command::isPaused() ) {
		if ( ! pausePushLockout ) {
			pausePushLockout = true;
			pauseMode.autoPause = true;
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
		lcd.clear();
		lcd.setCursor(0,0);
		switch(host::getHostState()) {
		case host::HOST_STATE_READY:
			lcd.writeString(host::getMachineName());
			break;
		case host::HOST_STATE_BUILDING:
		case host::HOST_STATE_BUILDING_FROM_SD:
			lcd.writeString(host::getBuildName());
			lcd.setCursor(0,1);
			lcd.writeFromPgmspace(LOCALIZE(mon_completed_percent));
			break;
		case host::HOST_STATE_ERROR:
			lcd.writeFromPgmspace(LOCALIZE(mon_error));
			break;
		case host::HOST_STATE_CANCEL_BUILD :
			break;
		}

		lcd.setCursor(0,2);
		lcd.writeFromPgmspace(LOCALIZE(mon_extruder_temp));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(mon_platform_temp));

		lcd.setCursor(15,3);
		if ( command::getPauseAtZPos() == 0 )	lcd.write(' ');
		else					lcd.write('*');
	}

	overrideForceRedraw = false;

	//Flash the temperature indicators
	toggleHeating ^= true;

	if ( flashingTool ) {
		lcd.setCursor(5,2);
		lcd.write(toggleHeating?LCD_CUSTOM_CHAR_EXTRUDER_NORMAL:LCD_CUSTOM_CHAR_EXTRUDER_HEATING);
	}
	if ( flashingPlatform ) {
		lcd.setCursor(5,3);
		lcd.write(toggleHeating?LCD_CUSTOM_CHAR_PLATFORM_NORMAL:LCD_CUSTOM_CHAR_PLATFORM_HEATING);
	}

	OutPacket responsePacket;

	// Redraw tool info
	switch (updatePhase) {
	case UPDATE_PHASE_TOOL_TEMP:
		lcd.setCursor(7,2);
		if (extruderControl(0, SLAVE_CMD_GET_TEMP, EXTDR_CMD_GET, responsePacket, 0)) {
			uint16_t data = responsePacket.read16(1);
			lcd.writeInt(data,3);
		} else {
			lcd.writeString((char *)"XXX");
		}
		break;

	case UPDATE_PHASE_TOOL_TEMP_SET_POINT:
		lcd.setCursor(11,2);
		uint16_t data;
		data = 0;
		if (extruderControl(0, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0)) {
			data = responsePacket.read16(1);
			lcd.writeInt(data,3);
		} else {
			lcd.writeString((char *)"XXX");
		}

		lcd.setCursor(5,2);
		if (extruderControl(0, SLAVE_CMD_IS_TOOL_READY, EXTDR_CMD_GET, responsePacket, 0)) {
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
			lcd.writeInt(data,3);
		} else {
			lcd.writeString((char *)"XXX");
		}
		break;

	case UPDATE_PHASE_PLATFORM_SET_POINT:
		lcd.setCursor(11,3);
		data = 0;
		if (extruderControl(0, SLAVE_CMD_GET_PLATFORM_SP, EXTDR_CMD_GET, responsePacket, 0)) {
			data = responsePacket.read16(1);
			lcd.writeInt(data,3);
		} else {
			lcd.writeString((char *)"XXX");
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
		if ( command::getPauseAtZPos() == 0 )	lcd.write(' ');
		else					lcd.write('*');
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
				lcd.setCursor(0,1);
				lcd.writeFromPgmspace(LOCALIZE(mon_completed_percent));
				lcd.setCursor(11,1);
				buf[0] = '\0';

				completedPercent = (float)command::getBuildPercentage();

				appendUint8(buf, sizeof(buf), (uint8_t)completedPercent);
				strcat(buf, "% ");
				lcd.writeString(buf);
				break;
			case BUILD_TIME_PHASE_ELAPSED_TIME:
				lcd.setCursor(0,1);
				lcd.writeFromPgmspace(LOCALIZE(mon_elapsed_time));
				lcd.setCursor(9,1);
				buf[0] = '\0';

				if ( host::isBuildComplete() ) secs = lastElapsedSeconds; //We stop counting elapsed seconds when we are done
				else {
					lastElapsedSeconds = Motherboard::getBoard().getCurrentSeconds();
					secs = lastElapsedSeconds;
				}
				appendTime(buf, sizeof(buf), (uint32_t)secs);
				lcd.writeString(buf);
				break;
			case BUILD_TIME_PHASE_TIME_LEFT:
				tsecs = command::estimatedTimeLeftInSeconds();

				if ( tsecs > 0 ) {
					lcd.setCursor(0,1);
					lcd.writeFromPgmspace(LOCALIZE(mon_time_left));
					lcd.setCursor(9,1);

					buf[0] = '\0';
					if 	  ((tsecs > 0 ) && (tsecs < 60) && ( host::isBuildComplete() ) ) {
						appendUint8(buf, sizeof(buf), (uint8_t)tsecs);
						lcd.writeString(buf);
						lcd.writeFromPgmspace(LOCALIZE(mon_time_left_secs));	
					} else if (( tsecs <= 0) || ( host::isBuildComplete()) ) {
#ifdef HAS_FILAMENT_COUNTER
						command::addFilamentUsed();
#endif
						lcd.writeFromPgmspace(LOCALIZE(mon_time_left_none));
					} else {
						appendTime(buf, sizeof(buf), (uint32_t)tsecs);
						lcd.writeString(buf);
					}
					break;
				}
				//We can't display the time left, so we drop into ZPosition instead
				else	buildTimePhase = (enum BuildTimePhase)((uint8_t)buildTimePhase + 1);

			case BUILD_TIME_PHASE_ZPOS:
				lcd.setCursor(0,1);
				lcd.writeFromPgmspace(LOCALIZE(mon_zpos));
				lcd.setCursor(6,1);

				position = steppers::getStepperPosition();
			
				//Divide by the axis steps to mm's
				lcd.writeFloat(stepperAxisStepsToMM(position[2], Z_AXIS), 3);

				lcd.writeFromPgmspace(LOCALIZE(mon_zpos_mm));
				break;
			case BUILD_TIME_PHASE_FILAMENT:
				lcd.setCursor(0,1);
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
				lcd.setCursor(0,1);
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
				lcd.setCursor(0,1);
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
        lcd.setCursor(0, 0);
        lcd.writeString((char *)"DOS1: ");
        lcd.writeFloat(debug_onscreen1, 3);
        lcd.writeString((char *)" ");

        lcd.setCursor(0, 1);
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
	const static PROGMEM prog_uchar v_version1[] = "Motherboard: _._";
	const static PROGMEM prog_uchar v_version2[] = "   Extruder: _._";
	const static PROGMEM prog_uchar v_version3[] = "   Revision:___";
	const static PROGMEM prog_uchar v_version4[] = "FreeSram: ";

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(v_version1));

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(LOCALIZE(v_version2));

		lcd.setCursor(0,2);
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

		lcd.setCursor(12, 2);
		lcd.writeString((char *)STR(SVN_VERSION));

		lcd.setCursor(0,3);
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
		lcd.setCursor(0,(lastDrawIndex%height));
		lcd.write(' ');
	}

	lcd.setCursor(0,(itemIndex%height));
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


CancelBuildMenu::CancelBuildMenu() {
	pauseMode.autoPause = false;
	itemCount = 6;
	reset();
	pauseDisabled = false;
	if (( steppers::isHoming() ) || (sdcard::getPercentPlayed() >= 100.0))	pauseDisabled = true;

	if ( host::isBuildComplete() )
		printAnotherEnabled = true;
	else	printAnotherEnabled = false;

}

void CancelBuildMenu::resetState() {
	pauseMode.autoPause = false;
	pauseDisabled = false;	
	if (( steppers::isHoming() ) || (sdcard::getPercentPlayed() >= 100.0))	pauseDisabled = true;

	if ( host::isBuildComplete() )
		printAnotherEnabled = true;
	else	printAnotherEnabled = false;

	if ( pauseDisabled )	{
		itemIndex = 2;
		itemCount = 4;
	} else {
		itemIndex = 1;
		itemCount = 6;
	}

	if ( printAnotherEnabled ) {
		itemIndex = 1;
	}

	firstItemIndex = itemIndex;
}


void CancelBuildMenu::update(LiquidCrystal& lcd, bool forceRedraw) {
	if ( command::isPaused() )	interface::popScreen();
	else				Menu::update(lcd, forceRedraw);
}


void CancelBuildMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar cb_choose[]		= "Please Choose:";
	const static PROGMEM prog_uchar cb_abort[]		= "Abort Print   ";
	const static PROGMEM prog_uchar cb_printAnother[]	= "Print Another";
	const static PROGMEM prog_uchar cb_pauseZ[]		= "Pause at ZPos ";
	const static PROGMEM prog_uchar cb_pause[]		= "Pause         ";
	const static PROGMEM prog_uchar cb_pauseNoHeat[]	= "Pause No Heat ";
	const static PROGMEM prog_uchar cb_back[]		= "Continue Build";

	if (( steppers::isHoming() ) || (sdcard::getPercentPlayed() >= 100.0))	pauseDisabled = true;

	//Implement variable length menu
	uint8_t lind = 0;

	if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_choose));
	lind ++;

	if (( pauseDisabled ) && ( ! printAnotherEnabled )) lind ++;

	if ( index == lind)	lcd.writeFromPgmspace(LOCALIZE(cb_abort));
	lind ++;

	if ( printAnotherEnabled ) {
		if ( index == lind ) lcd.writeFromPgmspace(LOCALIZE(cb_printAnother));
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_pauseZ));
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_pause));
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_pauseNoHeat));
		lind ++;
	}

	if ( index == lind )	lcd.writeFromPgmspace(LOCALIZE(cb_back));
	lind ++;
}

void CancelBuildMenu::handleSelect(uint8_t index) {
	//Implement variable length menu
	uint8_t lind = 0;

	if (( pauseDisabled ) && ( ! printAnotherEnabled )) lind ++;

	lind ++;

	if ( index == lind) {
#ifdef HAS_FILAMENT_COUNTER
		command::addFilamentUsed();
#endif
		// Cancel build, returning to whatever menu came before monitor mode.
		// TODO: Cancel build.
		interface::popScreen();
		host::stopBuild();
	}
	lind ++;

	if ( printAnotherEnabled ) {
		if ( index == lind ) {
			command::buildAnotherCopy();
			interface::popScreen();
		}
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind )	interface::pushScreen(&pauseAtZPosScreen);
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind ) {
			command::pause(true, false);
			pauseMode.autoPause = false;
			interface::pushScreen(&pauseMode);
		}
		lind ++;
	}

	if ( ! pauseDisabled ) {
		if ( index == lind ) {
			command::pause(true, true);
			pauseMode.autoPause = false;
			interface::pushScreen(&pauseMode);
		}
		lind ++;
	}

	if ( index == lind ) {
		// Don't cancel print, just close dialog.
                interface::popScreen();
	}
	lind ++;
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

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(main_monitor));
		break;
	case 1:
		lcd.writeFromPgmspace(LOCALIZE(main_build));
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(main_jog));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(main_preheat));
		break;
	case 4:
		lcd.writeFromPgmspace(LOCALIZE(main_extruder));
		break;
	case 5:
		lcd.writeFromPgmspace(LOCALIZE(main_homeAxis));
		break;
	case 6:
		lcd.writeFromPgmspace(LOCALIZE(main_advanceABP));
		break;
	case 7:
		lcd.writeFromPgmspace(LOCALIZE(main_steppersS));
		break;
	case 8:
		lcd.writeFromPgmspace(LOCALIZE(main_moodlight));
		break;
	case 9:
		lcd.writeFromPgmspace(LOCALIZE(main_buzzer));
		break;
	case 10:
		lcd.writeFromPgmspace(LOCALIZE(main_buildSettings));
		break;
	case 11:
		lcd.writeFromPgmspace(LOCALIZE(main_profiles));
		break;
	case 12:
		lcd.writeFromPgmspace(LOCALIZE(main_extruderFan));
		break;
	case 13:
		lcd.writeFromPgmspace(LOCALIZE(main_calibrate));
		break;
	case 14:
		lcd.writeFromPgmspace(LOCALIZE(main_homeOffsets));
		break;
	case 15:
		lcd.writeFromPgmspace(LOCALIZE(main_filamentUsed));
		break;
	case 16:
		lcd.writeFromPgmspace(LOCALIZE(main_currentPosition));
		break;
	case 17:
		lcd.writeFromPgmspace(LOCALIZE(main_endStops));
		break;
	case 18:
		lcd.writeFromPgmspace(LOCALIZE(main_homingRates));
		break;
	case 19:
		lcd.writeFromPgmspace(LOCALIZE(main_versions));
		break;
#ifdef EEPROM_MENU_ENABLE
	case 20:
		lcd.writeFromPgmspace(LOCALIZE(main_eeprom));
		break;
#endif
	}
}


void MainMenu::handleSelect(uint8_t index) {
	switch (index) {
		case 0:
			// Show monitor build screen
                        interface::pushScreen(&monitorMode);
			break;
		case 1:
			// Show build from SD screen
                        interface::pushScreen(&sdMenu);
			break;
		case 2:
			// Show build from SD screen
                        interface::pushScreen(&jogger);
			break;
		case 3:
			// Show preheat menu
			interface::pushScreen(&preheatMenu);
			preheatMenu.fetchTargetTemps();
			break;
		case 4:
			// Show extruder menu
			interface::pushScreen(&extruderMenu);
			break;
		case 5:
			// Show home axis
			interface::pushScreen(&homeAxisMode);
			break;
		case 6:
			// Show advance ABP
			interface::pushScreen(&advanceABPMode);
			break;
		case 7:
			// Show steppers menu
			interface::pushScreen(&steppersMenu);
			break;
		case 8:
			// Show Mood Light Mode
                        interface::pushScreen(&moodLightMode);
			break;
		case 9: 
			// Show Buzzer Mode
			interface::pushScreen(&buzzerSetRepeats);
			break;
		case 10: 
			// Show Build Settings Mode
			interface::pushScreen(&buildSettingsMenu);
			break;
		case 11: 
			// Show Profiles Menu
			interface::pushScreen(&profilesMenu);
			break;
		case 12: 
			// Show Extruder Fan Mode
			interface::pushScreen(&extruderFanMenu);
			break;
		case 13:
			// Show Calibrate Mode
                        interface::pushScreen(&calibrateMode);
			break;
		case 14:
			// Show Home Offsets Mode
                        interface::pushScreen(&homeOffsetsMode);
			break;
		case 15:
			// Show Filament Used Mode
                        interface::pushScreen(&filamentUsedMode);
			break;
		case 16:
			// Show Current Position Mode
                        interface::pushScreen(&currentPositionMode);
			break;
		case 17:
			// Show test end stops menu
			interface::pushScreen(&testEndStopsMode);
			break;
		case 18:
			// Show Homing Rates Menu
			interface::pushScreen(&homingFeedRatesMode);
			break;
		case 19:
			// Show build from SD screen
                        interface::pushScreen(&versionMode);
			break;
#ifdef EEPROM_MENU_ENABLE
		case 20:
			//Eeprom Menu
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

SDMenu::SDMenu() {
	reset();
	updatePhase = 0;
	drawItemLockout = false;
}

void SDMenu::resetState() {
	itemCount = countFiles();
	updatePhase = 0;
	lastItemIndex = 0;
	drawItemLockout = false;
}

// Count the number of files on the SD card
uint8_t SDMenu::countFiles() {
	uint8_t count = 0;

	sdcard::SdErrorCode e;

	// First, reset the directory index
	e = sdcard::directoryReset();
	if (e != sdcard::SD_SUCCESS) {
		// TODO: Report error
		return 6;
	}

	const int MAX_FILE_LEN = 2;
	char fnbuf[MAX_FILE_LEN];

	// Count the files
	do {
		e = sdcard::directoryNextEntry(fnbuf,MAX_FILE_LEN);
		if (fnbuf[0] == '\0') {
			break;
		}

		// If it's a dot file, don't count it.
		if (fnbuf[0] == '.') {
		}
		else {
			count++;
		}
	} while (e == sdcard::SD_SUCCESS);

	// TODO: Check for error again?

	return count;
}

bool SDMenu::getFilename(uint8_t index, char buffer[], uint8_t buffer_size) {
	sdcard::SdErrorCode e;

	// First, reset the directory list
	e = sdcard::directoryReset();
	if (e != sdcard::SD_SUCCESS) {
                return false;
	}


	for(uint8_t i = 0; i < index+1; i++) {
		// Ignore dot-files
		do {
			e = sdcard::directoryNextEntry(buffer,buffer_size);
			if (buffer[0] == '\0') {
                                return false;
			}
		} while (e == sdcard::SD_SUCCESS && buffer[0] == '.');

		if (e != sdcard::SD_SUCCESS) {
                        return false;
		}
	}

        return true;
}

void SDMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	if (index > itemCount - 1) {
		// TODO: report error
		return;
	}

	const uint8_t MAX_FILE_LEN = host::MAX_FILE_LEN;
	char fnbuf[MAX_FILE_LEN];

        if ( !getFilename(index, fnbuf, MAX_FILE_LEN) ) {
                // TODO: report error
		return;
	}

	//Figure out length of filename
	uint8_t filenameLength;
	for (filenameLength = 0; (filenameLength < MAX_FILE_LEN) && (fnbuf[filenameLength] != 0); filenameLength++) ;

	uint8_t idx;
	uint8_t longFilenameOffset = 0;
	uint8_t displayWidth = lcd.getDisplayWidth() - 1;

	//Support scrolling filenames that are longer than the lcd screen
	if (filenameLength >= displayWidth) longFilenameOffset = updatePhase % (filenameLength - displayWidth + 1);

       for (idx = 0; (idx < displayWidth) && ((longFilenameOffset + idx) < MAX_FILE_LEN) &&
                        (fnbuf[longFilenameOffset + idx] != 0); idx++)
		lcd.write(fnbuf[longFilenameOffset + idx]);

	//Clear out the rest of the line
	while ( idx < displayWidth ) {
		lcd.write(' ');
		idx ++;
	}
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

	Menu::update(lcd, forceRedraw);

	updatePhase ++;
}

void SDMenu::notifyButtonPressed(ButtonArray::ButtonName button) {
	updatePhase = 0;
	Menu::notifyButtonPressed(button);
}

void SDMenu::handleSelect(uint8_t index) {
	if (host::getHostState() != host::HOST_STATE_READY) {
		// TODO: report error
		return;
	}

	drawItemLockout = true;

	char* buildName = host::getBuildName();

        if ( !getFilename(index, buildName, host::MAX_FILE_LEN) ) {
		// TODO: report error
		return;
	}

        sdcard::SdErrorCode e;
	e = host::startBuildFromSD();
	if (e != sdcard::SD_SUCCESS) {
		// TODO: report error
		interface::pushScreen(&unableToOpenFileMenu);
		return;
	}
}

void ValueSetScreen::reset() {
	value = eeprom::getEeprom8(location, default_value);
}

void ValueSetScreen::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar vs_message4[] = "Up/Dn/Ent to Set";

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(message1);

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(vs_message4));
	}


	// Redraw tool info
	lcd.setCursor(0,1);
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
	if (extruderControl(0, SLAVE_CMD_GET_SP, EXTDR_CMD_GET, responsePacket, 0)) {
		tool0Temp = responsePacket.read16(1);
	}
	if (extruderControl(0, SLAVE_CMD_GET_PLATFORM_SP, EXTDR_CMD_GET, responsePacket, 0)) {
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

	OutPacket responsePacket;
	switch (index) {
		case 0:
			// Toggle Extruder heater on/off
			if (tool0Temp > 0) {
				extruderControl(0, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, 0);
			} else {
				uint8_t value = eeprom::getEeprom8(eeprom::TOOL0_TEMP, EEPROM_DEFAULT_TOOL0_TEMP);
				extruderControl(0, SLAVE_CMD_SET_TEMP, EXTDR_CMD_SET, responsePacket, (uint16_t)value);
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
				extruderControl(0, SLAVE_CMD_SET_PLATFORM_TEMP, EXTDR_CMD_SET, responsePacket, value);
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
		lcd.clear();
		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(ha_home1));

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(LOCALIZE(ha_home2));

		lcd.setCursor(0,2);
		lcd.writeFromPgmspace(LOCALIZE(ha_home3));

		lcd.setCursor(0,3);
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
        	case ButtonArray::YMINUS:
        	case ButtonArray::ZMINUS:
        	case ButtonArray::YPLUS:
        	case ButtonArray::ZPLUS:
        	case ButtonArray::XMINUS:
        	case ButtonArray::XPLUS:
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
	const static PROGMEM prog_uchar ed_disable[] =  "Disable";
	const static PROGMEM prog_uchar ed_enable[]  =  "Enable";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(msg1);
		break;
	case 1:
		if ( msg2 ) lcd.writeFromPgmspace(msg2);
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(ed_disable));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(ed_enable));
		break;
	}
}

void EnabledDisabledMenu::handleSelect(uint8_t index) {
	if ( index == 2 ) enable(false);
	if ( index == 3 ) enable(true);
	interface::popScreen();
}

bool SteppersMenu::isEnabled() {
	if (( stepperAxisIsEnabled(0) ) ||
	    ( stepperAxisIsEnabled(1) ) ||
	    ( stepperAxisIsEnabled(2) ) ||
	    ( stepperAxisIsEnabled(3) )) return true;
	return false;
}

void SteppersMenu::enable(bool enabled) {
	steppers::enableAxis(0, enabled);
	steppers::enableAxis(1, enabled);
	steppers::enableAxis(2, enabled);
	steppers::enableAxis(3, enabled);
}

void SteppersMenu::setupTitle() {
	const static PROGMEM prog_uchar step_msg1[] = "Stepper Motors:";
	const static PROGMEM prog_uchar step_msg2[] = "";
	msg1 = LOCALIZE(step_msg1);
	msg2 = LOCALIZE(step_msg2);
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
		lcd.clear();
		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(tes_test1));

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(LOCALIZE(tes_test2));

		lcd.setCursor(0,2);
		lcd.writeFromPgmspace(LOCALIZE(tes_test3));

		lcd.setCursor(0,3);
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
	switch (button) {
        	case ButtonArray::YMINUS:
        	case ButtonArray::ZMINUS:
        	case ButtonArray::YPLUS:
        	case ButtonArray::ZPLUS:
        	case ButtonArray::XMINUS:
        	case ButtonArray::XPLUS:
        	case ButtonArray::ZERO:
        	case ButtonArray::OK:
        	case ButtonArray::CANCEL:
               		interface::popScreen();
			break;
	}
}

void PauseMode::reset() {
	lastDirectionButtonPressed = (ButtonArray::ButtonName)0;
	lastPauseState = PAUSE_STATE_NONE;
	userViewMode = eeprom::getEeprom8(eeprom::JOG_MODE_SETTINGS, EEPROM_DEFAULT_JOG_MODE_SETTINGS) & 0x01;
}

void PauseMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar p_waitForCurrentCommand[] = "Entering pause..";
	const static PROGMEM prog_uchar p_retractFilament[]	  = "Retract filament";
	const static PROGMEM prog_uchar p_clearingBuild[]	  = "Clearing build..";
	const static PROGMEM prog_uchar p_heating[]		  = "Heating...      ";
	const static PROGMEM prog_uchar p_leavingPaused[]	  = "Leaving pause.. ";
	const static PROGMEM prog_uchar p_paused1[] 		  = "Paused(";
	const static PROGMEM prog_uchar p_paused2[] 		  = "   Y+         Z+";
	const static PROGMEM prog_uchar p_paused3[] 		  = "X- Rev X+  (Fwd)";
	const static PROGMEM prog_uchar p_paused4[] 		  = "   Y-         Z-";

	enum PauseState pauseState = command::pauseState();

	if ( forceRedraw || ( lastPauseState != pauseState) )	lcd.clear();

	OutPacket responsePacket;

	lcd.setCursor(0,0);

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
			break;
		case PAUSE_STATE_PAUSED:
			lcd.writeFromPgmspace(LOCALIZE(p_paused1));
			lcd.writeFloat(stepperAxisStepsToMM(command::getPausedPosition()[Z_AXIS], Z_AXIS), 3);
			lcd.writeString((char *)"):");

			lcd.setCursor(0,1);
			lcd.writeFromPgmspace(LOCALIZE(p_paused2));
			lcd.setCursor(0,2);
			lcd.writeFromPgmspace(LOCALIZE(p_paused3));
			lcd.setCursor(0,3);
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
		case PAUSE_STATE_NONE:
			//Pop off the pause screen and the menu underneath to return to the monitor screen
                	interface::popScreen();
			if ( ! autoPause ) interface::popScreen();
			break;
	}

	if ( lastDirectionButtonPressed ) {
		if (interface::isButtonPressed(lastDirectionButtonPressed))
			jog(lastDirectionButtonPressed, true);
		else {
			lastDirectionButtonPressed = (ButtonArray::ButtonName)0;
			steppers::abort();
		}
	}

	lastPauseState = pauseState;
}

void PauseMode::notifyButtonPressed(ButtonArray::ButtonName button) {
	if ( command::pauseState() == PAUSE_STATE_PAUSED ) {
		if ( button == ButtonArray::CANCEL )	host::pauseBuild(false);
		else					jog(button, true);
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
	const static PROGMEM prog_uchar pz_message4[] = "Up/Dn/Ent to Set";
	const static PROGMEM prog_uchar pz_mm[]       = "mm   ";

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(pz_message1));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(pz_message4));
	}

	// Redraw tool info
	lcd.setCursor(0,1);
	lcd.writeFloat((float)pauseAtZPos, 3);
	lcd.writeFromPgmspace(LOCALIZE(pz_mm));
}

void PauseAtZPosScreen::notifyButtonPressed(ButtonArray::ButtonName button) {
	switch (button) {
		case ButtonArray::ZERO:
			break;
		case ButtonArray::OK:
			//Set the pause
			command::pauseAtZPos(stepperAxisMMToSteps(pauseAtZPos, Z_AXIS));
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
		case ButtonArray::XMINUS:
		case ButtonArray::XPLUS:
			break;
	}

	if ( pauseAtZPos < 0.001 )	pauseAtZPos = 0.0;
}

void AdvanceABPMode::reset() {
	abpForwarding = false;
}

void AdvanceABPMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	const static PROGMEM prog_uchar abp_msg1[] = "Advance ABP:";
	const static PROGMEM prog_uchar abp_msg2[] = "hold key...";
	const static PROGMEM prog_uchar abp_msg3[] = "           (fwd)";

	if (forceRedraw) {
		lcd.clear();
		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(abp_msg1));

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(LOCALIZE(abp_msg2));

		lcd.setCursor(0,2);
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
        	case ButtonArray::YMINUS:
        	case ButtonArray::ZMINUS:
        	case ButtonArray::YPLUS:
        	case ButtonArray::ZPLUS:
        	case ButtonArray::XMINUS:
        	case ButtonArray::XPLUS:
        	case ButtonArray::ZERO:
        	case ButtonArray::CANCEL:
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
		lcd.clear();
		lcd.setCursor(0,0);
		switch(calibrationState) {
			case CS_START1:
				lcd.writeFromPgmspace(LOCALIZE(c_calib1));
				lcd.setCursor(0,1);
				lcd.writeFromPgmspace(LOCALIZE(c_calib2));
				lcd.setCursor(0,2);
				lcd.writeFromPgmspace(LOCALIZE(c_calib3));
				lcd.setCursor(0,3);
				lcd.writeFromPgmspace(LOCALIZE(c_calib4));
				break;
			case CS_START2:
				lcd.writeFromPgmspace(LOCALIZE(c_calib5));
				lcd.setCursor(0,1);
				lcd.writeFromPgmspace(LOCALIZE(c_calib6));
				lcd.setCursor(0,2);
				lcd.writeFromPgmspace(LOCALIZE(c_calib7));
				lcd.setCursor(0,3);
				lcd.writeFromPgmspace(LOCALIZE(c_calib4));
				break;
			case CS_PROMPT_MOVE:
				lcd.writeFromPgmspace(LOCALIZE(c_calib8));
				lcd.setCursor(0,1);
				lcd.writeFromPgmspace(LOCALIZE(c_calib9));
				lcd.setCursor(0,3);
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
				lcd.setCursor(0,1);
				lcd.writeFromPgmspace(LOCALIZE(c_regen));
				lcd.setCursor(0,3);
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
			steppers::definePosition(Point(0,0,0,0,0));
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
        	case ButtonArray::OK:
        	case ButtonArray::YMINUS:
        	case ButtonArray::ZMINUS:
        	case ButtonArray::YPLUS:
        	case ButtonArray::ZPLUS:
        	case ButtonArray::XMINUS:
        	case ButtonArray::XPLUS:
        	case ButtonArray::ZERO:
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
	const static PROGMEM prog_uchar ho_message4[]  = "Up/Dn/Ent to Set";
	const static PROGMEM prog_uchar ho_mm[]        = "mm";

	if ( homeOffsetState != lastHomeOffsetState )	forceRedraw = true;

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
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

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(ho_message4));
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

	lcd.setCursor(0,1);
	lcd.writeFloat((float)position, 3);
	lcd.writeFromPgmspace(LOCALIZE(ho_mm));

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
	const static PROGMEM prog_uchar bsr_message4[] = "Up/Dn/Ent to Set";
	const static PROGMEM prog_uchar bsr_times[]    = " times ";

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(bsr_message1));

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(LOCALIZE(bsr_message2));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(bsr_message4));
	}

	// Redraw tool info
	lcd.setCursor(0,2);
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

	extruderControl(0, SLAVE_CMD_TOGGLE_FAN, EXTDR_CMD_SET, responsePacket, (enabled)?1:0);
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
		lcd.clear();

		lcd.setCursor(0,0);
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
		float filamentUsedMMB = stepperAxisStepsToMM(filamentUsedB, B_AXIS);

		float filamentUsedMM = filamentUsedMMA + filamentUsedMMB;

		lcd.setCursor(0,1);
		lcd.writeFloat(filamentUsedMM / 1000.0, 4);
		lcd.write('m');

		lcd.setCursor(0,2);
		if ( lifetimeDisplay )	lcd.writeFromPgmspace(LOCALIZE(fu_but_life));
		else			lcd.writeFromPgmspace(LOCALIZE(fu_but_trip));

		lcd.setCursor(0,3);
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
		case ButtonArray::ZPLUS:
		case ButtonArray::ZMINUS:
		case ButtonArray::YPLUS:
		case ButtonArray::YMINUS:
		case ButtonArray::XMINUS:
		case ButtonArray::XPLUS:
			break;
	}
}

BuildSettingsMenu::BuildSettingsMenu() {
	itemCount = 5;
	reset();
}

void BuildSettingsMenu::resetState() {
	if ( eeprom::getEeprom8(eeprom::ACCELERATION_ON, EEPROM_DEFAULT_ACCELERATION_ON) & 0x01 )	acceleration = true;
	else												acceleration = false;

	if ( acceleration )	itemCount = 6;
	else			itemCount = 5;

	itemIndex = 0;
	firstItemIndex = 0;
}

void BuildSettingsMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar bs_item1[] = "Override Temp";
	const static PROGMEM prog_uchar bs_item2[] = "ABP Copies (SD)";
	const static PROGMEM prog_uchar bs_item3[] = "Ditto Print";
	const static PROGMEM prog_uchar bs_item4[] = "Extruder Hold";
	const static PROGMEM prog_uchar bs_item5[] = "Accel. On/Off";
	const static PROGMEM prog_uchar bs_item6[] = "Accel. Settings";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(bs_item1));
		break;
	case 1:
		lcd.writeFromPgmspace(LOCALIZE(bs_item2));
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(bs_item3));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(bs_item4));
		break;
	case 4:
		lcd.writeFromPgmspace(LOCALIZE(bs_item5));
		break;
	case 5:
		if ( ! acceleration ) return;
		lcd.writeFromPgmspace(LOCALIZE(bs_item6));
		break;
	}
}

void BuildSettingsMenu::handleSelect(uint8_t index) {
	OutPacket responsePacket;

	switch (index) {
		case 0:
			//Override the gcode temperature
			interface::pushScreen(&overrideGCodeTempMenu);
			break;
		case 1:
			//Change number of ABP copies
			interface::pushScreen(&abpCopiesSetScreen);
			break;
		case 2:
			//Ditto Print
			interface::pushScreen(&dittoPrintMenu);
			break;
		case 3:
			//Extruder Hold
			interface::pushScreen(&extruderHoldOnOffMenu);
			break;
		case 4:
			//Acceleraton On/Off Menu
			interface::pushScreen(&accelerationOnOffMenu);
			break;
		case 5:
			if ( ! acceleration )	return;
			interface::pushScreen(&acceleratedSettingsMode);
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
	const static PROGMEM prog_uchar abp_message4[] = "Up/Dn/Ent to Set";

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(abp_message1));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(abp_message4));
	}

	// Redraw tool info
	lcd.setCursor(0,1);
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

        case ButtonArray::XMINUS:
        case ButtonArray::XPLUS:
		break;
	}
}

bool OverrideGCodeTempMenu::isEnabled() {
	if ( eeprom::getEeprom8(eeprom::OVERRIDE_GCODE_TEMP, EEPROM_DEFAULT_OVERRIDE_GCODE_TEMP) ) return true;
	return false;
}

void OverrideGCodeTempMenu::enable(bool enabled) {
	eeprom_write_byte((uint8_t*)eeprom::OVERRIDE_GCODE_TEMP,(enabled)?1:0);
}

void OverrideGCodeTempMenu::setupTitle() {
	static const PROGMEM prog_uchar ogct_msg1[] = "Override GCode";
	static const PROGMEM prog_uchar ogct_msg2[] = "Temperature:";
	msg1 = LOCALIZE(ogct_msg1);
	msg2 = LOCALIZE(ogct_msg2);
}

AccelerationOnOffMenu::AccelerationOnOffMenu() {
	itemCount = 4;
	reset();
}

void AccelerationOnOffMenu::resetState() {
	uint8_t accel = eeprom::getEeprom8(eeprom::ACCELERATION_ON, EEPROM_DEFAULT_ACCELERATION_ON);
	if	( accel & 0x01 )	itemIndex = 3;
	else				itemIndex = 2;
	firstItemIndex = 2;
}

void AccelerationOnOffMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar aof_msg1[] = "Accelerated";
	const static PROGMEM prog_uchar aof_msg2[] = "Printing:";
	const static PROGMEM prog_uchar aof_off[]  = "Off";
	const static PROGMEM prog_uchar aof_on[]   = "On";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(aof_msg1));
		break;
	case 1:
		lcd.writeFromPgmspace(LOCALIZE(aof_msg2));
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(aof_off));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(aof_on));
		break;
	}
}

void AccelerationOnOffMenu::handleSelect(uint8_t index) {
	uint8_t oldValue = eeprom::getEeprom8(eeprom::ACCELERATION_ON, EEPROM_DEFAULT_ACCELERATION_ON);
	uint8_t newValue = oldValue;
	
	switch (index) {
		case 2:  
			newValue = 0x00;
			interface::popScreen();
			break;
		case 3:
			newValue = 0x01;
                	interface::popScreen();
			break;
	}

	//If the value has changed, do a reset
	if ( newValue != oldValue ) {
		cli();
		eeprom_write_byte((uint8_t*)eeprom::ACCELERATION_ON, newValue);
		sei();
		//Reset
		host::stopBuildNow();
	}
}

ExtruderHoldOnOffMenu::ExtruderHoldOnOffMenu() {
	itemCount = 4;
	reset();
}

void ExtruderHoldOnOffMenu::resetState() {
	uint8_t extruderHold = ((eeprom::getEeprom8(eeprom::EXTRUDER_HOLD, EEPROM_DEFAULT_EXTRUDER_HOLD)) != 0);
	if	( extruderHold )	itemIndex = 3;
	else				itemIndex = 2;
	firstItemIndex = 2;
}

void ExtruderHoldOnOffMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	const static PROGMEM prog_uchar eof_msg1[] = "Extruder";
	const static PROGMEM prog_uchar eof_msg2[] = "Hold:";
	const static PROGMEM prog_uchar eof_off[]  = "Off";
	const static PROGMEM prog_uchar eof_on[]   = "On";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(LOCALIZE(eof_msg1));
		break;
	case 1:
		lcd.writeFromPgmspace(LOCALIZE(eof_msg2));
		break;
	case 2:
		lcd.writeFromPgmspace(LOCALIZE(eof_off));
		break;
	case 3:
		lcd.writeFromPgmspace(LOCALIZE(eof_on));
		break;
	}
}

void ExtruderHoldOnOffMenu::handleSelect(uint8_t index) {
	uint8_t oldValue = ((eeprom::getEeprom8(eeprom::EXTRUDER_HOLD, EEPROM_DEFAULT_EXTRUDER_HOLD)) != 0);
	uint8_t newValue = oldValue;
	
	switch (index) {
		case 2:  
			newValue = 0x00;
			interface::popScreen();
			break;
		case 3:
			newValue = 0x01;
                	interface::popScreen();
			break;
	}

	//If the value has changed, do a reset
	if ( newValue != oldValue ) {
		cli();
		eeprom_write_byte((uint8_t*)eeprom::EXTRUDER_HOLD, newValue);
		sei();
		//Reset
		host::stopBuildNow();
	}
}

bool DittoPrintMenu::isEnabled() {
	if ( eeprom::getEeprom8(eeprom::DITTO_PRINT_ENABLED, EEPROM_DEFAULT_DITTO_PRINT_ENABLED) ) return true;
	return false;
}

void DittoPrintMenu::enable(bool enabled) {
	eeprom_write_byte((uint8_t*)eeprom::DITTO_PRINT_ENABLED,(enabled)?1:0);
}

void DittoPrintMenu::setupTitle() {
	static const PROGMEM prog_uchar dp_msg1[] = "Ditto Printing:";
	static const PROGMEM prog_uchar dp_msg2[] = "";
	msg1 = LOCALIZE(dp_msg1);
	msg2 = LOCALIZE(dp_msg2);
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

	// ??????
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
	const static PROGMEM prog_uchar pcn_message4[] = "Up/Dn/Ent to Set";
	const static PROGMEM prog_uchar pcn_blank[]    = " ";

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(pcn_message1));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(pcn_message4));
	}

	lcd.setCursor(0,1);
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
	const static PROGMEM prog_uchar cp_msg1[] = "X:";
	const static PROGMEM prog_uchar cp_msg2[] = "Y:";
	const static PROGMEM prog_uchar cp_msg3[] = "Z:";
	const static PROGMEM prog_uchar cp_msg4[] = "A:";
	const static PROGMEM prog_uchar cp_mm[]   = "mm";

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(cp_msg1));

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(LOCALIZE(cp_msg2));

		lcd.setCursor(0,2);
		lcd.writeFromPgmspace(LOCALIZE(cp_msg3));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(cp_msg4));
	}

	Point position = steppers::getStepperPosition();

	lcd.setCursor(3, 0);
	lcd.writeFloat(stepperAxisStepsToMM(position[0], X_AXIS), 3);
	lcd.writeFromPgmspace(LOCALIZE(cp_mm));

	lcd.setCursor(3, 1);
	lcd.writeFloat(stepperAxisStepsToMM(position[1], Y_AXIS), 3);
	lcd.writeFromPgmspace(LOCALIZE(cp_mm));

	lcd.setCursor(3, 2);
	lcd.writeFloat(stepperAxisStepsToMM(position[2], Z_AXIS), 3);
	lcd.writeFromPgmspace(LOCALIZE(cp_mm));

	lcd.setCursor(3, 3);
	lcd.writeFloat(stepperAxisStepsToMM(position[3], A_AXIS), 3);
	lcd.writeFromPgmspace(LOCALIZE(cp_mm));
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
	const static PROGMEM prog_uchar utof_msg2[]   = "file.  Name too";
	const static PROGMEM prog_uchar utof_msg3[]   = "long?";
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
	const static PROGMEM prog_uchar as_message4[]			= "Up/Dn/Ent to Set";
	const static PROGMEM prog_uchar as_blank[]			= "    ";

	if ( accelerateSettingsState != lastAccelerateSettingsState )	forceRedraw = true;

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		switch(accelerateSettingsState) {
                	case AS_MAX_ACCELERATION_X:
				lcd.writeFromPgmspace(LOCALIZE(as_message1xMaxAccelRate));
				break;
                	case AS_MAX_ACCELERATION_Y:
				lcd.writeFromPgmspace(LOCALIZE(as_message1yMaxAccelRate));
				break;
                	case AS_MAX_ACCELERATION_Z:
				lcd.writeFromPgmspace(LOCALIZE(as_message1zMaxAccelRate));
				break;
                	case AS_MAX_ACCELERATION_A:
				lcd.writeFromPgmspace(LOCALIZE(as_message1aMaxAccelRate));
				break;
                	case AS_MAX_ACCELERATION_B:
				lcd.writeFromPgmspace(LOCALIZE(as_message1bMaxAccelRate));
				break;
                	case AS_MAX_EXTRUDER_NORM:
				lcd.writeFromPgmspace(LOCALIZE(as_message1ExtruderNorm));
				break;
                	case AS_MAX_EXTRUDER_RETRACT:
				lcd.writeFromPgmspace(LOCALIZE(as_message1ExtruderRetract));
				break;
                	case AS_ADVANCE_K:
				lcd.writeFromPgmspace(LOCALIZE(as_message1AdvanceK));
				break;
                	case AS_ADVANCE_K2:
				lcd.writeFromPgmspace(LOCALIZE(as_message1AdvanceK2));
				break;
                	case AS_EXTRUDER_DEPRIME_A:
				lcd.writeFromPgmspace(LOCALIZE(as_message1ExtruderDeprimeA));
				break;
                	case AS_EXTRUDER_DEPRIME_B:
				lcd.writeFromPgmspace(LOCALIZE(as_message1ExtruderDeprimeB));
				break;
                	case AS_SLOWDOWN_FLAG:
				lcd.writeFromPgmspace(LOCALIZE(as_message1SlowdownLimit));
				break;
                	case AS_MAX_SPEED_CHANGE_X:
				lcd.writeFromPgmspace(LOCALIZE(as_message1MaxSpeedChangeX));
				break;
                	case AS_MAX_SPEED_CHANGE_Y:
				lcd.writeFromPgmspace(LOCALIZE(as_message1MaxSpeedChangeY));
				break;
                	case AS_MAX_SPEED_CHANGE_Z:
				lcd.writeFromPgmspace(LOCALIZE(as_message1MaxSpeedChangeZ));
				break;
                	case AS_MAX_SPEED_CHANGE_A:
				lcd.writeFromPgmspace(LOCALIZE(as_message1MaxSpeedChangeA));
				break;
                	case AS_MAX_SPEED_CHANGE_B:
				lcd.writeFromPgmspace(LOCALIZE(as_message1MaxSpeedChangeB));
				break;
			default:
				break;
		}

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(as_message4));
	}

	uint32_t value = 0;

	uint8_t currentIndex = accelerateSettingsState - AS_MAX_ACCELERATION_X;

	value = values[currentIndex];

	lcd.setCursor(0,1);

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
		case ButtonArray::XMINUS:
		case ButtonArray::XPLUS:
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
	const static PROGMEM prog_uchar esc_message4[] = "Up/Dn/Ent to Set";
	const static PROGMEM prog_uchar esc_blank[]    = " ";

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(LOCALIZE(esc_message1));

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(esc_message4));
	}

	// Redraw tool info
	lcd.setCursor(0,1);
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
		case ButtonArray::XMINUS:
		case ButtonArray::XPLUS:
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
	const static PROGMEM prog_uchar hfr_message4[]  = "Up/Dn/Ent to Set";
	const static PROGMEM prog_uchar hfr_mm[]        = "mm/min ";

	if ( homingFeedRateState != lastHomingFeedRateState )	forceRedraw = true;

	if (forceRedraw) {
		lcd.clear();

		lcd.setCursor(0,0);
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

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(LOCALIZE(hfr_message4));
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

	lcd.setCursor(0,1);
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
		case ButtonArray::XMINUS:
		case ButtonArray::XPLUS:
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

			lcd.setCursor(0,0);
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg1));

			lcd.setCursor(0,1);
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg2));

			lcd.setCursor(0,2);
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg3));

			lcd.setCursor(0,3);
			lcd.writeFromPgmspace(LOCALIZE(eeprom_msg4));
		}
	}
	else {
		if ( itemSelected != -1 ) {
			lcd.clear();
			lcd.setCursor(0,0);
		}

		const static PROGMEM prog_uchar eeprom_message_dump[]		= "Saving...";
		const static PROGMEM prog_uchar eeprom_message_restore[]	= "Restoring...";
		const static PROGMEM prog_uchar eeprom_message_erase[]		= "Erasing...";

		const char dumpFilename[] = "eeprom_dump.bin";

		const static PROGMEM prog_uchar eeprom_message_error[]		= "Error:";

		switch ( itemSelected ) {
			case 0:	//Dump
				if ( ! sdcard::fileExists(dumpFilename) ) {
					lcd.writeFromPgmspace(LOCALIZE(eeprom_message_dump));
					if ( ! eeprom::saveToSDFile(dumpFilename) ) {
						const static PROGMEM prog_uchar eeprom_msg11[] = "Write Failed!";
						lcd.clear();
						lcd.setCursor(0,0);
						lcd.writeFromPgmspace(LOCALIZE(eeprom_msg11));
						_delay_us(5000000);
					}
				} else {
					const static PROGMEM prog_uchar eeprom_msg12[] = "File exists!";
					lcd.clear();
					lcd.setCursor(0,0);
					lcd.writeFromPgmspace(LOCALIZE(eeprom_message_error));
					lcd.setCursor(0,1);
					lcd.writeString((char *)dumpFilename);
					lcd.setCursor(0,2);
					lcd.writeFromPgmspace(LOCALIZE(eeprom_msg12));
					_delay_us(5000000);
				}
				interface::popScreen();
				break;

			case 1: //Restore
				if ( sdcard::fileExists(dumpFilename) ) {
					lcd.writeFromPgmspace(LOCALIZE(eeprom_message_restore));
					if ( ! eeprom::restoreFromSDFile(dumpFilename) ) {
						const static PROGMEM prog_uchar eeprom_msg5[] = "Read Failed!";
						const static PROGMEM prog_uchar eeprom_msg6[] = "EEPROM may be";
						const static PROGMEM prog_uchar eeprom_msg7[] = "corrupt";
						lcd.clear();
						lcd.setCursor(0,0);
						lcd.writeFromPgmspace(LOCALIZE(eeprom_msg5));
						lcd.setCursor(0,1);
						lcd.writeFromPgmspace(LOCALIZE(eeprom_msg6));
						lcd.setCursor(0,2);
						lcd.writeFromPgmspace(LOCALIZE(eeprom_msg7));
						_delay_us(5000000);
					}
					host::stopBuildNow();
				} else {
					const static PROGMEM prog_uchar eeprom_msg8[] = "File not found!";
					lcd.clear();
					lcd.setCursor(0,0);
					lcd.writeFromPgmspace(LOCALIZE(eeprom_message_error));
					lcd.setCursor(0,1);
					lcd.writeString((char *)dumpFilename);
					lcd.setCursor(0,2);
					lcd.writeFromPgmspace(LOCALIZE(eeprom_msg8));
					_delay_us(5000000);
					interface::popScreen();
				}
				break;

			case 2: //Erase
				lcd.writeFromPgmspace(LOCALIZE(eeprom_message_erase));
				_delay_us(5000000);
				eeprom::erase();
				interface::popScreen();
				host::stopBuildNow();
				break;
			default:
				Menu::update(lcd, forceRedraw);
				break;
		}

		lcd.setCursor(0,3);
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
		switch (button) {
			case ButtonArray::YPLUS:
				warningScreen = false;
				return;
			default:
				Menu::notifyButtonPressed(ButtonArray::CANCEL);
				return;
		}

		return;
	}

        if ( button == ButtonArray::YMINUS || button == ButtonArray::ZMINUS ||
	     button == ButtonArray::YPLUS  || button == ButtonArray::ZPLUS )
		safetyGuard = 0;

	Menu::notifyButtonPressed(button);
}


#endif

#endif

#ifndef MENU_HH_
#define MENU_HH_

#include "Types.hh"
#include "ButtonArray.hh"
#include "LiquidCrystal.hh"
#include "Configuration.hh"
#include "CircularBuffer.hh"
#include "Timeout.hh"
#include "Command.hh"
#include "StepperAccelPlanner.hh"

/// The screen class defines a standard interface for anything that should
/// be displayed on the LCD.
class Screen {
public:
        /// Get the rate that this display should be updated. This is called
        /// after every screen display, so it can be used to dynamically
        /// adjust the update rate. This can be as fast as you like, however
        /// refreshing too fast during a build is certain to interfere with
        /// the serial and stepper processes, which will decrease build quality.
        /// \return refresh interval, in microseconds.
	virtual micros_t getUpdateRate();

        /// Update the screen display,
        /// \param[in] lcd LCD to write to
        /// \param[in] forceRedraw if true, redraw the entire screen. If false,
        ///                        only updated sections need to be redrawn.
	virtual void update(LiquidCrystal& lcd, bool forceRedraw);

        /// Reset the screen to it's default state
	virtual void reset();

        /// Get a notification that a button was pressed down.
        /// This function is called for every button that is pressed. Screen
        /// logic can be updated, however the screen should not be redrawn
        /// until update() is called again.
        ///
        /// Note that the current implementation only supports one button
        /// press at a time, and will discard any other events.
        /// \param button Button that was pressed
        virtual void notifyButtonPressed(ButtonArray::ButtonName button);
};


/// The menu object can be used to display a list of options on the LCD
/// screen. It handles updating the display and responding to button presses
/// automatically.
class Menu: public Screen {
public:
	virtual micros_t getUpdateRate() {return 500L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

	virtual void resetState();

        void notifyButtonPressed(ButtonArray::ButtonName button);

protected:

        uint8_t itemIndex;              ///< The currently selected item
        uint8_t lastDrawIndex;          ///< The index used to make the last draw
        uint8_t itemCount;              ///< Total number of items
        uint8_t firstItemIndex;         ///< The first selectable item. Set this
                                        ///< to greater than 0 if the first
                                        ///< item(s) are a title)

        /// Draw an item at the current cursor position.
        /// \param[in] index Index of the item to draw
        /// \param[in] LCD screen to draw onto
	virtual void drawItem(uint8_t index, LiquidCrystal& lcd);

        /// Handle selection of a menu item
        /// \param[in] index Index of the menu item that was selected
	virtual void handleSelect(uint8_t index);

        /// Handle the menu being cancelled. This should either remove the
        /// menu from the stack, or pop up a confirmation dialog to make sure
        /// that the menu should be removed.
	virtual void handleCancel();
};

/// Display a welcome splash screen, that removes itself when updated.
class SplashScreen: public Screen {
public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class UserViewMenu: public Menu {
public:
	UserViewMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class JoggerMenu: public Screen {
public:
        enum distance_t {
          DISTANCE_0_1MM = 0,
          DISTANCE_1MM,
          DISTANCE_CONT,
        };

	micros_t getUpdateRate() {return 50L * 1000L;}

	distance_t jogDistance;

	ButtonArray::ButtonName lastDirectionButtonPressed;

	void jog(ButtonArray::ButtonName direction, bool pauseModeJog);

	bool userViewMode;
};

/// Display a message for the user, and provide support for
/// user-specified pauses.
class MessageScreen: public Screen {
private:
	uint8_t x, y;
	const static int BUF_SIZE = 24 * 4 + 1;	//Maximum LCD columns and rows size
	char message[BUF_SIZE];
	uint8_t cursor;
	bool needsRedraw;
	bool incomplete;
	Timeout timeout;

public:
	MessageScreen() : Screen() { message[0] = '\0'; }

	/// poll if the screen is waiting on a timer
	bool screenWaiting(void);

	void setXY(uint8_t xpos, uint8_t ypos) { x = xpos; y = ypos; }

	void addMessage(CircularBuffer& buf);
	void addMessage(const prog_uchar msg[]);
	void clearMessage();
	void setTimeout(uint8_t seconds);//, bool pop);
	void refreshScreen();

	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

	void notifyButtonPressed(ButtonArray::ButtonName button);

	bool buttonsDisabled;
};

class JogMode: public JoggerMenu {
private:
	UserViewMenu userViewMenu;

	bool distanceChanged;
	bool userViewModeChanged;

public:
	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class UnableToOpenFileMenu: public Menu {
public:
	UnableToOpenFileMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class SDMenu: public Menu {
private:
	uint8_t updatePhase;
	uint8_t lastItemIndex;
	bool	drawItemLockout;
        bool    selectable;
	int8_t  folderStackIndex;
	uint8_t folderStack[4]; // Recall file itemIndex up to 4 folders deep
	UnableToOpenFileMenu unableToOpenFileMenu;

public:
	SDMenu();

	void resetState();

	micros_t getUpdateRate() {return 500L * 1000L;}
	void notifyButtonPressed(ButtonArray::ButtonName button);

	void update(LiquidCrystal& lcd, bool forceRedraw);
protected:
	uint8_t countFiles();

        bool getFilename(uint8_t index,
                         char buffer[],
                         uint8_t buffer_size,
		         bool *isdir);

	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class PauseMode: public JoggerMenu {
private:
	enum PauseState lastPauseState;

public:
	bool autoPause;

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};


class PauseAtZPosScreen: public Screen {
private:
	float pauseAtZPos;

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};


class ChangeSpeedScreen: public Screen {
private:
	uint8_t alterSpeed;
	FPTYPE  speedFactor;

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};


class CancelBuildMenu: public Menu {
public:
	CancelBuildMenu();

	void resetState();

protected:
	void update(LiquidCrystal& lcd, bool forceRedraw);

	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
private:
	PauseMode		pauseMode;
	bool			pauseDisabled;
	PauseAtZPosScreen	pauseAtZPosScreen;
	ChangeSpeedScreen       changeSpeedScreen;
	bool			printAnotherEnabled;
};

class MonitorMode: public Screen {
private:
	CancelBuildMenu cancelBuildMenu;

	enum UpdatePhase {
		UPDATE_PHASE_FIRST = 0,
		UPDATE_PHASE_TOOL_TEMP = UPDATE_PHASE_FIRST,
		UPDATE_PHASE_TOOL_TEMP_SET_POINT,
		UPDATE_PHASE_PLATFORM_TEMP,
		UPDATE_PHASE_PLATFORM_SET_POINT,
		UPDATE_PHASE_BUILD_PHASE_SCROLLER,
		UPDATE_PHASE_LAST	//Not counted, just an end marker
	};

	enum BuildTimePhase {
		BUILD_TIME_PHASE_FIRST = 0,
		BUILD_TIME_PHASE_COMPLETED_PERCENT = BUILD_TIME_PHASE_FIRST,
		BUILD_TIME_PHASE_ELAPSED_TIME,
		BUILD_TIME_PHASE_TIME_LEFT,
		BUILD_TIME_PHASE_ZPOS,
		BUILD_TIME_PHASE_FILAMENT,
		BUILD_TIME_PHASE_COPIES_PRINTED,
		BUILD_TIME_PHASE_ACCEL_STATS,
		BUILD_TIME_PHASE_LAST	//Not counted, just an end marker
	};

	enum UpdatePhase updatePhase;
	enum BuildTimePhase buildTimePhase, lastBuildTimePhase;
	float   lastElapsedSeconds;
	PauseMode pauseMode;
	bool	pausePushLockout;
	bool buildCompleteBuzzPlayed;
	int32_t buildDuration;
	bool	overrideForceRedraw;
	uint8_t	copiesPrinted;
	bool	toggleHeating;
	bool	flashingTool;
	bool	flashingPlatform;

public:
	micros_t getUpdateRate() {return 500L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};


class VersionMode: public Screen {
private:

public:
	micros_t getUpdateRate() {return 500L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};


class ValueSetScreen: public Screen {
private:
	uint8_t value;

public:
	const prog_uchar *message1;
	const prog_uchar *units;
	uint16_t location;
	uint8_t default_value;

	micros_t getUpdateRate() {return 100L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};


class PreheatMenu: public Menu {
public:
	PreheatMenu();

	void fetchTargetTemps();

protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);

private:
	uint16_t tool0Temp;
	uint16_t platformTemp;

        /// Static instances of our menus
        ValueSetScreen heaterTempSetScreen;
};

class ExtruderTooColdMenu: public Menu {
public:
	ExtruderTooColdMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);

	void handleCancel();
};

class ExtruderMode: public Screen {
private:
	enum extrudeSeconds {
		EXTRUDE_SECS_CANCEL = 0,
		EXTRUDE_SECS_1S     = 1,
		EXTRUDE_SECS_2S     = 2,
		EXTRUDE_SECS_5S     = 5,
		EXTRUDE_SECS_10S    = 10,
		EXTRUDE_SECS_30S    = 30,
		EXTRUDE_SECS_60S    = 60,
		EXTRUDE_SECS_90S    = 90,
		EXTRUDE_SECS_120S   = 120,
		EXTRUDE_SECS_240S   = 240,
	};

	enum extrudeSeconds extrudeSeconds;
	bool timeChanged;
	int16_t lastDirection;
	ExtruderTooColdMenu extruderTooColdMenu;
        ValueSetScreen extruderSetMMSScreen;

	uint8_t updatePhase;

	void extrude(int32_t seconds, bool overrideTempCheck);

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

	void notifyButtonPressed(ButtonArray::ButtonName button);
};

class MoodLightSetRGBScreen: public Screen {
private:
	uint8_t red;
	uint8_t green;
	uint8_t blue;

	int inputMode;	//0 = red, 1 = green, 2 = blue
	bool redrawScreen;

public:
	micros_t getUpdateRate() {return 100L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};


class MoodLightMode: public Screen {
private:
	uint8_t updatePhase;

	uint8_t scriptId;

        MoodLightSetRGBScreen   moodLightSetRGBScreen;

public:
	micros_t getUpdateRate() {return 100L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

	void notifyButtonPressed(ButtonArray::ButtonName button);
};

class EndStopConfigScreen: public Screen {
private:
	uint8_t endstops;

public:
	micros_t getUpdateRate() {return 100L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class HomeAxisMode: public Screen {
private:
        void home(ButtonArray::ButtonName direction);
	EndStopConfigScreen endStopConfigScreen;

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class EnabledDisabledMenu: public Menu {
public:
	EnabledDisabledMenu();

	void resetState();

protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);

	virtual void enable(bool enabled) {};
	
	virtual bool isEnabled() {return false;};

	virtual void setupTitle() {};

	const prog_uchar *msg1, *msg2;
};

class SteppersMenu: public EnabledDisabledMenu {
private:
	void enable(bool enabled);
	
	bool isEnabled();

	void setupTitle();
};

class TestEndStopsMode: public Screen {
private:

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class AdvanceABPMode: public Screen {
private:
	bool abpForwarding;

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class CalibrateMode: public Screen {
private:
	enum calibrateState {
		CS_NONE,
		CS_START1,	//Disable steppers
		CS_START2,	//Disable steppers
		CS_PROMPT_MOVE,	//Prompt user to move build platform
		CS_HOME_Z,
		CS_HOME_Z_WAIT,
		CS_HOME_Y,
		CS_HOME_Y_WAIT,
		CS_HOME_X,
		CS_HOME_X_WAIT,
		CS_PROMPT_CALIBRATED
	};

	enum calibrateState calibrationState, lastCalibrationState;

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class HomeOffsetsMode: public Screen {
private:
	enum homeOffState {
		HOS_NONE,
		HOS_OFFSET_X,
		HOS_OFFSET_Y,
		HOS_OFFSET_Z,
	};

	enum homeOffState homeOffsetState, lastHomeOffsetState;

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class BuzzerSetRepeatsMode: public Screen {
private:
	uint8_t repeats;

public:
	micros_t getUpdateRate() {return 100L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class ExtruderFanMenu: public EnabledDisabledMenu {
private:
	void enable(bool enabled);
	
	bool isEnabled();

	void setupTitle();
};

class FilamentUsedResetMenu: public Menu {
public:
	FilamentUsedResetMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class FilamentUsedMode: public Screen {
private:
	FilamentUsedResetMenu filamentUsedResetMenu;

	bool overrideForceRedraw;
	bool lifetimeDisplay;
public:
	micros_t getUpdateRate() {return 100L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class ABPCopiesSetScreen: public Screen {
private:
	uint8_t value;

public:
	micros_t getUpdateRate() {return 100L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class OverrideGCodeTempMenu: public EnabledDisabledMenu {
private:
	void enable(bool enabled);
	
	bool isEnabled();

	void setupTitle();
};

class AccelerationOnOffMenu: public Menu {
public:
	AccelerationOnOffMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class ExtruderHoldOnOffMenu: public Menu {
public:
	ExtruderHoldOnOffMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class ToolheadSystemOnOffMenu: public Menu {
public:
	ToolheadSystemOnOffMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class AcceleratedSettingsMode: public Screen {
private:
	enum accelerateSettingsState {
		AS_NONE = 0,
		AS_MAX_ACCELERATION_X,
		AS_MAX_ACCELERATION_Y,
		AS_MAX_ACCELERATION_Z,
		AS_MAX_ACCELERATION_A,
		AS_MAX_ACCELERATION_B,
		AS_MAX_EXTRUDER_NORM,
		AS_MAX_EXTRUDER_RETRACT,
		AS_ADVANCE_K,
		AS_ADVANCE_K2,
		AS_EXTRUDER_DEPRIME_A,
		AS_EXTRUDER_DEPRIME_B,
		AS_SLOWDOWN_FLAG,
		AS_MAX_SPEED_CHANGE_X,
		AS_MAX_SPEED_CHANGE_Y,
		AS_MAX_SPEED_CHANGE_Z,
		AS_MAX_SPEED_CHANGE_A,
		AS_MAX_SPEED_CHANGE_B,
		AS_LAST_ENTRY = AS_MAX_SPEED_CHANGE_B
	};

	enum accelerateSettingsState accelerateSettingsState, lastAccelerateSettingsState;

	uint32_t values[AS_LAST_ENTRY];

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class DittoPrintMenu: public EnabledDisabledMenu {
private:
	void enable(bool enabled);
	
	bool isEnabled();

	void setupTitle();
};

class BuildSettingsMenu: public Menu {
private:
	OverrideGCodeTempMenu		overrideGCodeTempMenu;
	ABPCopiesSetScreen		abpCopiesSetScreen;
	DittoPrintMenu			dittoPrintMenu;
	AccelerationOnOffMenu		accelerationOnOffMenu;
	AcceleratedSettingsMode		acceleratedSettingsMode;
	ExtruderHoldOnOffMenu		extruderHoldOnOffMenu;
	ToolheadSystemOnOffMenu		toolheadSystemOnOffMenu;

	bool acceleration;
public:
	BuildSettingsMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class ProfileChangeNameMode: public Screen {
private:
	uint8_t	cursorLocation;
	uint8_t profileName[8+1];

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);

	uint8_t profileIndex;
};

class ProfileDisplaySettingsMenu: public Menu {
private:
	uint8_t profileName[8+1];
	int32_t homeX, homeY, homeZ;
	uint8_t hbpTemp, tool0Temp, tool1Temp, extruderMMS;
public:
	ProfileDisplaySettingsMenu();

	void resetState();

	uint8_t profileIndex;
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class ProfileSubMenu: public Menu {
private:
	ProfileChangeNameMode	   profileChangeNameMode;
	ProfileDisplaySettingsMenu profileDisplaySettingsMenu;

public:
	ProfileSubMenu();

	void resetState();

	uint8_t profileIndex;
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class ProfilesMenu: public Menu {
private:
	ProfileSubMenu profileSubMenu;
public:
	ProfilesMenu();

	void resetState();
protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);
};

class CurrentPositionMode: public Screen {
private:

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

class HomingFeedRatesMode: public Screen {
private:
	enum homingFRateState {
		HFRS_NONE,
		HFRS_OFFSET_X,
		HFRS_OFFSET_Y,
		HFRS_OFFSET_Z,
	};

	enum homingFRateState homingFeedRateState, lastHomingFeedRateState;

	uint32_t homingFeedRate[3];

public:
	micros_t getUpdateRate() {return 50L * 1000L;}

	void update(LiquidCrystal& lcd, bool forceRedraw);

	void reset();

        void notifyButtonPressed(ButtonArray::ButtonName button);
};

#ifdef EEPROM_MENU_ENABLE

class EepromMenu: public Menu {
public:
        EepromMenu();

        void resetState();
protected:
        void drawItem(uint8_t index, LiquidCrystal& lcd);

        void handleSelect(uint8_t index);

	void update(LiquidCrystal& lcd, bool forceRedraw);

        void notifyButtonPressed(ButtonArray::ButtonName button);
private:
	uint8_t safetyGuard;

	bool warningScreen;
	
	int8_t itemSelected;
};

#endif

class MainMenu: public Menu {
public:
	MainMenu();

protected:
	void drawItem(uint8_t index, LiquidCrystal& lcd);

	void handleSelect(uint8_t index);

	void update(LiquidCrystal& lcd, bool forceRedraw);

private:
        /// Static instances of our menus
        MonitorMode monitorMode;
        SDMenu sdMenu;
        JogMode jogger;
	PreheatMenu preheatMenu;
	ExtruderMode extruderMenu;
	HomeAxisMode homeAxisMode;
	SteppersMenu steppersMenu;
	AdvanceABPMode advanceABPMode;
	BuzzerSetRepeatsMode buzzerSetRepeats;
	BuildSettingsMenu buildSettingsMenu;
	ProfilesMenu profilesMenu;
	ExtruderFanMenu extruderFanMenu;
	CalibrateMode calibrateMode;
	HomeOffsetsMode homeOffsetsMode;
	FilamentUsedMode filamentUsedMode;
	CurrentPositionMode currentPositionMode;
	TestEndStopsMode testEndStopsMode;
        VersionMode versionMode;
	MoodLightMode	moodLightMode;
	HomingFeedRatesMode homingFeedRatesMode;
#ifdef EEPROM_MENU_ENABLE
	EepromMenu eepromMenu;
#endif

	int64_t checkAndGetEepromDefault(const uint16_t location, const int64_t default_value);

	float lcdTypeChangeTimer;
};

#endif

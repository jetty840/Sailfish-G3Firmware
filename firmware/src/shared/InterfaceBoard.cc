#include "InterfaceBoard.hh"
#include "Configuration.hh"
#include "LiquidCrystal.hh"
#include "Host.hh"
#include "Motherboard.hh"
        

#if defined HAS_INTERFACE_BOARD

InterfaceBoard::InterfaceBoard(ButtonArray& buttons_in,
                               LiquidCrystal& lcd_in,
                               const Pin& foo_pin_in,
                               const Pin& bar_pin_in,
                               Screen* mainScreen_in,
                               Screen* buildScreen_in,
			       MoodLightController& moodLight_in,
			       MessageScreen* messageScreen_in) :
        lcd(lcd_in),
        buttons(buttons_in),
        foo_pin(foo_pin_in),
        bar_pin(bar_pin_in),
	moodLight(moodLight_in) {
        buildScreen = buildScreen_in;
        mainScreen = mainScreen_in;
	messageScreen = messageScreen_in;
}

void InterfaceBoard::init() {
        buttons.init();

        lcd.begin(lcd.getDisplayWidth(), lcd.getDisplayHeight());
        lcd.clear();
        lcd.home();

        foo_pin.setValue(false);
        foo_pin.setDirection(true);
        bar_pin.setValue(false);
        bar_pin.setDirection(true);

        building = false;

        screenIndex = -1;
	waitingMask = 0;

	messageScreenVisible = false;

        pushScreen(mainScreen);
}

void InterfaceBoard::doInterrupt() {
	buttons.scanButtons();
}

micros_t InterfaceBoard::getUpdateRate() {
	if ( messageScreenVisible )	return messageScreen->getUpdateRate();

	return screenStack[screenIndex]->getUpdateRate();
}

void InterfaceBoard::doUpdate() {

	// If we are building, make sure we show a build menu; otherwise,
	// turn it off.
	switch(host::getHostState()) {
	case host::HOST_STATE_BUILDING:
	case host::HOST_STATE_BUILDING_FROM_SD:
		if (!building) {
                        pushScreen(buildScreen);
			building = true;
		}
		break;
	default:
		if (building) {
			popScreen();
			building = false;
		}
		break;
	}


        static ButtonArray::ButtonName button;


	if (buttons.getButton(button)) {
		if (((1<<button) & waitingMask) != 0) {
			waitingMask = 0;
		} else {
			if ( ! messageScreenVisible )
				screenStack[screenIndex]->notifyButtonPressed(button);
		}
	}

	if ( messageScreenVisible )	messageScreen->update(lcd, false);
	else				screenStack[screenIndex]->update(lcd, false);
}

bool InterfaceBoard::isButtonPressed(ButtonArray::ButtonName button) {
	if ( messageScreenVisible )	return false;

	bool buttonPressed = buttons.isButtonPressed(button);

	if ( buttonPressed ) screenStack[screenIndex]->notifyButtonPressed(button);

	return buttonPressed;
}

void InterfaceBoard::pushScreen(Screen* newScreen) {
	if (screenIndex < SCREEN_STACK_DEPTH - 1) {
		screenIndex++;
		screenStack[screenIndex] = newScreen;
	}
	screenStack[screenIndex]->reset();
	screenStack[screenIndex]->update(lcd, true);
}

void InterfaceBoard::popScreen() {
	// Don't allow the root menu to be removed.
	if (screenIndex > 0) {
		screenIndex--;
	}

	screenStack[screenIndex]->update(lcd, true);
}

/// Tell the interface board that the system is waiting for a button push
/// corresponding to one of the bits in the button mask. The interface board
/// will not process button pushes directly until one of the buttons in the
/// mask is pushed.
void InterfaceBoard::waitForButton(uint16_t button_mask) {
	waitingMask = button_mask;
}

/// Check if the expected button push has been made. If waitForButton was
/// never called, always return true.
bool InterfaceBoard::buttonPushed() {
	return waitingMask == 0;
}

/// Returns true is the message screen is currently being displayed
bool InterfaceBoard::isMessageScreenVisible(void) {
	return messageScreenVisible;
}

/// Displays the message screen
void InterfaceBoard::showMessageScreen(void) {
	messageScreenVisible = true;
	messageScreen->update(lcd, true);
}

/// Hides the message screen
void InterfaceBoard::hideMessageScreen(void) {
	messageScreenVisible = false;
	screenStack[screenIndex]->update(lcd, true);
}

#endif

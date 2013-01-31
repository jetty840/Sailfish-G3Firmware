#ifndef LIQUID_CRYSTAL_HH
#define LIQUID_CRYSTAL_HH

// TODO: Proper attribution

#include <stdint.h>
#include <avr/pgmspace.h>
#include "Pin.hh"

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// flags for display on/off control
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00

// flags for function set
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00

// Custom chars 0x00 - 0x0F
#define LCD_CUSTOM_CHAR_DEGREE		        1
#define LCD_CUSTOM_CHAR_EXTRUDER_NORMAL		2
#define LCD_CUSTOM_CHAR_EXTRUDER_HEATING	3
#define LCD_CUSTOM_CHAR_PLATFORM_NORMAL		4
#define LCD_CUSTOM_CHAR_PLATFORM_HEATING	5
#define LCD_CUSTOM_CHAR_ARROW		     0x7e
#define LCD_CUSTOM_CHAR_FOLDER                  7  // Must not be 0
#define LCD_CUSTOM_CHAR_RETURN                  8  // Must not be 0

class LiquidCrystal {
public:
  LiquidCrystal(Pin rs, Pin enable,
		Pin d0, Pin d1, Pin d2, Pin d3,
		Pin d4, Pin d5, Pin d6, Pin d7);
  LiquidCrystal(Pin rs, Pin rw, Pin enable,
		Pin d0, Pin d1, Pin d2, Pin d3,
		Pin d4, Pin d5, Pin d6, Pin d7);
  LiquidCrystal(Pin rs, Pin rw, Pin enable,
		Pin d0, Pin d1, Pin d2, Pin d3);
  LiquidCrystal(Pin rs, Pin enable,
		Pin d0, Pin d1, Pin d2, Pin d3);

  void init(uint8_t fourbitmode, Pin rs, Pin rw, Pin enable,
	    Pin d0, Pin d1, Pin d2, Pin d3,
	    Pin d4, Pin d5, Pin d6, Pin d7);

  void reloadDisplayType(void);

  void begin(uint8_t cols, uint8_t rows, uint8_t charsize = LCD_5x8DOTS);

  void clear();
  void home();

  void homeCursor(); // faster version of home()
  void clearHomeCursor();  // clear() and homeCursor() combined
  void noDisplay();
  void display();
  void noBlink();
  void blink();
  void noCursor();
  void cursor();
  void scrollDisplayLeft();
  void scrollDisplayRight();
  void leftToRight();
  void rightToLeft();
  void autoscroll();
  void noAutoscroll();

  void createChar(uint8_t, uint8_t[]);
  void setCursor(uint8_t, uint8_t); 
  void setRow(uint8_t); 
  virtual void write(uint8_t);

  /** Added by MakerBot Industries to support storing strings in flash **/
  void writeInt(uint16_t value, uint8_t digits);

  void writeFloat(float value, uint8_t decimalPlaces);

  void writeFixedPoint(int64_t value, uint8_t padding, uint8_t precision);

  char *writeLine(char* message);

  void writeString(char message[]);

  void writeFromPgmspace(const prog_uchar message[]);

  void command(uint8_t);

  uint8_t getDisplayWidth();
  uint8_t getDisplayHeight();
  void	  nextLcdType();

private:
  void send(uint8_t, bool);
  void write4bits(uint8_t);
  void write8bits(uint8_t);
  void pulseEnable();

  Pin _rs_pin; // LOW: command.  HIGH: character.
  Pin _rw_pin; // LOW: write to LCD.  HIGH: read from LCD.
  Pin _enable_pin; // activated by a HIGH pulse.
  Pin _data_pins[8];

  uint8_t _displayfunction;
  uint8_t _displaycontrol;
  uint8_t _displaymode;

  uint8_t _initialized;

  uint8_t _numlines,_currline;

  uint8_t _displayWidth;
  uint16_t _clearDelay;
};

#endif // LIQUID_CRYSTAL_HH

/*
 * Part 2 of the Cat run RAIN SENSOR project.
 *
 * Read the incoming 433Mhz data stream and extract the relevant data portions into individual values
 *
 */
#include "Arduino.h"

// 433Mhz Receiver
#include <VirtualWire.h>

// Touch (Capacitance) Switch
#include <CapacitiveSensor.h>

// MP3 Player
#include <SoftwareSerial.h>

// For I2C communication to the LCD
#include <Wire.h>

// For the LCD functions
#include <LiquidCrystal_I2C.h>

// Hexadecimal address of the LCD unit
#define I2C_ADDR 0x27 //0x3F for 16 x 2 units

// DEBUG indicator (control debugPrint statements to Serial Window
#define isDebug true

// variables to hold the separate data values (eg Celsius, darKness, Rain sensor, Humidity, Errors)
unsigned int rainSensor;
signed char temperature;
unsigned char digitalRain;
unsigned char humidity;
unsigned int darkness;
unsigned char error;
unsigned int rainMap[5][2] = { { 0, 200 }, { 201, 400 }, { 401, 700 }, { 701, 1015 }, { 1016, 1023 } };
char rainMapDesc[5][9] = { "Stormy  ", "Pouring ", "Light   ", "Drizzle ", "No Rain " };

// darker (higher value) than this) means too dark
#define daylight 700

// Basic beeper to indicate rain has been detected
#define beepPin 12

/* Touch pins, foil sensor on last pin, 1M ohm resistor from first pin to second pin
 * and 100pF - 1000pF capacitor from second pin to ground for stability
 * Increase resistor to 2M - 10M ohm if you want it to detect presence rather than touch
 * but watch out for false triggering!
 */
#define touchPins 6,5
#define touchLED 9
bool isActive = true;

// LED pin to indicate system is functioning (receiving data)
#define allOKpin 7

// MP3 player pins and fixed values for communicating with module
int mp3Pin[2] = { 10, 11 };
#define startByte 0x7E
#define endByte 0xEF
#define versionByte 0xFF
#define dataLength 0x06
#define infoReq 0x01

// Global objects
CapacitiveSensor touch = CapacitiveSensor(touchPins);
SoftwareSerial mp3(mp3Pin[0], mp3Pin[1]); // Rx, Tx

// Define the bit patters for each of our custom chars. These are 5 bits wide and 8 dots deep
uint8_t custChar[8][8] = {
//
		{ 31, 31, 31, 0, 0, 0, 0, 0 },      // Small top line - 0
		{ 0, 0, 0, 0, 0, 31, 31, 31 },      // Small bottom line - 1
		// This shows an alternative way of defining custom characters, a bit more visually
		{
		B11111,
		B00000,
		B00000,
		B00000,
		B00000,
		B00000,
		B00000,
		B11111, },
		//{31, 0, 0, 0, 0, 0, 0, 31},		// Small lines top and bottom -2
		{ 0, 0, 0, 0, 0, 0, 0, 31 },		// Thin bottom line - 3
		{ 31, 31, 31, 31, 31, 31, 15, 7 },	// Left bottom chamfer full - 4
		{ 28, 30, 31, 31, 31, 31, 31, 31 },	// Right top chamfer full -5
		{ 31, 31, 31, 31, 31, 31, 30, 28 },	// Right bottom chamfer full -6
		{ 7, 15, 31, 31, 31, 31, 31, 31 },	// Left top chamfer full -7
		};

// Define our numbers 0 thru 9 plus minus symbol (can't use any more custom chars above, boo)
// 254 is blank and 255 is the "Full Block"
uint8_t bigNums[10][6] = {
//
		{ 7, 0, 5, 4, 1, 6 },         //0
		{ 0, 5, 254, 1, 255, 1 },     //1
		{ 0, 2, 5, 7, 3, 1 },         //2
		{ 0, 2, 5, 1, 3, 6 },         //3
		{ 7, 3, 255, 254, 254, 255 }, //4
		{ 7, 2, 0, 1, 3, 6 },         //5
		{ 7, 2, 0, 4, 3, 6 },         //6
		{ 0, 0, 5, 254, 7, 254 },     //7
		{ 7, 2, 5, 4, 3, 6 },         //8
		{ 7, 2, 5, 1, 3, 6 },         //9
		};

// Tell the LCD what type we have (eg 16 x 2 or 20 x 4)
LiquidCrystal_I2C lcd(I2C_ADDR, 20, 4);

//  Forward declarations
int splitData(String array, char startChar, char endChar);

// Generic array size fetcher
template<typename T, unsigned S>
inline unsigned arraysize(const T (&v)[S]) {
	return S;
}

// ----------------------------------------------------------------------------
// One size fits all Serial Monitor debugging messages
// ----------------------------------------------------------------------------
template<typename T>
void debugPrint(T printMe, bool newLine = false) {
	if (isDebug) {
		if (newLine) {
			Serial.println(printMe);
		}
		else {
			Serial.print(printMe);
		}
		Serial.flush();
	}
}

// -------------------------------------------------------------------------------------
// SETUP     SETUP     SETUP     SETUP     SETUP     SETUP     SETUP     SETUP     SETUP
// -------------------------------------------------------------------------------------
void setup() {
	// Required for DR3100
	vw_set_ptt_inverted(true);

	// Receive pin
	vw_set_rx_pin(2);

	// Bits per second (bps)
	vw_setup(2000);

	//Serial Monitor window bps
	Serial.begin(9600);

	// Start the receiver PLL running
	vw_rx_start();

	// Capacitive touch sensor calibration
	touch.set_CS_AutocaL_Millis(5000);
	pinMode(touchLED, OUTPUT);

	// Reduce volume of MP3 player (default = max vol)
	// command code 0x06 followed by high byte / low byte
	mp3.begin(9600);

	// Delay required before communicating with MP3 player
	delay(1000);
	sendMP3Command(0x06, 0, 25);

	// MP3 Equaliser setting (optional)
	sendMP3Command(0x07, 0, 5);

	// Play specific numbered track (0, 1 = track file name beginning "0001")
	// As long as file names start with four digits they can contain anything else
	// such as 0001LightRain.mp3
	sendMP3Command(0x12, 0, 99);

	// Initialise the LCD: turn on backlight and ensure cursor top left
	lcd.begin();
	lcd.backlight();
	delay(500);

	// Create custom LCD character map (8 characters only!)
	for (unsigned int cnt = 0; cnt < sizeof(custChar) / 8; cnt++) {
		lcd.createChar(cnt, custChar[cnt]);
	}
	lcd.home();
	lcd.print("- Benny's Cat Run -");

	// Degrees Centigrade
	lcd.setCursor(10, 2);
	lcd.print((char) 161);
	lcd.setCursor(10, 3);
	lcd.print("C");

	// Below zero indicator (Minus)
	lcd.setCursor(0, 3);
	lcd.print("+");

	// First digit
	printBigNum(9, 2, 2);

	// Second digit
	printBigNum(9, 6, 2);

	// To run the LCD test uncomment next line
	//lcdTest();

	// All done
	debugPrint("Setup v1.2 completed successfully.", true);
}

// -------------------------------------------------------------------------------------
// LOOP     LOOP     LOOP     LOOP     LOOP     LOOP     LOOP     LOOP     LOOP     LOOP
// -------------------------------------------------------------------------------------
void loop() {
	// Get touch sensor input
	getTouch();

	// Get data (stored in global vars for convenience) but could have used pointers here
	if (getData()) {

		// map the rain reading (0 - 1023) into various ranges. We're limiting the sensor here to values
		// between 100 (very wet) and 1000 (dry) and then mapping those into specific ranges.
		char rainLevel = 0;
		for (uint8_t cnt = 0; cnt < 5; cnt++) {
			if (rainSensor >= rainMap[cnt][0] && rainSensor <= rainMap[cnt][1]) {
				rainLevel = cnt;
				break;
			}
		}

		// Send rain description to LCD
		printAt(2, 12, rainMapDesc[(int) rainLevel]);

		// Print humidity
		printAt(3, 12, (char *) "RH ");
		char buf[2];
		printAt(3, 15, itoa((int) humidity, buf, 10));
		printAt(3, 17, (char *) "%");

		// Print darkness (reason for no warning)
		if (darkness >= daylight) {
			printAt(1, 12, (char *) "Too Dark");
		}
		else {
			printAt(1, 12, (char *) "        ");
		}

		// Only do anything whilst it is still daytime (daylight)
		if (isActive && darkness < daylight) {
			// Take required action depending on level of dampness
			switch (rainLevel) {

				case 0:
					// Storm
					for (int cnt = 0; cnt < 4 - rainLevel; cnt++) {
						tone(beepPin, 1500, 150);
						delay(450);
					}

					// Play MP3 warning - file starting "0003"
					sendMP3Command(0x12, 0, 3);
					break;

				case 1:
					// Rain
					for (int cnt = 0; cnt < 4 - rainLevel; cnt++) {
						tone(beepPin, 1500, 150);
						delay(450);
					}

					// Play MP3 warning - file starting "0002"
					sendMP3Command(0x12, 0, 2);
					break;

				case 2:
					// Light rain
				case 3:
					// Drizzle
					for (int cnt = 0; cnt < 4 - rainLevel; cnt++) {
						tone(beepPin, 1500, 150);
						delay(450);
					}

					// Play MP3 warning - file starting "0001"
					sendMP3Command(0x12, 0, 1);
					break;

				case 4:
					// Dry(-ish)
					break;
			}

		}
	}
}

// -------------------------------------------------------------------------------------
// GET DATA     GET DATA     GET DATA     GET DATA     GET DATA     GET DATA     GET DATA
// -------------------------------------------------------------------------------------
bool getData() {
	// Array to hold the received data
	uint8_t buf[VW_MAX_MESSAGE_LEN];
	uint8_t buflen = VW_MAX_MESSAGE_LEN;

	// Just for monitoring purposes
	static int loopCnt = 0;

	// So we know that we have got some data
	bool dataFlag = false;

	// Get the data into the buffer variable, length of data also received
	while (vw_get_message(buf, &buflen)) {
		// String object array to hold final output and each char received
		String output = "";

		debugPrint("#");
		debugPrint(++loopCnt);
		debugPrint(" ");

		// Append each byte received to output string and print the raw data
		for (int i = 0; i < buflen; i++) {
			char hexData = buf[i];
			output += hexData;
			debugPrint(hexData);
		}
		debugPrint("", true);

		// Make sure we have received the entire string (not truncated)
		if (buflen < 12) {
			debugPrint("Truncated data received, bytes: ");
			debugPrint(buflen, true);
			break;
		}

		// Set flag
		dataFlag = true;

		// Now split the data into constituent parts
		debugPrint("Rain Sensor: ");
		rainSensor = splitData(output, 'R', 'C');
		debugPrint(rainSensor);

		debugPrint("\tTemperature (C): ");
		temperature = splitData(output, 'C', 'D');
		debugPrint(temperature);
		printTemperature(temperature);

		debugPrint("\tHumidity: ");
		humidity = splitData(output, 'H', 'K');
		debugPrint(humidity);

		debugPrint("\tLight Level (lower is brighter): ");
		darkness = splitData(output, 'K', 'E');
		debugPrint(darkness);

		debugPrint("\tErrors: ");
		error = splitData(output, 'E', '\0');
		debugPrint(error);

		debugPrint("\tRaining? ");
		digitalRain = splitData(output, 'D', 'H');
		debugPrint(digitalRain == 1 ? "No" : "Yes", true);
	}

	return dataFlag;
}

// -------------------------------------------------------------------------------------
// This is not a generic function but tightly coupled to the data format
//
// R1022C25D1H56K128E00
//
// -------------------------------------------------------------------------------------
int splitData(String array, char startChar, char endChar) {

	// Determine where the data substring starts
	int posStart = array.indexOf(startChar);

	// And ends (null means the end of the array)
	unsigned int posEnd;
	if (endChar != '\0') {
		posEnd = array.indexOf(endChar);
	}
	else {
		posEnd = array.length();
	}

	// Get all the characters from the string that make up the value
	// eg "1", "0", "1", "4"
	String value = array.substring(posStart + 1, posEnd);

	// Convert that string representation into an integer value
	int val = value.toInt();

	// All done
	return val;
}

// -------------------------------------------------------------------------------------
// MP3 COMMAND AND DATA     MP3 COMMAND AND DATA     MP3 COMMAND AND DATA     MP3 COMMAN
// -------------------------------------------------------------------------------------
void sendMP3Command(byte Command, byte Param1, byte Param2) {

	// Calculate the checksum
	unsigned int checkSum = -(versionByte + dataLength + Command + infoReq + Param1 + Param2);

	// Construct the command line
	byte commandBuffer[10] = { startByte, versionByte, dataLength, Command, infoReq, Param1, Param2, highByte(checkSum),
			lowByte(checkSum), endByte };

	for (int cnt = 0; cnt < 10; cnt++) {
		mp3.write(commandBuffer[cnt]);
	}

	// Delay needed between successive commands
	delay(30);
}

// Print the current temperature
void printTemperature(int currTemp) {

	// Below zero?
	if (currTemp < 0) {
		lcd.setCursor(0, 2);
		lcd.print("_");
		lcd.setCursor(0, 3);
		lcd.print(" ");
	}
	else {
		lcd.setCursor(0, 2);
		lcd.print(" ");
		lcd.setCursor(0, 3);
		lcd.print("+");
	}

	// First digit
	int firstDigit = (currTemp / 10);
	//debugPrint("First digit:");
	//debugPrint(firstDigit);
	printBigNum(firstDigit, 2, 2);

	// Second Digit
	int secondDigit = currTemp % 10;
	//debugPrint("  Second digit:");
	//debugPrint(secondDigit, true);
	printBigNum(secondDigit, 6, 2);
}

// -----------------------------------------------------------------
// Print big number over 2 lines, 3 columns per half digit
// -----------------------------------------------------------------
void printBigNum(int number, int startCol, int startRow) {

	// Position cursor to requested position (each char takes 3 cols plus a space col)
	lcd.setCursor(startCol, startRow);

	// Each number split over two lines, 3 chars per line. Retrieve character
	// from the main array to make working with it here a bit easier.
	uint8_t thisNumber[6];
	for (int cnt = 0; cnt < 6; cnt++) {
		thisNumber[cnt] = bigNums[number][cnt];
	}

	// First line (top half) of digit
	for (int cnt = 0; cnt < 3; cnt++) {
		lcd.print((char) thisNumber[cnt]);
	}

	// Now position cursor to next line at same start column for digit
	lcd.setCursor(startCol, startRow + 1);

	// 2nd line (bottom half)
	for (int cnt = 3; cnt < 6; cnt++) {
		lcd.print((char) thisNumber[cnt]);
	}
}

// -----------------------------------------------------------------
// Simplified LCD printing helper
// -----------------------------------------------------------------
void printAt(int Row, int Col, char msg[]) {
	lcd.setCursor(Col, Row);
	lcd.print(msg);
}

// -----------------------------------------------------------------
// Simplified LCD printing helper
// -----------------------------------------------------------------
void getTouch() {
	static long prevMillis = millis();

	if (millis() - prevMillis > 500) {
		long touchPad = touch.capacitiveSensor(30);
		//debugPrint("Touch: ");
		//debugPrint(touchPad, true);
		if (touchPad > 200) {
			isActive = !isActive;
			tone(beepPin, 1500, 100);
		}
		digitalWrite(touchLED, isActive ? HIGH : LOW);
		prevMillis = millis();

		// Don't exit this routine until user lets go of touch pad
		while (touch.capacitiveSensor(30) > 100) {delay(10);}
	}
}

// -----------------------------------------------------------------
// Test method to prove LCD prints large fonts OK (see video #23)
// -----------------------------------------------------------------
void lcdTest() {
	lcd.clear();

	lcd.print("CURRENT LOUNGE TEMP");
	lcd.setCursor(16, 2);
	lcd.print((char) 161);
	lcd.setCursor(16, 3);
	lcd.print("C");
	lcd.setCursor(11, 3);
	lcd.print(",");
	printBigNum(2, 4, 2);
	printBigNum(4, 8, 2);
	printBigNum(9, 12, 2);

	while (true) {
		for (int cnt = 8; cnt >= 0; cnt--) {
			delay(1000);
			printBigNum(cnt, 12, 2);
		}

		for (int cnt = 1; cnt <= 9; cnt++) {
			delay(1000);
			printBigNum(cnt, 12, 2);
		}
	}
}

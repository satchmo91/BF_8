// Libraries
#include <MIDIUSB.h>
#include <MIDIUSB_Defs.h>
#include <TM1637Display.h>
#include <Bounce2.h>
#include <EEPROM.h>

// Pin definitions
#define CD4051_INH 19    // CD4051 inhibit pin
#define CD4051_A   9    // CD4051 control pin A
#define CD4051_B   10   // CD4051 control pin B
#define CD4051_C   15   // CD4051 control pin C
#define BUTTON_PINS {1, 2, 3, 4, 5, 6, 7, 8} // 8 buttons
#define PROGRAM_BUTTON 0 // Programming button
#define TM1637_CLK 16   // TM1637 CLK pin
#define TM1637_DIO 14   // TM1637 DIO pin
#define ANALOG_PIN A0   // CD4051 output to ADC

// MIDI settings
#define MIDI_CHANNEL_DEFAULT 1  // Default MIDI channel
const int CC_FADERS_DEFAULT[] = {1, 2, 3, 4, 5, 6, 7, 8}; // Default fader CCs
const int CC_BUTTONS_DEFAULT[] = {9, 10, 11, 12, 13, 14, 15, 16}; // Default button CCs

// Smoothing settings
#define NUM_FADERS 8
#define NUM_BUTTONS 8
#define AVERAGE_SAMPLES 20 // Number of ADC readings to average
#define SMOOTHING_DELAY 500

// Program mode settings
#define PROGRAM_TIMEOUT 4000 // ms to revert to operational mode (4s)

// TM1637 display
TM1637Display display(TM1637_CLK, TM1637_DIO);

// Display Library for future use
//                         0b0 gfedcba
// uint8_t LET_A = 0b01110111;
// uint8_t LET_B = 0b01111100;
// uint8_t LET_C = 0b00111101;
// uint8_t LET_D = 0b01011110;
// uint8_t LET_E = 0b01111001;
// uint8_t LET_G = 0b01101111;
// uint8_t LET_H = 0b01110110;
// uint8_t LET_I = 0b00000110;
// uint8_t LET_L = 0b00111000;
// uint8_t LET_N = 0b00110111;
// uint8_t LET_O = 0b00111111;
// uint8_t LET_P = 0b01110011;
// uint8_t LET_Q = 0b01100111;
// uint8_t LET_R = 0b01010000;
// uint8_t LET_S = 0b01101101;
// uint8_t LET_U = 0b00111110;

// Custom segment patterns
const uint8_t FADER_IDS[NUM_FADERS][4] = {
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_B | SEG_C, 0, 0}, // "F1"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_B | SEG_D | SEG_E | SEG_G, 0, 0}, // "F2"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_B | SEG_C | SEG_D | SEG_G, 0, 0}, // "F3"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_B | SEG_C | SEG_F | SEG_G, 0, 0},  // "F4"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, 0, 0},  // "F5"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, 0, 0},  // "F6"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_B | SEG_C, 0, 0},  // "F7"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, 0, 0},  // "F8"
};
const uint8_t CCF[] = {SEG_A | SEG_D | SEG_E | SEG_F, SEG_A | SEG_D | SEG_E | SEG_F, SEG_A | SEG_E | SEG_F | SEG_G, 0}; // "CCF"
const uint8_t CCB[] = {SEG_A | SEG_D | SEG_E | SEG_F, SEG_A | SEG_D | SEG_E | SEG_F, SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, 0}; // "CCB"
const uint8_t CH[] = {SEG_A | SEG_D | SEG_E | SEG_F, SEG_B | SEG_C | SEG_E | SEG_F | SEG_G, 0, 0}; // "CH"
const uint8_t BF8[] = {SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G, SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,0};

// Button debouncing
Bounce buttons[NUM_BUTTONS] = {Bounce(), Bounce(), Bounce(), Bounce(), Bounce(), Bounce(), Bounce(), Bounce()};
Bounce programButton = Bounce();

// Variables
int ccFaders[NUM_FADERS] = {CC_FADERS_DEFAULT}; // Fader CCs (EEPROM)
int ccButtons[NUM_BUTTONS] = {CC_BUTTONS_DEFAULT}; // Button CCs (EEPROM)
int midiChannel = MIDI_CHANNEL_DEFAULT; // MIDI channel (EEPROM)
int lastFaderValues[NUM_FADERS] = {-1, -1, -1, -1, -1, -1, -1, -1}; // Store last fader MIDI value
int lastButtonStates[NUM_BUTTONS] = {0, 0, 0, 0, 0, 0, 0, 0}; // Store last button state
int readings[NUM_FADERS][AVERAGE_SAMPLES]; // Moving average arrays
int readIndices[NUM_FADERS] = {0, 0, 0, 0, 0, 0, 0, 0}; // Current reading indices
long totals[NUM_FADERS] = {0, 0, 0, 0, 0, 0, 0, 0}; // Running totals
unsigned long buttonPressTime = 0; // Track button press time
int activeFader = 0; // Last-touched fader (0-7)
bool showingFaderID = false; // Track if showing "F1"-"F8"
unsigned long lastMessage; // Time that the last MIDI message was sent

// Program mode variables
int programMode = 0; // 0: off, 1: fader CC, 2: button CC, 3: channel
unsigned long lastProgramActivity = 0; // Track last activity for timeout
int programFader = -1; // Fader being programmed

void setup() {
  // Initialize pins
  pinMode(CD4051_INH, OUTPUT);
  pinMode(CD4051_A, OUTPUT);
  pinMode(CD4051_B, OUTPUT);
  pinMode(CD4051_C, OUTPUT);
  digitalWrite(CD4051_INH, LOW); // Enable CD4051

  // Initialize buttons
  int buttonPins[] = BUTTON_PINS;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    buttons[i].attach(buttonPins[i], INPUT);
    buttons[i].interval(5); // 5ms debounce time
  }

  // Initialize program button
  programButton.attach(PROGRAM_BUTTON, INPUT);
  programButton.interval(5);

  // Initialize TM1637 display
  display.setBrightness(4); // 0-7, 7 being max
  display.clear(); //Start from blank

  // Initialize readings arrays
  for (int i = 0; i < NUM_FADERS; i++) {
    for (int j = 0; j < AVERAGE_SAMPLES; j++) {
      readings[i][j] = 0;
    }
  }

  // Load settings from EEPROM
  for (int i = 0; i < NUM_FADERS; i++) {
    ccFaders[i] = EEPROM.read(i); //Fader address 0-7
    ccButtons[i] = EEPROM.read(i + NUM_BUTTONS); //Button addresses 8-15
    if (ccFaders[i] > 127 || ccFaders[i] < 1) ccFaders[i] = CC_FADERS_DEFAULT[i]; // Check if faders are within CC range, otherwise set them to default
    if (ccButtons[i] > 127 || ccButtons[i] < 1) ccButtons[i] = CC_BUTTONS_DEFAULT[i]; //Check if buttons are within CC range, set to default other
  }
  midiChannel = EEPROM.read(NUM_FADERS * 2); //MidiChannel address 16
  if (midiChannel < 1 || midiChannel > 16) midiChannel = MIDI_CHANNEL_DEFAULT; //Check if midiChannel is within channel range, otherwise set it to defualt
}

void loop() {
  Serial.println(activeFader);
  // Handle program button
  programButton.update();
  if (programButton.rose()) { //Only trigger button on rising edge
    // Cycle through program modes (0 -> 1 -> 2 -> 3 -> 0)
    programMode = (programMode + 1) % 4;
    lastProgramActivity = millis(); // Update activity time
    programFader = -1; // Reset programming fader
    if (programMode == 1) display.setSegments(CCF); // Show "CCF"
    else if (programMode == 2) display.setSegments(CCB); // Show "CCB"
    else if (programMode == 3) display.setSegments(CH); // Show "CH"
    else {
      display.clear();
      updateDisplay(lastFaderValues[activeFader]); // Restore fader value
    }
  }

  // Check for program mode timeout
  if (programMode != 0 && millis() - lastProgramActivity > PROGRAM_TIMEOUT) { // Check if we're in a program mode and the timeout period has elapsed
    if (programFader >= 0) {
      // Save settings if changes were made
      if (programMode == 1) {
        EEPROM.update(programFader, ccFaders[programFader]);
      } else if (programMode == 2) {
        EEPROM.update(programFader + NUM_FADERS, ccButtons[programFader]);
      } else if (programMode == 3) {
        EEPROM.update(NUM_FADERS * 2, midiChannel);
      }
    }
    programMode = 0; // Exit program mode
    display.clear();
    updateDisplay(lastFaderValues[activeFader]); // Restore fader value
  }

  // Normal mode: Read faders and buttons
  if (!programMode) {
    // Read faders via CD4051
    for (int i = 0; i < NUM_FADERS; i++) { //loop over each fader in decimal
      digitalWrite(CD4051_A, i & 0x01); // this line and the next two convert i from decimal to binary and properly address the CD4051 to the channel we want
      digitalWrite(CD4051_B, (i >> 1) & 0x01);
      digitalWrite(CD4051_C, (i >> 2) & 0x01);
      //Original filter
      int analogValue = analogRead(ANALOG_PIN);//read the analog pin
      totals[i] -= readings[i][readIndices[i]]; // remove the the value at the fader index for averaging
      readings[i][readIndices[i]] = analogValue; // insert the current analog value into the averaging array
      totals[i] += analogValue; //add the new value to the totals
      readIndices[i] = (readIndices[i] + 1) % AVERAGE_SAMPLES;
      int average = totals[i] / AVERAGE_SAMPLES;
      int midiValue = map(average, 0, 1023, 0, 128);

      if ((midiValue != lastFaderValues[i] && millis() - lastMessage < SMOOTHING_DELAY) || (abs(midiValue - lastFaderValues[i]) > 1 && millis() - lastMessage > SMOOTHING_DELAY)) {
        midiValue == 128 ? midiValue = 127 : midiValue = midiValue;
        sendMIDI(ccFaders[i], midiValue); // Send MIDI if fader value has changed
        lastMessage = millis();
        if (!showingFaderID && activeFader == i) {
          updateDisplay(midiValue);
        }
        lastFaderValues[i] = midiValue;
        activeFader = i;
      }
    }

    // Read buttons
    for (int i = 0; i < NUM_BUTTONS; i++) {
      buttons[i].update(); // Update each button state
      int buttonState = buttons[i].read(); // Read that state
      if (buttonState != lastButtonStates[i]) { // Compare to last state
        if (buttonState == HIGH) { // If state changed from LOW to HIGH (this could also be done with buttons[i].rose() )
          sendMIDI(ccButtons[i], 127); // Send button CC with value 127
          display.setSegments(FADER_IDS[i], 4, 0); // Display fader button ID
          showingFaderID = true;
          buttonPressTime = millis(); // log the time the display changed for timeout purposes below
          activeFader = i;
        } else {
          sendMIDI(ccButtons[i], 0); // Send button CC with value 0
          display.setSegments(FADER_IDS[i], 4, 0); // Display fader button ID
          showingFaderID = true;
          buttonPressTime = millis(); // log the time the display changed for timeout purposes below
          activeFader = i;
          }
        lastButtonStates[i] = buttonState;
      }
    }

    // Revert to fader value after 1 second
    if (showingFaderID && millis() - buttonPressTime > 1000) {
      display.clear();
      updateDisplay(lastFaderValues[activeFader]);
      showingFaderID = false;
    }
  } else {
    // Program mode: Read faders to set CCs or channel
    for (int i = 0; i < NUM_FADERS; i++) { //Same logic as above for operation mode. Consider converting this section to a function to avoid writing twice.
      digitalWrite(CD4051_A, i & 0x01);
      digitalWrite(CD4051_B, (i >> 1) & 0x01);
      digitalWrite(CD4051_C, (i >> 2) & 0x01);
      int analogValue = analogRead(ANALOG_PIN);
      totals[i] -= readings[i][readIndices[i]];
      readings[i][readIndices[i]] = analogValue;
      totals[i] += analogValue;
      readIndices[i] = (readIndices[i] + 1) % AVERAGE_SAMPLES;
      int average = totals[i] / AVERAGE_SAMPLES;
      int value = map(average, 0, 1023, 0, 128);
      value == 128 ? value = 127 : value = value;

      if (value != lastFaderValues[i]) {
        if (programMode == 1) {
          ccFaders[i] = value;
          programFader = i;
          display.showNumberDec(value, false, 3, 1); // Show CC value
        } else if (programMode == 2) {
          ccButtons[i] = value;
          programFader = i;
          display.showNumberDec(value, false, 3, 1); // Show CC value
        } else if (programMode == 3) {
          value =  map(average, 0, 1023, 1, 17);
          value == 17 ? value = 16 : value = value; 
          midiChannel = value;
          programFader = i;
          display.showNumberDec(value, true, 2, 2); // Show channel (01-16)
          value = map(average, 0, 1023, 0, 128);
          value == 128 ? value = 127 : value = value;
        }
        lastFaderValues[i] = value;
        lastProgramActivity = millis(); // Update activity time
      }
    }
  }

  // Small delay to avoid overloading MIDI
  delay(10);
}

// Send MIDI CC message
void sendMIDI(byte cc, byte value) {
  midiEventPacket_t midiPacket = {0x0B, 0xB0 | (midiChannel - 1), cc, value};
  MidiUSB.sendMIDI(midiPacket);
  MidiUSB.flush();
}

// Update TM1637 display with MIDI value (0-127)
void updateDisplay(int value) {
  display.showNumberDec(value, false, 3, 1); // Show "_127" on right 3 digits
}

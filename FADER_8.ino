//Libraries
#include <MIDIUSB.h>
#include <frequencyToNote.h>
#include <MIDIUSB_Defs.h>
#include <pitchToFrequency.h>
#include <pitchToNote.h>
#include <TM1637Display.h>
#include <Bounce2.h>
#include <EEPROM.h>

// Pin definitions
#define CD4051_INH 19    // CD4051 inhibit pin
#define CD4051_A   9    // CD4051 control pin A
#define CD4051_B   10    // CD4051 control pin B
#define CD4051_C   15   // CD4051 control pin C
#define BUTTON_PINS {1,2,3,4,5,6,7,8} // 4 buttons
#define PROGRAM_BUTTON 0 // Programming button
#define TM1637_CLK 16   // TM1637 CLK pin
#define TM1637_DIO 14  // TM1637 DIO pin
#define ANALOG_PIN A0   // CD4051 output to ADC

// MIDI settings
#define MIDI_CHANNEL_DEFAULT 1  // Default MIDI channel
const int CC_FADERS_DEFAULT[] =  {1, 2, 3, 4, 5, 6, 7, 8}; // Default fader CCs
const int CC_BUTTONS_DEFAULT[] =  {9, 10, 11, 12, 13, 14, 15, 16}; // Default button CCs

//Smoothing settings
#define NUM_FADERS 8
#define NUM_BUTTONS 8
#define AVERAGE_SAMPLES 10 //Number of ADC readings to average

// Program mode settings
#define TAP_WINDOW 500  // ms for double/triple tap detection
#define HOLD_TIME 500   // ms to detect hold

// TM1637 display
TM1637Display display(TM1637_CLK, TM1637_DIO);

//Display Library for future use
//                         0b0 gfedcba
//uint8_t LET_A = 0b01110111;
//uint8_t LET_B = 0b01111100;
//uint8_t LET_C = 0b00111101;
//uint8_t LET_D = 0b01011110;
//uint8_t LET_E = 0b01111001;
//uint8_t LET_G = 0b01101111;
//uint8_t LET_H = 0b01110110;
//uint8_t LET_I = 0b00000110;
//uint8_t LET_L = 0b00111000;
//uint8_t LET_N = 0b00110111;
//uint8_t LET_O = 0b00111111;
//uint8_t LET_P = 0b01110011;
//uint8_t LET_Q = 0b01100111;
//uint8_t LET_R = 0b01010000;
//uint8_t LET_S = 0b01101101;
//uint8_t LET_U = 0b00111110;

// Custom segment patterns
const uint8_t FADER_IDS[NUM_FADERS][4] = {
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_B | SEG_C, 0, 0}, // "F1"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_B | SEG_D | SEG_E | SEG_G, 0, 0}, // "F2"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_B | SEG_C | SEG_D | SEG_G, 0, 0}, // "F3"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_B | SEG_C | SEG_F | SEG_G, 0, 0},  // "F4"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, 0, 0},  // "F5"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, 0, 0},  // "F6"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A | SEG_B | SEG_C, 0, 0},  // "F7"
    {SEG_A | SEG_E | SEG_F | SEG_G, SEG_A |SEG_B | SEG_C | SEG_D | SEG_E| SEG_F | SEG_G, 0, 0},  // "F8"
};
const uint8_t CCF[] = {SEG_A | SEG_D | SEG_E | SEG_F, SEG_A | SEG_D | SEG_E | SEG_F, SEG_A | SEG_E | SEG_F | SEG_G, 0}; // "CCF"
const uint8_t CCB[] = {SEG_A | SEG_D | SEG_E | SEG_F, SEG_A | SEG_D | SEG_E | SEG_F, SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, 0}; // "CCB"
const uint8_t CH[] = {SEG_A | SEG_D | SEG_E | SEG_F, SEG_B | SEG_C | SEG_E | SEG_F | SEG_G, 0, 0}; // "CH"

// Button debouncing
Bounce buttons[NUM_BUTTONS] = {Bounce(), Bounce(), Bounce(), Bounce(), Bounce(), Bounce(), Bounce(), Bounce()};
Bounce programButton = Bounce();

// Variables
int ccFaders[NUM_FADERS] = {CC_FADERS_DEFAULT}; // Fader CCs (EEPROM)
int ccButtons[NUM_BUTTONS] = {CC_BUTTONS_DEFAULT}; // Button CCs (EEPROM)
int midiChannel = MIDI_CHANNEL_DEFAULT; // MIDI channel (EEPROM)
int lastFaderValues[NUM_FADERS] = {-1, -1, -1, -1,-1, -1, -1, -1}; // Store last fader MIDI value
int lastButtonStates[NUM_BUTTONS] = {0, 0, 0, 0,0, 0, 0, 0}; // Store last button state
int readings[NUM_FADERS][AVERAGE_SAMPLES]; // Moving average arrays
int readIndices[NUM_FADERS] = {0, 0, 0, 0,0, 0, 0, 0}; // Current reading indices
long totals[NUM_FADERS] = {0, 0, 0, 0,0, 0, 0, 0}; // Running totals
unsigned long buttonPressTime = 0; // Track button press time
int activeFader = 0; // Last-touched fader (0-3)
bool showingFaderID = false; // Track if showing "F1"-"F4"

// Program mode variables
int programMode = false; // 0: off, 1: fader CC, 2: button CC, 3: channel
unsigned long lastProgramPress = 0; // Last program button press
int tapCount = 0; // Track taps for double/triple tap
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
  display.clear();

  // Initialize readings arrays
  for (int i = 0; i < NUM_FADERS; i++) {
    for (int j = 0; j < AVERAGE_SAMPLES; j++) {
      readings[i][j] = 0;
    }
  }

  // Load settings from EEPROM
  for (int i = 0; i < NUM_FADERS; i++) {
    ccFaders[i] = EEPROM.read(i);
    ccButtons[i] = EEPROM.read(i + NUM_BUTTONS);
    if (ccFaders[i] > 127) ccFaders[i] = CC_FADERS_DEFAULT[i];
    if (ccButtons[i] > 127) ccButtons[i] = CC_BUTTONS_DEFAULT[i];
  }
  midiChannel = EEPROM.read(NUM_FADERS * 2);
  if (midiChannel < 1 || midiChannel > 16) midiChannel = MIDI_CHANNEL_DEFAULT;
  
}

void loop() {
  // Handle program button
  programButton.update();
  if (programButton.fell()) {
    unsigned long currentTime = millis();
    if (currentTime - lastProgramPress < TAP_WINDOW) {
      tapCount++;
    } else {
      tapCount = 1;
    }
    lastProgramPress = currentTime;
  }


  if (programButton.read() == HIGH && programButton.currentDuration() > HOLD_TIME) {
    if (!programMode) {
      programMode = tapCount; // 1: fader CC, 2: button CC, 3: channel
      tapCount = 0; // Reset taps
      programFader = -1; // Reset programming fader
      if (programMode == 1) display.setSegments(CCF); // Show "CCF"
      else if (programMode == 2) display.setSegments(CCB); // Show "CCB"
      else if (programMode == 3) display.setSegments(CH); // Show "CH"
    }
  } else if (programButton.fell() && programMode) { 
    // Save settings on release
    if (programMode == 1 && programFader >= 0) {
      EEPROM.update(programFader, ccFaders[programFader]);
    } else if (programMode == 2 && programFader >= 0) {
      EEPROM.update(programFader + NUM_FADERS, ccButtons[programFader]);
    } else if (programMode == 3 && programFader >= 0) {
      EEPROM.update(NUM_FADERS * 2, midiChannel);
    }
    programMode = 0; // Exit program mode
    display.clear();
    updateDisplay(lastFaderValues[activeFader]); // Restore fader value
  }

  // Normal mode: Read faders and buttons
  if (!programMode) {
    // Read faders via CD4051
    for (int i = 0; i < NUM_FADERS; i++) {
      digitalWrite(CD4051_A, i & 0x01);
      digitalWrite(CD4051_B, (i >> 1) & 0x01);
      digitalWrite(CD4051_C, (i >> 2) & 0x01);
      int analogValue = analogRead(ANALOG_PIN);
      totals[i] -= readings[i][readIndices[i]];
      readings[i][readIndices[i]] = analogValue;
      totals[i] += analogValue;
      readIndices[i] = (readIndices[i] + 1) % AVERAGE_SAMPLES;
      int average = totals[i] / AVERAGE_SAMPLES;
      int midiValue = map(average, 0, 1023, 0, 127);

      if (midiValue != lastFaderValues[i]) {
        sendMIDI(ccFaders[i], midiValue);
        if (!showingFaderID && activeFader == i) {
          updateDisplay(midiValue);
        }
        lastFaderValues[i] = midiValue;
        activeFader = i;
      }
    }

    // Read buttons
    for (int i = 0; i < NUM_BUTTONS; i++) {
      buttons[i].update();
      int buttonState = buttons[i].read();
      if (buttonState != lastButtonStates[i]) {
        if (buttonState == HIGH) {
          sendMIDI(ccButtons[i], 127);
          display.setSegments(FADER_IDS[i], 4, 0);
          showingFaderID = true;
          buttonPressTime = millis();
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
    for (int i = 0; i < NUM_FADERS; i++) {
      digitalWrite(CD4051_A, i & 0x01);
      digitalWrite(CD4051_B, (i >> 1) & 0x01);
      digitalWrite(CD4051_C, (i >> 2) & 0x01);
      int analogValue = analogRead(ANALOG_PIN);
      totals[i] -= readings[i][readIndices[i]];
      readings[i][readIndices[i]] = analogValue;
      totals[i] += analogValue;
      readIndices[i] = (readIndices[i] + 1) % AVERAGE_SAMPLES;
      int average = totals[i] / AVERAGE_SAMPLES;
      int value = (programMode == 3) ? map(average, 0, 1023, 1, 16) : map(average, 0, 1023, 0, 127);

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
          midiChannel = value;
          programFader = i;
          display.showNumberDec(value, true, 2, 2); // Show channel (01-16)
        }
        lastFaderValues[i] = value;
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

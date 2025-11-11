// Libraries
#include <MIDIUSB.h>
#include <MIDIUSB_Defs.h>
#include <TM1637Display.h>
#include <Bounce2.h>
#include <EEPROM.h>

// Pin definitions
#define CD4051_INH 19    // CD4051 inhibit pin
#define CD4051_A   9     // CD4051 control pin A
#define CD4051_B   10    // CD4051 control pin B
#define CD4051_C   15    // CD4051 control pin C
#define BUTTON_PINS {1, 2, 3, 4, 5, 6, 7, 8} // 8 buttons
#define PROGRAM_BUTTON 0 // Programming button
#define TM1637_CLK 16    // TM1637 CLK pin
#define TM1637_DIO 14    // TM1637 DIO pin
#define ANALOG_PIN A0    // CD4051 output to ADC

// MIDI settings
#define MIDI_CHANNEL_DEFAULT 1  // Default MIDI channel
#define MIDI_CHANNEL_MIN 1
#define MIDI_CHANNEL_MAX 16
#define MIDI_CC_MIN 1
#define MIDI_CC_MAX 127
#define MIDI_VALUE_MAX 127
#define MIDI_BUTTON_ON 127
#define MIDI_BUTTON_OFF 0

const int CC_FADERS_DEFAULT[] = {1, 2, 3, 4, 5, 6, 7, 8}; // Default fader CCs
const int CC_BUTTONS_DEFAULT[] = {9, 10, 11, 12, 13, 14, 15, 16}; // Default button CCs

// Smoothing settings
#define NUM_FADERS 8
#define NUM_BUTTONS 8
#define AVERAGE_SAMPLES 30 // Number of ADC readings to average (increased for better smoothing)
#define ADC_SAMPLES_PER_READ 5 // Number of ADC samples to take and average per fader read
#define CD4051_SETTLE_TIME 1 // Microseconds to wait after switching CD4051 channel
#define MIDI_DEADBAND 2 // Minimum MIDI value change required to update (hysteresis)
#define SMOOTHING_DELAY 500

// Program mode settings
#define PROGRAM_TIMEOUT 4000        // ms to revert to operational mode (4s)
#define DISPLAY_TIMEOUT 1000        // ms to revert display after button press
#define PROGRAM_MODE_OFF 0
#define PROGRAM_MODE_FADER_CC 1
#define PROGRAM_MODE_BUTTON_CC 2
#define PROGRAM_MODE_CHANNEL 3
#define PROGRAM_MODE_COUNT 4

// EEPROM addresses
#define EEPROM_ADDR_FADERS_START 0
#define EEPROM_ADDR_BUTTONS_START NUM_FADERS
#define EEPROM_ADDR_MIDI_CHANNEL (NUM_FADERS * 2)

// ADC and mapping constants
#define ADC_MAX 1023
#define MIDI_MAP_MAX 128
#define CHANNEL_MAP_MAX 17

// Display settings
#define DISPLAY_BRIGHTNESS 4
#define BUTTON_DEBOUNCE_INTERVAL 5
#define LOOP_DELAY 10

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
unsigned long lastMessage = 0; // Time that the last MIDI message was sent

// Program mode variables
int programMode = PROGRAM_MODE_OFF; // 0: off, 1: fader CC, 2: button CC, 3: channel
unsigned long lastProgramActivity = 0; // Track last activity for timeout
int programFader = -1; // Fader being programmed

// Forward declarations
void initializeHardware();
void initializeReadings();
void loadSettingsFromEEPROM();
void handleProgramButton();
void handleProgramModeTimeout();
void processNormalMode();
void processProgramMode();
int readFaderValue(int faderIndex);
int clampMidiValue(int value);
int clampMidiChannel(int value);
void selectCD4051Channel(int channel);
void updateFaderDisplay(int faderIndex, int midiValue);
void handleButtonPress(int buttonIndex, int buttonState);
void saveProgramModeSettings();

void setup() {
  initializeHardware();
  initializeReadings();
  loadSettingsFromEEPROM();
}

void loop() {
  programButton.update();
  
  handleProgramButton();
  handleProgramModeTimeout();
  
  if (programMode == PROGRAM_MODE_OFF) {
    processNormalMode();
  } else {
    processProgramMode();
  }
  
  delay(LOOP_DELAY);
}

void initializeHardware() {
  // Initialize CD4051 pins
  pinMode(CD4051_INH, OUTPUT);
  pinMode(CD4051_A, OUTPUT);
  pinMode(CD4051_B, OUTPUT);
  pinMode(CD4051_C, OUTPUT);
  digitalWrite(CD4051_INH, LOW); // Enable CD4051

  // Initialize buttons
  int buttonPins[] = BUTTON_PINS;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    buttons[i].attach(buttonPins[i], INPUT);
    buttons[i].interval(BUTTON_DEBOUNCE_INTERVAL);
  }

  // Initialize program button
  programButton.attach(PROGRAM_BUTTON, INPUT);
  programButton.interval(BUTTON_DEBOUNCE_INTERVAL);

  // Initialize TM1637 display
  display.setBrightness(DISPLAY_BRIGHTNESS);
  display.clear();
}

void initializeReadings() {
  for (int i = 0; i < NUM_FADERS; i++) {
    for (int j = 0; j < AVERAGE_SAMPLES; j++) {
      readings[i][j] = 0;
    }
  }
}

void loadSettingsFromEEPROM() {
  for (int i = 0; i < NUM_FADERS; i++) {
    ccFaders[i] = EEPROM.read(EEPROM_ADDR_FADERS_START + i);
    ccButtons[i] = EEPROM.read(EEPROM_ADDR_BUTTONS_START + i);
    
    if (ccFaders[i] > MIDI_CC_MAX || ccFaders[i] < MIDI_CC_MIN) {
      ccFaders[i] = CC_FADERS_DEFAULT[i];
    }
    if (ccButtons[i] > MIDI_CC_MAX || ccButtons[i] < MIDI_CC_MIN) {
      ccButtons[i] = CC_BUTTONS_DEFAULT[i];
    }
  }
  
  midiChannel = EEPROM.read(EEPROM_ADDR_MIDI_CHANNEL);
  if (midiChannel < MIDI_CHANNEL_MIN || midiChannel > MIDI_CHANNEL_MAX) {
    midiChannel = MIDI_CHANNEL_DEFAULT;
  }
}

void handleProgramButton() {
  if (programButton.rose()) {
    programMode = (programMode + 1) % PROGRAM_MODE_COUNT;
    lastProgramActivity = millis();
    programFader = -1;
    
    switch (programMode) {
      case PROGRAM_MODE_FADER_CC:
        display.setSegments(CCF);
        break;
      case PROGRAM_MODE_BUTTON_CC:
        display.setSegments(CCB);
        break;
      case PROGRAM_MODE_CHANNEL:
        display.setSegments(CH);
        break;
      default:
        display.clear();
        updateDisplay(lastFaderValues[activeFader]);
        break;
    }
  }
}

void handleProgramModeTimeout() {
  if (programMode != PROGRAM_MODE_OFF && 
      millis() - lastProgramActivity > PROGRAM_TIMEOUT) {
    saveProgramModeSettings();
    programMode = PROGRAM_MODE_OFF;
    display.clear();
    updateDisplay(lastFaderValues[activeFader]);
  }
}

void saveProgramModeSettings() {
  if (programFader >= 0 && programFader < NUM_FADERS) {
    switch (programMode) {
      case PROGRAM_MODE_FADER_CC:
        EEPROM.update(EEPROM_ADDR_FADERS_START + programFader, ccFaders[programFader]);
        break;
      case PROGRAM_MODE_BUTTON_CC:
        EEPROM.update(EEPROM_ADDR_BUTTONS_START + programFader, ccButtons[programFader]);
        break;
      case PROGRAM_MODE_CHANNEL:
        EEPROM.update(EEPROM_ADDR_MIDI_CHANNEL, midiChannel);
        break;
    }
  }
}

void processNormalMode() {
  // Read and process faders
  for (int i = 0; i < NUM_FADERS; i++) {
    int midiValue = readFaderValue(i);
    midiValue = clampMidiValue(midiValue);
    
    // Calculate change from last value
    int valueChange = abs(midiValue - lastFaderValues[i]);
    
    // Apply deadband/hysteresis: only update if change is significant enough
    // This prevents flickering when ADC oscillates between adjacent values
    bool shouldSend = false;
    if (lastFaderValues[i] == -1) {
      // First read - always send
      shouldSend = true;
    } else if (millis() - lastMessage < SMOOTHING_DELAY) {
      // Within smoothing delay - send if any change detected
      shouldSend = (valueChange > 0);
    } else {
      // Outside smoothing delay - require deadband threshold
      shouldSend = (valueChange >= MIDI_DEADBAND);
    }
    
    if (shouldSend) {
      sendMIDI(ccFaders[i], midiValue);
      lastMessage = millis();
      updateFaderDisplay(i, midiValue);
      lastFaderValues[i] = midiValue;
      activeFader = i;
    }
  }

  // Read and process buttons
  for (int i = 0; i < NUM_BUTTONS; i++) {
    buttons[i].update();
    int buttonState = buttons[i].read();
    if (buttonState != lastButtonStates[i]) {
      handleButtonPress(i, buttonState);
      lastButtonStates[i] = buttonState;
    }
  }

  // Revert to fader value after timeout
  if (showingFaderID && millis() - buttonPressTime > DISPLAY_TIMEOUT) {
    display.clear();
    updateDisplay(lastFaderValues[activeFader]);
    showingFaderID = false;
  }
}

void processProgramMode() {
  for (int i = 0; i < NUM_FADERS; i++) {
    int value = readFaderValue(i);
    value = clampMidiValue(value);
    
    if (value != lastFaderValues[i]) {
      if (programMode == PROGRAM_MODE_FADER_CC) {
        ccFaders[i] = value;
        programFader = i;
        display.showNumberDec(value, false, 3, 1);
      } else if (programMode == PROGRAM_MODE_BUTTON_CC) {
        ccButtons[i] = value;
        programFader = i;
        display.showNumberDec(value, false, 3, 1);
      } else if (programMode == PROGRAM_MODE_CHANNEL) {
        int average = totals[i] / AVERAGE_SAMPLES;
        value = map(average, 0, ADC_MAX, MIDI_CHANNEL_MIN, CHANNEL_MAP_MAX);
        value = clampMidiChannel(value);
        midiChannel = value;
        programFader = i;
        display.showNumberDec(value, true, 2, 2);
        // Restore value for lastFaderValues tracking
        value = readFaderValue(i);
        value = clampMidiValue(value);
      }
      lastFaderValues[i] = value;
      lastProgramActivity = millis();
    }
  }
}

int readFaderValue(int faderIndex) {
  selectCD4051Channel(faderIndex);
  delayMicroseconds(CD4051_SETTLE_TIME); // Allow signal to settle after channel switch
  
  // Take multiple ADC samples and average them to reduce noise
  long sampleSum = 0;
  for (int i = 0; i < ADC_SAMPLES_PER_READ; i++) {
    sampleSum += analogRead(ANALOG_PIN);
  }
  int analogValue = sampleSum / ADC_SAMPLES_PER_READ;
  
  // Update moving average
  totals[faderIndex] -= readings[faderIndex][readIndices[faderIndex]];
  readings[faderIndex][readIndices[faderIndex]] = analogValue;
  totals[faderIndex] += analogValue;
  readIndices[faderIndex] = (readIndices[faderIndex] + 1) % AVERAGE_SAMPLES;
  
  int average = totals[faderIndex] / AVERAGE_SAMPLES;
  return map(average, 0, ADC_MAX, 0, MIDI_MAP_MAX);
}

void selectCD4051Channel(int channel) {
  digitalWrite(CD4051_A, channel & 0x01);
  digitalWrite(CD4051_B, (channel >> 1) & 0x01);
  digitalWrite(CD4051_C, (channel >> 2) & 0x01);
}

int clampMidiValue(int value) {
  if (value == MIDI_MAP_MAX) {
    return MIDI_VALUE_MAX;
  }
  return value;
}

int clampMidiChannel(int value) {
  if (value == CHANNEL_MAP_MAX) {
    return MIDI_CHANNEL_MAX;
  }
  return value;
}

void updateFaderDisplay(int faderIndex, int midiValue) {
  if (!showingFaderID && activeFader == faderIndex) {
    updateDisplay(midiValue);
  }
}

void handleButtonPress(int buttonIndex, int buttonState) {
  if (buttonState == HIGH) {
    sendMIDI(ccButtons[buttonIndex], MIDI_BUTTON_ON);
  } else {
    sendMIDI(ccButtons[buttonIndex], MIDI_BUTTON_OFF);
  }
  
  display.setSegments(FADER_IDS[buttonIndex], 4, 0);
  showingFaderID = true;
  buttonPressTime = millis();
  activeFader = buttonIndex;
}

void sendMIDI(byte cc, byte value) {
  midiEventPacket_t midiPacket = {0x0B, 0xB0 | (midiChannel - 1), cc, value};
  MidiUSB.sendMIDI(midiPacket);
  MidiUSB.flush();
}

void updateDisplay(int value) {
  display.showNumberDec(value, false, 3, 1);
}

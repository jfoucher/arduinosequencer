#include "Wire.h"
#include "OPL2.h"
#include "instruments.h"
#include "Adafruit_MCP23017.h"
#include <Arduino.h>

#define DEBUG 1
#define CHIP1ADDR 0x21
#define CHIP2ADDR 0x20
// Rows are connected to ground
#define LED_ROWS 2
// Cols are connected to 5V
#define LED_COLS 3

#define ENCODER_TEMPO 0


Adafruit_MCP23017 mcp;
Adafruit_MCP23017 mcp2;
Adafruit_MCP23017 mcp3;

OPL2 opl2;

typedef struct  {
  int pin1;
  int pin2;
  int pin1_val;
  int pin2_val;
  int value;
  int max_value;
  int min_value;
  int step;
} Encoder;


Encoder encoders[] = {
  {8, 9, 0, 0, 100, 300, 20, 10}
};

int encoder0PinALast = LOW;
int encoder1PinALast = LOW;
int encoder2PinALast = LOW;
int encoder3PinALast = LOW;
int encoder4PinALast = LOW;
int encoder5PinALast = LOW;
int n = LOW;
int n1 = LOW;
int n2 = LOW;
int n3 = LOW;
int n4 = LOW;
int n5 = LOW;

int i = 0;
volatile bool drumsOn = true;
volatile bool changeModulator = false;
int note = 0;
int octave = 0;
int tempo = 100;

bool attackType = false;

int attackCarrier = 3;
int attackModulator = 3;

int decayCarrier = 3;
int decayModulator = 3;

int sustainCarrier = 3;
int sustainModulator = 3;

int releaseCarrier = 3;
int releaseModulator = 3;

int noteCarrier = 30;
int noteModulator = 1;

int selectedChannel = 0;

int modulatorWaveform = 0;
int carrierWaveform = 0;

volatile boolean awakenByInterrupt = false;

int leds[LED_ROWS * LED_COLS] = {false};

//Data for 6 voices, Carrier ADSR, carrier octave, carrier note, carrier wave form, carrier volume, modulator ADSR, modulator multiplier
int noteData[6][11] = {
  // A  D  S  R  WF Vo  MA MD MS MR
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0},
};

int tones[6][8][3] = {
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
};

// Array of booleans.
// 32 for each voice.
// First 6 are melodic voices, last 5 are percussion voices
bool playNotes[11][32];

void setup() {
  Serial.begin(9600);
  mcp.begin();

  for(int i = 0; i <= 15;i++) {
    mcp.pinMode(i, INPUT);
    mcp.pullUp(i, HIGH);
  }
  
  
  mcp2.begin(1);
  mcp2.pinMode(8, OUTPUT);
  mcp2.pinMode(0, OUTPUT);

  mcp3.begin(2);
  mcp3.pinMode(0, OUTPUT);
  mcp3.pinMode(1, OUTPUT);
  mcp3.pinMode(2, OUTPUT);
  mcp3.pinMode(3, OUTPUT);
  mcp3.pinMode(4, OUTPUT);
    
  opl2.init();

  // Set percussion mode and load instruments.
  opl2.setPercussion(true);
  opl2.setInstrument(0, INSTRUMENT_BDRUM1);
  opl2.setInstrument(0, INSTRUMENT_RKSNARE1);
  opl2.setInstrument(0, INSTRUMENT_TOM2);
  opl2.setInstrument(0, INSTRUMENT_CYMBAL1);
  opl2.setInstrument(0, INSTRUMENT_HIHAT2);

  opl2.setWaveFormSelect(true);

  //opl2.setDeepTremolo(true);
  // Set octave and frequency for bass drum.
  opl2.setBlock(6, 4);
  opl2.setFNumber(6, opl2.getNoteFNumber(NOTE_C));

  // Set octave and frequency for snare drum and hi-hat.
  opl2.setBlock(7, 3);
  opl2.setFNumber(7, opl2.getNoteFNumber(NOTE_C));
  // Set low volume on hi-hat
  opl2.setVolume(7, OPERATOR1, 16);

  // Set octave and frequency for tom tom and cymbal.
  opl2.setBlock(8, 3);
  opl2.setFNumber(8, opl2.getNoteFNumber(NOTE_A));
  Serial.begin(9600);

  //set timer1 interrupt at 4Hz
  TCCR1A = 0;// set entire TCCR1A register to 0
  TCCR1B = 0;// same for TCCR1B
  TCNT1  = 0;//initialize counter value to 0
  // set compare match register for 16hz increments
  OCR1A = (int) (16000000 / (2 * 1024)) - 1; // (must be <65536)
  // turn on CTC mode
  TCCR1B |= (1 << WGM12);
  // Set CS12 and CS10 bits for 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);  
  // enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);
  
  //attachInterrupt(digitalPinToInterrupt(2), toggleDrums, RISING);
}



ISR(TIMER1_COMPA_vect){//timer1 interrupt plays music
  awakenByInterrupt = true;
}
void play();

void play() {
  if (i % 4 == 0) {
    mcp2.digitalWrite(8, HIGH);
  }

  OCR1A = (int) (16000000 / (tempo * 8 * 4)) - 1;
  
  int rel = 3;

  //Serial.println(rel);
  int noteHeight = 500; //analogRead(A5);
  //int type = digitalRead(7);

  // play melodic instruments if required
  for(int j = 0; j<= 6; j++) {
    // This is wrong, all instruments should play, not only the selected one.
    if (noteData[j][10] == 1) {
      //Add rotary dial to set feedback
      opl2.setFeedback(j, 0);

      //Can't remember what this is for.
      opl2.setSynthMode(j, false);

      //Add button to set Tremolo/vibrato
      //opl2.setTremolo   (j, CARRIER, true);
      opl2.setVibrato   (j, CARRIER, true);

      //Set carrier multiplier to one because otherwise low notes are impossible
      opl2.setMultiplier(j, CARRIER, 0x01);

      // This is the way to adjust the modulator frequency. There is a button for that.
      opl2.setMultiplier(j, MODULATOR, noteModulator);
      // Add dial to set waveform
      opl2.setWaveForm(j, MODULATOR, modulatorWaveform);

      //Add button to set volume per channel
      opl2.setVolume(j, MODULATOR, 0);
      opl2.setAttack    (j, MODULATOR, attackModulator);
      opl2.setDecay     (j, MODULATOR, decayModulator);
      opl2.setSustain   (j, MODULATOR, sustainModulator);
      opl2.setRelease   (j, MODULATOR, releaseModulator);
      
      octave = noteCarrier >> 4;
      note = (int)((float)((noteCarrier % 16) & 0xf) / 16.0 * 12.0);

      //Add button to set volume per channel
      opl2.setVolume(j, CARRIER, 0);
      opl2.setWaveForm(j, CARRIER, carrierWaveform);
      opl2.setAttack    (j, CARRIER, attackCarrier);
      opl2.setDecay     (j, CARRIER, decayCarrier);
      opl2.setSustain   (j, CARRIER, sustainCarrier);
      opl2.setRelease   (j, CARRIER, releaseCarrier);
      
    }
    if (playNotes[j][i%8]) {
      opl2.playNote(j, octave, note);
    }
  }

  // play drums instruments if required
  opl2.setDrums(playNotes[6][i % 32], playNotes[7][i % 32], playNotes[8][i % 32], playNotes[9][i % 32], playNotes[10][i % 32]);

  //Flash tempo led.
  // Not sure if that will be necessary now.
  if ((i + 3) % 4 == 0) {
    mcp2.digitalWrite(8, LOW);
  }
  i++;
  if (i >= 32) {
    i = 0;
  }
  awakenByInterrupt = false;
}

int readEncoder(int encoder_id);

int readEncoder(int encoder_id) {
  Encoder encoder = encoders[encoder_id];
  int n = mcp.digitalRead(encoder.pin1);
  
  if ((encoder.pin1_val == LOW) && (n == HIGH)) {
    if (mcp.digitalRead(encoder.pin2) == LOW) {
      encoder.value -= encoder.step;
      if (encoder.value < encoder.min_value){
        encoder.value = encoder.min_value;
      }
    } else {
      encoder.value += encoder.step;
      if (encoder.value > encoder.max_value){
        encoder.value = encoder.max_value;
      }
    }
  }
  encoders[encoder_id].pin1_val = n;
  Serial.println(encoder.value);
  encoders[encoder_id].value = encoder.value;
  return encoder.value;
}
void readTempo();

void readTempo() {
  tempo = readEncoder(ENCODER_TEMPO);
}

void readAttack();
void readAttack() {
  n1 = mcp.digitalRead(10);
  if ((encoder1PinALast == LOW) && (n1 == HIGH)) {
    if (mcp.digitalRead(11) == LOW) {
      if (attackType) {
        attackCarrier+=1;
        if (attackCarrier >= 15) {
          attackCarrier = 15;
        }
      } else {
        attackModulator+=1;
        if (attackModulator >= 15) {
          attackModulator = 15;
        }
      }
    } else {
      if (attackType) {
        attackCarrier--;
        if (attackCarrier <= 0) {
          attackCarrier = 0;
        }
      } else {
        attackModulator--;
        if (attackModulator <= 0) {
          attackModulator = 0;
        }
      }
    }
  }
  encoder1PinALast = n1;
}

void readDecay();
void readDecay() {
  n3 = mcp.digitalRead(7);
  if ((encoder3PinALast == LOW) && (n3 == HIGH)) {
    if (mcp.digitalRead(6) == LOW) {
      if (attackType) {
        decayCarrier++;
        if (decayCarrier >= 15) {
          decayCarrier = 15;
        }
      } else {
        decayModulator++;
        if (decayModulator >= 15) {
          decayModulator = 15;
        }
      }
    } else {
      if (attackType) {
        decayCarrier--;
        if (decayCarrier <= 0) {
          decayCarrier = 0;
        }
      } else {
        decayModulator--;
        if (decayModulator <= 0) {
          decayModulator = 0;
        }
      }
    }
  }
  encoder3PinALast = n3;
}


void readRelease();
void readRelease() {
  n5 = mcp.digitalRead(2);
  if ((encoder5PinALast == LOW) && (n5 == HIGH)) {
    if (mcp.digitalRead(3) == LOW) {
      if (attackType) {
        releaseCarrier++;
        if (releaseCarrier >= 15) {
          releaseCarrier = 15;
        }
      } else {
        releaseModulator++;
        if (releaseModulator >= 15) {
          releaseModulator = 15;
        }
      }
    } else {
      if (attackType) {
        releaseCarrier--;
        if (releaseCarrier <= 0) {
          releaseCarrier = 0;
        }
      } else {
        releaseModulator--;
        if (releaseModulator <= 0) {
          releaseModulator = 0;
        }
      }
    }
  }
  encoder5PinALast = n5;
}
void readSustain();
void readSustain() {
  n4 = mcp.digitalRead(5);
  if ((encoder4PinALast == LOW) && (n4 == HIGH)) {
    if (mcp.digitalRead(4) == LOW) {
      if (attackType) {
        sustainCarrier++;
        if (sustainCarrier >= 15) {
          sustainCarrier = 15;
        }
      } else {
        sustainModulator++;
        if (sustainModulator >= 15) {
          sustainModulator = 15;
        }
      }
    } else {
      if (attackType) {
        sustainCarrier--;
        if (sustainCarrier <= 0) {
          sustainCarrier = 0;
        }
      } else {
        sustainModulator--;
        if (sustainModulator <= 0) {
          sustainModulator = 0;
        }
      }
    }
  }
  encoder4PinALast = n4;
}

void readNote();
void readNote() {
  n2 = mcp.digitalRead(14);
  if ((encoder2PinALast == LOW) && (n2 == HIGH)) {
    if (mcp.digitalRead(13) == LOW) {
      if (attackType) {
        noteCarrier+=1;
        if (noteCarrier >= 127) {
          noteCarrier = 127;
        }
      } else {
        noteModulator+=1;
        if (noteModulator >= 7) {
          noteModulator = 7;
        }
      }
    } else {
      if (attackType) {
        noteCarrier--;
        if (noteCarrier <= 0) {
          noteCarrier = 0;
        }
      } else {
        noteModulator--;
        if (noteModulator <= 0) {
          noteModulator = 0;
        }
      }
    }
  }
  encoder2PinALast = n2;
}

int debounceDelay = 300;
unsigned int lastDebounceTime = millis();
unsigned int lastSwitchDebounceTime = millis();

int oldSwitchPos = HIGH;
int oldSwitch0Pos = HIGH;

int oldVal = 0;
int currentRow = 0;
int litLed = 0;

unsigned int lastChanged = millis();
int changeLedRow();
int changeLedRow() {
  // Switch rows

  for (int k = 0; k < LED_ROWS;k++) {
    if (k != currentRow) {
      mcp3.digitalWrite(k, HIGH);
    }
    
  }
  
  for (int j = 0; j < LED_COLS;j++) {
    mcp3.digitalWrite(LED_ROWS + j, leds[currentRow * LED_COLS + j]);
  }
  mcp3.digitalWrite(currentRow, LOW);
    
  currentRow++;
  if (currentRow >= LED_ROWS) {
    currentRow = 0;
  }
}
int ledOn(int ref, int level);
int ledOn (int ref, int level) {
  leds[ref] = true;
}
int ledOff (int ref, int level);
int ledOff (int ref, int level) {
  leds[ref] = false;
}

void loop() {
  if (awakenByInterrupt) {
    play();
  }
  readNote();
  readTempo();
  readAttack();
  readDecay();
  readSustain();
  readRelease();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    int reading = mcp.digitalRead(0);
    if (reading != oldSwitch0Pos) {
      oldSwitch0Pos = reading;
      lastDebounceTime = millis();
      if (reading == LOW) {
        
        attackType = !attackType;
        mcp2.digitalWrite(0, attackType ? HIGH : LOW);
        Serial.print(!attackType ? "modulator" : "carrier");
        Serial.println();
      }
    }
  }
  

  changeLedRow();

  
  if ((millis() - lastSwitchDebounceTime) > debounceDelay) {
    int waveform = digitalRead(7);
    if (waveform != oldSwitchPos) {
      lastSwitchDebounceTime = millis();
      oldSwitchPos = waveform;
      if (waveform == LOW) {
        if (attackType) {
          carrierWaveform++;
          if (carrierWaveform > 3) {
            carrierWaveform = 0;
          }
          Serial.print("carrierWavefrom: ");
          Serial.println(carrierWaveform);

    
        } else {
          modulatorWaveform++;
          if (modulatorWaveform > 3) {
            modulatorWaveform = 0;
          }
          Serial.print("modulatorWaveform: ");
          Serial.println(modulatorWaveform);

        }
      }
      
    }
  }
  
  
}

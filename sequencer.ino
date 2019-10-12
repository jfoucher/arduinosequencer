
#include "OPL2.h"
#define DEBUG 1

OPL2 opl2;


#include "instruments.h"



int i = 0;
volatile bool drumsOn = false;
volatile bool changeModulator = false;
int note = 0;
int octave = 0;

//Data for 6 voices, Carrier ADSR, carrier octave, carrier note, carrier wave form, carrier volume, modulator ADSR, modulator multiplier, selected
int noteData[6][11] = {
  // A  D  S  R  WF Vo  MA MD MS MR Sel
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 1},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0},
};

int tones[6][8][3] = {
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
};

bool playNotes[6][8] = {
  {false, false, false, false, false, false, false, false},
  {false, false, false, false, false, false, false, false},
  {false, false, false, false, false, false, false, false},
  {true, false, true, false, true, false, true, false},
  {false, false, false, false, false, false, false, false},
  {false, false, false, false, false, false, false, false},
};

void setup() {
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

  //set timer0 interrupt at 2kHz
  //TCCR0A = 0;// set entire TCCR2A register to 0
  //TCCR0B = 0;// same for TCCR2B
  //TCNT0  = 0;//initialize counter value to 0
  // set compare match register for 2khz increments
  OCR0A = 124;// = (16*10^6) / (2000*64) - 1 (must be <256)
  // turn on CTC mode
  TCCR0A |= (1 << WGM01);
  // Set CS01 and CS00 bits for 64 prescaler
  TCCR0B |= (1 << CS01) | (1 << CS00);   
  // enable timer compare interrupt
  TIMSK0 |= (1 << OCIE0A);

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
  
  //attachInterrupt(digitalPinToInterrupt(2), saveVoice, RISING);
}

ISR(TIMER0_COMPA_vect){ //timer0 interrupt 2kHz toggles pin 8

  //Set tempo timer according to analogRead if A0;
  int tempoRead = analogRead(A0);

  int tempo = (byte)(tempoRead / 6) + 84;
  
  //Serial.println((int) (16000000 / (16 * tempoRead)) - 1);

  OCR1A = (int) (16000000 / (tempo * 5 * 4)) - 1;
}

ISR(TIMER1_COMPA_vect){//timer1 interrupt plays music

//  bool bass   = i % 4 == 0;           // Bass drum every 1st tick
//  bool snare  = (i + 2) % 4 == 0;     // Snare drum every 3rd tick
//  bool tom    = false;                // No tom tom
//  bool cymbal = i % 32 == 0;          // Cymbal every 32nd tick
//  bool hiHat  = true;                 // Hi-hat every tick
//
//  if(drumsOn) {
//    opl2.setDrums(bass, snare, tom, cymbal, hiHat);
//  }
  
  int attack = (analogRead(A1) >> 6);
  int decay = (analogRead(A2) >> 6);
  int sustain = (analogRead(A3) >> 6);
  int rel = (analogRead(A4) >> 6);

  //Serial.println(rel);
  int noteHeight = analogRead(A5);
  int type = digitalRead(7);
  
  for(int j = 0; j<= 6; j++) {
    if (noteData[j][10] == 1) {
      opl2.setFeedback(j, 2);
      opl2.setSynthMode(j, false);
      //Serial.println(opl2.getSynthMode(j));
      
      //opl2.setTremolo   (j, CARRIER, true);
      opl2.setVibrato   (j, CARRIER, true);
      opl2.setMultiplier(j, CARRIER, 0x01);
      int type = digitalRead(7);
      if (type == LOW) {
        opl2.setMultiplier(j, MODULATOR, noteHeight >> 7);
        opl2.setWaveForm(j, MODULATOR, 0);
        opl2.setVolume(j, MODULATOR, 0);
        opl2.setAttack    (j, MODULATOR, attack);
        opl2.setDecay     (j, MODULATOR, decay);
        opl2.setSustain   (j, MODULATOR, sustain);
        opl2.setRelease   (j, MODULATOR, rel);
      } else {
        octave = noteHeight >> 7;
        note = (int)((float)(noteHeight >> 3 & 0xf) / 16.0 * 12.0);
        
        opl2.setVolume(j, CARRIER, 0);
        opl2.setWaveForm(j, CARRIER, 0);
        opl2.setAttack    (j, CARRIER, attack);
        opl2.setDecay     (j, CARRIER, decay);
        opl2.setSustain   (j, CARRIER, sustain);
        opl2.setRelease   (j, CARRIER, rel);
      }
    }
    if (playNotes[j][i%8]) {
      opl2.playNote(j, octave, note);
    }
  }

  i ++;
}


void toggleDrums() {
    static unsigned long last_interrupt_time = 0;
   unsigned long interrupt_time = millis();
   if (interrupt_time - last_interrupt_time > 200) {
    Serial.write("Drum toggled\n");
    drumsOn = !drumsOn;
    changeModulator = !changeModulator;
   }
   last_interrupt_time = interrupt_time;
  
}

void loop() {
  
}

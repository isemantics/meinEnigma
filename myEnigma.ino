//#define TESTCRYPTO 6
/****************************************************************
 *  MyEnigma
 *  I'm not a programmer and writing this partly to learn 
 *  programming. That means that this code is not optimized and 
 *  several things could be done in a smarter way - I just don't 
 *  know about them (yet).
 *  If it bugs you - fix it and tell me about it, that's the way I learn.
 *
 *  Copyright: Peter Sjoberg <peters-src AT techwiz DOT ca>
 *  License: GPLv3
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License version 3 as 
 *    published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Status: way pre Alpha, still adding functions to the code.
 *
 *  History:
 *  v0.00 - test of library, keyboard and LEDs
 *  v0.01 - more parts now working, just missing wheel and enigma code.
 *  v0.02 - added wheel code
 *  v0.03 - added crypto code
 *  v0.04 - added basic serial API
 *  v0.05 - added virtual plugboard, morsecode and almost all functions
 *
 *
 * TODO: a lot but some "highlights"...
 *   tigh everything together in a user usable interface
 *
 * Decisions:
 *   The wheels are numbered left to right since that's the order they are
 *   in codebooks and it's the natural order of things when reading left to right.
 *   The are numbered 0-1-2-3 so walze/wheel/rotor 0 is that extra thin wheel the navy got.
 *   currentWalze always includes ring settings, only place it's adjusted for is in encryption.
 *   eeprom management will be simplified. 
 *    Drop the movement of save location and have it all at fixed positions. We still have 100000 saves quaranteed.
 *    Currently the array is 51 bytes and that would give at most 20 locations but by going
 *      with 15 locations of 64 bytes each we can easier change the content at a 
 *      later firmware revision and the code gets simpler at the cost of 4 presets.
 *    preset0 is always what was last used.
 *    preset 1-E are available to the user.
 *    the space of preset F is used for odometer and serial number
 *   factory reset will not clear the odometer, wouldn't do that on the real enigma (one with counter).
 *
 * To consider: Maybe change the code for UKW so when a UKW is selected the ukwD array is loaded with the config
 *	that way it's always same code no matter what UKW you have, fixed or dynamic (UKW-D)
 *	on the other hand, low on progmem so UKW-D does not fit.
 *
 * BUGS:
 *      no code to handle presets except from serial
 *	doublestepping is hardcoded to always enabled
 *	<s>plugboard always physical or none
 *      <s>Can not update plugboard over serial
 *      plugboard enabled even for models that doesn't have a plugboard
 *	pressing more than one key is handled wrong
 *	<s>Shorten the startup, changes so when in "model" mode pressing keys shows info
 *	<s>  like "v" for version, "o" for odometer and so on.
 *	<s?>when pressing multiple keys fast it sometimes doesn't clear all LEDs properly
 *	Running standalone is a problem with the switch on A6 changing all the time
 *	Running without plugboard doesn't show virtual plugboard in printSetting
 *	when setting virtual plugboard on physical config AA is possible
 *
 *Milestone:
 *	Need to check direction of all parts in checkWalze
 *
*/

#include "ht16k33.h"
#include "asciifont.h"
#include <EEPROM.h>
#include <Wire.h>
#ifdef ESP8266
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif


/****************/
//Morse code stuff

static const uint8_t morsecode[] PROGMEM={
  0b11111101,// A
  0b00001000,// B
  0b00001010,// C
  0b00000100,// D
  0b11111110,// E
  0b11110010,// F
  0b00000110,// G
  0b11110000,// H
  0b11111100,// I
  0b11110111,// J
  0b00000101,// K
  0b11110100,// L
  0b00000011,// M
  0b00000010,// N
  0b00000111,// O
  0b11110110,// P
  0b00001101,// Q
  0b11111010,// R
  0b11111000,// S
  0b00000001,// T
  0b11111001,// U
  0b11110001,// V
  0b11111011,// W
  0b00001001,// X
  0b00001011,// Y
  0b00001100,// Z
};
#define spkPin 11
//#define SPK // Speaker/Piezo or simple BEEP thingy

//toneFq and timeBase as variabes so they can be changed in sw later
uint8_t toneFq; // tone frequenze/100 so 1khz = 10 and 20khz=200
uint8_t timeBase; //ms to base all numbers on
#define timeDitt  timeBase
#define timeDah   timeBase*3
#define timePart  timeBase
#define timeLetter timeBase*2
#define timeSpace   timeBase*4

//End morsecode
/****************/

//Define the input pin for each of the 4 wheel encoders
//[wheel] [low, high]
//[wheel1low,wheel1high,wheel2low,wheel2high...

#define WALZECNT 4
//if count is something else than 4 the pin this definitions (and several other things) also need to change
static const uint8_t encoderPins[WALZECNT * 2] PROGMEM = {2, 3, 4, 5, 6, 7, 8, 9};
volatile uint8_t encoderState[WALZECNT] = {0xff, 0xff, 0xff, 0xff};
volatile unsigned long encoderChange[WALZECNT] = {0, 0, 0, 0};// When last change happened
volatile boolean encoderMoved[WALZECNT] = {false, false, false, false};

//port that the big red button is on - to reset to factory defaults
#define RESET 10

//analog port that the switch is at
#define Switch 6
//How many positions the switch has
#define SwitchPositions 5
// 5V/((SwitchPositions/1)*SwitchNo)
// 0V=pos 0
// 5V=pos SwitchPositions
// 1 less than SwitchPos1
// 2 less than SwitchPos2
// 3 less than SwitchPos3
// 4 less than SwitchPos4
// 5 above than SwitchPos4

#define maxADCval 1023
#define SwitchPos1 (maxADCval/((SwitchPositions-1)*2))*1  // "run" - normal running mode, rightmost pos
#define SwitchPos2 (maxADCval/((SwitchPositions-1)*2))*3  // Plugboard
#define SwitchPos3 (maxADCval/((SwitchPositions-1)*2))*5  // "wheels" - select what wheel is where
#define SwitchPos4 (maxADCval/((SwitchPositions-1)*2))*7  // "ukw" and "etw" (where that is an option) - which one and its psition (for the ones that can move)
#define SwitchPos5 (maxADCval/((SwitchPositions-1)*2))*9  // "model" - select what enigma model, leftmost pos
//6, OFF - leftmost position, powered off

enum operationMode_t {run,plugboard,rotortype,ukw,model} operationMode;
//Serial input stuff, arduino serial input buffer is 64 bytes
//max allowed msg lengh is 250 characters plus some spaces = 300
//Tried to make this buffer larger hoping to be able to capture long strings but it's still issues
// possible due to delays in the main loop and speed it arrives at (64 bytes takes 0.55ms at 115200)
#define MAXSERIALBUFF 150
String serialInputBuffer = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete

String msgCache = "";         // buffer to hold data while processing it

typedef struct {
  char letter[26];         // 26 to allow external Uhr box connected, then A->B doesn't mean B->A.
} letters_t;

// wiring info comes from http://www.cryptomuseum.com/crypto/enigma/wiring.htm

//26 letters and and a 6char name
const char etw0[]   PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZETW0  ";
const char etwKBD[] PROGMEM = "QWERTZUIOASDFGHJKPYXCVBNMLETWKBD";
const char etwJAP[] PROGMEM = "KZROUQHYAIGBLWVSTDXFPNMCJEETWJAP";

const uint8_t ETW0=0;
const uint8_t ETWKBD=1;
const uint8_t ETWJAP=2;
const char* const ETW[] PROGMEM =
  {
    etw0,
    etwKBD,
    etwJAP
  };

// 26 letters + 4 wheel + 5 display
//                            ABCDEFGHIJKLMNOPQRSTUVWXYZwwwwDDDDD
const char ukwA[]  PROGMEM = "EJMZALYXVBWFCRQUONTSPIKHGDUKWAUKWA "; // 0= A
const char ukwB[]  PROGMEM = "YRUHQSLDPXNGOKMIEBFZCWVJATUKWBUKWB "; // 1= B
const char ukwC[]  PROGMEM = "FVPJIAOYEDRZXWGCTKUQSBNMHLUKWCUKWC "; // 2= C
const char ukwBt[] PROGMEM = "ENKQAUYWJICOPBLMDXZVFTHRGSUKBTUKWBT"; // 3= Bthin
const char ukwCt[] PROGMEM = "RDOBJNTKVEHMLFCWZAXGYIPSUQUKCTUKWCT"; // 4= Cthin
const char ukwN[]  PROGMEM = "MOWJYPUXNDSRAIBFVLKZGQCHETUKWNUKWN "; // 5= Norway
const char ukwK[]  PROGMEM = "IMETCGFRAYSQBZXWLHKDVUPOJNUKWKUKWK "; // 6= EnigmaK or Swiss
//const char ukwD[]  PROGMEM = "AOCDEFGHIJKLMNBPQRSTUVWXYZUKWDUKWD "; // B-O is fixed, rest may vary
//TODO : Add ukw-D as described at http://www.cryptomuseum.com/crypto/enigma/ukwd/index.htm

const uint8_t UKWA=0;
const uint8_t UKWB=1;
const uint8_t UKWC=2;
const uint8_t UKWBT=3;
const uint8_t UKWCT=4;
const uint8_t UKWN=5;
const uint8_t UKWK=6;
//const uint8_t UKWD=7;
//
const uint8_t UKWCNT=7; // count
//
// http://www.cryptomuseum.com/crypto/enigma/ukwd/index.htm
//static char ukwDA[] = "AOCDEFGHIJKLMNBPQRSTUVWXYZ"; // B-O is fixed, rest is configurable

//
const uint8_t NA=255;

const char* const UKW[] PROGMEM =
  {
    ukwA,
    ukwB,
    ukwC,
    ukwBt,
    ukwCt,
    ukwN,
    ukwK,
  };

//Wiring followed by rollover notch(es)
//Notch defined as the letter showing when it's at the notch, not the letter next to the physical notch
//[0][0]is all letters in order and no notch (to define how many letters to start with)
//After letters comes 4 letters to show when selected and button is pressed
#define WALZEDISP 5
//                                 ABCDEFGHIJKLMNOPQRSTUVWXYZwwwwSNN, wwww=walze display long, s=walze display short, Notch(Notch)
const char walze0[]     PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";        // 0= reference
const char walzeI[]     PROGMEM = "EKMFLGDQVZNTOWYHXUSPAIBRCJ   I1Q";   // 1= military I
const char walzeII[]    PROGMEM = "AJDKSIRUXBLHWTMCQGZNPYFVOE  II2E";   // 2= military II
const char walzeIII[]   PROGMEM = "BDFHJLCPRTXVZNYEIWGAKMUSQO III3V";   // 3= military III
const char walzeIV[]    PROGMEM = "ESOVPZJAYQUIRHXLNFTGKDCMWB  IV4J";   // 4= military IV
const char walzeV[]     PROGMEM = "VZBRGITYUPSDNHLXAWMJQOFECK   V5Z";   // 5= military V
const char walzeVI[]    PROGMEM = "JPGVOUMFYQBENHZRDKASXLICTW  VI6ZM";  // 6= military VI
const char walzeVII[]   PROGMEM = "NZJHGRCXMYSWBOUFAIVLPEKQDT VII7ZM";  // 7= military VII
const char walzeVIII[]  PROGMEM = "FKQHTLXOCBJSPDZRAMEWNIUYGVVIII8ZM";  // 8= military VIII
const char walzeBeta[]  PROGMEM = "LEYJVCNIXWPBQMDRTAKZGFUHOSBETAB";    // 9= Beta
const char walzeGamma[] PROGMEM = "FSOKANUERHMBTIYCWLQPZXVGJDGAMMG";    // 10= Gamma

#ifdef NOLIMITS
//
const char walzeNI[]    PROGMEM = "WTOKASUYVRBXJHQCPZEFMDINLGN  I1Q";    // 11= Norway Walze I
const char walzeNII[]   PROGMEM = "GJLPUBSWEMCTQVHXAOFZDRKYNIN II2E";    // 12= Norway Walze II
const char walzeNIII[]  PROGMEM = "JWFMHNBPUSDYTIXVZGRQLAOEKCNIII3V";    // 13= Norway Walze III
const char walzeNIV[]   PROGMEM = "ESOVPZJAYQUIRHXLNFTGKDCMWBN IV4J";    // 14= Norway Walze IV
const char walzeNV[]    PROGMEM = "HEJXQOTZBVFDASCILWPGYNMURKN  V5Z";    // 15= Norway Walze V
//
const char walzeSKI[]    PROGMEM = "PEZUOHXSCVFMTBGLRINQJWAYDKK  I1Y";    // 16= Swiss Walze I
const char walzeSKII[]   PROGMEM = "ZOUESYDKFWPCIQXHMVBLGNJRATK II2E";    // 17= Swiss Walze II
const char walzeSKIII[]  PROGMEM = "EHRVXGAOBQUSIMZFLYNWKTPDJCKIII3N";    // 18= Swiss Walze III
#endif

//

const uint8_t WALZE0=0;
const uint8_t WALZE_I=1;
const uint8_t WALZE_II=2;
const uint8_t WALZE_III=3;
const uint8_t WALZE_IV=4;
const uint8_t WALZE_V=5;
const uint8_t WALZE_VI=6;
const uint8_t WALZE_VII=7;
const uint8_t WALZE_VIII=8;
const uint8_t WALZE_Beta=9;
const uint8_t WALZE_Gamma=10;
#ifdef NOLIMITS
const uint8_t WALZE_NI=11;
const uint8_t WALZE_NII=12;
const uint8_t WALZE_NIII=13;
const uint8_t WALZE_NIV=14;
const uint8_t WALZE_NV=15;
const uint8_t WALZE_SKI=16;
const uint8_t WALZE_SKII=17;
const uint8_t WALZE_SKIII=18;
#endif

const char* const WALZE[] PROGMEM =
  {
    walze0,
    walzeI,
    walzeII,
    walzeIII,
    walzeIV,
    walzeV,
    walzeVI,
    walzeVII,
    walzeVIII,
    walzeBeta,
    walzeGamma,
#ifdef NOLIMITS
    walzeNI,
    walzeNII,
    walzeNIII,
    walzeNIV,
    walzeNV,
    walzeSKI,
    walzeSKII,
    walzeSKIII
#endif
  };

//Can't figure out any simpler way to get lenght of WALZE[0][0]
#define letterCnt (int8_t)(strlen_P((char*)pgm_read_word(&WALZE[0]))) // the first is all letters and no notch

//List of valid commands, must start with "!"
#ifdef NOLIMITS
const char APICMDLIST[] PROGMEM = "!SETTINGS!MODEL!UKW!WALZE!RING!PLUGBOARD!START!SAVE!LOAD!LOGLEVEL!DEBUG!VERBOSE!DUKW!";
#else
const char APICMDLIST[] PROGMEM = "!SETTINGS!MODEL!UKW!WALZE!RING!PLUGBOARD!START!SAVE!LOAD!LOGLEVEL!";
#endif
static const uint8_t CMD_SETTINGS=0;
static const uint8_t CMD_MODEL=1;
static const uint8_t CMD_UKW=2;
static const uint8_t CMD_WALZE=3;
static const uint8_t CMD_RING=4;
static const uint8_t CMD_PLUGBOARD=5;
static const uint8_t CMD_START=6;
static const uint8_t CMD_SAVE=7;
static const uint8_t CMD_LOAD=8;
static const uint8_t CMD_LOGLEVEL=9;
static const uint8_t CMD_DEBUG=10;
static const uint8_t CMD_VERBOSE=11;
static const uint8_t CMD_DUKW=12; // config UKW-D, similar to plugboard

//Log level on serial port
//0 = all off, only message
//1 = some info as the letter traveling
//2 = debug code
uint8_t logLevel=0;

//Bitmask for different parts
// 0=DEBUGKB
// 1=Plugboard
static const uint8_t DEBUGKBD=0;
static const uint8_t DEBUGPLUG=1;
uint8_t debugMask=0;

//enigma models
// http://www.cryptomuseum.com/crypto/enigma/timeline.htm
// Year  Army/Air   Navy UKW    Wheels
// 1933     I       -    UKW-A  I II III
// 1934     I       M1   UKW-A  
// 1937     I       M1   UKW-B  
// 1938     I       M2          IV V
// 1939     I       M2   UKW-B  Navy added  VI VII VIII
// 1940     I       M3   UKW-C 
// 1941     I       M4   
// 1944     I       M4   UKW-D  UHR for luftwaffe
//NOTE: "M3" and "M4" is operational procedure, NOT number of wheels (or "M1" would have just 1 wheel :/ ),
//      it just happens to match up and is commonly used to represent 3 wheel or 4 wheel models

// Several enigma models and their wheel count & wiring
// http://www.cryptomuseum.com/crypto/enigma/wiring.htm

typedef enum {
  fixed,	// fixed, one or more to choose from but not moving and wires not changing
  program,	// wires can be changed around, ukw-D
  rotate} ukw_t; // one or more to choose from but it can be moved around, looks like a 4th rotor.

typedef enum {
  virtualpb,
  physicalpb,
  config
} vpb_t;

typedef enum {
  FIRST,
  EnigmaI,
  M3,
  M4,
  NorwayEnigma,
  SwissK,
  /* to be added
  EnigmaG,
  G31AbwehrEnigmaG312,
  G31AbwehrEnigmaG260,
  G31HungarianEnigmaG111,
  EnigmaD,
  EnigmaK,
  EnigmaKD,
  RailwayEnigma,
  EnigmaT,
  */
  LAST
  } enigmaModel_t;

//The different enigma models have different configs
typedef struct {
  enigmaModel_t model;
  char display[5]; //The name to show on the alnum display 4 UCASE letters plus \0
  char lname[100]; // Long description - for serial port
  uint8_t etw;     // each model has only one valid entry wheel
  uint8_t walze3[8]; // array of valid rotors for pos 1-3
  uint8_t walze4[2]; // array of valid rotors for 4th rotor
  ukw_t   ukwType;
  uint8_t ukw[4]; // array of valid reflector wheels
  boolean doublestep; //whatever the middle rotor does doublestep or not.
  boolean plugboard;  //whatever this model has a plugboard or not
} enigmaModels_t;

/* C99 version, not supported in gcc c++, need to go with C89 then :(
const enigmaModels_t EnigmaModelI = 
  {
  .model=EnigmaI,
  .sname="EnigmaI",
  .lname="German Army and Air Force (Wehrmacht, Luftwaffe)",
  .etw={0},
  .walze3={1,2,3,4,5},
  .ukwType=fixed,
  .ukw={2,3,4},
  .doublestep=true
  .plugboard=true
  };
*/

//Define how many models we have here
// info comes from http://www.cryptomuseum.com/crypto/enigma/wiring.htm
#define MODELCNT (sizeof(EnigmaModels)/sizeof(EnigmaModels[0]))
const enigmaModels_t EnigmaModels[] PROGMEM = {
  {
    EnigmaI,
    "EN I",
    "Enigma I, German Army and Air Force (Wehrmacht, Luftwaffe)",
    ETW0,
    {WALZE_I,WALZE_II,WALZE_III,WALZE_IV,WALZE_V},
    {WALZE0},
    fixed,
    {UKWA,UKWB,UKWC,NA},
    true,
    true
  },
  {
    M3,
    "M3  ",
    "Enigma M3, German Navy (Kriegsmarine)",
    ETW0,
    {WALZE_I,WALZE_II,WALZE_III,WALZE_IV,WALZE_V,WALZE_VI,WALZE_VII,WALZE_VIII},
    {WALZE0},
    fixed,
    {UKWB,UKWC,NA,NA},
    true,
    true
  },
  {
    M4,
    "M4  ",
    "Enigma M4, U-Boot Enigma",
    ETW0,
    {WALZE_I,WALZE_II,WALZE_III,WALZE_IV,WALZE_V,WALZE_VI,WALZE_VII,WALZE_VIII},
    {WALZE_Beta,WALZE_Gamma},
    fixed,
    {UKWBT,UKWCT,NA,NA},
    true,
    true
#ifdef NOLIMITS
  },
  {
    NorwayEnigma,
    "NORW",
    "Norway Enigma, Postwar usage",
    ETW0,
    {WALZE_NI,WALZE_NII,WALZE_NIII,WALZE_NIV,WALZE_NV},
    {WALZE0},
    fixed,
    {UKWN,NA,NA,NA},
    true,
    true // not sure if they had a plugboard or not
  },
  {
    SwissK,
    "SWIS",
    "Swiss Enigma K",
    ETW0,
    {WALZE_SKI,WALZE_SKII,WALZE_SKIII},
    {WALZE0},
    rotate, // no 4th wheel but the UKW is in 4th wheel position and can rotate
    {UKWK,NA,NA,NA},
    true,
    false
#endif
  }
};
  

//TODO: 
// Add something so we can have several presets in memory and then change between them
//  settings.preset is a preset number. the non active ones are +100
//  so if the active one is 1 and 2 is inactive we have two saved in eeprom
//  one with preset=1 and one with preset=102
//  this schema allows 100 (0-99) preset profiles which means we run out of eeprom space before we run out of profiles.
//
// EEPROM structure version - to be able to handle upgrades
#define VERSION 0

// rotor/wheel = walze
// Entry wheel = Eintrittswalze (ETW), static
// Reflector   = Umkehrwalze (UKW)
// wheels are counted from left to right
//  rightmost wheel is 3
//  leftmost wheel is 0 and not available on M3
//

// calc 1+1+1+1+4+1+4+1+26+4+1+2
//Maxumim number of presets
#define MAXPRESET 15 //end of eeprom is used for odometer and serial number
//If settingsize is changed firmware upgrade with new format may 
// not be possible without loosing all presets.
#define SETTINGSIZE 64
//also note that MAXPRESET*SETTINGSIZE < EEPROM.length()

typedef struct {
  uint8_t   fwVersion;         // firmware version
  enigmaModel_t model;
  uint8_t   ukw;               // Umkehrwalze - what reflector that is loaded
  uint8_t   walze[WALZECNT];   // what wheel that currently is in the 3 or 4 positions
  uint8_t   etw;               // Eintrittswalze - entry wheel, always 1 for military enigma.
  int8_t    ringstellung[WALZECNT]; // Setting of the wheel ring, left to right, 0-sizeof(walze[0]) not the letters!
  vpb_t     plugboardMode;     // if virtual plugboard is enabled or not. 
  letters_t plugboard;
  int8_t    currentWalze[WALZECNT]; // current position of the wheel, 0-sizeof(walze[0]) not the letters!
  uint8_t   grpsize;            // Size of groups to print out over serial
  boolean   morseCode;		// Send morsecode or not
  unsigned int checksum;
} machineSettings_t;
unsigned long odometer;      // How many characters this unit has en/decrypted
unsigned long serialNumber;  // static number stored in eeprom and set with other means than this program

/********************************/

machineSettings_t settings; // current settings and also what is saved in eeprome
enigmaModels_t EnigmaModel; // attributes for the current enigma model
boolean plugboardPresent=true;   // whatever the plugboard is physical(true) or virtual(false)
boolean plugboardEmpty=false;
int8_t  currentWalzePos[WALZECNT]; // current position of the wheel, used during config

uint8_t lastKey; // last key pressed, needed to pass the info between subroutines
char lastKeyCode; // after parsed trough scancode
uint8_t lastPreset; // last loaded preset

HT16K33 HT;

//  MCP23017 registers, all as seen from bank0
//
#define mcp_address 0x20 // I2C Address of MCP23017
#define IODIRA    0x00 // IO Direction Register Address of Port A
#define IODIRB    0x01 // IO Direction Register Address of Port B
#define IPOLA     0x02 // Input polarity port register 
#define IPOLB     0x03 // 
#define GPINTENA  0x04 // Interrupt on change
#define GPINTENB  0x05 // 
#define DEFVALA   0x06 // Default value register
#define DEFVALB   0x07 // 
#define INTCONA   0x08 // Interrupt on change control register
#define INTCONB   0x09 // 
#define IOCON     0x0A // Control register
#define IOCON     0x0B // 
#define GPPUA     0x0C // GPIO Pull-ip resistor register
#define GPPUB     0x0D // 
#define INTFA     0x0E // Interrupt flag register
#define INTFB     0x0F // 
#define INTCAPA   0x10 // Interrupt captred value for port register
#define INTCAPB   0x11 // 
#define GPIOA     0x12 // General purpose io register
#define GPIOB     0x13 // 
#define OLATA     0x14 // Output latch register
#define OLATB     0x15 // 

//Enigma lamp and Keyboard layout:
//   Q W E R T Z U I O
//    A S D F G H J K
//   P Y X C V B N M L

//The order the keys on the keyboard are coded
//Keyboard scancode table
//A-Z for keyboard, 0-3 for buttons under walze 0-3
//(char)pgm_read_byte(&scancodes[0]+key-1)
const char scancodes[] PROGMEM = "OIUZTREWQGHJKLMNBVCXYPASDF0123";

//  9  8  7  6  5  4  3  2  1
//   23 24 25 26 10 11 12 13 
// 22 21 20 19 18 17 16 15 14 
//

//A=65 so the [0] element contain the number of the LED that shows "A"
//after 'Z'-65 comes control LEDs [\]^_
//Usage: led['A'-65] or led[char-65]
//	pgm_read_byte(led+'A'-65));
//const byte led[] PROGMEM = {76,86,88,78,73,79,92,93,68,94,95,83,84,85,67,91,75,72,77,71,69,87,74,89,90,70};
const byte led[] PROGMEM = {12,22,24,14,9,15,28,29,4,30,31,19,20,21,3,27,11,8,13,7,5,23,10,25,26,6};
//  Q   W   E   R   T   Z   U   I   O
// 75  74  73  72  71  70  69  68  67
//
//    A   S   D   F   G   H   J   K
//   76  77  78  79  92  93  94  95
//
//  P   Y   X   C   V   B   N   M   L
// 91  90  89  88  87  86  85  84  83

//How the plugboard is mapped
//[0] is first input port on first mcp23017
// (char)pgm_read_byte(&steckerbrett[0]+key-1)
//                                   00000000001111111111222222
//                                   01234567890123456789012345
//const byte steckerbrett[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; //Place holder until final wiring is done
//with steckerbrett as A-Z the plugboard ends up as
// YVSPMJGDA
//  WTQNKHEB
// ZXUROLIFC
// or
//  Q   W   E   R   T   Z   U   I   O
// 24  21  18  15  12  09  06  03  00
//
//    A   S   D   F   G   H   J   K
//   22  19  16  13  10  07  04  01
//
//  P   Y   X   C   V   B   N   M   L
// 25  23  20  17  14  11  08  05  02
//
const byte steckerbrett[] PROGMEM = "OKLIJMUHNZGBTFVRDCESXWAYQP"; // 
//

//Decimal points are handled outside the ht16k33 (not enough wires).
//they are wired to analog io pins on the arduino and then some hardware that is in between will light up the correct leds decimal point at the right moment
static const uint8_t dp[] PROGMEM = {14,15,16,17}; // need SPI so use analog port a0,a1,a2,a3 for output (a4 & 5 are i2c, a6 is switch, a7 free but not digital.)

// bitmapped decimal point, bit0=0 means dp on for leftmost(0) wheel
//static uint8_t   dpStatus;

/****************************************************************/
// Write a single byte to i2c
uint8_t i2c_write(uint8_t unitaddr,uint8_t val){
  Wire.beginTransmission(unitaddr);
  Wire.write(val);
  return Wire.endTransmission();
}

/****************************************************************/
// Write two bytes to i2c
uint8_t i2c_write2(uint8_t unitaddr,uint8_t val1,uint8_t val2){
  Wire.beginTransmission(unitaddr);
  Wire.write(val1);
  Wire.write(val2);
  return Wire.endTransmission();
}

/****************************************************************/
// read a byte from specific address (send one byte(address to read) and read a byte)
uint8_t i2c_read(uint8_t unitaddr,uint8_t addr){
  i2c_write(unitaddr,addr);
  Wire.requestFrom(unitaddr,1);
  return Wire.read();    // read one byte
}

/****************************************************************/
// read 2 bytes starting at a specific address
// read is done in two sessions - as required by mcp23017 (page 5 in datasheet)
uint16_t i2c_read2(uint8_t unitaddr,uint8_t addr){
  uint16_t val;
  i2c_write(unitaddr,addr);
  Wire.requestFrom(unitaddr, 1);
  val=Wire.read();
  Wire.requestFrom(unitaddr, 1);
  return Wire.read()<<8|val;
}

/****************************************************************/
//Get a single character from a specific wheel and wheel position
//To be called like
//   ch=getWalzeChar(&WALZE[wheel],pos)

char getWalzeChar(const char* const *theWheel, uint8_t pos){
  
  // inner pgm_read_word gets the pointer to the beginning of the alphabet string
  // then add the position to that and read whats there to get the character we want
  return (char)pgm_read_byte(pgm_read_word(theWheel)+pos);
} // getWalzeChar

/****************************************************************/
//Make ch fit in range of 0-25
char normalize(char ch){
  while (ch<0){ch+=letterCnt;}
  while (ch>=letterCnt){ch-=letterCnt;}
  return ch;
}

/****************************************************************/
void printValError(String val){
  Serial.print(F("%ERROR: Unknown/Illegal value specified >"));
  Serial.print(val);
  Serial.println(F("< keeping old setting."));
} // printValError

/****************************************************************/
void printModel(){
  Serial.print(EnigmaModel.display);
} // printModel

/****************************************************************/
void printModelDescription(){
  Serial.print(EnigmaModel.lname);
} // printModel

/****************************************************************/
void printUKW(uint8_t ukw){ // one of 8 rotors
  uint8_t i;
  for (i=0;i<5;i++){
    Serial.print((char)getWalzeChar(&UKW[ukw],26+4+i));
  }
} // printRotor

/****************************************************************/
void printRotor(uint8_t rotor[4]){ // one of 8 rotors
  uint8_t i;
  for (i=0;i<WALZECNT;i++){
    Serial.print(rotor[i]);
    if (i<WALZECNT-1) Serial.print(F("-"));
  }
} // printRotor

/****************************************************************/
void printWheel(int8_t wheels[4]){ // what each rotor it is currently turned to show
  uint8_t i;
  char ch;

  for (i=0;i<WALZECNT;i++){
    if (settings.walze[i] == 0){
      Serial.print(F("-"));
    } else {
      ch=normalize(wheels[i]);
      Serial.print((char) (ch+'A'));
    }
    Serial.print(F(" "));
  }

  /*
  for (i=0;i<WALZECNT;i++){
    if (settings.walze[i] == 0){
      Serial.print(F("-"));
    } else {
      Serial.print((char) (wheels[i]+'A'));
    }
    if (i<WALZECNT-1) Serial.print(F(" "));
  }
  */
} // printWheel

/****************************************************************/
void printPlugboard(){
  uint8_t i;

  for (i = 0; i < sizeof(settings.plugboard); i++) {
    if (settings.plugboard.letter[i] > i) {
      Serial.print((char)(i+'A'));
      Serial.print((char)(settings.plugboard.letter[i]+'A'));
      Serial.print(F(" "));
    }
  }
} // printPlugboard

/****************************************************************/
void printSettings(){
  uint8_t i;

  Serial.print(F("fwVersion: "));
  Serial.println(settings.fwVersion, HEX);
  Serial.print(F("preset: "));
  Serial.println(lastPreset,DEC);
  Serial.println();
  Serial.print(F("Odo meter: "));
  Serial.println(odometer, DEC);
  Serial.print(F("morsecode: "));
  if (settings.morseCode){
    Serial.println(F("ON"));
  }else{
    Serial.println(F("OFF"));
  }
  Serial.print(F("Serial number: "));
  Serial.println(serialNumber, DEC);
  Serial.println();

  Serial.print(F("model: "));
  printModel();
  Serial.print(F("("));
  printModelDescription();
  Serial.println(F(")"));

  Serial.print(F("entry wheel: "));
  Serial.println(settings.etw, DEC);
  Serial.print(F("reflector: "));
  printUKW(settings.ukw);
  Serial.println(settings.ukw, DEC);
  Serial.print(F("Rotors: "));
  printRotor(&settings.walze[0]);

  Serial.println();
  Serial.print(F("ringstellung: "));
  printWheel(&settings.ringstellung[0]);
  Serial.println();
  Serial.print(F("Plugboard (Steckerbrett): "));
  if (plugboardPresent){
    if (settings.plugboardMode==virtualpb){
      Serial.print(F("virtual"));
    }else if (settings.plugboardMode==physicalpb){
      Serial.print(F("physical"));
      checkPlugboard();
    } else {
      Serial.print(F("config"));
    }
    Serial.print(F(" - "));
    if (settings.plugboardMode==physicalpb && plugboardEmpty){
      Serial.print(F("empty"));
    }else{
      printPlugboard();
    }
  } else {
    Serial.print(F("not present "));
  }
  Serial.println();

  Serial.print(F("currentWalze: "));
  printWheel(&settings.currentWalze[0]);
  Serial.println();

} // printSettings

/****************************************************************/
//calculate a checksum of a block
// basic function taken from https://www.arduino.cc/en/Tutorial/EEPROMCrc
unsigned int getCsum(void *block, uint8_t size) {
  unsigned char * data;
  unsigned int i, csum;

  const unsigned long crc_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
  };

  unsigned long crc = ~0L;

  csum = 0;
  data = (unsigned char *) block;

  for (int index = 0 ; index < size  ; ++index) {
    crc = crc_table[(crc ^ data[index]) & 0x0f] ^ (crc >> 4);
    crc = crc_table[(crc ^ (data[index] >> 4)) & 0x0f] ^ (crc >> 4);
    crc = ~crc;
  }
  
#ifdef DEBUG
  Serial.print(F(" getCsum: "));
  Serial.print(crc,HEX);
  Serial.print(F(" "));
  Serial.print(size,DEC);
  Serial.println();
#endif  
  
  return (int) crc;
} // getCsum

/****************************************************************/
// save the settings to eeprom
void saveSettings(uint8_t preset) {
  unsigned char *ptr = (unsigned char *)&settings;
#ifdef ESP8266
  uint16_t i;
#endif

  settings.checksum = getCsum((void*) ptr, sizeof(settings) - 2);
  if (logLevel>1){
    Serial.print(F("saveSettings preset no "));
    Serial.println(preset);
  }
#ifdef ESP8266
  for (i=0;i<sizeof(settings);i++){
    EEPROM.write((void*)(preset*SETTINGSIZE)+1,(const void*)&settings+1);//BUG:FIX THIS
  }
#else
  eeprom_update_block((const void*)&settings, (void*)(preset*SETTINGSIZE), sizeof(settings));
#endif

  // odometer is saved separately
#ifdef ESP8266
  EEPROM.write((uint8_t*)(EEPROM.length()-8),(uint8_t)(odometer>>24 & 0xFF));
  EEPROM.write((uint8_t*)(EEPROM.length()-7),(uint8_t)(odometer>>16 & 0xFF));
  EEPROM.write((uint8_t*)(EEPROM.length()-6),(uint8_t)(odometer>>8  & 0xFF));
  EEPROM.write((uint8_t*)(EEPROM.length()-5),(uint8_t)(odometer     & 0xFF));
  EEPROM.commit();
#else
  eeprom_write_byte((uint8_t*)(EEPROM.length()-8),(uint8_t)(odometer>>24 & 0xFF));
  eeprom_write_byte((uint8_t*)(EEPROM.length()-7),(uint8_t)(odometer>>16 & 0xFF));
  eeprom_write_byte((uint8_t*)(EEPROM.length()-6),(uint8_t)(odometer>>8  & 0xFF));
  eeprom_write_byte((uint8_t*)(EEPROM.length()-5),(uint8_t)(odometer     & 0xFF));
#endif

} // saveSettings

/****************************************************************/
// read the settings from eeprom
uint8_t readSettings(uint8_t preset) {
  machineSettings_t eesettings;
  //  unsigned char *ptr = (unsigned char *)&settings;
  unsigned char *eeptr = (unsigned char *)&eesettings;

  if (logLevel>1){
    Serial.print(F("readSettings preset no "));
    Serial.print(preset);
    Serial.print(F(" from "));
    Serial.println(preset*SETTINGSIZE);
  }
  eeprom_read_block((void*)&eesettings, (void*)(preset*SETTINGSIZE), sizeof(eesettings));

  //serial number is set outside this program
  serialNumber=(long)eeprom_read_byte((uint8_t*)(EEPROM.length()-4))<<24 | (long)eeprom_read_byte((uint8_t*)(EEPROM.length()-3))<<16 | (long)eeprom_read_byte((uint8_t*)(EEPROM.length()-2))<<8 | (long)eeprom_read_byte((uint8_t*)(EEPROM.length()-1));
    
  //odometer is stored in the end of the eeprom instead of inside the structure.
  odometer=(long)eeprom_read_byte((uint8_t*)(EEPROM.length()-8))<<24 | (long)eeprom_read_byte((uint8_t*)(EEPROM.length()-7))<<16 | (long)eeprom_read_byte((uint8_t*)(EEPROM.length()-6))<<8 | (long)eeprom_read_byte((uint8_t*)(EEPROM.length()-5));
  if (odometer==0xFFFFFFFF || odometer==0xFF3FFFFF){ // default eeprom value = never set/wiped
    odometer=0;
  }

  if (getCsum((void*) eeptr, sizeof(settings)- 2) == eesettings.checksum) { // if valid csum, make it active
    lastPreset=preset;
    memcpy(&settings,&eesettings,sizeof(eesettings));
    copyEnigmaModel(settings.model);
    return true;
  } else {
    return false; // csum invalid so don't change old settings
  }
} // readSettings

/****************************************************************/
//Some code from http://playground.arduino.cc/Main/PinChangeInterrupt
//This is tested on an arduino uno and arduino nano, other models may not work
void pciSetup(uint8_t pin)
{
  *digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
  PCIFR  |= bit (digitalPinToPCICRbit(pin)); // clear any outstanding interrupt
  PCICR  |= bit (digitalPinToPCICRbit(pin)); // enable interrupt for the group
}

/****************************************************************/
//The logic is
//  when both inputs are high (11) it's in resting position so check what prev condition was
//   if A was low - right turn
//   if B was low - left turn
//  debounce - if less than x ms since last interrupt
//
void updateEncoderState() {
  static uint8_t i, state;

  //read state of all 4 encoders
  for (i = 0; i < sizeof(encoderPins); i += 2) {
    state = digitalRead(pgm_read_byte(encoderPins+i + 1)) << 1 | digitalRead(pgm_read_byte(encoderPins+i));
    if ((encoderState[i / 2] & 0b11) != state) { // same as before ?
      // check time since last change to ignore bounces.
      // Test shown that turning fast at 1ms can still be correct while 6ms can still be bounce/wrong
      // going with >5ms since on the original Enigma you couldn't physically spin it to fast.
      if ((millis()-encoderChange[i / 2]) > 5){ 
        encoderState[i / 2] = (encoderState[i / 2] << 2) | state;
        encoderMoved[i / 2] = true;
	encoderChange[i / 2]=millis();
      } // if no bounce
    } // if state changed for this rotor
  } // for each rotor
} //updateEncoderState

ISR (PCINT2_vect) { // handle pin change interrupt for D0 to D7 here
  updateEncoderState();
} // ISR D0-D7

ISR (PCINT0_vect) { // handle pin change interrupt for D8 to D13 here
  updateEncoderState();
} // ISR D8-D13

/****************************************************************/
// Turn on/off decimal point
// 
void decimalPoint(uint8_t dpoint, boolean state) {
  uint8_t i;
  static uint8_t dpStatus=0;

  //The way it works is reversed
  // if the bit is _cleared_ the dp is on
  // if the bit is _set_ the dp is off
  
  if (!state){
    bitSet(dpStatus,dpoint);
  }else{
    bitClear(dpStatus,dpoint);
  }
  for (i=0;i<WALZECNT;i++){
    digitalWrite(pgm_read_byte(dp+i),bitRead(dpStatus,i));
  }
} // decimalPoint

/****************************************************************/
// Load enigma config to sram
void  copyEnigmaModel(enigmaModel_t model){
  enigmaModels_t checkModel;
  uint8_t i=-1;

  do {
    i++;
    memcpy_P(&checkModel,&EnigmaModels[i],sizeof(EnigmaModel));
  } while ((checkModel.model!=model) && i<MODELCNT);

  if (checkModel.model==model){ // check shouldn't really be needed, shouldn't come to this function unless it's valid
    // Copy over a enigma definition to sram so it's easier to manage
    memcpy_P(&EnigmaModel,&EnigmaModels[i],sizeof(EnigmaModel));
    settings.model=checkModel.model;
  }
} // copyEnigmaModel

/****************************************************************/
// from https://learn.adafruit.com/memories-of-an-arduino/measuring-free-memory
int freeRam () 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

/****************************************************************/
void parsePlugboard(char* plugboard){
  char ch,ch2;
  uint8_t i,len;

  if (strlen(plugboard)>0)
    settings.plugboardMode=virtualpb;

  //start with a setting of nothing plugged in, A=>A and so on.
  for (i = 0; i < sizeof(settings.plugboard.letter); i++) {
    settings.plugboard.letter[i]=i;
  }

  i=0;
  while (strlen(plugboard)>0){
    ch=plugboard[0];
    len=strlen(plugboard);

    memmove(plugboard,plugboard+1,len);

    if (ch<'A' || ch>'Z')
      continue;
    if (i==1){
      settings.plugboard.letter[ch-'A']=ch2-'A';
      settings.plugboard.letter[ch2-'A']=ch-'A';
      i=0;
    }else{
      ch2=ch;
      i=1;
    }
  }
} //parsePlugboard

/****************************************************************/
//Function to set the current config
void setConfig(enigmaModel_t model,
	       uint8_t ukw,
	       uint8_t rotor0,
	       uint8_t rotor1,
	       uint8_t rotor2,
	       uint8_t rotor3,
	       uint8_t etw,
	       char    ringSetting[WALZECNT],	//=" AAA"
	       char    plugboard[40],		//="AB CD EF GH IJ KL MN OP QR ST UV WX YZ",
	       char    currentrotor[WALZECNT]	//=" AAA"
	       ){
  uint8_t i;
  char ch,ch2;

  settings.model = model;
  copyEnigmaModel(settings.model);
  settings.ukw = ukw;
  settings.walze[0] = rotor0;
  settings.walze[1] = rotor1;
  settings.walze[2] = rotor2;
  settings.walze[3] = rotor3;
  for (i = 0; i < WALZECNT; i++) {
    settings.ringstellung[i]=ringSetting[i]-'A';
    settings.currentWalze[i]=currentrotor[i]-'A';
    if (settings.currentWalze[i]<0){settings.currentWalze[i]+=letterCnt;}
  }
  settings.etw = etw; // Entry walze

  if (strlen(plugboard)>0)
    parsePlugboard(plugboard);
} // setConfig

//#define DEBUGWL
/****************************************************************/
// display something on one of the "wheels"
//  rightmost wheel is 3
//  leftmost is 0 and not avaliable on model M3
//
void displayLetter(char letter, uint8_t walzeno) {
  uint8_t led;
  int8_t i;
  uint16_t val;

  led = (walzeno) * 16;
  if (letter>'`') // "toupper" plus {|}~
    letter-=('a'-'A');
  val = pgm_read_word(fontTable+letter-' '); // A= 0b1111001111000000 - 0xF3C0

#ifdef DEBUGWL
  if (letter >= 0 ){ // = always
    //  if (letter < ' ' || letter > 'Z' ){
  //  if (letter < 'A' || letter > 'Z' ){
  //    if (letter == 'N' && val != 0x3324 ){
  //  if (letter == 'A' && val != 0xF3C0 ){
    Serial.print(F(" Write wheel "));
    Serial.print(walzeno,DEC);
    Serial.print(F(" val "));
    Serial.print(letter,DEC);
    Serial.print(F(" code "));
    Serial.println(val,HEX);
}
#endif
  //no lookup table needed, all LEDs are at offset 64
  HT.setDisplayRaw(walzeno*2+64/8,val & 0xFF);
  HT.setDisplayRaw(walzeno*2+64/8+1,val>>8);
  HT.sendLed();
} // displayLetter

/****************************************************************/
// scroll out message at the speed of "sleep"
// 
void displayString(char msg[], uint16_t sleep) {
  uint8_t i;

  for (i = 0; i < strlen(msg); i++) {
    if (msg[i]>='A' && msg[i]<='Z') HT.setLedNow(pgm_read_byte(led+msg[i]-65));
    displayLetter(msg[i], WALZECNT-1);
    //BUG: - should dynamically adjust to WALZECNT
    
    if (i > 0) {
      displayLetter(msg[i - 1], WALZECNT-2);
    }
    if (i > 1) {
      displayLetter(msg[i - 2], WALZECNT-3);
    }
    if (i > 2) {
      displayLetter(msg[i - 3], WALZECNT-4);
    }
    delay(sleep);
    if (msg[i]>='A' && msg[i]<='Z') HT.clearLedNow(pgm_read_byte(led+msg[i]-65));
#ifdef DEBUGWL
    Serial.println();
#endif
  }
} // displayString

/****************************************************************/
// update the wheel display
//
void displayWalzes(){
  static const char phys[] PROGMEM ="PHYS";
  static const char virt[] PROGMEM ="VIRT";
  uint8_t i;
  char ch;

  for (i=0;i<WALZECNT;i++){
    // **************** RUN ****************
    if (operationMode==run){
      if (settings.walze[i]==0){
	displayLetter(' ',i);
      } else {
	ch=settings.currentWalze[i];
	while (ch>=letterCnt){ch-=letterCnt;}
	displayLetter(getWalzeChar(&WALZE[0],ch),i);
	if (settings.currentWalze[i]==settings.ringstellung[i]){
	  decimalPoint(i,true);
	}else{
	  decimalPoint(i,false);
	}
      }

      // **************** PLUGBOARD ****************
    } else if (operationMode==plugboard) { // PLUGBOARD
      if (settings.plugboardMode==virtualpb){
	displayLetter((char)pgm_read_byte(&virt[0]+i),i);
      }else if (settings.plugboardMode==physicalpb){
	displayLetter((char)pgm_read_byte(&phys[0]+i),i);
      } else { //only config left, the value to show is handled in checkWalze
	displayLetter(currentWalzePos[i],i);
      }

      // **************** WALZE ****************
    } else if (operationMode==rotortype) {
      if (lastKeyCode >='0' && lastKeyCode <='3'){ // A buttton is pressed
	if (settings.walze[lastKeyCode-'0']==0){
	  displayLetter('-',i);
	}else{
	  displayLetter(getWalzeChar(&WALZE[settings.walze[lastKeyCode-'0']],letterCnt+i),i);
	}
      }else{
	if (settings.walze[i]==0){
	  displayLetter(' ',i);
	}else{
	  displayLetter(getWalzeChar(&WALZE[settings.walze[i]],letterCnt+WALZEDISP-1),i); // show "real" number based on current model
	  /*
	} else if (settings.walze[i]<10){
	  displayLetter(settings.walze[i]+'0',i); // the first 9 are numeric
	}else{
	  displayLetter(settings.walze[i]+'A'-10,i); // next we go into alphabet
	  */
	}
      }

      // **************** UKW ****************
    } else if (operationMode==ukw) {
      displayLetter(getWalzeChar(&UKW[settings.ukw],letterCnt+i),i);

      // **************** MODEL ****************
    } else if (operationMode==model) {
      displayLetter(EnigmaModel.display[i],i);
#ifdef FAILSAFE
    } else {
      if (i==0){
	Serial.println();
	Serial.print(F("FATAL ERROR - unknown operation mode!"));
      }
      displayLetter('?',i);
#endif
    }
  }
}  // displayWalzes

/****************************************************************/
uint8_t checkKB() {
  boolean ready;
  uint8_t i,keyflag;
  int8_t key;

  if (bitRead(debugMask,DEBUGKBD)==1){
    HT16K33::KEYDATA oldkeys,keys;

    //    ready=HT.keysPressed();
    //    if (ready != 0) {
    HT.readKeyRaw(oldkeys,false); // false to go from cache - what was read before
    key=HT.readKey(); // this will update cache but also clear the chip mem, "true" to ignore other keys
    HT.readKeyRaw(keys,false); // false to go from cache - what was read above
    if (key != 0) {
      Serial.print(F(" "));
      for (i = 0; i < 3; i++) {
	if (oldkeys[i] < 0x1000) {
	  Serial.print(F("0"));
	}
	if (oldkeys[i] < 0x100)  {
	  Serial.print(F("0"));
	}
	if (oldkeys[i] < 0x10)   {
	  Serial.print(F("0"));
	}
	Serial.print(oldkeys[i], HEX);
	Serial.print(F(" "));
      }
      Serial.println();

      Serial.print(F(" "));
      for (i = 0; i < 3; i++) {
	if (keys[i] < 0x1000) {
	  Serial.print(F("0"));
	}
	if (keys[i] < 0x100)  {
	  Serial.print(F("0"));
	}
	if (keys[i] < 0x10)   {
	  Serial.print(F("0"));
	}
	Serial.print(keys[i], HEX);
	Serial.print(F(" "));
      }
      Serial.println();
      Serial.print(F(", keyReady is: "));
      Serial.print(ready);
      Serial.print(F("  readkey is: "));
      Serial.print(key);
      Serial.println();
      return key;
    }
  } else {
    return HT.readKey();
  }
} // checkKB

/****************************************************************/
// Check what position the switch is in 
// 6 - OFF, no power to mcu
// 5 - "model" - select what enigma model, leftmost pos
//   if button is pressed a preset can be choosen
// 4 - "ukw" - which one and its position (for the ones that can move)
// 3 - "rotortype" - select what rotors that are installed
// 2 - "plugboard" - plugboard settings - if not physical
// Ringstelling is done as part of normal wheel setting when in switch pos 1
// 1 - "run" - normal running mode, rightmost pos
//
// Any time the switch is changed all settings are saved. This is so that when
// you turn it on next time it will be same as when you turned it off. It is not
// saved after each keypress since then you wear out the eeprom prematurely.
//
uint8_t checkSwitchPos(){
  uint16_t adcval;
  uint8_t i,j;
  operationMode_t newMode;
  const __FlashStringHelper  *newModeTxt;
  boolean allGood;

  adcval=analogRead(Switch);delay(1);//set internal arduino mux to position "Switch" and wait 1ms for value to stabalize
  adcval=analogRead(Switch); // get the value

  if (adcval<SwitchPos1){
    newMode=run;
    newModeTxt=F("run");
  } else if (adcval<SwitchPos2){
    newMode=plugboard;
    newModeTxt=F("plugboard");
  } else if (adcval<SwitchPos3){
    newMode=rotortype;
    newModeTxt=F("rotortype");
  } else if (adcval<SwitchPos4){
    newMode=ukw;
    newModeTxt=F("ukw");
  } else {
    newMode=model;
    newModeTxt=F("model");
  }

  if (operationMode!=newMode){ // someone moved the switch to a new mode
    if (logLevel>3){
      Serial.print(adcval);
      Serial.print(F(": "));
    }
    Serial.print(F("machine mode: "));
    Serial.println(newModeTxt);

    //Sanity check, if model changed the UKW/Walze/ETW may no longer be valid
    //Check done here since at this point the model is set
    //If done in checkWalze you would lose your config if you go from M4 to M3 and back to M4
    // UKW
    for (i=0;i<sizeof(EnigmaModel.ukw);i++){
      if (settings.ukw == EnigmaModel.ukw[i])
	break;
    }
    //    if (settings.ukw != EnigmaModel.ukw[i]){
    if (i==sizeof(EnigmaModel.ukw)){
      settings.ukw = EnigmaModel.ukw[0];
    }
    
    // WALZE4
    for (i=0;i<sizeof(EnigmaModel.walze4);i++){
      if (settings.walze[0] == EnigmaModel.walze4[i])
	break;
    }
    if (i==sizeof(EnigmaModel.walze4)){
      settings.walze[0] = EnigmaModel.walze4[0];
    }

    //Make sure no same wheel in two positions
    // if any one is wrong - reset all 3 wheels
    if (settings.walze[1]==settings.walze[2] ||
	settings.walze[1]==settings.walze[3] ||
	settings.walze[2]==settings.walze[3])
      for (j=1;j<WALZECNT;j++){
	settings.walze[j]=EnigmaModel.walze3[j-1];
      }

    // WALZE3
    for (j=1;j<WALZECNT;j++){
      for (i=0;i<sizeof(EnigmaModel.walze3);i++){
	if (settings.walze[j] == EnigmaModel.walze3[i])
	  break;
      }
      if (i==sizeof(EnigmaModel.walze3) || settings.walze[j] != EnigmaModel.walze3[i])
	settings.walze[j] = EnigmaModel.walze3[j-1];
    }

    settings.etw=EnigmaModel.etw; // each model only have one valid ETW

    for (i=0;i<WALZECNT;i++){ // clear all decimalpoints, to be set correctly later and make sure settings is valid
      decimalPoint(i,false);
      if (settings.ringstellung[i] < 0 || settings.ringstellung[i] >= letterCnt) settings.ringstellung[i]=0;
      if (settings.currentWalze[i] < 0 || settings.currentWalze[i] >= letterCnt) settings.currentWalze[i]=0;
    }
    if (settings.plugboardMode==config) // if we where in config mode, move on to virtual plugboard before saving it
      settings.plugboardMode=virtualpb;
    saveSettings(0);
    operationMode=newMode;
    displayWalzes();
    printSettings();
  }
} // checkSwitchPos


/****************************************************************/
//Set up a factory default config
void loadDefaults(){
  //Clear the settings currently in memory(SRAM)
  setConfig(M3,UKWB,WALZE0,WALZE_I,WALZE_II,WALZE_III,ETW0," AAA",""," AAA");
  settings.fwVersion = VERSION;
  settings.plugboardMode=physicalpb;
  settings.grpsize  = 5; // size of groups printed over serial
  settings.morseCode=false;// Don't send morsecode by default
  saveSettings(0);
}//loadDefaults

/****************************************************************/
// Erase everything in eeprom, to clean out all saved keys
// practically speaking restore to factory defaults
void eraseEEPROM(){
  uint16_t i;

  for (i=0;i<EEPROM.length()-SETTINGSIZE;i++){//Last setting (SETTINGSIZE bytes) are reserved, last 8 are 4 bytes odometer and 4 bytes serial
    EEPROM.update(i,0xff);
  }
  loadDefaults();
}// eraseEEPROM 



/****************************************************************/
/****************************************************************/
void setup() {
  uint8_t i, j, key;
  unsigned long ccsum;
  char strBuffer[]="PR X";

  Serial.begin(38400);
  Serial.println(F("My enigma v0.05"));
  Serial.println();

#ifdef TESTCRYPTO
  Serial.print(F(" Test crypto no "));
  Serial.println(TESTCRYPTO);
#endif

  //Reserve some space for input buffer
  serialInputBuffer.reserve(MAXSERIALBUFF);
  msgCache.reserve(MAXSERIALBUFF);

  // load prev setting (preset=0)
  if (!readSettings(0)){
    Serial.println(F("BAD checksum, setting defaults"));
    odometer=0;
    loadDefaults();
  } else {
    Serial.print(F("Checksum seems ok, ("));
    Serial.print(settings.checksum,HEX);
    Serial.println(F(") keeping the values"));
  }

  pinMode(RESET,INPUT_PULLUP);
  //Check if the big red button is pressed
  if (digitalRead(RESET)== LOW){
    eraseEEPROM();
  }

  copyEnigmaModel(settings.model);

  Wire.begin(); // enable the wire lib

  //Check for plugboard
  Wire.beginTransmission(mcp_address);
  if (Wire.endTransmission()==0){
    Serial.println(F("Preparing plugboard"));
    // Setup the port multipler
    i2c_write2(mcp_address,IOCON,0b00011110);   // Init value for IOCON, bank(0)+INTmirror(no)+SQEOP(addr inc)+DISSLW(Slew rate disabled)+HAEN(hw addr always enabled)+ODR(INT open)+INTPOL(act-low)+0(N/A)
    i2c_write2(mcp_address,IODIRA,0xff); // Set all ports to inputs
    i2c_write2(mcp_address,IODIRB,0xff); // Set all ports to inputs
    i2c_write2(mcp_address,GPPUA,0xff); // enable pullup, seems to sometimes be false readings otherwise and guessing to slow on pullup
    i2c_write2(mcp_address,GPPUB,0xff); //

    //The other chip
    i2c_write2(mcp_address+1,IOCON,0b00011110);   // Init value for IOCON, bank(0)+INTmirror(no)+SQEOP(addr inc)+DISSLW(Slew rate disabled)+HAEN(hw addr always enabled)+ODR(INT open)+INTPOL(act-low)+0(N/A)
    i2c_write2(mcp_address+1,IODIRA,0xff); // Set all ports to inputs
    i2c_write2(mcp_address+1,IODIRB,0xff); // Set all ports to inputs
    //  i2c_write2(mcp_address+1,GPPUA,0); // disable pullup (for now,to save power)
    //  i2c_write2(mcp_address+1,GPPUB,0); //
    i2c_write2(mcp_address+1,GPPUA,0xff); // enable pullup, seems to sometimes be a problem otherwise
    i2c_write2(mcp_address+1,GPPUB,0xff); //
    plugboardPresent=true;
  }else{
    Serial.println(F("No plugboard found"));
    plugboardPresent=false;
    settings.plugboardMode=virtualpb;
  }

  HT.begin(0x00);

  // Prep decimal point
  for (i=0;i<WALZECNT;i++){
    pinMode(pgm_read_byte(dp+i), OUTPUT);
  }
 
  // Test the screen
  Serial.println(F("All LEDs on"));
  for (i = 0; i < 128; i++) {
    HT.setLed(i);
  }
  HT.sendLed();
  // including all decimal points
  for (i=0;i<WALZECNT;i++){
    decimalPoint(i,true);
  }

  if (digitalRead(RESET)== LOW){
    Serial.println(F("EEPROM settings erased"));
    while (digitalRead(RESET)== LOW){}
  } else {
    delay(500);
  }

  Serial.println(F("All LEDs OFF"));
  for (i = 0; i < 128; i++) {
    HT.clearLed(i);
  }
  HT.sendLed();
  for (i=0;i<WALZECNT;i++){
    decimalPoint(i,false);
  }
  delay(500);

#ifdef ESP8266
  EEPROM.begin(1024);
#endif

  //Setup encoder wheel interrupt
  for (i = 0; i < sizeof(encoderPins); i++) {
    pinMode(pgm_read_byte(encoderPins+i), INPUT_PULLUP);
#ifdef ESP8266
    attachInterrupt(digitalPinToInterrupt(pgm_read_byte(encoderPins+i)),updateEncoderState,CHANGE);
#else
    pciSetup(pgm_read_byte(encoderPins+i));
#endif
  }

#ifndef TESTCRYPTO
  displayString("ENIGMA", 200);
  delay(500);
#endif
  
  key=checkKB();
  if (key>0){
    i=pgm_read_byte(&scancodes[0]+(key)-1);
    if (i>='0' && i <= '3'){
      switch (i) {
      case '0': // leftmost button UNDER WALZE 0
	if (!readSettings(1)){
#ifdef NOLIMITS
	  Serial.println();
	  Serial.println();
	  Serial.println(F("Preset 1 - M3,UKWB, wheel III,II,I, ringstell AAA:"));
#endif
	  setConfig(M3,UKWB,WALZE0,WALZE_III,WALZE_II,WALZE_I,ETW0," AAA",""," AAA");
	}
	break;
      case '1':
	if (!readSettings(2)){
#ifdef NOLIMITS
	  Serial.println();
	  Serial.println();
	  Serial.println(F("Preset 2 - M4,UKWBt, wheel beta,III,II,I, ringstell AAAA:"));
#endif
	  setConfig(M4,UKWBT,WALZE_Beta,WALZE_III,WALZE_II,WALZE_I,ETW0,"AAAA","","AAAA");
	}
	break;
      case '2':
	if (!readSettings(3)){
#ifdef NOLIMITS
	  Serial.println();
	  Serial.println();
	  Serial.println(F("Preset 3 - M3, wheel I,II,III, ringstell AAA"));
#endif
	  setConfig(M3,UKWB,WALZE0,WALZE_I,WALZE_II,WALZE_III,ETW0," AAA",""," AAA");
	}
	break;
      case '3':
	if (!readSettings(4)){
#ifdef NOLIMITS
	  Serial.println();
	  Serial.println();
	  Serial.println(F("Preset 4 - NORW,UKWN, wheel NIII,NII,NI, ringstell AAA:"));
	  setConfig(NorwayEnigma,UKWN,WALZE0,WALZE_NIII,WALZE_NII,WALZE_NI,ETW0," AAA",""," AAA");
#else
	  setConfig(EnigmaI,UKWB,WALZE0,WALZE_III,WALZE_II,WALZE_I,ETW0," AAA","AB CD EF GH IJ KL MN OP QR ST"," AAA");
#endif
	}
	break;
      } // switch
      saveSettings(i-'0'+1);
      Serial.print(F("Loaded preset "));
      Serial.println((char)i+1);
      strBuffer[3]=i+1;
      displayString(strBuffer, 200);
      delay(2000);
    } // if 0-3
  } // if key>0

  checkSwitchPos();
  displayWalzes();
  logLevel=0;
  printSettings();

  //for morsecode
  pinMode(spkPin,OUTPUT);
#ifdef SPK
    noTone(spkPin);
#else
    digitalWrite(spkPin,LOW);
#endif
  timeBase=90; // ms to base everything off
  toneFq=15; // 1.5khz

  Serial.println();
  Serial.println(F("Ready"));

} // setup

/****************************************************************/
// The my_strstr() function is similar to strstr() except that s1 is a far pointer to a string in program space.
// The my_strstr() function finds the first occurrence of the substring s2 in the string s1.
// it returns a pointer to the beginning of the substring, or NULL if the substring is not found.
//   http://articles.leetcode.com/2010/10/implement-strstr-to-find-substring-in.html

// 
// *s1 means "the character where s1 is pointing".
// s1[j] means "*(s1+j)" or "the character j positions after where s1 is pointing"
// s[i] really means *(s + i), so you don't have to dereference it again. The way you have it would read **(s + i), and since it's a single pointer you can't do that.

PGM_P my_strstr(PGM_P s1,char* s2){
  
  uint8_t i, j;
  char ch;
    
  if ((s1 == NULL || s2 == NULL))
    return NULL;

  ch=pgm_read_byte_near(s1+i);
  for( i = 0; ch != '\0'; i++){
    ch=pgm_read_byte_near(s1+i);
    if (ch == s2[0]) { //does the beginning match?
      for (j = i; ; j++){ // yes, check the rest of s2
        if (s2[j-i] == '\0'){ // end of s2 - we made it
	  return (char*)(s1+i);
	}
	ch=pgm_read_byte_near(s1+j);
        if (ch != s2[j-i]) 
	  break;
      }
    }
  }

  return NULL;
} // my_strstr

/****************************************************************/
// print from progmem until delim is found
void print_P(PGM_P ptr, char delim ){
  uint8_t i=0;
  char ch;

  while (true) {
    ch=pgm_read_byte_near(ptr+i);
    if (ch == delim){
      break;
    }
    if (isPrintable(ch)){
      Serial.print(ch);
    } else {
      Serial.print(F("("));
      Serial.print(ch,DEC);
      Serial.print(F(")"));
    }
    if (++i==255){
      break;
    }
  }
} // print_P


/****************************************************************/
// Find out what command that is passed.
// can't use strtok_P or strstr_P since they search for a flash stored delemiter in SRAM and I need to search in flash
// look for a command and return command number if found.
// -1 if not found.
// -2 if not unique

int8_t getCommand(char* line) {
  uint8_t i, j, cno=0;
  char ch;
  PGM_P ptr;
  PGM_P ptr2;


  ptr=my_strstr(APICMDLIST,line);
  if (ptr==NULL){
    return -1;
  }
  
  //First lets check if it's another one
  ptr2=my_strstr(ptr+1,line);
  if (ptr2!=NULL){
    return -2; // got another hit, it's not unique
  }

  // Ok, we got a match, what command number is it?
  ptr2=APICMDLIST;
  i=0;
  cno=0;
  while (ptr2<ptr){
    ch=pgm_read_byte_near(ptr2);
    if (ch == '!'){
      cno++;
    }
    ptr2++;
  }
  return cno;
} // getCommand

/****************************************************************/
void updateRingStellung(uint8_t wheelNo,uint8_t prevCurrent){
  int8_t key;
  static unsigned long ms=0;

  if (prevCurrent == settings.ringstellung[wheelNo]){
    //Check if button pressed and if so change ring also
#ifdef KEYLOCAL
    //We can't check the keyboard more often then every 20ms (scan rate)
    if ((millis()-ms) <29){
      Serial.print(F(" waiting 30"));Serial.println((millis()-ms));
      delay(30-(millis()-ms));
    }
    ms=millis();

    delay(30);


    key=checkKB();
#else
    key=lastKey; // the main loop collect keys
#endif
    if (key!=0){
      if ((key-27) == wheelNo){
	settings.ringstellung[wheelNo]=settings.currentWalze[wheelNo];
      }// if key-27==i
    } // if key pressed
  } // if at ringstellung
} // updateRingStellung

//#define DEBUGVR
/****************************************************************/
//Helper function to find valid rotors
uint8_t getValidRotors(uint8_t walzeNo, uint8_t *valid,uint8_t *vcnt){
  static uint8_t current,validCnt,j,l;

  for (j=0;j<sizeof(EnigmaModel.walze3);j++) { // clear all rotors
    valid[j]=0;
  }
  validCnt=1;// one valid router so far and that one is blank
  if (walzeNo==0){
    if (EnigmaModel.walze4[0]!=0){ // we have a 4th rotor
      if (settings.walze[walzeNo]==EnigmaModel.walze4[0]){ // only 2 to choose from so just change to the other.
	valid[1]=EnigmaModel.walze4[1];
      } else {
	valid[1]=EnigmaModel.walze4[0];
      }
      validCnt++;
    }
  } else { // not leftmost rotor
    for (j=0;j<sizeof(EnigmaModel.walze3);j++){ // copy over valid rotors from the array
      if (EnigmaModel.walze3[j] != 0 ) { // didn't hit the end
	for (l=0;l<WALZECNT;l++){ // check if it's in use elsewhere
	  if (l==walzeNo){continue;} // skip my own - that one is always valid (so I can find where I am in the list)
	  if (EnigmaModel.walze3[j]==settings.walze[l]) break; // it's used
	} // for walze
	if (l==WALZECNT){ // didn't break because it's in use
	  valid[validCnt++]=EnigmaModel.walze3[j]; //save it as valid
	} // if valid
      } // if not end
    } // for EnigmaModel.walze3
  }

  //At this point valid[] contains only valid rotors and validCnt is number of valid rotors
  current=0;
  while (settings.walze[walzeNo] != valid[current]){ // Find where we are in the array
    if (++current>7){  // emergency break, didn't find current, start from beginning
      current=0;
      break;
    }
  } // while not same

  *vcnt=validCnt;

#ifdef DEBUGVR
  Serial.println();
  Serial.print(F("walzeNo="));
  Serial.print(walzeNo);
  Serial.print(F(", valid[]={"));
  for (j=0;j<sizeof(EnigmaModel.walze3);j++) {
    if (j!=0) Serial.print(F(","));
    Serial.print(valid[j]);
  }
  Serial.println(F("}"));
  Serial.print(F("vcnt(k)="));
  Serial.print(*vcnt);
  Serial.print(F(",validCnt="));
  Serial.print(validCnt);
  Serial.print(F(", current="));
  Serial.println(current);
#endif

  return current;
} // getValidRotors

/****************************************************************/
// Check the physical wheels, if they moved and something was updated
// Return true if any wheel changed
//
//logic implemented:
//  when both inputs are high (11) it's in resting position so check what prev condition was
//   if A was low - up turn
//   if B was low - down turn
//  debounce - if less than x ms since last interrupt
//


//Local helper function
//Return true if any dup is found
boolean p_checkDups(uint8_t pairs[13][2]){
  uint8_t i,len;
  char used[27];//letters used
  
  used[0]='\0'; // Zero the string
  for (i=0;i<13;i++){
    if (pairs[i][0]==' ' && pairs[i][1]==' ')
      continue;
    if (strlen(used)>0){
      if (strchr(used,pairs[i][0]) != NULL || strchr(used,pairs[i][1]) != NULL)
	return true;
    }
    len=strlen(used);
    used[len+2] = '\0';
    used[len+1] = pairs[i][1];
    used[len]   = pairs[i][0];
  }
  return false;
}// p_checkDups

/****************/
boolean checkWalzes() {
  uint8_t walzeNo,currentRotor,validCnt,prev,i,j,x;
  boolean changed;
  uint8_t valid[8]; // valid walze in a given position
  uint8_t key;
  static  uint8_t pbpos;
  static  uint8_t pb[13][2];
  enum { none,up,down} direction;
  static uint8_t pbpairs[13][2]={
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
    {' ',' '},
  };

  changed=false; // no wheel has changed
  for (walzeNo = 0; walzeNo < WALZECNT; walzeNo++) {
    if (encoderMoved[walzeNo]) {
      encoderMoved[walzeNo] = false; // ack the change
      //  interrupt on only one pin
      //  when A goes low, check B
      //   if B is low - right turn
      //   if B is high - left turn

      direction=none;//assume this walze has not moved
      if ((encoderState[walzeNo] & 0b11)==0b11) {//current state is bottom of the click
	switch ((encoderState[walzeNo] & 0b1100)>>2) { //check prev state
	case B00:
	case B11: // if current is 11 prev can't be 11 also
	  break;
	case B10:
	  changed = true;
	  direction = up;
	  break;
	  
	case B01:
	  changed = true;
	  direction = down;
	  break;
	} // switch
      } // if current state is bottom of the click 
      
      if (direction != none){  // walzeNo moved, take action
	// **************** RUN ****************
	if (operationMode==run){
	  if ((walzeNo==0 && EnigmaModel.walze4[0] != 0) || (walzeNo != 0 )) {
	    prev=settings.currentWalze[walzeNo];
	    if (direction==up){
	      if (settings.currentWalze[walzeNo] == 0) {
		settings.currentWalze[walzeNo] = letterCnt-1;
	      } else {
		settings.currentWalze[walzeNo]--;
	      }
	    } else { // dir=down
	      if (settings.currentWalze[walzeNo] == letterCnt-1) {  // index start at 0
		settings.currentWalze[walzeNo] = 0;
	      } else {
		settings.currentWalze[walzeNo]++;
	      } // 
	    } // if direction
	    updateRingStellung(walzeNo,prev); // check if we are changing ringstellung
	  } // if walze==0

	    // **************** Plugboard ****************
	} else if (operationMode==plugboard){
	  if (walzeNo==0){
	    if (settings.plugboardMode==virtualpb){
	      settings.plugboardMode=physicalpb;
	      //Clearing of the plugboard values will be done next time it's checked
	    }else{
	      settings.plugboardMode=virtualpb;
	      pbpos=0;
	    }
	  }
	  if (settings.plugboardMode!=physicalpb ){
	    if (walzeNo==1){
	      settings.plugboardMode=config;
	      if (pbpairs[pbpos][0]==' ' || pbpairs[pbpos][1]==' '){ // if you move on before connecting the other side it's removed.
		pbpairs[pbpos][0]=' ';
		pbpairs[pbpos][1]=' ';
	      }
	      if (direction==up){
		if (pbpos==0){
		  pbpos=12;
		}else{
		  pbpos--;
		}
	      }else{ //if dir==up
		if (pbpos==12){
		  pbpos=0;
		}else{
		  pbpos++;
		}
	      } // if direction
	    } else if (walzeNo==2 || walzeNo==3){
	      //BUG: - should read current config from settings.plugboard.letter to pbpairs
	      //BUG: - should accept keys also
	      //BUG:? - should flash the letters on the lampboard as they are active

	      if (direction==up){
		do {
		  if ( pbpairs[pbpos][walzeNo-2]==' ' || pbpairs[pbpos][walzeNo-2] == 'A'){
		    pbpairs[pbpos][walzeNo-2]='Z';
		  }else{
		    pbpairs[pbpos][walzeNo-2]--;
		  }
	        } while (p_checkDups(pbpairs));		  
	      }else{
		do {
		  if ( pbpairs[pbpos][walzeNo-2]==' ' || pbpairs[pbpos][walzeNo-2] == 'Z'){
		    pbpairs[pbpos][walzeNo-2]='A';
		  }else{
		    pbpairs[pbpos][walzeNo-2]++;
		  }
	        } while (p_checkDups(pbpairs));
	      }
	      for (i = 0; i < 13; i++) {
		if ( pbpairs[pbpos][0]==' ')
		  continue;
		settings.plugboard.letter[pbpairs[i][0]]=pbpairs[i][1];
		settings.plugboard.letter[pbpairs[i][1]]=pbpairs[i][0];
	      }
	    } // if walze==2 or 3

	    if (walzeNo!=0){
	      if (pbpos>=9){
		currentWalzePos[0]='1';
	      }else{
		currentWalzePos[0]=' ';
	      }
	      currentWalzePos[1]=(pbpos+1)%10+'0';
	      currentWalzePos[2]=pbpairs[pbpos][0];
	      currentWalzePos[3]=pbpairs[pbpos][1];
	    }
	  } // if ! physpb
	  if (settings.plugboardMode==physicalpb){
	    currentWalzePos[walzeNo]=0;
	  }
	  
	  // **************** WALZE ****************
	} else if (operationMode==rotortype){
	  currentRotor=getValidRotors(walzeNo,(uint8_t*) &valid,&validCnt);
	  if (direction==up){
	    if (currentRotor==0){
	      settings.walze[walzeNo]=valid[validCnt-1];
	    }else{
	      settings.walze[walzeNo]=valid[currentRotor-1];
	    } // if edge
	  }else{
	    if (currentRotor==validCnt-1){
	      settings.walze[walzeNo]=valid[0];
	    }else{
	      settings.walze[walzeNo]=valid[currentRotor+1];
	    } // if edge
	  } // if direction
	  
	  // **************** UKW ****************
	} else if (operationMode==ukw){
	  if (walzeNo==0){//Only change UKW with leftmost rotor
	    j=NA;
	    for (i=0;i<sizeof(EnigmaModel.ukw);i++){ // find what the current one is
	      if (settings.ukw == EnigmaModel.ukw[i]){
		j=i; // save current
	      }
	      if (EnigmaModel.ukw[i]==NA){ break;}
	    }
#ifdef DEBUGVR
	    Serial.println();
	    Serial.print(F("walzeNo="));
	    Serial.print(walzeNo);
	    Serial.print(F(", EnigmaModel.ukw[]={"));
	    for (x=0;x<sizeof(EnigmaModel.ukw);x++) {
	      if (x!=0) Serial.print(F(","));
	      Serial.print(EnigmaModel.ukw[x]);
	    }
	    Serial.println(F("}"));
	    Serial.print(F(" j(current)="));
	    Serial.print(j);
	    Serial.print(F(",i(tot)="));
	    Serial.print(i);
	    Serial.print(F(", settings.ukw="));
	    Serial.print(settings.ukw);
#endif
	    // j=current one, i=tot
	    if (direction==up){
	      if (j == NA ||(i-1 == j) ){ // if not found || last
		settings.ukw = EnigmaModel.ukw[0];
	      }else {
		if (EnigmaModel.ukw[j+1] != 0){ // only change if next is a valid one (this model might only have one UKW)
		  settings.ukw = EnigmaModel.ukw[j+1];
		}
	      }
	    }else{
	      if (j == NA ){ // if not found
		settings.ukw = EnigmaModel.ukw[0];
	      } else if (j == 0){ // if at beginning
		j=i-1;
	      } else {
		j--;
	      }
	      if (EnigmaModel.ukw[j]!=NA){
		settings.ukw = EnigmaModel.ukw[j];
	      }
	    } // if direction

#ifdef DEBUGVR
	    Serial.print(F(" => "));
	    Serial.println(settings.ukw);
#endif
	  }
	  // here would changing of ETW when walzeNo==3 be but each model only have one option there so nothing to change.
	  
	  // **************** MODEL ****************
	} else if (operationMode==model){
	  if (walzeNo==0){ // only change walze on leftmost rotor (just like UKW)
	    // Is this the best way to deal with this ? - going prev/next in a enum list
	    if (direction==up){
	      settings.model=(enigmaModel_t)((int)settings.model-1);
	      if (settings.model<=FIRST){
		settings.model=(enigmaModel_t)((int)LAST-1);
	      }
	    }else{
	      settings.model=(enigmaModel_t)((int)settings.model+1);
	      if (settings.model>=LAST){
		settings.model=(enigmaModel_t)((int)FIRST+1);
	      }
	    }  // if direction 
	    copyEnigmaModel(settings.model);
	  } // if walze=0
	  //BUG: Need to handle presets here
	} // if op=model
      } // if direction != none
    } // if encoderMoved[] is true
  } // for i in walzecnt
  if (changed) 
    displayWalzes();
  return changed;
} // checkWalzes

/****************************************************************/
// check the Steckerbrett
// update the plugboard array with what plugs that are connected

void checkPlugboard() {
  //  uint8_t plug,bitt,mcp,port,i,plug2;
  uint8_t plug,i,plug2,valA[4];
  uint16_t val;
  letters_t newplugboard;
  letters_t plugs;
  static const uint8_t pbLookup[26][3]={ // could make it PROGMEM but then the code below gets bigger instead
    //mcp=i2c address,port=0 or 1,bit=0 to 7 
    {mcp_address,0,0},
    {mcp_address,0,1},
    {mcp_address,0,2},
    {mcp_address,0,3},
    {mcp_address,0,4},
    {mcp_address,0,5},
    {mcp_address,0,6},
    {mcp_address,0,7},
    {mcp_address,1,0},
    {mcp_address,1,1},
    {mcp_address,1,2},
    {mcp_address,1,3},
    {mcp_address,1,4},
    {mcp_address,1,5},
    {mcp_address,1,6},
    {mcp_address,1,7},
    {mcp_address+1,0,0},
    {mcp_address+1,0,1},
    {mcp_address+1,0,2},
    {mcp_address+1,0,3},
    {mcp_address+1,0,4},
    {mcp_address+1,0,5},
    {mcp_address+1,0,6},
    {mcp_address+1,0,7},
    {mcp_address+1,1,0},
    {mcp_address+1,1,1}};

  //start with a setting of nothing plugged in, A=>A and so on.
  for (i = 0; i < sizeof(plugs); i++) {
    plugs.letter[i]=i;
  }

  plugboardEmpty=true; // signal that nothing is plugged in

  for (plug=0;plug<sizeof(settings.plugboard);plug++){

    //make port "plug" output
    i2c_write2(pbLookup[plug][0],IODIRA+pbLookup[plug][1],0xff ^ (1<<pbLookup[plug][2]));
    //set  port "plug" low
    i2c_write2(pbLookup[plug][0],GPIOA+pbLookup[plug][1],0xff ^ (1<<pbLookup[plug][2]));

    //get all values back
    val=i2c_read2(mcp_address,GPIOA);
    valA[0]=val & 0xFF;
    valA[1]=val >> 8;

    val=i2c_read2(mcp_address+1,GPIOA);
    valA[2]=val & 0xFF;
    valA[3]=val >> 8;
    //if any one is low we have a connection
    for (i=0;i<26;i++){ // sizeof(pbLookup)/sizeof(pbLookup[0])
      if (i==plug)
	continue; // skip current port
      if (bitRead(valA[(pbLookup[i][0]-mcp_address)*2+pbLookup[i][1]],pbLookup[i][2])==0){
	plugs.letter[plug]=i; // that plug is connected to 'i'
	plugboardEmpty=false; // something is plugged in
      }
    }
    
    //make the port input again
    //i2c_write2(mcp,GPIOA+port,0xff);
    //i2c_write2(mcp,IODIRA+port,0xff);
    i2c_write2(pbLookup[plug][0],GPIOA+pbLookup[plug][1],0xff);
    i2c_write2(pbLookup[plug][0],IODIRA+pbLookup[plug][1],0xff);
  } // for plug
  
  //transpose the plugboard to real letters
  for (i = 0; i < sizeof(newplugboard.letter); i++) {
    newplugboard.letter[pgm_read_byte(steckerbrett+i)-'A']=pgm_read_byte(&steckerbrett[0]+plugs.letter[i])-'A';
  }

  if (plugboardEmpty==true && settings.plugboardMode != physicalpb) 
    return;

  // If anything is plugged in on the physical plugboard it overrides any virtual config
  settings.plugboardMode=physicalpb;

  //If something changed, copy it over and save the new setting
  if (memcmp(settings.plugboard.letter, newplugboard.letter, sizeof(settings.plugboard)) != 0){
    memcpy(settings.plugboard.letter, newplugboard.letter,sizeof(settings.plugboard));
    if (settings.plugboardMode==config) // if we where in config mode, move on to virtual plugboard before saving it
      settings.plugboardMode=virtualpb;
    saveSettings(0);
    //PSDEBUG
    if (bitRead(debugMask,1)==1 || logLevel>1){
      Serial.print(F("New Plugboard (Steckerbrett) setting: "));
      printPlugboard();
      Serial.println();
      delay(200); // mostly to debounce the plug connection
    }
  }
} // checkPlugboard

//debug doublestep
//#define DEBUGDS
/****************************************************************/
// Check if the wheel is at a notch position
// Need to handle ringsetting also here because the ring moves the notch also! 
//   That is, if the notch is at "Q" it is at what shown as "Q" even after moving the ring!
boolean checkNotch(uint8_t i){
  uint8_t notchcnt;
  char ch;

  ch=settings.currentWalze[i];
  while (ch>=letterCnt){ch-=letterCnt;}
  ch=getWalzeChar(&WALZE[0],ch);

  notchcnt=0;
  //  while (getWalzeChar(&WALZE[settings.walze[i]],letterCnt+notchcnt) != 0){ // loop until nothing left to check
  while (getWalzeChar(&WALZE[settings.walze[i]],letterCnt+WALZEDISP+notchcnt) != 0){ // loop until nothing left to check
#ifdef DEBUGDS
    Serial.print(F("walze["));
    Serial.print(i);
    Serial.print(F("] current letter is # "));
    Serial.print(settings.currentWalze[i],DEC);
    Serial.print(F("=>"));
    //    Serial.print(getWalzeChar(&WALZE[0],settings.currentWalze[i]));
    Serial.print(ch);
    Serial.print(F(", the notch is ="));
    Serial.print(getWalzeChar(&WALZE[settings.walze[i]],letterCnt+notchcnt),DEC);
    Serial.print(F("=>"));
    if (getWalzeChar(&WALZE[settings.walze[i]],letterCnt+WALZEDISP+notchcnt)>=32 && getWalzeChar(&WALZE[settings.walze[i]],letterCnt+WALZEDISP+notchcnt) <127){
      Serial.print((char)getWalzeChar(&WALZE[settings.walze[i]],letterCnt+WALZEDISP+notchcnt));
    }else{
      Serial.print(F(" "));
    }
    Serial.println(F(" "));
#endif
    if ( ch == getWalzeChar(&WALZE[settings.walze[i]],letterCnt+WALZEDISP+notchcnt)){ // check if current wheel letter is notch
      return true; // yes it is at a notch
    }
    notchcnt++; // move to next notch letter
  }
  return false;
} // checkNotch

/****************************************************************/
// http://www.cryptomuseum.com/crypto/enigma/working.htm#double
// http://www.matematiksider.dk/enigma_eng.html#how_enigma_works
//   Notch: Q V E
//   Start: S U D
//   key1:  S U E # normal, only right turned
//   key2:  S V F # normal, middle and right turned but now middle is at notch
//   key3:  T W G # no wait for right to go full turn - turn left _and_ middle directly
//   key4:  T W H # back to normal
//
// for test cases see http://arduinoenigma.blogspot.ca/2014/11/some-test-cases-for-double-stepping.html
//   and http://wiki.franklinheath.co.uk/index.php/Enigma/Paper_Enigma#Double_Stepping
//
// Logic:
//   - check and store notch 
//   - rotate
//   - _was_ it a notch?
//     -if yes
//       - check and store notch of next wheel
//       - move next wheel
//       - if it wasn't a notch - return
//       - move 3rd wheel (this case only happens when (de)coding _starts_ at a notch
//   - next wheel
//   - is it a notch?
//     -if no
//        -return
//     -if yes
//        move this wheel
//        move next wheel
//        return
//(4th wheel never turn by notch so notch on 3rd doesn't matter)
//
//BUG: this works for military enigma but not for some other like enigmaG which doesn't have double step issue
//     and when enigmaG is implemented that issue will be addressed
//

//local helper function
void _rotateSingleWheel(uint8_t i){
  if (settings.currentWalze[i] == letterCnt-1){
    settings.currentWalze[i]=0;
  }else{
    settings.currentWalze[i]++;
  }
} // _rotateSingleWheel

void rotateWheel(){
  uint8_t i,notchcnt;
  boolean notch;
  boolean doubleStep=true;

  i=WALZECNT-1;

  // Start with first wheel, the rightmost one
  // first check if notch is there
  notch=checkNotch(i);
  // then just rotate the wheel
  _rotateSingleWheel(i);

  //next wheel (one left)

  i--;
  //Check notch on prev right to see if it's time to move this one
  if (notch){
#ifdef DEBUGDS
    Serial.print(i+1);
    Serial.print(F(" now "));
    Serial.print(getWalzeChar(&WALZE[0],settings.currentWalze[i+1]));
    Serial.println(F(":was a notch, move next"));
#endif

    notch=checkNotch(i);
    // Move next wheel, middle in M3
    _rotateSingleWheel(i);
    if (!notch){
      return; // if it was a notch on rightmost wheel but not the middle, just rotate middle wheel then return
    }
#ifdef DEBUGDS
    Serial.print(i);
    Serial.print(F(":"));
    Serial.print(getWalzeChar(&WALZE[0],settings.currentWalze[i]));
    Serial.println(F(":Found a notch, move next"));
#endif
    //Middle was on a notch, need to rotate left wheel also
    //This only happens if the (de)cryption _starts_ on a notch, then it's no doblestep
    i--;
    // Next wheel, leftmost in M3
    _rotateSingleWheel(i);
  }

  // rightmost wheel was not on a notch, check if middle wheel is on a notch
  if (checkNotch(i)){
#ifdef DEBUGDS
    Serial.print(i);
    Serial.print(F(":"));
    //    Serial.print(getWalzeChar(&WALZE[0],settings.currentWalze[i]));
    if (getWalzeChar(&WALZE[0],settings.currentWalze[i]) >=32 && getWalzeChar(&WALZE[0],settings.currentWalze[i]) <127) {
      Serial.print(getWalzeChar(&WALZE[0],settings.currentWalze[i]));
    } else {
      Serial.print(F(">"));
      Serial.print(getWalzeChar(&WALZE[0],settings.currentWalze[i]),HEX);
      Serial.print(F("<"));
    }
    Serial.println(F(":Middle wheel on a notch, moving this and next"));
#endif

    // middle is on a notch, move the middle and left
    _rotateSingleWheel(i);

    i--;
    // Next wheel, leftmost in M3
    _rotateSingleWheel(i);
  }
  // The leftmost wheel on M4 is never stepped by notch

#ifndef TESTCRYPTO
  //update alnum display
  displayWalzes();
#endif

} // rotateWheel

//#define DEBUGR
/****************************************************************/
char encrypt(char ch){

  uint8_t j,minWalze;
  int8_t i;
  char  rsch;
  
  // tried to make it clear but it's not that easy to make it clear with all this conditions of
  // the current/prev walze rotation, ringstellung and so on.

  if (settings.walze[0]==0){ // if 4 wheel version, leftmost wheel is there
    minWalze=1;
  } else {
    minWalze=0;
  }

#ifdef NOLIMITS
  if (logLevel > 1 ){
    Serial.print(F("sb:  "));for (i=0;i<letterCnt;i++){Serial.print((char)(settings.plugboard.letter[i]+'A'));};Serial.println();
    Serial.print(F("ETW: "));for (i=0;i<letterCnt;i++){Serial.print(getWalzeChar(&ETW[settings.etw],i));};Serial.println();
    Serial.print(F("R3:  "));for (i=0;i<letterCnt;i++){Serial.print(getWalzeChar(&WALZE[settings.walze[3]],i));};Serial.print(F(" at "));Serial.print(settings.currentWalze[3],DEC);Serial.println();
    Serial.print(F("R2:  "));for (i=0;i<letterCnt;i++){Serial.print(getWalzeChar(&WALZE[settings.walze[2]],i));};Serial.print(F(" at "));Serial.print(settings.currentWalze[2],DEC);Serial.println();
    Serial.print(F("R1:  "));for (i=0;i<letterCnt;i++){Serial.print(getWalzeChar(&WALZE[settings.walze[1]],i));};Serial.print(F(" at "));Serial.print(settings.currentWalze[1],DEC);Serial.println();
    if (settings.walze[0]!=0){Serial.print(F("R0:  "));for (i=0;i<letterCnt;i++){Serial.print(getWalzeChar(&WALZE[settings.walze[0]],i));};Serial.print(F(" at "));Serial.print(settings.currentWalze[0],DEC);Serial.println();}
    Serial.print(F("UKW: "));for (i=0;i<letterCnt;i++){Serial.print(getWalzeChar(&UKW[settings.ukw],i));};Serial.println();
    Serial.print(F("minWalze=:  "));
    Serial.println(minWalze);
  }
#endif

  if (logLevel > 0 ){ // print letter used to go in to plugboard (no rotor so always same as keyboard but to be consistent...)
    if (ch>=65 && ch<91){
      Serial.print(ch);
    }else{
      Serial.print(ch,DEC);
    }
  }
  
  // go through the plugboard
  ch=settings.plugboard.letter[ch-'A'];

  if (logLevel > 0 ){ // Print letter coming out from plugboard
    Serial.print(F(">SB>"));
    Serial.print((char)(ch+'A'));
    Serial.print(F(" - "));

    // print letter used to go in to ETW (no rotor so always same as out from plugboard but to be consistent...)
    Serial.print((char)(ch+'A'));
  }

  // go through the ETW
  ch=getWalzeChar(&ETW[settings.etw],ch)-'A';
  
  if (logLevel > 0 ){ // Print letter coming out from ETW
    Serial.print(F(">ETW>"));
    Serial.print((char)(ch+'A'));
    Serial.print(F(" - "));
  }

  // "ch" is what's at wiring level - as if ringstellung was AAAA
  // "rsch" is what is shown on rotor after ringstelling is included
  //
  //rotors - right to left
  for (i=WALZECNT-1;i>=minWalze;i--){
    // Current walze rotation
    ch+=settings.currentWalze[i]-settings.ringstellung[i]; // Calculate what wiring to use (no ringstellung)
    
    if (i<WALZECNT-1){ //need to compensate for the prev wheels rotation
      ch-=settings.currentWalze[i+1]-settings.ringstellung[i+1];
    }
    ch=normalize(ch);
    
    if (logLevel > 0 ){ // print letter shown to go in to the rotor
      rsch=normalize(ch+settings.ringstellung[i]); // calculate what is shown (ch with ringstellung)
      Serial.print((char)(rsch+'A')); // the letter shown on the outside
      Serial.print(F("("));
      Serial.print((char)(ch+'A')); // real letter at wiring level
      Serial.print(F(")"));
    }
    
    // go through the rotor
    ch=normalize(getWalzeChar(&WALZE[settings.walze[i]],ch)-'A');
    
    if (logLevel > 0 ){ // print letter coming out from that rotor
      rsch=normalize(ch+settings.ringstellung[i]);
      Serial.print(F(">R"));
      Serial.print(i);
      Serial.print(F("(#"));
      Serial.print(settings.walze[i],DEC);
      Serial.print(F(")>"));

      Serial.print((char)(rsch+'A')); // the letter shown on the outside
      Serial.print(F("("));
      Serial.print((char)(ch+'A'));
      Serial.print(F(") - "));
    }
  }

  //UKW - turn around

  // Prev walze rotation
  ch-=settings.currentWalze[minWalze]-settings.ringstellung[minWalze];
  ch=normalize(ch);  //make sure it's in range
  
  if (logLevel > 0 ){ // print letter used to go in to the UKW
    Serial.print(F("("));
    Serial.print((char)(ch+'A'));
    Serial.print(F(")>UKW>("));
  }

  // go through the UKW (reflector)
  //  if (settings.ukw==UKWD){ // UKWD was a reconfigurable reflector
  //    ch=ukwDA[ch]-'A';
  //  }else{
    ch=getWalzeChar(&UKW[settings.ukw],ch)-'A';
    //  }
  
  if (logLevel > 0 ){ // print letter used when leving the UKW
      Serial.print((char)(ch+'A'));
      Serial.print(F(") - "));
  }

  // Rotors - left to right
  for (i=minWalze;i<WALZECNT;i++){
    // Current walze rotation
    ch+=settings.currentWalze[i]-settings.ringstellung[i];// Calculate what wiring to use (no ringstellung)

    if (i > minWalze){ //need to compensate for the prev wheels rotation
      ch-=settings.currentWalze[i-1]-settings.ringstellung[i-1];
    }

    ch=normalize(ch);  //make sure it's in range

    if (logLevel > 0 ){ // print letter shown to go in to the rotor
      rsch=ch+settings.ringstellung[i]; // calculate what is shown (ch with ringstellung)
      rsch=normalize(rsch);  //make sure it's in range
      Serial.print((char)(rsch+'A')); // the letter shown on the outside
      Serial.print(F("("));
      Serial.print((char)(ch+'A')); // real letter at wiring level
      Serial.print(F(")"));
    }

    // go through the rotor
    // need to traverse other way, from left to right side 
    // and since it's in progmem things like strbrk won't work
    for (j=0;j<letterCnt;j++){
      if (getWalzeChar(&WALZE[settings.walze[i]],j)-'A' == ch){
	ch=j;
	break;
      }
    }

    if (logLevel > 0 ){ // print letter coming out from that rotor
      rsch=normalize(ch+settings.ringstellung[i]);
      Serial.print(F(">R"));
      Serial.print(i);
      Serial.print(F("(#"));
      Serial.print(settings.walze[i],DEC);
      Serial.print(F(")>"));

      Serial.print((char)(rsch+'A')); // the letter shown on the outside
      Serial.print(F("("));
      Serial.print((char)(ch+'A'));
      Serial.print(F(") - "));
    }
  } // for i in walze

#ifdef DEBUGRS
  Serial.print(F(" {"));
  Serial.print(ch,DEC);
  Serial.print(F("-"));
  Serial.print(settings.currentWalze[WALZECNT-1]);
  Serial.print(F("+"));
  Serial.print(settings.ringstellung[WALZECNT-1]);
  Serial.print(F("="));
#endif

  //Next, the ETW
  // handle prev weels rotation
  ch+=settings.ringstellung[WALZECNT-1]-settings.currentWalze[WALZECNT-1];
#ifdef DEBUGRS
  Serial.print(ch,DEC);
  Serial.print(F("<>"));
#endif

  ch=normalize(ch);  //make sure it's in range
#ifdef DEBUGRS
  Serial.print(ch,DEC);
  Serial.print(F("} "));
#endif
  if (logLevel > 0 ){ // print letter used to go in to EKW
      Serial.print((char)(ch+'A'));
  }

  // go through the ETW
  ch=getWalzeChar(&ETW[settings.etw],ch)-'A';

  if (logLevel > 0 ){ // print letter coming out from the ETW
    Serial.print(F(">ETW>"));
    Serial.print((char)(ch+'A'));
    Serial.print(F(" - "));

    //and finally the plugboard
    Serial.print((char)(ch+'A'));
  }

  // go through the plugboard
  ch=settings.plugboard.letter[ch];

  if (logLevel > 0 ){ // print letter coming out from the plugboard
    Serial.print(F(">SB>"));
    Serial.print((char)(ch+'A'));
    Serial.print(F("  :"));
  }

#ifdef DEBUGR
  Serial.println();
#endif

  odometer++; // one more letter encrypted...

  //PSDEBUG - final check that shouldn't fail
  if (ch>26 || ch<0){
    Serial.println();
    Serial.print(F(" ENCRYPT ERROR: "));
    Serial.println(ch,DEC);
    ch=normalize(ch);
  }
  return ch+'A';
}// encrypt

/****************************************************************/
void parseWheels(String val,int8_t rotor[4]){
  uint8_t pos,i,j;
  char ch,r[4];
  
  pos=0;
  for (i=0;i<WALZECNT;i++){
    do {
      ch=val.charAt(pos++);
    } while (ch!='\0' && (ch<'A' || ch >'Z'));
    if (ch=='\0'){
      break;
    }
    r[i]=ch;
  }
  if (i<3 || i>4){
    printValError(val);
  } else {
    if (i==3) {
      r[3]=r[2];
      r[2]=r[1];
      r[1]=r[0];
      r[0]='A';
    }
    for (i=0;i<WALZECNT;i++){
      rotor[i]=r[i]-'A';
    }
  }
} // parseWheels

/****************************************************************/
//the serialInputBuffer starts with a "!" for command
// check what command it is and execute it
void parseCommand() {
  uint8_t i,r[4],j;
  int8_t pos,pos2;
  int8_t cmd;
  char ch,ch2;
  char cmdline[7];
  String command;
  String val;
  String val2;
  enigmaModels_t checkModel;
  
  /*
    command starts with "!"
    a ":" separate command from values
    nothing after ":" means show current setting
    Commands;
    !M[ODEL]
    !SE[TTINGS]
    !U[KW]
    !W[ALZE]
    !R[ING]
    !ST[ART]
    !PL[UGBOARD]
    !DUKW
    !SA[VE]
    !LOA[D]
    !LOG[LEVEL]
    !D[EBUG]
    !V[ERBOSE]
    !DUKW
    "#" as input is treated as a comment and ignored
    "# as output is respond to a command
    A-Z is text to encrypt/decrypt
    ">" starts a encrypted/decrypted text response
    Sample:
    !MODEL?
    #MODELS:
    # M3
    # M4
    !MODEL: M3
    #MODEL:M3
    !SETTINGS:
    #Settings; M3:B:I-II-III:AAA:AAA:
    !UKW: B
    #UKW:B
    !WALZE: VII-I-IV
    #WALZE:VII-I-IV
    !WALZE: 6,1,3
    #WALZE:VI-I-III
    !RING: A B C
    #RING:A B C
    !PLUGBOARD: TH EQ UI CK BR OW NF XJ MP SV
    #PLUGBOARD:BR CK EQ MP NF OW SV TH UI XJ
    !START: F S F
    #Rotor start set to: F S F
    !SETTINGS:
    #SETTINGS:M3,B,VI-I-III,ABC,FSF,BR CK EQ MP NF OW SV TH UI XJ
    !SAVE:1
    #Settings saved as preset:1
    ENI
    >
    !START: OLD
    #Rotor start set to; O L D
    RBRHY ESYXZ KVDKA IIVOU UYIZG LOBSF OVFGM FQVT
    >(the decrypted text)

    !LOAD:2
    #Loaded preset 2
    #SETTINGS:M3,B,V-I-IV,XYZ,FSF,
    !SETTINGS:M3,B,6-1-3,ABC,FSF,H EQ UI CK BR OW NF XJ MP SV
    #SETTINGS:M3,B,VI-I-III,ABC,FSF,BR CK EQ MP NF OW SV TH UI XJ

  */

  pos=serialInputBuffer.indexOf('?');
  if (pos <0){ // if not questionmark
    pos=serialInputBuffer.indexOf(':');
  }
  if (pos==-1){
    Serial.println(F("ERROR: No value found"));
    serialInputBuffer = "";
    stringComplete = false;
  } else if (pos>=0){
    command=serialInputBuffer.substring(0,pos);
    val=serialInputBuffer.substring(pos+1);	
    val.trim();
    cmd=getCommand((char*)command.c_str());
#ifdef DEBUG
    Serial.print(F("cmdpart: "));
    Serial.print(command);
    Serial.print(F(", val: "));
    Serial.println(val);
    Serial.print(F("Command no: "));
    Serial.println(cmd);
#endif
    serialInputBuffer = "";
    stringComplete = false;
    
    if (cmd==-2){
      Serial.println(F("#ERROR: command not unique:"));
      print_P(APICMDLIST,'\0');
      Serial.println();

    } else if (cmd==-1){
      Serial.println(F("#ERROR: unknown command:"));
      print_P(APICMDLIST,'\0');
      Serial.println();

    } else if (cmd==CMD_SETTINGS){
      // SETTINGS
#ifdef NOLIMITS
      //TODO/BUG: Suppose to be able to set it all here also but no program space for that code
      Serial.print(F("%SETTINGS: "));
      printModel();
      Serial.print(F(","));
      printUKW(settings.ukw);
      Serial.print(F(","));
      printRotor(&settings.walze[0]);
      Serial.print(F(","));
      printWheel(&settings.ringstellung[0]);
      Serial.print(F(","));
      printWheel(&settings.currentWalze[0]);
      Serial.print(F(","));
      printPlugboard();
      Serial.println();
#else
      printSettings();
#endif

    } else if (cmd==CMD_MODEL){
      // MODEL
      if (val.length() != 0){
	i=-1;
	val2=val;
	val2+="    ";
	val2=val2.substring(0,4);
	do {
	  i++;
	  memcpy_P(&checkModel,&EnigmaModels[i],sizeof(EnigmaModel));
	} while ((strcmp(checkModel.display,val2.c_str()) != 0) && i<MODELCNT);
	if (i<MODELCNT){ // we had a hit
	  memcpy_P(&EnigmaModel,&EnigmaModels[i],sizeof(EnigmaModel));
	  settings.model=checkModel.model;
	}else{
	  printValError(val);
	  Serial.println(F("Valid models: "));
	  for (i=0;i<MODELCNT;i++){
	    memcpy_P(&checkModel,&EnigmaModels[i],sizeof(EnigmaModel));
	    Serial.print(F("  "));
	    Serial.print(checkModel.display);
	    Serial.print(F(" ("));
	    Serial.print(checkModel.lname);
	    Serial.println(F(")"));
	  }
	  Serial.println();
	}
      }
      Serial.print(F("%MODEL:"));
      printModel();

    } else if (cmd==CMD_UKW){
      // UKW
      if (val.length()!=0){
	i=-1;
	do {
	  i++;
	  val2="";
	  for (j=0;j<5;j++){
	    if (getWalzeChar(&UKW[i],26+4+j) != ' '){
	      val2+=getWalzeChar(&UKW[i],26+4+j);
	    }else{
	      val2+='\0';
	    }
	  }
	} while ((strcmp(val.c_str(),val2.c_str()) != 0) && i<UKWCNT);

	if (i<UKWCNT){
	  settings.ukw=i;
	} else {
	  printValError(val);
	  Serial.println(F("Valid UKWs: "));
	  for (i=0;i<UKWCNT;i++){
	    Serial.print(F("  "));
	    for (j=0;j<5;j++){
	      Serial.print((char)getWalzeChar(&UKW[i],26+4+j));
	    }
	    Serial.println();
	  }
	}
      }
      Serial.print(F("%UKW: "));
      printUKW(settings.ukw);
      displayWalzes();

    } else if (cmd==CMD_WALZE){
      // WALZE
      if (val.length()!=0){
	val+=",";
	i=0;
	pos=0;
	pos2=val.indexOf(',');
	while (pos2>0 && i<6){
	  val2=val.substring(pos,pos2);
	  r[i++]=val2.toInt();
	  pos=pos2+1;
	  pos2=val.indexOf(',',pos2+1);
	}
	if (i<3 || i>4){
	  printValError(val);
	} else {
	  if (i==3) {
	    r[3]=r[2];
	    r[2]=r[1];
	    r[1]=r[0];
	    r[0]=0;
	  }
	  for (i=0;i<WALZECNT;i++){
	    settings.walze[i]=r[i];
	  }
	}
      }
      Serial.print(F("%WALZE: "));
      printRotor(&settings.walze[0]);
      displayWalzes();

    } else if (cmd==CMD_RING){
      // RING
      if (val.length()!=0){
	parseWheels(val,&settings.ringstellung[0]);
      }
      Serial.print(F("%RING: "));
      printWheel(&settings.ringstellung[0]);
      displayWalzes();

    } else if (cmd==CMD_PLUGBOARD){
      // PLUGBOARD
      
      if (val.length()!=0){
	parsePlugboard((char*)val.c_str());
      }
      Serial.print(F("%PLUGBOARD: "));
      printPlugboard();

    } else if (cmd==CMD_START){
      //START
      if (val.length()!=0){
	parseWheels(val,&settings.currentWalze[0]);
      }
      Serial.print(F("%START: "));
      printWheel(&settings.currentWalze[0]);
      displayWalzes();
    } else if (cmd==CMD_SAVE){
      //SAVE
      Serial.print(F("%SAVE"));
      if (val.length()==0){
	Serial.println(F("ERROR: No position given"));
      } else {
	if (val.toInt()>0 && val.toInt()<MAXPRESET){ // preset 0 is reserved
	  Serial.print(F(" - Save settings as preset "));
	  Serial.println(val.toInt());
	  saveSettings(val.toInt());
	} else {
	  printValError(val);
	}
      }
    } else if (cmd==CMD_LOAD){
      //LOAD
      Serial.print(F("%LOAD"));
      if (val.length()==0){
	Serial.println(F("ERROR: No position given"));
      } else {
	if (val.toInt()>=0 && val.toInt()<MAXPRESET){
	  if (readSettings(val.toInt())){
	    Serial.print(F(" - Read settings from preset "));
	    Serial.println(val.toInt());
	    displayWalzes();
	    printSettings();
	  }else{
	    Serial.print(F(" - No valid preset found at "));
	    Serial.println(val.toInt());
	  }
	} else {
	  printValError(val);
	}
      }
      //    } else if (cmd==CMD_DUKW){
      //Here config of UKW-D suppose to be

    } else if (cmd==CMD_LOGLEVEL){
      //LOGLEVEL
      if (val.length()!=0){
	logLevel=val.toInt();
      }	
      Serial.print(F("%LOGLEVEL:"));
      Serial.println(logLevel);
  
      /*    
    } else if (cmd==CMD_DEBUG){
      //DEBUG
      if (val.length()!=0){
	debugMask=val.toInt();
      }	
      Serial.print(F("%DEBUG:"));
      Serial.println(debugMask);
    } else if (cmd==CMD_VERBOSE){
      //VERBOSE
      Serial.print(F("%VERBOSE - not implemented"));
      */
    }
  }
  Serial.println();
} // parseCommand

/****************************************************************/
void sendLetter(char ch){
  uint8_t flag,i;

  if (ch<'A' || ch>'Z')
    return; // outside the range of this implementation

  ch=pgm_read_byte(morsecode+ch-'A');

  flag=ch & 0b1000000; // get what bit to skip
  i=0;
  while ((ch & 0b1000000)==flag && i<7){ // shift as long as it's same
    ch=ch<<1;
    i++;
  }

  while (i<7){ // now start sending the code
#ifdef SPK
    tone(spkPin,toneFq*100);
#else
    digitalWrite(spkPin,HIGH);
#endif
    if ((ch & 0b1000000)==0){
      //      Serial.print(".");
      delay(timeDitt);
    }else{
      //      Serial.print("-");
      delay(timeDah);
    }
#ifdef SPK
    noTone(spkPin);
#else
    digitalWrite(spkPin,LOW);
#endif
    delay(timePart);
    ch=ch<<1;
    i++;
  }
} // sendLetter

#ifndef TESTCRYPTO

/****************************************************************/
/****************************************************************/
/*
 * wait for key pressed
 * once pressed, light up corresponding LED (letter)
 * if more than one key is pressed, turn off all letters
 * and leave them off until all keys are released.
 * The reason is that on the original enigma you can not press two keys at 
 * the same time since then the wheel rotation gets messed up
 * Optional:
 *   press one key
 *      rotate and encrypt
 *      light up encrypted letter
 *   press second key
 *      do NOT rotate
 *      encrypt second key
 *      if conflict - lights off
 *      otherwise on with both
 *   release first key
 *      do NOT rotate
 *      encrypt second key and light up letter
 *   need to watch out for 3 keys becoming 4 keys
 *
 */

void loop() {
  static uint8_t ledOn=0;
  uint8_t i,pos;
  int8_t key;
  uint16_t val,cnt;
  char j,ench;
  static int16_t prevFreeRam;
  int16_t freeNow;
  static unsigned long ms=0;
  char strBuffer[11];
  HT16K33::DisplayRam_t prevRamState;

#ifdef PSDEBUG
  //PSDEBUG
  freeNow=freeRam();
  if (freeNow != prevFreeRam){
    Serial.print(F("Free ram = "));
    Serial.print(freeNow);
    Serial.print(F(", changed from "));
    Serial.print(prevFreeRam);
    Serial.print(F(" = "));
    Serial.print(freeNow-prevFreeRam);
    Serial.println(F(" bytes change"));
    Serial.println ();
    prevFreeRam=freeNow;
  }
#endif

  //Check if emergency erase is pressed
  if (digitalRead(RESET)== LOW){
    eraseEEPROM();
    for (i = 0; i < 128; i++) {
      HT.setLed(i);
    }
    HT.sendLed();
    for (i=0;i<WALZECNT;i++){
      decimalPoint(i,true);
    }
    Serial.println(F("EEPROM settings erased"));
    Serial.println(F("Now you need to powercycle it to start over"));
    key=0;
    while (true){
      for (i=0;i<WALZECNT;i++){
	if (i==key){
	  decimalPoint(i,true);
	}else{
	  decimalPoint(i,false);
	}
      } //for
      key++;
      if (key==WALZECNT)
	key=0;
      delay(500);
    } // while true
  } // If reset
  /* Check serial input */
  if (stringComplete) {
    //Analyze what we got...
    serialInputBuffer.toUpperCase();
    serialInputBuffer.trim(); // trim off any trailing \n or white space.
    Serial.println();
    Serial.println(serialInputBuffer);
    if (serialInputBuffer.length()==0)
      serialInputBuffer="#";
    if (serialInputBuffer[0]=='#'){
#ifdef DEBUGAPI
      Serial.println();
      Serial.println(F("Received a comment - ignoring it"));
#endif
      serialInputBuffer = "";
      stringComplete = false;
      
    } else if (serialInputBuffer[0]=='?'){
      Serial.println();
      Serial.println(F("Available commands:"));
      print_P(APICMDLIST,'\0');Serial.println();
      serialInputBuffer = "";
      stringComplete = false;
    } else if (serialInputBuffer[0]=='!'){
#ifdef DEBUGAPI
      Serial.println(F("Received a command"));
#endif
      parseCommand();
    } else if (serialInputBuffer[0]>='A' && serialInputBuffer[0]<='Z'){ // just encrypt/decrypt the text
      //Need to free the buffer in case more is coming
      msgCache=serialInputBuffer;
      serialInputBuffer = "";
      stringComplete = false;
      if (logLevel==0){
	Serial.print(F(">"));
      }
      pos=1;
      for (i=0;i<msgCache.length();i++){
	serialEvent();//grab and store any new character
	if (logLevel>0){
	  Serial.print(F(">"));
	}
	ench=msgCache.charAt(i);
	if (ench>='A' && ench <='Z'){
	  checkPlugboard();
	  rotateWheel();
	  Serial.print(encrypt((char)ench));
	  if (logLevel>0){
	    Serial.println();
	  }else{
	    if ((pos++%settings.grpsize)==0){
	      Serial.print(F(" "));
	      if ((pos%80)>75){
		Serial.println();
		Serial.print(F(">"));
		pos=1;
	      } // if >75
	    } // if settings.grpsize
	  }// if loglevel
	} // if a-z
      } // for msg[]
      Serial.println();
    } else {
      Serial.print(F("Unknown input: "));
      Serial.println(serialInputBuffer);
      serialInputBuffer = "";
      stringComplete = false;
    }
  } // if stringComplete

  /* Update switch position */
  checkSwitchPos();
 
  // Don't check to fast or it won't work!
  // to scan all keys including debounce we talking 20ms
  // and if key register is read faster than that it will be 0 on the second read within a 20ms frame
  // Page 17 in the datacheet hints about it.
  // Waiting 30ms to be on the safe side
  while ((millis()-ms) <30){
    serialEvent();
    // and while we wait - check the rotors to have good response there
    if (checkWalzes()){ // rotor(s) where changed
      displayWalzes();
    }
    if (logLevel>1)
      	checkPlugboard();
  }

  ms=millis();

  /* Check the keyboard */
  key=checkKB();

  if (key!=0){
    lastKey=key;//Store last key for checkWalze and displayWalze
    if (key<0){
      lastKeyCode=pgm_read_byte(&scancodes[0]+(-key)-1);
    } else {
      lastKeyCode=pgm_read_byte(&scancodes[0]+key-1);
    }
    cnt=0;
    if (logLevel>0){
      Serial.print(F(" Key pressed: "));
      Serial.print(key);
      Serial.print(F(" - "));
      Serial.println((char)lastKeyCode);
    }
  } else { // no key pressed
    if ( cnt ==2 ){
      cnt++;
      displayWalzes();
    } else {
      cnt++;
    }
  } // if key!=0;else

  if (HT.keysPressed()==1) { // how many keys are pressed?
    if (key>0 && key<=letterCnt){
      if (operationMode == model){ // possible show some info/test things
	//L turn on all lights
	//M turn on/off morsecode
	//V Show version
	//S Show serial number
	//O Show odometer

	for (i=0;i<sizeof(prevRamState);i++){prevRamState[i]=HT.displayRam[i];} // Save current state
	switch (pgm_read_byte(&scancodes[0]+(key)-1)) {
	case 'L': // turn on all lights
	  Serial.println(F("All lighs on"));
	  for (i = 0; i < 128; i++) {
	    HT.setLed(i);
	  }
	  HT.sendLed();
	  // including all decimal points
	  for (i=0;i<WALZECNT;i++){
	    decimalPoint(i,true);
	  }
	  while (HT.keysPressed()==1){delay(30);key=checkKB();}; // stick around until key is released
	  // including all decimal points
	  Serial.println(F("All lighs off"));
	  for (i = 0; i < 128; i++) {
	    HT.clearLed(i);
	  }
	  HT.sendLed();
	  for (i=0;i<WALZECNT;i++){
	    decimalPoint(i,false);
	  }
	  break;

	case 'M': // turn on/off morsecode
	  Serial.print(F("morsecode "));
	  if (settings.morseCode){
	    Serial.println(F("OFF"));
	    sendLetter('O');delay(timeLetter);
	    sendLetter('F');delay(timeLetter);
	    sendLetter('F');
	    settings.morseCode=false;
	  }else{
	    Serial.println(F("ON"));
	    settings.morseCode=true;
	    sendLetter('O');delay(timeLetter);
	    sendLetter('N');
	  }
	  break;

	case 'V': // Show version
	  displayString("V005",0);
	  decimalPoint(1,true);
	  delay(2000);
	  decimalPoint(1,false);
	  break;

	case 'S': // Show serial number
	  ultoa(serialNumber,strBuffer,10);
	  displayString("    ",0);
	  displayString(strBuffer,400);
	  delay(2000);
	  break;

	case 'O': // Show odometer
	  ultoa(odometer,strBuffer,10);
	  displayString("    ",0);
	  displayString(strBuffer,400);
	  delay(2000);
	  break;

	} // switch
	for (i=0;i<sizeof(prevRamState);i++){HT.displayRam[i]=prevRamState[i];} // Restore current state
	HT.sendLed();

      }else if (operationMode != run){ // the keyboard works literally (no encryption)
	ledOn=pgm_read_byte(led+((byte)pgm_read_byte(&scancodes[0]+key-1)-'A'));
	if (logLevel>1){
	  Serial.print(F("  turning on LED: "));
	  Serial.println(ledOn);
	}
	HT.setLedNow(ledOn);
	delay(11);
      } else {
	checkPlugboard();
	rotateWheel();
	ench=encrypt(pgm_read_byte(&scancodes[0]+key-1));
	Serial.print(ench);
	ledOn=pgm_read_byte(led+ench-'A');
	if (logLevel>1){
	  Serial.print(F("  turning on LED: "));
	  Serial.println(ledOn);
	}
	HT.setLedNow(ledOn);
	if (settings.morseCode)
	  sendLetter(ench);
      }
    }
  }

  if (HT.keysPressed()!=1){
    lastKey=0;//clear last key
    lastKeyCode=0;
    if (ledOn!=0){
      for (i=0;i<sizeof(led);i++){
	if (HT.getLed(pgm_read_byte(led+i))){ // Check if it's set
	    if (logLevel>1){
	      Serial.print(F("  turning off LED: "));
	      Serial.println(pgm_read_byte(led+i));
	    }
	    HT.clearLed(pgm_read_byte(led+i));
	  }
      }
      HT.sendLed();
      ledOn=0;
    }
  }
} // loop
#endif


#ifdef TESTCRYPTO

//http://enigma.louisedade.co.uk/enigma.html?m4;b_thin;b321;AAAA;AAAA
const char msg1[] PROGMEM = {"FTZMGISXIPJWGDNJJCOQTYRIGDMXFIESRWZGTOIUIEKKDCSHTPYOEPVXNHVRWWESFRUXDGWOZDMNKIZWNCZDUCOBLTUYHDZGOVBUYPKOJWBOWSEEMTZFWYGKODTBZDQRCZCIFDIDXCQZOOKVIIOMLLEGMSOJXHNFHBOFDZCTZQPOWVOMQNWQQUOZUFMSDXMJXIYZKOZDEWGEDJXSMYHKJKRIQXWBITWLYUSTHZQMGTXXWIHDOBTKCGZU"};

// http://enigma.louisedade.co.uk/enigma.html?m4;b_thin;b321;BCDE;AUDS
const char msg2a[] PROGMEM = {"BJCXRHMTOKXQRYHMSVHYKLVBSMRHZYKWXRFSUJFQZZXFSVCXCWLTVIJTRZTPOBXEGYHDYYGWLGYBYOBHVZXMMOCTRNOSDZXTDYIFVQGIVJPUSKIBHODHFRZMMRSKBDQWWOOHSMRUHIOCRFJSUZXQEBNIBIJIIHTDURIOZUDRSRFZXCBKQVFOFOGKSI"};
// https://people.physik.hu-berlin.de/~palloks/js/enigma/enigma-u_v20_en.html same settings
const char msg2[]  PROGMEM = {"BJCXRHMTOKXQRYHMSVHYKLVBSMRHZYKWXRFSUJFQZZXFSVCXCWLTVIJTRZTPOBXEGYHDYYGWLGYBYOBHVZXMMOCTRNOSDZXTDYIFVQGIVJPUSKIBHODHFRZMMRSKBDQWWOOHSMRUHIOCRFJSUZXQEBNIBIJIIHTDURIOZUDRSRFZXCBKQVFOFOGKSIZTKHNMRTESWOIQVDXIBGLXSIQKOVBJTEBMGNTDLSGEIPPPJMYFQYDLMRPZULSQ"};

// http://enigma.louisedade.co.uk/enigma.html?m3;b;b321;ABCD;AAAA
const char msg3[] PROGMEM = {"REZCTKKDKXNSDORKGCZKPWZKQUMFTTKXEKOYDOWLYPBYFDFKUKIVCTFZGRJDPUDSBYNUBKPPTZUXSWCTFSGSNYFBVYDULJUHDDGWOZDMNKIZWNCZDUCOBLTULYFZVZWZDRQSUBOOJEGLCPXKUBDZGCICGWYVIEDKTRPJPCHOLLNQQFLYZZVBBDMOTYHDKSBMETKGQVZKGQZEMOIWGQKOZDPILDUPPXQYXFNNEQNUJXLBWVNKHUGIJLQK"};

// With   setConfig(M3,UKWB,0,WALZE_I,WALZE_II,WALZE_III,ETW0," BCD",""," UDS"); this should all decrypt to "A"
// http://enigma.louisedade.co.uk/enigma.html?m3;b;b123;ABCD;AUDS
const char msg4[] PROGMEM = {"XUHHQJDMTYDDVKLBUXYEHWCKJBJRMJBFSYBXKLJCDMDYPOILEQWFCPQMQEBJEETGVBIZJKDJZFZMRVUEELGSMOJGUCSQLYGNVLFKGKTLNRJYPPNLPJZWCSEVPCNVXUWXLOMYRJFDNCOHXSGJGTOWJIMDFYMISDICIUCKLFNDMDTXYXEFVIBWNXPLSRPORRDMLTJRNDFTMBKBJGSQVNOJFJXESBBXZXJOWWFRTQSYETDCMOQZKZMZCUVHYWHRQIMDFNMDRGFKOECMQGNEBSZKUQNWVCBPKLIFUDPUYJWBDXKBIZMQESRRQRCTQEGSEDDCTKFPFDQPLEIXDEVEPPJTBBTLYRHVLZSEQPNNPOWLDUWELTGYICIEDRTJCONOKLCCGZIMFZVNDVFISGGDBLZFBRMMDGZHZRQWKXZQYDHCFBPXJMXENBLZTYSJDTWHWGHEOSYCOONUDXIPGCNZKWFUFPPHCNQIFYSBRZWNESLCBZYOFBWIVGCXSSNGCLWVYHBLHXPEMOPLXGTVNBTQWDRTQPBRXVMVEUHXLWQXKLGRKBUUHTPTDWMFMCTZSRFJYPQVIDWCXMPKEMBJFIDUZTZLHQZXCWZCQFKGTGRVKGWZPOPLMMWLOOEDNMDQTNDDBJZDOXSNEGHTYSJFKMEEMORCVNLURFLOJIMDFQMUBVIRPWGGZNIONRWIGGKGHHCTQSEIJRCJWZMGSKDRDNDMXUITEOHLRPMTLKPHGWZTJJVQOWMQCNOYVRKJRMHXPIEETONVVHGKGQPNMMVVUJVTSHXUWWGFEXSQGIYKCVOLORQMJQXVHUNEZHDHQPRKXXQEPJPVXTREDGZHFOUBLZPSZHLLOKLHTMWWLOZEWRPJLSXYKGHIBUETHVHOMOKQTBNPYKNQONMLIBOZVYBJDPFUJVLKLTNWGFQPBCWWYYCXOYIRXXXTLKWTNEWMDOTQWKXUKTJGHVXLSQKMUPMIBQZFGSVQPOOLLJWNRDZYVVMOPJVTCBLZGTMDQPPKBOMRQGYORSHUH"};

// setConfig(M4,UKWBT,WALZE_Beta,WALZE_VIII,WALZE_VII,WALZE_VI,ETW0,"QWER","","ASDF");
// http://enigma.louisedade.co.uk/enigma.html?m4;b_thin;b876;QWER;ASDF
const char msg5[] PROGMEM = {"WYVTZJYQSBJNKBVDEWQKMIFDXUKPJBLYLGNNYSRLWFLESBWRQUBPRYMMXGRJJIKCYKVJMVRKJFVVKUSPCBLPZYWSDQGHQEPFFDCLTEESJCOKISRJZYLFLQQKPSOSISPMMRYSKPXIIKUCQWPOPTQXIXUKQLUDFXMNVGEXQEJNMOGWCZDQXDRWOOLTPQIQMNQLSOMPFWLZSCECUJHQQPBTMBYMNWBYWLCGYWDMCZFOXFGCJSHUKXUQZNMIWBEZGLTTBXKTHHHKPZMZOEYYWEGSLMSFQTWLPTFEENDZEKLEERGHGIUWLXTZKNWVDSYPEKDGNKYCFLEFUVLFHZHPSWTULMCJTXHYFNEREKSJYYQDCNZYEEVUSFFODEEYXQMUWMOFYXPRDLPIKYKTGISBYWMWYHEZQCURBCHHHZHUHGKYIPMDPDKTYMIMHIVVSFGRCKWULYRXNYHYCHYRVMYTWXPTBREBCHYJXXSEQDKQQXRPWVKLQPQRYOOHEEGNUJIKSRTEZHDJHCGMBZLIEZMNJUHQMDKQMZUFEBNHYQEGXDDFFYORSJCGHMIOHHMDVVGUKSCPUXNUGLWXVUFMELZGGUKZOBLOYWHNKODHIWVRRQIKYYWWLPQPNHNOXJTKXQZWPWZUWGVXSXFSFKUBJJFZMYXNQPFGPYJEXVYOETVIRHDGHLKKBYFCWDKKSVXXBWKFKBNYYMXNYODHVJUNBPVMEHMCXQRSKKUZVBXKLDDCFNSLVVTDFPTQRUBFOVQSCWKYFCVCLKSVNUHOFGSXPOJFKIHYSCCTIJDNTDJZUBXYYBFKUNXNFITCRPVMYQIPGBNXKPKDOHYCZJRBXYYNDQVKGNOKYOBNLOHLHHJHWIMWOPGHYXRFFTOMIWQCQMROVRBKRTSOSOVLFFFKYEPXLERGKEFUVXQJQVSBPWLHSFVYEZJGZMDMOVBNYJOBBUCXZLKSBORNDCRHPKBLGUSDXUWEVSDFFKBSVVQFQTCOOHPUYCRJBMNQGRTHMGMOQMKTFOEIMCNRWIZLVDOGCKKBRTYXQLPGJZHRSUCRTQQIPBOZGNLFDRGYNWXIGKZXZWROERFFFXRZQOUDIDMCGEWDLRJQVQGUWROQXJDLFOTBQIIQEMQZCOISLWTFFZEGMCDBQZFFBNGMWSXUWGJVRSDCJFFGQWRUJOHPKMJEUNMKIBMZBLTVBHKGHIHCUTOPQCVSVILWRQESXVVVOQLQCSWQRDJDNMHRWDMRKYFBLYYJVINFVTCPHYLKIQGKTROTSCSROLLQTYPBSMWSZKUINPOIXNGNWJPXOFEWMWHKGMISGHUOXKZXBSZNVIRSWPFUJTYTRBBMHLZYHNSCHDBYIDLJSBIGVZSPMSVYFZRKFNSPJQVVTMPJCBZEYZGHUELNSCRUZYTHYNWFTZJQPRXUXIMNXDGECBEJYKGMDHMVBDJCBNWGGGMFFGQNEKQCZEDOBUDTPTYTJPDYESBEHLTESWWUOTUSDIFXTHTRYEHUBYWOJSTPUMZCTMBXCNLYTPZDUEXPQXFTZSBEJGPIPCLBWJMYJVRZQHDXSCMHOZTTIZICBOWNGHEBNSBLQWLQRSNENBOCUWHRIBRCBRZKNUOBILYHVYRFBZENEOEEQITXNWYNFDVVILZLCYJWCNIWMSMBQYPZKVBDYWNMXRIKHWKHCOOXUFXPNCGDXQOOLZQHGEYQVEPRYPVUIXGLJEXPUCGUPZBUKQRESDLJKPXOECGHGKEGNXXXDWFUHIVQKCRVYXBVTDCBKSYCUIYEPTVLTYFGOYSKSPYRHNYCSESZIFLVZVMXJKLYETUYIOLNOWNZEFGJFFWRWSWXEZLNGDBQMSYJQBKOIOMNTWPFGMNCLE"};
const char msg6[] PROGMEM = {"WYVTZJYQSBJNKBVDEWQKMIFDXUKPJBLYLGNNYSRLWFLESBWRQUBPRYMMXGRJJIKCYKVJMVRKJFVVKUSPCBLPZYWSDQGHQEPFFDCLTEESJCOKISRJZYLFLQQKPSOSISPMMRYSKPXIIKUCQWPOPTQXIXUKQLUDFXMNVGEXQEJNMOGWCZDQXDRWOOLTPQIQMNQLSOMPFWLZSCECUJHQQPBTMBYMNWBYWLCGYWDMCZFOXFGCJSHUKXUQZNMIWBEZGLTTBXKTHHHKPZMZOEYYWEGSLMSFQTWLPTFEENDZEKLEERGHGIUWLXTZKNWVDSYPEKDGNKYCFLEFUVLFHZHPSWTULMCJTXHYFNEREKSJYYQDCNZYEEVUSFFODEEYXQMUWMOFYXPRDLPIKYKTGISBYWMWYHEZQCURBCHHHZHUHGKYIPMDPDKTYMIMHIVVSFGRCKWULYRXNYHYCHYRVMYTWXPTBREBCHYJXXSEQDKQQXRPWVKLQPQRYOOHEEGNUJIKSRTEZHDJHCGMBEYZGHUELNSCRUZYTHYNWFTZJQPRXUXIMNXDGECBEJYKGMDHMVBDJCBNWGGGMFFGQNEKQCZEDOBUDTPTYTJPDYESBEHLTESWWUOTUSDIFXTHTRYEHUBYWOJSTPUMZCTMBXCNLYTPZDUEXPQXFTZSBEJGPIPCLBWJMYJVRZQHDXSCMHOZTTIZICBOWNGHEBNSBLQWLQRSNENBOCUWHRIBRCBRZKNUOBILYHVYRFBZENEOEEQITXNWYNFDVVILZLCYJWCNIWMSMBQYPZKVBDYWNMXRIKHWKHCOOXUFXPNCGDXQOOLZQHGEYQVEPRYPVUIXGLJEXPUCGUPZBUKQRESDLJKPXOECGHGKEGNXXXDWFUHIVQKCRVYXBVTDCBKSYCUIYEPTVLTYFGOYSKSPYRHNYCSESZIFLVZVMXJKLYETUYIOLNOWNZEFGJFFWRWSWXEZLNGDBQMSYJQBKOIOMNTWPFGMNCLEWYVTZJYQSBJNKBVDEWQKMIFDXUKPJBLYLGNNYSRLWFLESBWRQUBPRYMMXGRJJIKCYKVJMVRKJFVVKUSPCBLPZYWSDQGHQEPFFDCLTEESJCOKISRJZYLFLQQKPSOSISPMMRYSKPXIIKUCQWPOPTQXIXUKQLUDFXMNVGEXQEJNMOGWCZDQXDRWOOLTPQIQMNQLSOMPFWLZSCECUJHQQPBTMBYMNWBYWLCGYWDMCZFOXFGCJSHUKXUQZNMIWBEZGLTTBXKTHHHKPZMZOEYYWEGSLMSFQTWLPTFEENDZEKLEERGHGIUWLXTZKNWVDSYPEKDGNKYCFLEFUVLFHZHPSWTULMCJTXHYFNEREKSJYYQDCNZYEEVUSFFODEEYXQMUWMOFYXPRDLPIKYKTGISBYWMWYHEZQCURBCHHHZHUHGKYIPMDPDKTYMIMHIVVSFGRCKWULYRXNYHYCHYRVMYTWXPTBREBCHYJXXSEQDKQQXRPWVKLQPQRYOOHEEGNUJIKSRTEZHDJHCGMBZLIEZMNJUHQMDKQMZUFEBNHYQEGXDDFFYORSJCGHMIOHHMDVVGUKSCPUXNUGLWXVUFMELZGGUKZOBLOYWHNKODHIWVRRQIKYYWWLPQPNHNOXJTKXQZWPWZUWGVXSXFSFKUBJJFZMYXNQPFGPYJEXVYOETVIRHDGHLKKBYFCWDKKSVXXBWKFKBNYYMXNYODHVJUNBPVMEHMCXQRSKKUZVBXKLDDCFNSLVVTDFPTQRUBFOVQSCWKYFCVCLKSVNUHOFGSXPOJFKIHYSCCTIJDNTDJZUBXYYBFKUNXNFITCRPVMYQIPGBNXKPKDOHYCZJRBXYYNDQVKGNOKYOBNLOHLHHJHWIMWOPGHYXRFFTOMIWQCQMROVRBKRTSOSOVLFFFKYEPXLERGKEFUVXQJQVSBPWLHSFVYEZJGZMDMOVBNYJOBBUCXZLKSBORNDCRHPKBLGUSDXUWEVSDFFKBSVVQFQTCOOHPUYCRJBMNQGRTHMGMOQMKTFOEIMCNRWIZLVDOGCKKBRTYXQLPGJZHRSUCRTQQIPBOZGNLFDRGYNWXIGKZXZWROERFFFXRZQOUDIDMCGEWDLRJQVQGUWROQXJDLFOTBQIIQEMQZCOISLWTFFZEGMCDBQZFFBNGMWSXUWGJVRSDCJFFGQWRUJOHPKMJEUNMKIBMZBLTVBHKGHIHCUTOPQCVSVILWRQESXVVVOQLQCSWQRDJDNMHRWDMRKYFBLYYJVINFVTCPHYLKIQGKTROTSCSROLLQTYPBSMWSZKUINPOIXNGNWJPXOFEWMWHKGMISGHUOXKZXBSZNVIRSWPFUJTYTRBBMHLZYHNSCHDBYIDLJSBIGVZSPMSVYFZRKFNSPJQVVTMPJCBZEYZGHUELNSCRUZYTHYNWFTZJQPRXUXIMNXDGECBEJYKGMDHMVBDJCBNWGGGMFFGQNEKQCZEDOBUDTPTYTJPDYESBEHLTESWWUOTUSDIFXTHTRYEHUBYWOJSTPUMZCTMBXCNLYTPZDUEXPQXFTZSBEJGPIPCLBWJMYJVRZQHDXSCMHOZTTIZICBOWNGHEBNSBLQWLQRSNENBOCUWHRIBRCBRZKNUOBILYHVYRFBZENEOEEQITXNWYNFDVVILZLCYJWCNIWMSMBQYPZKVBDYWNMXRIKHWKHCOOXUFXPNCGDXQOOLZQHGEYQVEPRYPVUIXGLJEXPUCGUPZBUKQRESDLJKPXOECGHGKEGNXXXDWFUHIVQKCRVYXBVTDCBKSYCUIYEPTVLTYFGOYSKSPYRHNYCSESZIFLVZVMXJKLYETUYIOLNOWNZEFGJFFWRWSWXEZLNGDBQMSYJQBKOIOMNTWPFGMNCLEWYVTZJYQSBJNKBVDEWQKMIFDXUKPJBLYLGNNYSRLWFLESBWRQUBPRYMMXGRJJIKCYKVJMVRKJFVVKUSPCBLPZYWSDQGHQEPFFDCLTEESJCOKISRJZYLFLQQKPSOSISPMMRYSKPXIIKUCQWPOPTQXIXUKQLUDFXMNVGEXQEJNMOGWCZDQXDRWOOLTPQIQMNQLSOMPFWLZSCECUJHQQPBTMBYMNWBYWLCGYWDMCZFOXFGCJSHUKXUQZNMIWBEZGLTTBXKTHHHKPZMZOEYYWEGSLMSFQTWLPTFEENDZEKLEERGHGIUWLXTZKNWVDSYPEKDGNKYCFLEFUVLFHZHPSWTULMCJTXHYFNEREKSJYYQDCNZYEEVUSFFODEEYXQMUWMOFYXPRDLPIKYKTGISBYWMWYHEZQCURBCHHHZHUHGKYIPMDPDKTYMIMHIVVSFGRCKWULYRXNYHYCHYRVMYTWXPTBREBCHYJXXSEQDKQQXRPWVKLQPQRYOOHEEGNUJIKSRTEZHDJHCGMBZLIEZMNJUHQMDKQMZUFEBNHYQEGXDDFFYORSJCGHMIOHHMDVVGUKSCPUXNUGLWXVUFMELZGGUKZOBLOYWHNKODHIWVRRQIKYYWWLPQPNHNOXJTKXQZWPWZUWGVXSXFSFKUBJJFZMYXNQPFGPYJEXVYOETVIRHDGHLKKBYFCWDKKSVXXBWKFKBNYYMXNYODHVJUNBPVMEHMCXQRSKKUZVBXKLDDCFNSLVVTDFPTQRUBFOVQSCWKYFCVCLKSVNUHOFGSXPOJFKIHYSCCTIJDNTDJZUBXYYBFKUNXNFITCRPVMYQIPGBNXKPKDOHYCZJRBXYYNDQVKGNOKYOBNLOHLHHJHWIMWOPGHYXRFFTOMIWQCQMROVRBKRTSOSOVLFFFKYEPXLERGKEFUVXQJQVSBPWLHSFVYEZJGZMDMOVBNYJOBBUCXZLKSBORNDCRHPKBLGUSDXUWEVSDFFKBSVVQFQTCOOHPUYCRJBMNQGRTHMGMOQMKTFOEIMCNRWIZLVDOGCKKBRTYXQLPGJZHRSUCRTQQIPBOZGNLFDRGYNWXIGKZXZWROERFFFXRZQOUDIDMCGEWDLRJQVQGUWROQXJDLFOTBQIIQEMQZCOISLWTFFZEGMCDBQZFFBNGMWSXUWGJVRSDCJFFGQWRUJOHPKMJEUNMKIBMZBLTVBHKGHIHCUTOPQCVSVILWRQESXVVVOQLQCSWQRDJDNMHRWDMRKYFBLYYJVINFVTCPHYLKIQGKTROTSCSROLLQTYPBSMWSZKUINPOIXNGNWJPXOFEWMWHKGMISGHUOXKZXBSZNVIRSWPFUJTYTRBBMHLZYHNSCHDBYIDLJSBIGVZSPMSVYFZRKFNSPJQVVTMPJCBZEYZGHUELNSCRUZYTHYNWFTZJQPRXUXIMNXDGECBEJYKGMDHMVBDJCBNWGGGMFFGQNEKQCZEDOBUDTPTYTJPDYESBEHLTESWWUOTUSDIFXTHTRYEHUBYWOJSTPUMZCTMBXCNLYTPZDUEXPQXFTZSBEJGPIPCLBWJMYJVRZQHDXSCMHOZTTIZICBOWNGHEBNSBLQWLQRSNENBOCUWHRIBRCBRZKNUOBILYHVYRFBZENEOEEQITXNWYNFDVVILZLCYJWCNIWMSMBQYPZKVBDYWNMXRIKHWKHCOOXUFXPNCGDXQOOLZQHGEYQVEPRYPVUIXGLJEXPUCGUPZBUKQRESDLJKPXOECGHGKEGNXXXDWFUHIVQKCRVYXBVTDCBKSYCUIYEPTVLTYFGOYSKSPYRHNYCSESZIFLVZVMXJKLYETUYIOLNOWNZEFGJFFWRWSWXEZLNGDBQMSYJQBKOIOMNTWPFGMNCLEWYVTZJYQSBJNKBVDEWQKMIFDXUKPJBLYLGNNYSRLWFLESBWRQUBPRYMMXGRJJIKCYKVJMVRKJFVVKUSPCBLPZYWSDQGHQEPFFDCLTEESJCOKISRJZYLFLQQKPSOSISPMMRYSKPXIIKUCQWPOPTQXIXUKQLUDFXMNVGEXQEJNMOGWCZDQXDRWOOLTPQIQMNQLSOMPFWLZSCECUJHQQPBTMBYMNWBYWLCGYWDMCZFOXFGCJSHUKXUQZNMIWBEZGLTTBXKTHHHKPZMZOEYYWEGSLMSFQTWLPTFEENDZEKLEERGHGIUWLXTZKNWVDSYPEKDGNKYCFLEFUVLFHZHPSWTULMCJTXHYFNEREKSJYYQDCNZYEEVUSFFODEEYXQMUWMOFYXPRDLPIKYKTGISBYWMWYHEZQCURBCHHHZHUHGKYIPMDPDKTYMIMHIVVSFGRCKWULYRXNYHYCHYRVMYTWXPTBREBCHYJXXSEQDKQQXRPWVKLQPQRYOOHEEGNUJIKSRTEZHDJHCGMBZLIEZMNJUHQMDKQMZUFEBNHYQEGXDDFFYORSJCGHMIOHHMDVVGUKSCPUXNUGLWXVUFMELZGGUKZOBLOYWHNKODHIWVRRQIKYYWWLPQPNHNOXJTKXQZWPWZUWGVXSXFSFKUBJJFZMYXNQPFGPYJEXVYOETVIRHDGHLKKBYFCWDKKSVXXBWKFKBNYYMXNYODHVJUNBPVMEHMCXQRSKKUZVBXKLDDCFNSLVVTDFPTQRUBFOVQSCWKYFCVCLKSVNUHOFGSXPOJFKIHYSCCTIJDNTDJZUBXYYBFKUNXNFITCRPVMYQIPGBNXKPKDOHYCZJRBXYYNDQVKGNOKYOBNLOHLHHJHWIMWOPGHYXRFFTOMIWQCQMROVRBKRTSOSOVLFFFKYEPXLERGKEFUVXQJQVSBPWLHSFVYEZJGZMDMOVBNYJOBBUCXZLKSBORNDCRHPKBLGUSDXUWEVSDFFKBSVVQFQTCOOHPUYCRJBMNQGRTHMGMOQMKTFOEIMCNRWIZLVDOGCKKBRTYXQLPGJZHRSUCRTQQIPBOZGNLFDRGYNWXIGKZXZWROERFFFXRZQOUDIDMCGEWDLRJQVQGUWROQXJDLFOTBQIIQEMQZCOISLWTFFZEGMCDBQZFFBNGMWSXUWGJVRSDCJFFGQWRUJOHPKMJEUNMKIBMZBLTVBHKGHIHCUTOPQCVSVILWRQESXVVVOQLQCSWQRDJDNMHRWDMRKYFBLYYJVINFVTCPHYLKIQGKTROTSCSROLLQTYPBSMWSZKUINPOIXNGNWJPXOFEWMWHKGMISGHUOXKZXBSZNVIRSWPFUJTYTRBBMHLZYHNSCHDBYIDLJSBIGVZSPMSVYFZRKFNSPJQVVTMPJCBZEYZGHUELNSCRUZYTHYNWFTZJQPRXUXIMNXDGECBEJYKGMDHMVBDJCBNWGGGMFFGQNEKQCZEDOBUDTPTYTJPDYESBEHLTESWWUOTUSDIFXTHTRYEHUBYWOJSTPUMZCTMBXCNLYTPZDUEXPQXFTZSBEJGPIPCLBWJMYJVRZQHDXSCMHOZTTIZICBOWNGHEBNSBLQWLQRSNENBOCUWHRIBRCBRZKNUOBILYHVYRFBZENEOEEQITXNWYNFDVVILZLCYJWCNIWMSMBQYPZKVBDYWNMXRIKHWKHCOOXUFXPNCGDXQOOLZQHGEYQVEPRYPVUIXGLJEXPUCGUPZBUKQRESDLJKPXOECGHGKEGNXXXDWFUHIVQKCRVYXBVTDCBKSYCUIYEPTVLTYFGOYSKSPYRHNYCSESZIFLVZVMXJKLYETUYIOLNOWNZEFGJFFWRWSWXEZLNGDBQMSYJQBKOIOMNTWPFGMNCLEWYVTZJYQSBJNKBVDEWQKMIFDXUKPJBLYLGNNYSRLWFLESBWRQUBPRYMMXGRJJIKCYKVJMVRKJFVVKUSPCBLPZYWSDQGHQEPFFDCLTEESJCOKISRJZYLFLQQKPSOSISPMMRYSKPXIIKUCQWPOPTQXIXUKQLUDFXMNVGEXQEJNMOGWCZDQXDRWOOLTPQIQMNQLSOMPFWLZSCECUJHQQPBTMBYMNWBYWLCGYWDMCZFOXFGCJSHUKXUQZNMIWBEZGLTTBXKTHHHKPZMZOEYYWEGSLMSFQTWLPTFEENDZEKLEERGHGIUWLXTZKNWVDSYPEKDGNKYCFLEFUVLFHZHPSWTULMCJTXHYFNEREKSJYYQDCNZYEEVUSFFODEEYXQMUWMOFYXPRDLPIKYKTGISBYWMWYHEZQCURBCHHHZHUHGKYIPMDPDKTYMIMHIVVSFGRCKWULYRXNYHYCHYRVMYTWXPTBREBCHYJXXSEQDKQQXRPWVKLQPQRYOOHEEGNUJIKSRTEZHDJHCGMBZLIEZMNJUHQMDKQMZUFEBNHYQEGXDDFFYORSJCGHMIOHHMDVVGUKSCPUXNUGLWXVUFMELZGGUKZOBLOYWHNKODHIWVRRQIKYYWWLPQPNHNOXJTKXQZWPWZUWGVXSXFSFKUBJJFZMYXNQPFGPYJEXVYOETVIRHDGHLKKBYFCWDKKSVXXBWKFKBNYYMXNYODHVJUNBPVMEHMCXQRSKKUZVBXKLDDCFNSLVVTDFPTQRUBFOVQSCWKYFCVCLKSVNUHOFGSXPOJFKIHYSCCTIJDNTDJZUBXYYBFKUNXNFITCRPVMYQIPGBNXKPKDOHYCZJRBXYYNDQVKGNOKYOBNLOHLHHJHWIMWOPGHYXRFFTOMIWQCQMROVRBKRTSOSOVLFFFKYEPXLERGKEFUVXQJQVSBPWLHSFVYEZJGZMDMOVBNYJOBBUCXZLKSBORNDCRHPKBLGUSDXUWEVSDFFKBSVVQFQTCOOHPUYCRJBMNQGRTHMGMOQMKTFOEIMCNRWIZLVDOGCKKBRTYXQLPGJZHRSUCRTQQIPBOZGNLFDRGYNWXIGKZXZWROERFFFXRZQOUDIDMCGEWDLRJQVQGUWROQXJDLFOTBQIIQEMQZCOISLWTFFZEGMCDBQZFFBNGMWSXUWGJVRSDCJFFGQWRUJOHPKMJEUNMKIBMZBLTVBHKGHIHCUTOPQCVSVILWRQESXVVVOQLQCSWQRDJDNMHRWDMRKYFBLYYJVINFVTCPHYLKIQGKTROTSCSROLLQTYPBSMWSZKUINPOIXNGNWJPXOFEWMWHKGMISGHUOXKZXBSZNVIRSWPFUJTYTRBBMHLZYHNSCHDBYIDLJSBIGVZSPMSVYFZRKFNSPJQVVTMPJCBZEYZGHUELNSCRUZYTHYNWFTZJQPRXUXIMNXDGECBEJYKGMDHMVBDJCBNWGGGMFFGQNEKQCZEDOBUDTPTYTJPDYESBEHLTESWWUOTUSDIFXTHTRYEHUBYWOJSTPUMZCTMBXCNLYTPZDUEXPQXFTZSBEJGPIPCLBWJMYJVRZQHDXSCMHOZTTIZICBOWNGHEBNSBLQWLQRSNENBOCUWHRIBRCBRZKNUOBILYHVYRFBZENEOEEQITXNWYNFDVVILZLCYJWCNIWMSMBQYPZKVBDYWNMXRIKHWKHCOOXUFXPNCGDXQOOLZQHGEYQVEPRYPVUIXGLJEXPUCGUPZBUKQRESDLJKPXOECGHGKEGNXXXDWFUHIVQKCRVYXBVTDCBKSYCUIYEPTVLTYFGOYSKSPYRHNYCSESZIFLVZVMXJKLYETUYIOLNOWNZEFGJFFWRWSWXEZLNGDBQMSYJQBKOIOMNTWPFGMNCLE"};

/****************************************************************/
/****************************************************************/
// Test version 
// to test enigma encryption
//
void loop() {

  uint16_t i,j;
  char ch;
  unsigned long msStart,msEnd;
  
  /*
  // Test the screen
  Serial.println(F("All LEDs on"));
  for (i = 0; i < 128; i++) {
    HT.setLed(i);
  }
  HT.sendLed();
  while (Serial.available() == 0){}
  */

  //  char *msga= "JBBNXQPIEMCEJVFBRRTCEWDDPZBPJDWPKLECIDEZWZK";
  //  char *msga= "AAAAAAAAAAAAAAAA";
  
  Serial.println();
  operationMode=run;
  
  //#define TEST 1 // basic M4 AAAA test
  //#define TEST 2 // M4 plugboard
  //#define TEST 3 // M4, ring, doublestep
  //#define TEST 4 // M3, ringstellung

  //javascript sim: https://people.physik.hu-berlin.de/~palloks/js/enigma/enigma-u_v20_en.html

#if TESTCRYPTO==1
  // http://enigma.louisedade.co.uk/enigma.html?m4;b_thin;b321;AAAA;AAAA
  setConfig(M4,UKWBT,WALZE_Beta,WALZE_III,WALZE_II,WALZE_I,ETW0,"AAAA","","AAAA"); // etw,ring,plug,start
  printSettings();

  logLevel=0;
  
  Serial.print(F("MSG: "));
  for (i=0;i<strlen_P(msg1);i++){
    Serial.print((char)pgm_read_byte_near(msg1+i));
  }
  Serial.println();
  Serial.println();

  for (i=0;i<strlen_P(msg1);i++){
    rotateWheel();
    Serial.print(encrypt((char)pgm_read_byte_near(msg1+i)));
    if (logLevel>0){
      Serial.println();
    } else {
      if ((i%78)==77){Serial.println();}
    }

  }
  Serial.println();
#elif TESTCRYPTO==2
  // http://enigma.louisedade.co.uk/enigma.html?m4;b_thin;b321;BCDE;AUDS
  // http://enigma.louisedade.co.uk/enigma.html?m4;b_thin;b321;BCDE;AUDS
  setConfig(M4,UKWBT,WALZE_Beta,WALZE_III,WALZE_II,WALZE_I,ETW0,"BCDE","","AUDS");// etw,ring,plug,start
  printSettings();

  logLevel=0;
  Serial.print(F("MSG: "));
  for (i=0;i<strlen_P(msg2);i++){
    Serial.print((char)pgm_read_byte_near(msg2+i));
  }
  Serial.println();
  Serial.println();

#ifdef undef
  logLevel=2;
  for (i=0;i<1;i++){
#else
  logLevel=0;
  for (i=0;i<strlen_P(msg2);i++){
#endif
    rotateWheel();
    Serial.print(encrypt((char)pgm_read_byte_near(msg2+i)));
    if (logLevel>0){
      Serial.println();
    } else {
      if ((i%78)==77){Serial.println();}
    }    
  }
  Serial.println();
#elif TESTCRYPTO==3
  // http://enigma.louisedade.co.uk/enigma.html?m3;b;b321;ABCD;AAAA
  setConfig(M3,UKWB,0,WALZE_III,WALZE_II,WALZE_I,ETW0," BCD",""," AAA"); // etw,ring,plug,start
  printSettings();
 
  Serial.println();
  Serial.print(F("MSG ("));
  Serial.print(strlen_P(msg5),DEC);
  Serial.print(F("): "));
  for (i=0;i<strlen_P(msg3);i++){
    Serial.print((char)pgm_read_byte_near(msg3+i));
  }
  Serial.println();
  Serial.println();

#ifdef undef
  logLevel=2;
  for (i=0;i<1;i++){
#else
  logLevel=0;
  for (i=0;i<strlen_P(msg3);i++){
#endif
    rotateWheel();
    Serial.print(encrypt((char)pgm_read_byte_near(msg3+i)));
    if ((i%78)==77){Serial.println();}
  }

  Serial.println();
#elif TESTCRYPTO==4
  setConfig(M3,UKWB,0,WALZE_I,WALZE_II,WALZE_III,ETW0," BCD",""," UDS");
  printSettings();
  Serial.println();
  Serial.print(F("strlen_P="));
  Serial.println(strlen_P(msg4),DEC);
  Serial.println();
  delay(1000);

  
  logLevel=0;
    for (i=0;i<strlen_P(msg4);i++){
  //  for (i=0;i<7;i++){
    rotateWheel();
    Serial.print(encrypt((char)pgm_read_byte_near(msg4+i)));
    //Serial.print(pgm_read_byte_near(msg4+i),DEC);
    //Serial.print(F(" "));
    if ((i%78)==77){Serial.println();}
  }

  Serial.println();
#elif TESTCRYPTO==5 // Grand Finale
// http://enigma.louisedade.co.uk/enigma.html?m4;b_thin;b876;QWER;ASDF
  setConfig(M4,UKWBT,WALZE_Beta,WALZE_VIII,WALZE_VII,WALZE_VI,ETW0,"QWER","","ASDF");
  printSettings();
  Serial.println();
  Serial.print(F("strlen_P="));
  Serial.println(strlen_P(msg5),DEC);
  Serial.println();
  delay(1000);

  
  logLevel=0;
  msStart=millis();
  for (i=0;i<strlen_P(msg5);i++){
    rotateWheel();
    Serial.print(encrypt((char)pgm_read_byte_near(msg5+i)));
    if ((i%78)==77){Serial.println();}
  }
  Serial.println();
#elif TESTCRYPTO==6 // speed test
// http://enigma.louisedade.co.uk/enigma.html?m4;b_thin;b876;QWER;ASDF
//  setConfig(M4,UKWBT,WALZE_Beta,WALZE_VIII,WALZE_VII,WALZE_VI,ETW0,"QWER","","ASDF");
  setConfig(M3,UKWB,0,WALZE_III,WALZE_II,WALZE_I,ETW0," BCD",""," AAA"); // etw,ring,plug,start
  printSettings();
  Serial.println();
  Serial.print(F("strlen_P="));
  Serial.println(strlen_P(msg6),DEC);
  Serial.println();
  delay(1000);

  logLevel=0;
  msStart=millis();
  for (i=0;i<strlen_P(msg6);i++){
    rotateWheel();
#if 1
    encrypt((char)pgm_read_byte_near(msg6+i));
#else
    Serial.print(encrypt((char)pgm_read_byte_near(msg6+i)));
    if ((i%78)==77){Serial.println();}
#endif
  }
  msEnd=millis();
  Serial.println();
  Serial.print(F("Time to encipher "));
  Serial.print(strlen_P(msg6));
  Serial.print(F(" characters: "));
  Serial.print(msEnd-msStart,DEC);
  Serial.print(F(" ms or "));
  Serial.print((float)(msEnd-msStart)/strlen_P(msg6));
  Serial.print(F(" ms per character, that comes to "));
  Serial.print((float)60000/((float)(msEnd-msStart)/strlen_P(msg6)));
  Serial.println(F(" character in 60 seconds "));
#else
  ERROR - unknown TESTCRYPTO Level
#endif

  Serial.println();



  Serial.println(F("Done"));

  while (true){
    checkSwitchPos();
    serialEvent();
    if (stringComplete) {
      Serial.print(F("Received something: "));
      Serial.println(serialInputBuffer);
      serialInputBuffer = "";
      stringComplete = false;
    }
  /*
    if (Serial.available() > 0) {
      // read the incoming byte:
      int incomingByte = Serial.read();

      // say what you got:
      Serial.print(F("I received: "));
      Serial.print(incomingByte, DEC);
      if (incomingByte > ' ' && incomingByte < 127){
	Serial.print(F("("));
	Serial.print((char)incomingByte);
	Serial.print(F(")"));
      }
      Serial.println();
    }
  */

  }

} // loop test
#endif

/****************************************************************/
// from https://www.arduino.cc/en/Tutorial/SerialEvent
/*
  SerialEvent occurs whenever a new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 Using double buffer because when a long 64+ string is sent it takes a while to process and
 the arduino buffer may be lost.
*/

void serialEvent() {
  char inChar;

  if (!stringComplete){ // make sure we don't keep adding before the current buff is processed
    while (Serial.available()) {
      // get the new byte:
      inChar = (char)Serial.read();
      if (inChar == '\r' ) { // treat carriage return as line feed
	inChar='\n';
      }
      // add it to the serialInputBuffer:
      serialInputBuffer += inChar;
      //      Serial.print((char)tolower(inChar)); // PSDEBUG
      // if the incoming character is a newline, set a flag
      // so the main loop can do something about it:
      //make sure we don't go to far, just force in a new line after MAXSERIALBUFF characters
      if (serialInputBuffer.length()==MAXSERIALBUFF-2){
	serialInputBuffer += '\n';
	inChar = '\n';
      }
      if (inChar == '\n' ) {
	stringComplete = true;
	break; // break out so we can start processing the buffer
      } // if \n
    } // while Serial.available
  } // if ! stringComplete
} // serialEvent

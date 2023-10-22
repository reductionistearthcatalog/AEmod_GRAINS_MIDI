/*
  "West Coast Osc" / "Scheveningen"; an oscillator for the Ginko Synthese "Grains" module,
  by Kassen Oud. This is based on the "West Coast" style of synthesis, where
  we build up sounds starting with a simple waveform and adding harmonics, as
  opposed to starting with a harmonically rich sound and filtering that down.
  All synthesis bits written by Kassen Oud, using infrastructure borrowed from
  Peter Knight's work, which was adapted by Jan Willem before I got my hands on
  it.
LICENSE:
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
DESCRIPTION:
  We use two sine waves, the first we'll call the "leader", the second the
  "follower". The leader just tracks the pitch CV. The follower is some set
  interval above the leader. In addition the follower is hard-synced to the
  leader. These two are ring-modulating each other. This is followed by a
  wave-folder; a classical "West Coast" style effect which is a bit like a
  clip, except once the wave hits the maximum excursion it "folds" back
  towards the centre. Of these two effects, the ring-modulator and the
  wave-wrapper, the first is more mellow and smooth, while the second tends
  to be more up-front. By combining those, and modulating them, we can get a
  rich pallet of timbres from a relatively simple module like the Grains.
  I tried to comment all of this so people who'd like to can learn from it or
  borrow ideas. However, because we're also trying to do fairly advanced 
  stuff on a modest CPU, there is some trickery going on that may be hard to
  understand for people who aren't yet familiar with the intricacies of 
  digital integer math and bitwise operations. If that's you then perhaps the
  "PWM-SAW" code would be a better place to start your journey.
MANUAL:
  A2 (CV in 1/knob 1): follower osc pitch knob (keep switch in manual position, this acts as an offset for the follower parameter)
  A1: (CV in 2/knob 2): HS CV (keep switch in "in 2" position, knob acts as an attenuator for incoming follower CV)
    Offset of the follower oscillator's pitch, relative to the leader's.
  A0: (knob 3): wrap knob (knob acts as an offset for incoming wrap CV)
  A3: (CV in 3): wrap CV
    Amount of wave-folding.
CHANGELOG:
  Oct 2 2020; initial release
  Oct 22 2023: MIDI implementation added by the reductionist earth catalog
*/

//libraries used
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
// added MIDI library
#include <MIDI.h>
// added ramp library for portamento
#include <Ramp.h>

//variables
bool leader_polarity = 0;
bool follower_polarity = 0;
uint8_t output = 0;
uint16_t leader_phase = 0;
uint16_t phase_inc = 1;
uint16_t follower_offset_current = 1;
uint16_t follower_offset_measured = 1;
uint16_t wrap_factor = 64;
uint16_t neg_wrap_factor = 64;
uint16_t follower_phase = 0;
uint16_t leader_value = 0;
uint16_t follower_value = 0;
uint32_t accumulator = 0; //I want a hardware accumulator :-(
// try setting up a counter for the loop function
//byte count = 0;
// for portamento
bool port = false;
uint16_t portTime = 0;
rampInt portRamp;

// set up a global variable for midi note information. set C1 as the minimum midi note, 84 is max to retain roughly the original pitch range
uint16_t midinote = 0;

// Map inputs
#define FOLLOWER_KNOB      (A2)
#define FOLLOWER_CV     (A1)
#define WRAP_KNOB     (A0)
#define WRAP_CV     (A3)


// Changing these will also requires rewriting audioOn()
//    Output is on pin 11
#define PWM_PIN       11
#define PWM_VALUE     OCR2A
// this line added for digital output
#define DIGITAL_OUT_PIN 8
#define PWM_INTERRUPT TIMER2_OVF_vect

// added this line to create MIDI default instance
MIDI_CREATE_DEFAULT_INSTANCE();

//table mapping CV values measured to phase increase per sample
const uint16_t freqTable[] PROGMEM = {
69,   69,   69,   69,   70,   70,   70,   70,   70,   71,   71,   71,   71,   72,   72,
72,   72,   73,   73,   73,   73,   74,   74,   74,   74,   75,   75,   75,   75,   76,
76,   76,   76,   77,   77,   77,   77,   78,   78,   78,   79,   79,   79,   79,   80,
80,   80,   80,   81,   81,   81,   82,   82,   82,   82,   83,   83,   83,   83,   84,
84,   84,   85,   85,   85,   85,   86,   86,   86,   87,   87,   87,   88,   88,   88,
88,   89,   89,   89,   90,   90,   90,   91,   91,   91,   91,   92,   92,   92,   93,
93,   93,   94,   94,   94,   95,   95,   95,   96,   96,   96,   97,   97,   97,   98,
98,   98,   99,   99,   99,   100,  100,  100,  101,  101,  101,  102,  102,  102,  103,
103,  103,  104,  104,  104,  105,  105,  105,  106,  106,  107,  107,  107,  108,  108,
108,  109,  109,  109,  110,  110,  111,  111,  111,  112,  112,  112,  113,  113,  114,
114,  114,  115,  115,  116,  116,  116,  117,  117,  117,  118,  118,  119,  119,  119,
120,  120,  121,  121,  122,  122,  122,  123,  123,  124,  124,  124,  125,  125,  126,
126,  127,  127,  127,  128,  128,  129,  129,  130,  130,  130,  131,  131,  132,  132,
133,  133,  134,  134,  135,  135,  135,  136,  136,  137,  137,  138,  138,  139,  139,
140,  140,  141,  141,  142,  142,  142,  143,  143,  144,  144,  145,  145,  146,  146,
147,  147,  148,  148,  149,  149,  150,  150,  151,  151,  152,  152,  153,  154,  154,
155,  155,  156,  156,  157,  157,  158,  158,  159,  159,  160,  160,  161,  161,  162,
163,  163,  164,  164,  165,  165,  166,  166,  167,  168,  168,  169,  169,  170,  170,
171,  172,  172,  173,  173,  174,  175,  175,  176,  176,  177,  178,  178,  179,  179,
180,  181,  181,  182,  182,  183,  184,  184,  185,  186,  186,  187,  187,  188,  189,
189,  190,  191,  191,  192,  193,  193,  194,  195,  195,  196,  197,  197,  198,  199,
199,  200,  201,  201,  202,  203,  203,  204,  205,  205,  206,  207,  207,  208,  209,
210,  210,  211,  212,  212,  213,  214,  215,  215,  216,  217,  218,  218,  219,  220,
220,  221,  222,  223,  223,  224,  225,  226,  227,  227,  228,  229,  230,  230,  231,
232,  233,  234,  234,  235,  236,  237,  238,  238,  239,  240,  241,  242,  242,  243,
244,  245,  246,  247,  247,  248,  249,  250,  251,  252,  252,  253,  254,  255,  256,
257,  258,  259,  259,  260,  261,  262,  263,  264,  265,  266,  267,  267,  268,  269,
270,  271,  272,  273,  274,  275,  276,  277,  278,  278,  279,  280,  281,  282,  283,
284,  285,  286,  287,  288,  289,  290,  291,  292,  293,  294,  295,  296,  297,  298,
299,  300,  301,  302,  303,  304,  305,  306,  307,  308,  309,  310,  311,  312,  314,
315,  316,  317,  318,  319,  320,  321,  322,  323,  324,  325,  327,  328,  329,  330,
331,  332,  333,  334,  335,  337,  338,  339,  340,  341,  342,  344,  345,  346,  347,
348,  349,  351,  352,  353,  354,  355,  357,  358,  359,  360,  361,  363,  364,  365,
366,  368,  369,  370,  371,  373,  374,  375,  376,  378,  379,  380,  382,  383,  384,
385,  387,  388,  389,  391,  392,  393,  395,  396,  397,  399,  400,  401,  403,  404,
405,  407,  408,  410,  411,  412,  414,  415,  417,  418,  419,  421,  422,  424,  425,
427,  428,  429,  431,  432,  434,  435,  437,  438,  440,  441,  443,  444,  446,  447,
449,  450,  452,  453,  455,  456,  458,  460,  461,  463,  464,  466,  467,  469,  471,
472,  474,  475,  477,  479,  480,  482,  484,  485,  487,  488,  490,  492,  493,  495,
497,  498,  500,  502,  504,  505,  507,  509,  510,  512,  514,  516,  517,  519,  521,
523,  524,  526,  528,  530,  532,  533,  535,  537,  539,  541,  542,  544,  546,  548,
550,  552,  554,  555,  557,  559,  561,  563,  565,  567,  569,  571,  573,  575,  577,
579,  580,  582,  584,  586,  588,  590,  592,  594,  596,  598,  600,  602,  605,  607,
609,  611,  613,  615,  617,  619,  621,  623,  625,  627,  630,  632,  634,  636,  638,
640,  642,  645,  647,  649,  651,  653,  656,  658,  660,  662,  665,  667,  669,  671,
674,  676,  678,  681,  683,  685,  687,  690,  692,  695,  697,  699,  702,  704,  706,
709,  711,  714,  716,  718,  721,  723,  726,  728,  731,  733,  736,  738,  741,  743,
746,  748,  751,  753,  756,  758,  761,  764,  766,  769,  771,  774,  777,  779,  782,
784,  787,  790,  793,  795,  798,  801,  803,  806,  809,  812,  814,  817,  820,  823,
825,  828,  831,  834,  837,  839,  842,  845,  848,  851,  854,  857,  860,  862,  865,
868,  871,  874,  877,  880,  883,  886,  889,  892,  895,  898,  901,  904,  907,  910,
914,  917,  920,  923,  926,  929,  932,  935,  939,  942,  945,  948,  951,  955,  958,
961,  964,  968,  971,  974,  978,  981,  984,  988,  991,  994,  998,  1001, 1004, 1008,
1011, 1015, 1018, 1022, 1025, 1028, 1032, 1035, 1039, 1042, 1046, 1050, 1053, 1057, 1060,
1064, 1067, 1071, 1075, 1078, 1082, 1086, 1089, 1093, 1097, 1100, 1104, 1108, 1112, 1115,
1119, 1123, 1127, 1131, 1135, 1138, 1142, 1146, 1150, 1154, 1158, 1162, 1166, 1170, 1174,
1178, 1182, 1186, 1190, 1194, 1198, 1202, 1206, 1210, 1214, 1218, 1222, 1226, 1231, 1235,
1239, 1243, 1247, 1252, 1256, 1260, 1264, 1269, 1273, 1277, 1282, 1286, 1290, 1295, 1299,
1303, 1308, 1312, 1317, 1321, 1326, 1330, 1335, 1339, 1344, 1348, 1353, 1357, 1362, 1367,
1371, 1376, 1381, 1385, 1390, 1395, 1399, 1404, 1409, 1414, 1418, 1423, 1428, 1433, 1438,
1443, 1448, 1452, 1457, 1462, 1467, 1472, 1477, 1482, 1487, 1492, 1497, 1502, 1508, 1513,
1518, 1523, 1528, 1533, 1538, 1544, 1549, 1554, 1559, 1565, 1570, 1575, 1581, 1586, 1591,
1597, 1602, 1608, 1613, 1619, 1624, 1630, 1635, 1641, 1646, 1652, 1657, 1663, 1669, 1674,
1680, 1686, 1691, 1697, 1703, 1709, 1714, 1720, 1726, 1732, 1738, 1744, 1750, 1756, 1762,
1768, 1774, 1780, 1786, 1792, 1798, 1804, 1810, 1816, 1822, 1828, 1835, 1841, 1847, 1853,
1860, 1866, 1872, 1879, 1885, 1891, 1898, 1904, 1911, 1917, 1924, 1930, 1937, 1943, 1950,
1956, 1963, 1970, 1976, 1983, 1990, 1997, 2003, 2010, 2017, 2024, 2031, 2037, 2044, 2051,
2058, 2065, 2072, 2079, 2086, 2093, 2101, 2108, 2115, 2122, 2129, 2136, 2144, 2151, 2158,
2165, 2173, 2180, 2188
};

//maps midi note to indexes in freqTable
//this might need tuning in the future
//table mapping midi values measured to phase increase per sample
const uint16_t midiTable[] PROGMEM = {
0,    17,   33,   51,   66,   86,   101,  119,  136,  153,  169,  187,  205,  221,  238, 
255,  273,  290,  307,  325,  341,  359,  375,  393,  410,  427,  444,  461,  478,  495, 
512,  529,  546,  563,  581,  597,  615,  631,  649,  666,  683,  699,  717,  734,  751, 
768,  785,  802,  819,  836,  853,  870,  888,  904,  922,  939,  956,  973,  990, 1007, 
1023
};

uint16_t mapMIDI(uint16_t input)
{
  //return pgm_read_word_near(midiTable + input);
  return pgm_read_word_near(freqTable + pgm_read_word_near(midiTable + input));
}

uint16_t mapFreq(uint16_t input) {
  return pgm_read_word_near(freqTable + input);
}

// added this function to handle MIDI noteOn information
void handleNoteOn(byte channel, byte pitch, byte velocity)
{
    digitalWrite(DIGITAL_OUT_PIN, HIGH);

    // limit ourselves to the original pitch range, subtract out 24 (the low end of the range) so we get an index into phase increase table
    midinote = max( 24, min( 84, pitch ) ) - 24;
    // to make sure we won't overflow: minimum value of the above is 0 (maximum of either 24 or x is at least 24, 24-24 = 0)
    //                                maximum value of the above is 60 (minimum of either 84 or pitch is at most 84, 84-24 = 60, so the 61st entry in pitch table)
    if (port) {
      portRamp.go(mapMIDI(midinote), portTime, LINEAR, ONCEFORWARD);
      }
    else {
      portRamp.go(mapMIDI(midinote));
    }
}

// added this function to handle MIDI noteOff information
void handleNoteOff(byte channel, byte pitch, byte velocity)
{
    digitalWrite(DIGITAL_OUT_PIN, LOW);
    // Note that NoteOn messages with 0 velocity are interpreted as NoteOffs.
}

// control change
void handleControlChange(byte channel, byte num, byte val)
{
  if (num == 5) {
      if (val == 0) {
        port = false;
      }
      else {
        port = true;
        portTime = 20 * val;
      }
    }
}

void audioOn() {
  // Set up PWM to 31.25kHz, phase accurate
  TCCR2A = _BV(COM2A1) | _BV(WGM20);
  TCCR2B = _BV(CS20);
  TIMSK2 = _BV(TOIE2);
}


void setup() {
   //set up digital output pin
  pinMode(DIGITAL_OUT_PIN, OUTPUT);

  pinMode(PWM_PIN,OUTPUT);
  audioOn();

  //Added for MIDI implementation:
  // Initiate MIDI communications, listen to omni
  MIDI.begin(MIDI_CHANNEL_OMNI);
  // begin baud rate at 31250
  Serial.begin(31250);

  // Connect the handleNoteOn function to the library,
  // so it is called upon reception of a NoteOn.
  MIDI.setHandleNoteOn(handleNoteOn);  // Put only the name of the function

  // Do the same for NoteOffs
  MIDI.setHandleNoteOff(handleNoteOff);

  // for portamento
  // set up ramp grain to 1 ms
  MIDI.setHandleControlChange(handleControlChange);
  portRamp.setGrain(1);
  portRamp.go(mapMIDI(midinote));
  
  // set up phase_inc
  phase_inc = portRamp.update();

}


void loop() {
  //added to read MIDI information
  MIDI.read();
  
  // the following is commented out because we won't be using the original pitch determination method
  //calculate the pitch
  //int pwmv = min( 1023,  analogRead(PITCH_CV) + analogRead(PITCH_KNOB));
  //look up the phase increase per sample
  //phase_inc = mapFreq(pwmv);
  phase_inc = portRamp.update();
  //Measure the follower osc's frequency offset
  //we later smooth this to avoid unintended clicks in the sound
  follower_offset_measured = min( 1023,  analogRead(FOLLOWER_CV) + analogRead(FOLLOWER_KNOB));
  //Measure the amoun of wave-folding needed. This actually results in 2
  //values as we use asymmetrical wave-folding to preserve more of the fundamental
  wrap_factor =  min( 64 + ( min( 1023,  analogRead(WRAP_CV) + analogRead(WRAP_KNOB)) >> 1 ), 511);
  neg_wrap_factor = max( wrap_factor >> 1, 64 );
}


SIGNAL(PWM_INTERRUPT)
{
  //increase the leader phase
  leader_phase += phase_inc;

  //follower osc pitch offset smoothing. Without this we'd get undesired clicks in the sound
  if (follower_offset_current < follower_offset_measured) follower_offset_current++;
  else if (follower_offset_current > follower_offset_measured) follower_offset_current--;

  //turn leader into a sine, or rather; a close approximation. The proper sine function
  //is slow and practical tables are lo-fi and/or slow as progmem is so slow
  leader_value = leader_phase;
  leader_value <<= 1;
  leader_value >>= 8;
  leader_value *= (~leader_value) & 255;
  leader_value <<= 2;
  leader_value >>= 8;
  leader_polarity = leader_phase & 0b1000000000000000;

  //calculate the follower phase in 32bit resolution
  //multiply the phase by a 10 bit number, then go down 8 bits again
  //the result will be a 2 octave increase max.
  //On top of that we say that we want the follower to at least run at
  //the leader's phase's rate.
  //Finally we truncate back into 16 bit, as though the follower phase flowed over
  accumulator = leader_phase;
  accumulator *= follower_offset_current;
  accumulator >>= 8;
  accumulator += leader_phase;
  accumulator &= 65535; //max 16 bit int

  follower_phase = accumulator;

  //turn follower into a sine too, like we did with the leader.
  follower_value = follower_phase;
  follower_value <<= 1;
  follower_value >>= 8;
  follower_value *= (~follower_value) & 255;
  follower_value <<= 2;
  follower_value >>= 8;
  follower_polarity = follower_phase & 0b1000000000000000;

  //multiply leader and follower for the ring-mod,
  //taking care to invert the phase where needed
  leader_value *= follower_value;
  leader_value >>= 8;
  leader_polarity ^= follower_polarity;

  leader_value = leader_value >> 1;

  //wave-wrapping can result in frequency doubling, and we want to preserve bass
  //so we use asymmetrical wave-wrapping
  leader_value *= leader_polarity?wrap_factor:neg_wrap_factor;

  //bit-shifting by 8 bits is way faster than by other amounts
  //so this is faster than >>= 6, as we know the topmost bits are 0
  leader_value <<= 2;
  leader_value >>= 8;

  //detect whether folding will be more involved than inverting the range
  //from 128 to 254
  if (leader_value & 0b1111111100000000) 
  {
    //values between 255 and 511 fold back past the "0" line
    if (leader_value & 0b0000000100000000) leader_polarity = !leader_polarity;
    //mask out bits beyond which the process just repeats
    leader_value &= 0b0000000011111111;
  }
  //actual folding
  if (leader_value > (uint16_t)127)
  {
    leader_value = 127 - (leader_value & 0b0000000001111111);
  }

  //only now do we make a bipolar signal
  if (leader_polarity) output = 127 - leader_value;
  else output = 128 + leader_value;
  PWM_VALUE = output;
}

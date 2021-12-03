/****************************************************************************** ****************************************
	RTTTL (RingTone Text Transfer Language) is the primary format used to distribute.
	ringtones for Nokia phones..

	An RTTTL file is a text file, containing three colon-separated sections:
		# ringtone name
	# control section
		# comma separated sequence of tone commands
	Arbitrary use of whitespace is valid.

	Format
		[name]:[control[,control[,control]]]:tone[,tone[,...]]

		name    : ASCII ringtone name, max 10 characters

		control : tag=val
			tag : b = BPM*                  .. see below*      default = 63
			      d = default tone Duration .. {1,2,4,8,16,32} default = 4
			      o = default tone Octave   .. {4,5,6,7}       default = 6

			* A "beat" is (generally) a "quarter note".
			  Valid BPMs are { 25,  28,  31,  35,  40,  45,  50,  56,  63,
			                   70,  80,  90, 100, 112, 125, 140, 160, 180,
			                  200, 225, 250, 285, 320, 355, 400, 450, 500,
			                  565, 635, 715, 800, 900}

		tone     : [dur]note[oct][dot]
			dur  = note duration {1,2,4,8,16,32} .. 4=one-beat, 8=half-beat, etc.
			note = note name {a,a#,b,c,c#,d,d#,e,f,f#,g,g#,p} .. p = pause/rest
			oct  = octave {4,5,6,7} .. A4 = 440Hz*
			dot  = increase duration by 50%

			* Total note range (of Nokia 61xx) is {a4..b7} (39 notes)

		Example:.
			Simpsons : d=4,o=5,b=160 : 32p,c.6,e6,f#6,8a6,g.6,e6,c6,8a,8f#,8f#,8f#,2g
******************************************************************************* ***************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "driver/ledc.h"

#include "esp_err.h"
#include "freertos/semphr.h"

//----------------------------------------------------------------------------- ----------------------------------------
// Define GPIO pins
//
#define LED_PIN   CONFIG_LED_PIN
#define PWM_PIN1  CONFIG_PWM_PIN

//----------------------------------------------------------------------------- ----------------------------------------
// ESP32-S2 only has LOW_SPEED timers
//
ledc_timer_config_t  cfg1 = {
	.duty_resolution = LEDC_TIMER_13_BIT,
	.freq_hz         = 0,
	.speed_mode      = LEDC_LOW_SPEED_MODE,
	.timer_num       = LEDC_TIMER_1,
	.clk_cfg         = LEDC_AUTO_CLK
};

ledc_channel_config_t chan1 = {
	.channel    = LEDC_CHANNEL_0,
	.duty       = 0,
	.gpio_num   = PWM_PIN1,
	.speed_mode = LEDC_LOW_SPEED_MODE,
	.hpoint     = 0,
	.timer_sel  = LEDC_TIMER_1
};

//+============================================================================ ========================================
// Initialise the PWM channel
//
void  pwmInit (void)
{
	// Configure timer (off)
	ledc_timer_config(&cfg1);

	// Attach timer to pin
	ledc_channel_config(&chan1);
}

//+============================================================================ ========================================
// Beep the speaker
// This function blocks (ie. will not return until the beep is finished)
//
// fHz  : frequency in Hz (0, is a rest)
// duty : duty cycle - can be used as a crass volume control
// time : length of note - specified as (mS / portTICK_PERIOD_MS) ...NOT mS
//
void  pwmBeep (int fHz,  int duty,  int time)
{
	if (fHz) {
		// Set frequency
		cfg1.freq_hz = fHz;
		ledc_timer_config(&cfg1);

		// Start sound
		ledc_set_duty(chan1.speed_mode, chan1.channel, duty);
		ledc_update_duty(chan1.speed_mode, chan1.channel);

		// Wait
		vTaskDelay(time);

		// Stop sound
		ledc_set_duty(chan1.speed_mode, chan1.channel, 0);
		ledc_update_duty(chan1.speed_mode, chan1.channel);

	} else {
		// No sound, just a "rest"
		vTaskDelay(time);
	}
}

//----------------------------------------------------------------------------- ----------------------------------------
// The official RTTTL spec offers tones in the range {A4..B7}, we offer {C4..B7}
//
#define  OCTAVE_LO   4
#define  OCTAVE_HI   7

const uint16_t  freq[] = {
	   0,                                                                    // rest
	 262,  277,  294,  311,  330,  349,  370,  392,  415,                    // C4 .. G#4
	                                                       440,  466,  494,  // A4 .. B4
	 523,  554,  587,  622,  659,  698,  740,  784,  831,  880,  932,  988,  // 5
	1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976,  // 6
	2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951,  // 7
};

// We have (1 + 6) volume steps (IE. 0 is mute, and 1..6 are volume levels)
// The "base"(/minimum) volume we allow is actually (1<<5)
#define VOL_MAX   6  // {0,1..6}
#define VOL_BASE  5  //        -> {0,5...11}

//+============================================================================ ========================================
// An RTTTL tone is comprised of
//   vol  : volume {0=mute, 1..VOL_MAX}
//   dur  : duration (in quarter notes - EG. 4=one-beat, 8=half-beat, etc.)
//   note : See freq table for info ...0 is a 'rest' (no note)
//   fs   : -flats / +sharps ...We allow MULTIPLE b's and #'s (which is out of spec)
//   oct  : octave number {4..7}
//   dot  : dotted note  ...We allow MULTIPLE dot's (which is out of spec)
//   tpd  : ticks/duration ... ie. BPM represented in ESP portTICKs
//   gap  : silence after the note ...In mS ...20 seems to work well, ymmv
//
void  playtone (int vol,  int dur,  int note,  int fs,  int oct,  int dot,  int tpd,  int gap)
{
	// Toggle the LED on/off
	static  int  blink = 0;
	gpio_set_level(LED_PIN, (blink = !blink));

	// calculate frequency and duration
	int  fHz   = freq[(note != -1) * (1 + note + fs + ((oct - OCTAVE_LO) * 12))];
	int  ticks = (tpd * 4) / dur;
	for (int len = ticks >> 1;  dot;  dot--, len >>= 1)  ticks += len ;  // add dots

	// Das beepen quartz
	if (vol > VOL_MAX)  vol = VOL_MAX ;
	pwmBeep((vol && (oct <= OCTAVE_HI)) ? fHz : 0, 1 << (VOL_BASE + vol - 1), ticks);

	if (gap)  vTaskDelay(gap / portTICK_PERIOD_MS) ;
}

//----------------------------------------------------------------------------- ----------------------------------------
// playRtttl() error codes and local-global error store
//
typedef
	enum {
		RTTTL_ERR_OK = 0,
		RTTTL_ERR_TRUNC,
		RTTTL_ERR_DUR,
		RTTTL_ERR_OCT,
	}
rtttl_err_t;

rtttl_err_t  rtttl_err = RTTTL_ERR_OK;

//+============================================================================ ========================================
// Parse & play an RTTTL (Nokia ringtone)
//
// Return  0 - OK
//        >0 - position of error - see rtttl_err for code
//
// See readme for ringtone specification
// We have added the v=? 'setup' variable to specify the volume (out-of-spec)
// We allow ANY BPM (out-of-spec).  The spec lists specific values in the range {25..900}
//

#define SETERR(s)  (rtttl_err = RTTTL_ERR_ ## s), (int)(cp - rtttl)

int  playRtttl (const char* rtttl,  int scVol)
{
	int   mode;
	char  ch;

	// Start cursor ----------------------------------------
	const char*  cp = rtttl;

	// Name ------------------------------------------------
//	const char*  name = cp;
//	      int    nLen = 0;

	while (*cp && *cp != ':')  cp++ ;  // find colon
	if (!*cp)  return SETERR(TRUNC) ;  // found premature end
//	nLen = cp - rtttl;                 // get name length

//	printf("Song: ¦%.*s¦", nLen, name);

	// Parse setup -----------------------------------------
	int  dfVol = scVol;  // out-of-spec
	int  dfDur = 4;      // default values from the RTTTL spec
	int  dfOct = 6;      // ...
	int  bpm   = 63;     // ...

	for (mode = 0,  cp++;  *cp && (*cp != ':');  cp++) {
		switch (*cp) {
			// pram end
			case ','  :  mode = 0 ;
			case ' '  :  // ignore whitespace
			case '\t' :  continue ;
		}

		// mode = {',', 0, bdo, BDO}
		switch (mode) {
			// waiting for comma
			case ',':  continue ;

			// waiting for bdo
			case 0:
				switch ((mode = *cp | 0x20)) {
					default:   mode = ',' ;  // skip unknown field
					case 'b':
					case 'd':
					case 'o':
					case 'v':  continue ;
				}

			// waiting for equals
			case 'b':
			case 'd':
			case 'o':
			case 'v':
				mode = (*cp == '=') ? (mode & ~0x20) : ',' ;  // ?skip badly formed field
				continue;

			// waiting for value
			case 'B':  bpm   = atoi(cp);  mode  = ',';  continue ;
			case 'D':  dfDur = atoi(cp);  mode  = ',';  continue ;
			case 'O':  dfOct = atoi(cp);  mode  = ',';  continue ;
			case 'V':  dfVol = atoi(cp);  mode  = ',';  continue ;
			default:   continue ;
		}
	}
	if (!*cp)  return 0 ;  // found premature end

	// Parse notes -----------------------------------------
//	char*  rpt  = cp;     // remember the repeat point

	int    vol  = dfVol;  // volume {0=off .. 5=full}

	int    dur  = dfDur;  // duration
	int    note = -1;     // note position in chromatic scale (c=0)
	int    fs   = 0;      // -flats .. +sharps
	int    oct  = dfOct;  // octave {a4..b7}
	int    dot  = 0;      // dotted note

	int    gap  = 20;     // ms rest between notes

	// Convert the duration to ticks-per-duration
	int    tpd  = (60 * 1000) / (portTICK_PERIOD_MS * bpm);

	// parse notes
	mode = 'd';  // seeking: duration
	for (cp++;  *cp;  cp++) {
		switch (*cp) {
			case ' ' :  // ignore whitespace
			case '\t':  continue ;

			case ',' : {  // comma terminates a note
				playtone(vol, dur, note, fs, oct, dot, tpd, gap);

				// Reset to defaults for next note
				dur  = dfDur;
				note = -1;
				fs   = 0;
				oct  = dfOct;
				dot  = 0;

				mode = 'd';  // seeking: duration
				continue;
			}
			case '.':  dot++;  continue ;  // double dot   - out of spec
			case '_':  fs--;   continue ;  // double flat  - out of spec
			case '#':  fs++;   continue ;  // double sharp - out of spec
			case 'p':  // Pause/rest
			case 'P':  continue ;
		}

		// handle all note names
		ch = *cp;
		if ((ch == 'h') || (ch == 'H'))  ch = 'b' ;
		if ((ch >= 'A') && (ch <= 'G'))  ch &= ~0x20 ;
		if ((ch >= 'a') && (ch <= 'g')) {
			//             a  b c d e f g
			note = (int[]){9,11,0,2,4,5,7}[ch - 'a'];
			mode = 'o';  // seeking: octave
			continue;
		}

		// decode Duration or Octave value
		switch (mode) {
			case 'd':
				switch (*cp) {
					case '1':  // 1,16
						if (*(cp+1) == '6')  dur = 16,  cp += 1 ;
						else                 dur =  1 ;
						break;
					case '3':  // 3,32
						if (*(cp+1) == '2')  dur = 32,  cp += 1 ;
						else                 dur = 12;  // triplets - out of spec
						break;
					case '2':  // 2,4,8
					case '4':
					case '8':  dur = *cp - '0';  break ;
					default:   return SETERR(DUR) ;  // bad duration
				}
				mode = 'n';  // seeking: note
				continue;
			case 'o':
				switch (*cp) {  // octave
					case '4':  oct = 4;  continue ;
					case '5':  oct = 5;  continue ;
					case '6':  oct = 6;  continue ;
					case '7':  oct = 7;  continue ;
					default:   return SETERR(OCT) ;  // bad octave
				}
		}
	}
	playtone(vol, dur, note, fs, oct, dot, tpd, 0);

	return RTTTL_ERR_OK ;
}

//+============================================================================ ========================================
void app_main (void)
{
	// Nokia ringtones
	char*  tune[] = {
		"GranVals:d=4,o=5,b=225:8e6,8d6,f#,g#,8c#6,8b,d,e,8b,8a,c#,e,2e."
		,"Kyle'sMo:d=16,o=5,b=160:2c6,8f,f,f,8f,c,c,f,f,8f,8f,c,c,f,f,8f,8g,f,f,8f,8e,8e,8c,8e,e,e,8e,c,c,e,e,8e,8e,d,d,8c,c,c,8d,e,e,8g,8f,8f"
		,"6Million:d=16,o=6,b=80:c#.,f#.,b.,4g#.,a.,g#.,e.,d.,b.5,c#.,e.,d#.,2c#,p,4b5,c#.,f#.,b.,4g#.,a.,g#.,e.,d.,b.5,c#.,e.,d#.,2c#,32f#5,32g#5,32a5,32b5,32c#,32d,32e,32f,4f#,c#,p,b5,p,e5,4f#5"
		,"Theme:d=4,o=6,b=100:e5,16a5,16b5,16d,e,16g,16f_,16d,e,16g,16f_,16d,e,8d,16f_,b5,a5,8g5,16a5,8f_5,16d5,g5,16c,16d,16f,g,16c,16b,16f,g,16c,16b,16f,g,8f,16a,d,c,8b5,16d,8a5,16f5,g5,16c,16d,16f,g,16c,16b,16f"
		,NULL
	};

	// Init the PWM timer
	pwmInit();

	// Configure das blinken pin
	gpio_reset_pin(LED_PIN);
	gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

	// Play the array of tunes over-and-over-and-over-and-...
	while (1) {
		for (char** rt = tune;  *rt;  rt++) {
			(void)playRtttl(*rt, VOL_MAX);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
	}
}


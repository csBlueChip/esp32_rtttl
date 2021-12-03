# Play RTTTL (Nokia Ringtones)

Plays an RTTTL string via a piezo speaker using PWM

LED will blink on/off with each note

1. Set target : `idf.py  set-target esp32s2`
2. Choose GPIO pins: `idf.py menuconfig` ...see RTTTL Configuration
3. Build demo : `idf.py build`
4. Flash to ESP32 : `idf.py  -p /dev/ttyUSB0  -b 115200  flash`

Here are (10,825 example RTTTL tunes)[https://picaxe.com/rtttl-ringtones-for-tune-command/]
also (available on archive.org)[https://web.archive.org/web/20210414044550/https://picaxe.com/rtttl-ringtones-for-tune-command/]

# RTTTL Format Specification

```
RTTTL (RingTone Text Transfer Language) is the primary format used to distribute 
ringtones for Nokia phones. 

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
			  
		* A "beat" is (generally) a "quarter note" 
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

	Example: 
		Simpsons : d=4,o=5,b=160 : 32p,c.6,e6,f#6,8a6,g.6,e6,c6,8a,8f#,8f#,8f#,2g
```

The default code uses the onboard LED to flash when notes are played
The only thing you need to do is add a Piezo Buzzer
`ESP32/GPIO:2 --[red]--> 220ohm resistor ----> Piezo Buzzer --[black]--> Gnd`

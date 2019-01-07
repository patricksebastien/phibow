// DDS output thru PWM on timer0 OC0A (pin B.3)
// Mega644 version
// FM synthesis
// with good synth parameters
// and good rate

#include <inttypes.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h> 
#include <math.h> 		// for sine
#include <stdio.h>
#include <util/delay.h>
#include "uart.h"
// set up serial for debugging
FILE uart_str = FDEV_SETUP_STREAM(uart_putchar, uart_getchar, _FDEV_SETUP_RW);

//I like these definitions
#define begin {
#define end   } 

// The DDS variables 
volatile unsigned int acc_main1, acc_fm1 ;
volatile unsigned char high_main1, high_fm1 ;
volatile unsigned int inc_main1, inc_fm1, amp_main1, amp_fm1 ;
volatile unsigned int rise_phase_main1, amp_rise_main1, amp_fall_main1 ;

volatile unsigned int acc_main2, acc_fm2 ;
volatile unsigned char high_main2, high_fm2 ;
volatile unsigned int inc_main2, inc_fm2, amp_main2, amp_fm2 ;
volatile unsigned int rise_phase_main2, amp_rise_main2, amp_fall_main2 ;

volatile unsigned int max_amp ;
#define n_synth 8 
// 0 string-like
// 1 slow rise horn-like
// 2 slow rise horn-like lower pitch
// 3 high chime-like
// 4 low distortion-base
// 5 very low ugly sound
// 6 high string-like
// 7 gong-like
volatile unsigned char  rise_main[n_synth] = {1, 3, 3, 1, 1, 1, 3, 1};
volatile unsigned char decay_main[n_synth] = {5, 6, 6, 5, 5, 4, 4, 5};
volatile unsigned char   depth_fm[n_synth] = {8, 8, 8, 7, 8, 7, 6, 9};
volatile unsigned char   decay_fm[n_synth] = {2, 5, 5, 4, 5, 4, 3, 5};
float freq_main[n_synth] = {1.0, 1.0, 0.5, 1.0, 1.00, 0.50, 2.0, 0.5};
float   freq_fm[n_synth] = {1.5, 1.0, 0.5, 2.0, 0.25, 0.75, 3.0, 0.6};

// the current setting for sound quality
char current_v1_synth, current_v2_synth ;
 
// tables for DDS			
signed char sineTable[256], saw_table[256] ;
// fm DDS sample
signed char fm1, fm2 ;

// tempo
#define n_tempo 4
volatile int tempo[n_tempo] = {0x07ff, 0x0fff, 0x1fff, 0x3fff };
volatile char current_tempo1, current_tempo2 ;
// button state machine
unsigned char PushState, pushed ; 

// pentatonic scale
// Transposing the pitches to fit into one octave rearranges 
// the pitches into the major pentatonic scale: C, D, E, G, A, C.
//   C2   65.4
//   C3   130.8
//   C4   262 Hz (middle C)   
//   D4   294
//	E4   330
//	F4   349
//	G4   392
//	A4   440  
//	B4   494
//	C5   523  
// see: http://www.phy.mtu.edu/~suits/notefreqs.html
float notes[8] = { 262, 294, 330, 392, 440, 523, 587, 659 } ; 

// masks for random noise shift register
// 32 bits
// http://en.wikipedia.org/wiki/Linear_feedback_shift_register
#define bit30 0b01000000000000000000000000000000
#define bit27 0b00001000000000000000000000000000
volatile unsigned long noise_gen ;
int note_index1, note_index2 ;
char bit0, bit1 ; // linear feedback bits
unsigned char rand ;

// note trigger variables
volatile char pluck1, pluck2, beat_enable1, beat_enable2 ;

// Time variables
// the volitile is needed because the time is only set in the ISR
// time counts mSec, sample counts DDS samples (62.5 KHz)
volatile unsigned int time, time_step ;

// indexs
unsigned int i, j, k;

// transition matrix
#define n_matrix 4
char current_v1_matrix, current_v2_matrix ;
unsigned char matrix[n_matrix][8][8] = 
	{ 47, 40, 40, 0, 0, 0, 0, 0, // up-down by one
	0, 40, 47, 40, 0, 0, 0, 0,
	0, 0, 40, 47, 40, 0, 0, 0, 
	0, 0, 0, 40, 47, 40, 0, 0, 
	0, 0, 0, 0, 40, 47, 40, 0,  
	0, 0, 0, 0, 40, 47, 40, 0, 
	0, 0, 0, 0, 0, 40, 47, 40, 
	0, 0, 0, 0, 0, 40, 40, 47, 	// end up-down by one	 

/*
89,27,8,2,1,0,0,0,  // s=0.3
22,73,22,7,2,1,0,0,
6,21,70,21,6,2,1,0,
2,6,21,68,21,6,2,1,
1,2,6,21,68,21,6,2,
0,1,2,6,21,70,21,6,
0,0,1,2,7,22,73,22,
0,0,0,1,2,8,27,89,  // end s=0.3
*/
//64,32,16,8,4,2,1,0, // s=0.5
//26,50,26,13,6,3,2,1,
//12,23,47,23,12,6,3,1,
//6,11,23,44,23,11,6,3,
//3,6,11,23,44,23,11,6,
//1,3,6,12,23,47,23,12,
//1,2,3,6,13,26,50,26,
//0,1,2,4,8,16,32,64,  // end s=0.5

84,21,9,5,3,2,2,1, // interval^s s=-2
18,72,18,8,5,3,2,1,
8,17,68,17,8,4,3,2,
4,7,17,68,17,7,4,3,
3,4,7,17,68,17,7,4,
2,3,4,8,17,68,17,8,
1,2,3,5,8,18,72,18,
1,2,2,3,5,9,21,84, // interval^s s=-2

46,23,16,12,9,8,7,6, // interval^s s=-1
21,40,21,14,10,8,7,6,
13,19,39,19,13,10,8,6,
9,13,19,37,19,13,9,8,
8,9,13,19,37,19,13,9,
6,8,10,13,19,39,19,13,
6,7,8,10,14,21,40,21,
6,7,8,9,12,16,23,46, // interval^s s=-1


//36,26,20,15,11,8,6,5, // s^n  s=0.75
//23,29,23,17,13,10,7,5,
//16,21,25,21,16,12,9,7,
//11,15,20,27,20,15,11,8,
//8,11,15,20,27,20,15,11,
//7,9,12,16,21,25,21,16,
//5,7,10,13,17,23,29,23,
//5,6,8,11,15,20,26,36}; // end s^interval  s=0.75

38,22,16,13,11,10,9,8, // interval^s s=-0.75
20,33,20,15,12,10,9,8,
14,19,32,19,14,11,10,8,
11,14,19,30,19,14,11,9,
9,11,14,19,30,19,14,11,
8,10,11,14,19,32,19,14,
8,9,10,12,15,20,33,20,
8,9,10,11,13,16,22,38};

 
// beat patterns
#define n_beats 10
int beat[n_beats] = {
		0b0101010101010101, // 1-on 1-off phase 2
		0b1111111011111110, // 7-on 1-off
		0b1110111011101110, // 3-on 1-off
		0b1100110011001100, // 2-on 2-off 
		0b1010101010101010, // 1-on 1-off 
		0b1111000011110000, // 4-on 4-off 
		0b1100000011000000, // 2-on 6-off 
		0b0011001100110011, // 2-on 2-off phase 2
		0b1110110011101100, // 3-on 1-off 2-on 2-off 3-on 1-off 2-on 2-off 
		0b0000111100001111, // 4-on 4-off phase 2
		//0b1111111111111111  // on
	} ;

int beat_count1, beat_count2 ;
// max-beats <= 16
#define max_beats 16
char current_v1_beat, current_v2_beat ;

// eeprom define addressee
#define eeprom_true 0
#define eeprom_tempo1 1
#define eeprom_tempo2 2
#define eeprom_matrix1 3
#define eeprom_matrix2 4
#define eeprom_beat1 5
#define eeprom_beat2 6
#define eeprom_synth1 7
#define eeprom_synth2 8


/// sample ISR ////////////////////////////////////////////////////////
ISR (TIMER1_COMPA_vect) // Fs = 8000
begin 

	// compute exponential attack and decay of amplitude
	// the (time & 0x0ff) slows down the decay computation by 256 times		
	if ((time & 0x0ff) == 0) begin
		amp_fall_main1 = amp_fall_main1 - (amp_fall_main1>>decay_main[current_v1_synth]) ;
		rise_phase_main1 = rise_phase_main1 - (rise_phase_main1>>rise_main[current_v1_synth]);
		// compute exponential decay of FM depth of modulation
		amp_fm1 = amp_fm1 - (amp_fm1>>decay_fm[current_v1_synth]) ;

		amp_fall_main2 = amp_fall_main2 - (amp_fall_main2>>decay_main[current_v2_synth]) ;
		rise_phase_main2 = rise_phase_main2 - (rise_phase_main2>>rise_main[current_v2_synth]);
		// compute exponential decay of FM depth of modulation
		amp_fm2 = amp_fm2 - (amp_fm2>>decay_fm[current_v2_synth]) ;
	end

	// form (1-exp(-t/tau)) for the attack phase
	amp_rise_main1 =  max_amp - rise_phase_main1;
	// product of rise and fall exponentials is the amplitude envelope
	amp_main1 = (amp_rise_main1>>8) * (amp_fall_main1>>8) ;
	//amp_main = ((long)amp_rise_main * (long)amp_fall_main) >> 16;
	// cut down on roundoff noise
	if (amp_main1<0x400) amp_main1=0;

	// form (1-exp(-t/tau)) for the attack phase
	amp_rise_main2 =  max_amp - rise_phase_main2;
	// product of rise and fall exponentials is the amplitude envelope
	amp_main2 = (amp_rise_main2>>8) * (amp_fall_main2>>8) ;
	//amp_main = ((long)amp_rise_main * (long)amp_fall_main) >> 16;
	// cut down on roundoff noise
	if (amp_main2<0x400) amp_main2=0;
	

	// end the note
	if (pluck1>1) begin
		amp_fall_main1 = (amp_fall_main1 >> 1) ; // + (amp_fall_main1 >> 2);
		rise_phase_main1 = (rise_phase_main1 >>1) ; // + (rise_phase_main1 >>2) ;
		pluck1-- ;
	end

	if (pluck1==1) begin
		amp_fall_main1 = max_amp; 
		rise_phase_main1 = max_amp ;
	//	amp_rise_main1 = 0 ;
		amp_fm1 = max_amp ;
		pluck1 = 0;
	end

	// end the note
	if (pluck2>1) begin
		amp_fall_main2 = (amp_fall_main2 >> 1) ; //+ (amp_fall_main2 >> 2) ;
		rise_phase_main2 = (rise_phase_main2 >>1) ; // + (rise_phase_main2 >>2);
		pluck2-- ;
	end
	// Init the synth2
	if (pluck2==1) begin
		amp_fall_main2 = max_amp; 
		rise_phase_main2 = max_amp ;
	//	amp_rise_main2 = 0 ;
		amp_fm2 = max_amp ;
		pluck2 = 0;
	end

	
	//the FM DDR -- feeds into final DDR
	acc_fm1 = acc_fm1 + inc_fm1 ;
	high_fm1 = (char)(acc_fm1 >> 8) ;
	fm1 = sineTable[high_fm1] ;

	//the FM DDR -- feeds into final DDR 2
	acc_fm2 = acc_fm2 + inc_fm2 ;
	high_fm2 = (char)(acc_fm2 >> 8) ;
	fm2 = sineTable[high_fm2] ;


	//the final output DDR 
	// phase accum = main_DDR_freq + FM_DDR * (FM amplitude)
	acc_main1 = acc_main1 + (inc_main1 + (fm1*(amp_fm1>>depth_fm[current_v1_synth]))) ;
	high_main1 = (char)(acc_main1 >> 8) ;
	
	acc_main2 = acc_main2 + (inc_main2 + (fm2*(amp_fm2>>depth_fm[current_v2_synth]))) ;
	high_main2 = (char)(acc_main2 >> 8) ;


	// output the wavefrom sample
	// scale amplitude to use only high byte and shift into range
	// 0 to 255
	PORTB = 128 + (((amp_main1>>8) * (int)sineTable[high_main1])>>7) 
				+ (((amp_main2>>8) * (int)sineTable[high_main2])>>7) ;

	time++;     //ticks at 8 KHz 
	if (((time & tempo[current_tempo1]) == 0) && current_tempo1<3) beat_enable1 = 1;
	if (((time & tempo[current_tempo2]) == 0) && current_tempo2<3) beat_enable2 = 1;
end 

// rand gen /////////////////////////////////////////
// make some random #s using 32 bit linear feedback shift register
// need to init noise_gen to nonzero!
char rand_7_bit()
begin
	//noise_gen = noise_gen << 1 ;
	// & with bit 30 for 'linear feedback shoft register'
	noise_gen = noise_gen << 1 ;
	bit0 = (noise_gen & bit27)>0 ;
	// & with bit 27
	bit1 = (noise_gen & bit30)>0 ;
	// set the xored bit 
	noise_gen = noise_gen + (bit0 ^ bit1) ;	
	// return 7 bits
	return (char) (noise_gen & 0x7f) ;
end
/////////////////////////////////////////////////////
int main(void)
begin 
   
   // make B an output
   DDRB = 0xff  ;
   // and C an input and no pullups
   DDRC = 0; PORTC = 0;
   // and D runs LEDs
   DDRD = 0xff ;

   //init the UART -- uart_init() is in uart.c
  //	uart_init();
  //	stdout = stdin = stderr = &uart_str;
  //	fprintf(stdout,"Starting...\n\r");

   // init the sine table
   for (i=0; i<256; i++)
   begin
   		sineTable[i] = (char)(127.0 * sin(6.283*((float)i)/256.0)) ;
		saw_table[i] = (char)(( (i<17)? i*7 : (i<241)? 128-i : i*7-1792 )) ;
   end  

   // init the time counter
   time=0;

   // timer 0 runs at full rate
   //TCCR0B = 1 ;  
   //turn off timer 0 overflow ISR
   //TIMSK0 = 0 ;
   // turn on PWM
   // turn on fast PWM and OC0A output
   // at full clock rate, toggle OC0A (pin B3) 
   // 16 microsec per PWM cycle sample time
   //TCCR0A = (1<<COM0A0) | (1<<COM0A1) | (1<<WGM00) | (1<<WGM01) ; 
   //OCR0A = 128 ; // set PWM to half full scale
	
	// timer 1 ticks at 8000 Hz or 125 microsecs period=2000 ticks
	OCR1A =  1999 ; // 1999- 2000 ticks
	TIMSK1 = (1<<OCIE1A) ;
	TCCR1B = 0x09; 	//full speed; clear-on-match
  	TCCR1A = 0x00;	//turn off pwm and oc lines

	 //init the A to D converter 
   //channel zero/ left adj /EXTERNAL Aref
   //!!!CONNECT Aref jumper!!!!
   ADMUX = (1<<ADLAR) ;  
    
   //enable ADC and set prescaler to 1/128*16MHz=125,000
   //and clear interupt enable
   //and start a conversion
   ADCSRA = (1<<ADEN) | (1<<ADSC) + 7 ; 
	
	// init the notes
    current_tempo1 = 0 ;
    current_tempo2 = 0 ;
   	note_index1 = 0;
	note_index2 = 0;
	current_v1_matrix = 0 ; //2
	current_v2_matrix = 3 ; //1
	current_v1_beat = 5;
	current_v2_beat = 9 ;
	current_v1_synth = 0 ;
	current_v2_synth = 1 ;

   // eeprom init
   if (eeprom_read_byte((uint8_t*)eeprom_true) != 'T')
 	begin 
		eeprom_write_byte((uint8_t*)eeprom_true,'T');
		eeprom_write_byte((uint8_t*)eeprom_tempo1,current_tempo1);
		eeprom_write_byte((uint8_t*)eeprom_tempo2,current_tempo2);
		eeprom_write_byte((uint8_t*)eeprom_matrix1,current_v1_matrix);
		eeprom_write_byte((uint8_t*)eeprom_matrix2,current_v2_matrix);
		eeprom_write_byte((uint8_t*)eeprom_beat1,current_v1_beat);
		eeprom_write_byte((uint8_t*)eeprom_beat2,current_v2_beat);
		eeprom_write_byte((uint8_t*)eeprom_synth1,current_v1_synth);
		eeprom_write_byte((uint8_t*)eeprom_synth2,current_v2_synth);
 	end 
	else begin
		current_tempo1 = eeprom_read_byte((uint8_t*)eeprom_tempo1);
		current_tempo2 = eeprom_read_byte((uint8_t*)eeprom_tempo2);
		current_v1_matrix = eeprom_read_byte((uint8_t*)eeprom_matrix1);
		current_v2_matrix = eeprom_read_byte((uint8_t*)eeprom_matrix2);
		current_v1_beat = eeprom_read_byte((uint8_t*)eeprom_beat1);
		current_v2_beat = eeprom_read_byte((uint8_t*)eeprom_beat2);
		current_v1_synth = eeprom_read_byte((uint8_t*)eeprom_synth1);
		current_v2_synth = eeprom_read_byte((uint8_t*)eeprom_synth2);
 	end 
	

   // turn on all ISRs
   sei() ;
   
  
   ///////////////////////////////////////////////////
   // Sound parameters
   ///////////////////////////////////////////////////
   
   // init the random gen
   // delay to read ADC
   _delay_us(200) ;
   noise_gen = 1 + ADCH ;
	ADCSRA = 0; // disable ADC


   // cumsum the matricies
   for (k=0; k<4; k++) begin
	   for (i=0; i<8; i++) begin
	   	 for (j=1; j<8; j++) begin
			matrix[k][i][j] = matrix[k][i][j] + matrix[k][i][j-1] ;
	 	  end
		end
	end

	// beat control and beat masks
	beat_count1 = 0;
	beat_count2 = 0;
	
	// init the button state machine
	PushState = 1;

  ////////////////////////////////////////////////////

   while(1) begin  
		//  
		// spin the rand gen during main
		rand_7_bit();

		// voice 1
		if (beat_enable1) begin //1fff
			beat_enable1 = 0 ;
			
			// voice 1
			if ((beat[current_v1_beat]<<beat_count1) & 0x8000)
			begin
				// accumulator_length/sample_rate * desired_frequency = 
				// 2^16/8000*freq = 8.192*freq
				inc_main1 = (int)(8.192 * notes[note_index1]*freq_main[current_v1_synth]) ;
				inc_fm1 = (int)(8.192 * notes[note_index1] * freq_fm[current_v1_synth]) ;
				max_amp = 24000 ;
				//if ((time & 0x3000) == 0) max_amp = 32767 ;
				pluck1 = 5;	
			
				// pick the next note
				//rand = (char) (noise_gen & 0x7f) ; // random number <128
				for (j=0; j<8; j++) begin
					if (matrix[current_v1_matrix][note_index1][j]>rand_7_bit())
						break;
				end
				note_index1 = j ;
				if (note_index1 > 7) note_index1 = 7;
			end
			// beat counter
			beat_count1 ++;
			if (beat_count1 == max_beats) beat_count1 = 0;
		end

		// spin the rand gen during main
		rand_7_bit();
		
		// voice 2
		if (beat_enable2) begin //1fff
			beat_enable2 = 0 ;
			// voice 2
			if ((beat[current_v2_beat]<<beat_count2) & 0x8000)
			begin
				//note_index = noise_gen & 0x07 ;
				inc_main2 = (int)(8.192 * notes[note_index2]*freq_main[current_v2_synth]) ; 
				inc_fm2 = (int)(8.192 * notes[note_index2] * freq_fm[current_v2_synth]);
				max_amp = 24000 ;;
				pluck2 = 5;	
			
				// pick the next note
				//rand = (char) (noise_gen & 0x7f) ; // random number <128
				for (j=0; j<8; j++) begin
					if (matrix[current_v2_matrix][note_index2][j]>rand_7_bit())
						break;
				end
				note_index2 = j ;
				if (note_index2 > 7) note_index2 = 7;
			end

			// beat counter
			beat_count2 ++;
			if (beat_count2 == max_beats) beat_count2 = 0;

		end	
		
		// spin the rand gen during main
		rand_7_bit();

		// button state machine about every 32 mSec
		if ((time & 0xff) == 0)
		begin
			switch (PushState)
			begin
			     case 1: 
			        if (!(PINC==0xff)) 
					begin
						PushState=2;
						pushed = ~PINC;
						 
					end
			        else PushState=1;
			        break;
			     case 2:
			        if ((~PINC & 0xff) == pushed ) 
			        begin
			           PushState=3; 
					   if (pushed == 1) //sw0 tempo1
					   begin  
			           		current_tempo1++ ;
				 	   		if (current_tempo1 >= n_tempo) current_tempo1 = 0 ;
							PORTD = current_tempo1 ;
							eeprom_write_byte((uint8_t*)eeprom_tempo1,current_tempo1);
						end
						if (pushed == 2) //sw0 tempo2
					   begin  
			           		current_tempo2++ ;
				 	   		if (current_tempo2 >= n_tempo) current_tempo2 = 0 ;
							PORTD = current_tempo2 ;
							eeprom_write_byte((uint8_t*)eeprom_tempo2,current_tempo2);
						end
						if (pushed == 4)  //sw1 matrix 1
					   begin  
			           		current_v1_matrix++ ;
				 	   		if (current_v1_matrix >= n_matrix) current_v1_matrix = 0 ;
							PORTD = current_v1_matrix ;
							eeprom_write_byte((uint8_t*)eeprom_matrix1,current_v1_matrix);
						end
						if (pushed == 8) //sw2 matrix 2
					   begin  
			           		current_v2_matrix++ ;
				 	   		if (current_v2_matrix >= n_matrix) current_v2_matrix = 0 ;
							PORTD = current_v2_matrix ;
							eeprom_write_byte((uint8_t*)eeprom_matrix2,current_v2_matrix);
						end
						if (pushed == 16) //sw3 beat 1
					   begin  
			           		current_v1_beat++ ;
				 	   		if (current_v1_beat >= n_beats) current_v1_beat = 0 ;
							PORTD = current_v1_beat ;
							eeprom_write_byte((uint8_t*)eeprom_beat1,current_v1_beat);
						end
						if (pushed == 32) //sw4 beat 2
					   begin  
			           		current_v2_beat++ ;
				 	   		if (current_v2_beat >= n_beats) current_v2_beat = 0 ;
							PORTD = current_v2_beat ;
							eeprom_write_byte((uint8_t*)eeprom_beat2,current_v2_beat);
						end
						if (pushed == 64) //sw5 sound quality 1
					   begin  
			           		current_v1_synth++ ;
				 	   		if (current_v1_synth >= n_synth) current_v1_synth = 0 ;
							PORTD = current_v1_synth ;
							eeprom_write_byte((uint8_t*)eeprom_synth1,current_v1_synth);
						end
						if (pushed == 128) //sw6  sound quality 2
					   begin  
			           		current_v2_synth++ ;
				 	   		if (current_v2_synth >= n_synth) current_v2_synth = 0 ;
							PORTD = current_v2_synth ;
							eeprom_write_byte((uint8_t*)eeprom_synth2,current_v2_synth);
						end
			        end
			        else PushState=1;
			        break;
			     case 3:  
			        if ((~PINC & 0xff) == pushed )  PushState=3; 
			        else PushState=4;    
			        break;
			     case 4:
			        if ((~PINC & 0xff) == pushed )  PushState=3; 
			        else 
			        begin
			           PushState=1;
			        end    
			        break;
			 end	// switch (PushState)		  
		end // if ((time & 0x3ff) == 0) 		
   end // while(1)
end  //end main
////////////////////////////////////////////////////////
/*
Examples:      

Chime:
   inc_main = (int)(8.192 * 261.0) ; 
   decay_main = 5 ;
   rise_main = 1 ;
   inc_fm1 = (int)(8.192 * 350.0) ;
   depth_fm1 = 9 ;
   decay_fm1 = 5 ;

Plucked String:
	inc_main = (int)(8.192 * 500.0) ; 
   decay_main = 3 ;
   rise_main = 1 ;
   inc_fm1 = (int)(8.192 * 750.0) ;
   depth_fm1 = 8 ;
   decay_fm1 = 3 ;

Plucked String:
   inc_main = (int)(8.192 * 600) ; 
   decay_main = 5 ;
   rise_main = 0 ;
   inc_fm1 = (int)(8.192 * 150) ;
   depth_fm1 = 8 ;
   decay_fm1 = 6 ;

Bowed string
   inc_main = (int)(8.192 * 300) ;  
   decay_main = 5 ;
   rise_main = 4 ;
   inc_fm1 = (int)(8.192 * 300) ;
   depth_fm1 = 8 ;
   decay_fm1 = 6 ;

Small, stiff rod
 	inc_main = (int)(8.192 * 1440) ;   
   decay_main = 3 ;
   rise_main = 1 ;   
   inc_fm1 = (int)(8.192 * 50) ; // at 100 get stiff string; at 200 get hollow pipe
   depth_fm1 = 10 ; //or 9
   decay_fm1 = 5 ;

Bell/chime
   inc_main = (int)(8.192 * 1440) ; 
   decay_main = 5 ;
   rise_main = 1 ;
   inc_fm1 = (int)(8.192 * 600) ;
   depth_fm1 = 8 ;
   decay_fm1 = 6 ;

Bell
   inc_main = (int)(8.192 * 300) ; 
   decay_main = 5 ;
   rise_main = 0 ;
   inc_fm1 = (int)(8.192 * 1000) ;
   depth_fm1 = 8 ;
   decay_fm1 = 6 ;
*/

/*
================================================================

% compute normalized markov matrix
% 8x8 for test
clear all
clc

s = 1/2 ; % the ratio of one element to the next in a row

A = [ 1 s s^2 s^3 s^4 s^5 s^6 s^7; 
      s 1 s s^2 s^3 s^4 s^5 s^6 ;
      s^2 s 1 s s^2 s^3 s^4 s^5 ;
      s^3 s^2 s 1 s s^2 s^3 s^4 ;
      s^4 s^3 s^2 s 1 s s^2 s^3 ;
      s^5 s^4 s^3 s^2 s 1 s s^2 ;
      s^6 s^5 s^4 s^3 s^2 s 1 s ;
      s^7 s^6 s^5 s^4 s^3 s^2 s 1 ;] ;
  
for i=1:8
    norm = 127/sum(A(i,:));
    Anorm(i,:) = A(i,:)*norm ;
end

Anorm = round(Anorm);

for i=1:8
   d = sum(Anorm(i,:)) - 127 ;
   Anorm(i,i) = Anorm(i,i) - d ;
   fprintf('%d,%d,%d,%d,%d,%d,%d,%d,\n', Anorm(i,:))
end

*/
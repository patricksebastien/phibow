#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#define F 242720 // half a period for 200kHz interrupt
#define COUNTER_BITS 11
#define ASIZE (1<<COUNTER_BITS)
#define MASK (ASIZE-1)
 
// assuming 16-bit ints
unsigned int x[ASIZE];
unsigned int p;
//pitch 36 is 203.877 - C2 384.868 
unsigned int f[12] = { F/138, F/215, F/228.844, F/242.452, F/256.869, F/272.143, F/288.325, F/305.470, F/323.634, F/342.879, F/363.267, F/388 }; 
 
 
 // 1380 == 203.877hz
 // 5152 == 769.736hz
 
 void timer1_init()
{
	DDRA = 0xFF;
	DDRC = 0xFF;
	DDRB = 0xFF;
	DDRG = 0xFF;
	DDRH = 0xFF;
	DDRE = 0xFF;
	DDRL = 0xFF;
	
	TCCR2A |= (1<<WGM21) | (1<<COM2A0 ) | (1<<COM2B0 );
	TCCR2B |= (1<<CS20);
	
	OCR2A = 32; // 200khz == 5us
	
	// enable compare interrupt
	TIMSK2 |= (1 << OCIE2A);
}


ISR (TIMER2_COMPA_vect)
{
	interrupt_routine();
}

void pwm_init() {
 
  unsigned int i;
  unsigned int mask;
 
  for (i = 0; i < ASIZE; i++) {
    x[i] = 0;
  }
 
  mask = 1;
  for (i = 0; i < 12; i++) {
    x[f[i]] |= mask;
    mask <<= 1;
  }
 
  p = 0;
}
 
inline void toggle(int i) {
	if(i > 7) {
		PORTC ^= (1 << (i - 8));
	} else {
		PORTA ^= (1 << i);
	}
}
 
void interrupt_routine() {
 
  unsigned int i;
  unsigned int z;
  unsigned int mask;
 
  p++;
  z = x[p&MASK];
 
  if (z) {
    x[p&MASK] = 0;
    mask = 1;
    
    for (i = 0; i <12; i++) {
      if (z&mask) {
        toggle(i);
        x[(p+f[i])&MASK] |= mask;
      }
      mask <<= 1;
    }

    
  }
}
 
 
void main() {
  timer1_init();
  pwm_init();
  sei();
  while (1) {
    // should really be run in the interrupt
    //interrupt_routine();
  }
 
}

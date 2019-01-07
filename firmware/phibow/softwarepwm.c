#define F_CPU 16000000UL
//#define DEBUG

#include <avr/io.h>
#include <avr/interrupt.h>

#define sbi(b,n) (b |= (1<<n))          /* Set bit number n in byte b    */
#define cbi(b,n) (b &= (~(1<<n)))       /* Clear bit number n in byte b  */

volatile unsigned int testa = 0;
volatile unsigned int testb = 0;
volatile unsigned int testc = 0;
volatile unsigned int testd = 0;
volatile unsigned int teste = 0;
volatile unsigned int testf = 0;

void IOConf()
{
	// PWM - output
	DDRB = 0xFF;
	DDRG = 0xFF;
	DDRH = 0xFF;
	DDRE = 0xFF;
	DDRL = 0xFF;
	
	// LED - output
	DDRA = 0xFF;
	DDRC = 0xFF;
	
	// MOSFET - output + PC4/7
	DDRJ = 0XFF;
	
	// SWITCH - input PG5 is PWM
	DDRG = 0x20;
	PORTG = 0x1F; // pull-up enable
	
	// ANALOG - input
	DDRF = 0x00;
	DDRK = 0x00;
}
/*
void timer1_init()
{
	TCCR2A |= (1<<WGM21) | (1<<COM2A0 ) | (1<<COM2B0 );
	
	// min is 16 with 128 [rescale = 136us = 7352.9411hz;
	
	TCCR2B |= (1<<CS20);
	OCR2A = 70; //
	//180 @ 274 int == 323.62
	
	
	// enable compare interrupt
	TIMSK2 |= (1 << OCIE2A);
}


ISR (TIMER2_COMPA_vect)
{
	testa++;

}
*/



////////////////////////////////////////////////////////////////////////
// MAIN
////////////////////////////////////////////////////////////////////////
int main(void)
{
	IOConf();

	//timer1_init();
	//sei();
	
	
	//int j = 0;
	
	for (;;) {		/* main event loop */
		testa++;
		testb++;
		testc++;
		testd++;
		teste++;
		
		
		if(testa > 702) { sbi(PORTA, 0); testa = 0; }
		
		
	
		
		
	}
	return 0;
}

/*
* File: main file for avr capacitive touch test
* Author: Tuomas Nylund (tuomas.nylund@gmail.com)
* Website: http://tuomasnylund.fi
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
//standard libs
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//avr libs
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/power.h>
#include <avr/interrupt.h>

//own stuff
#include "touch.h"



/**********************************************************
 * Global variables
 **********************************************************/

static touch_channel_t btn1 = {
    .mux = 7,
    .port = &PORTF,
    .portmask = (1<<PF7),
    .min = 700,
    .max = 1024
};

static touch_channel_t btn2 = {
    .mux = 6,
    .port = &PORTF,
    .portmask = (1<<PF6),
    .min = 500,
    .max = 800
};

static touch_channel_t btn3 = {
    .mux = 5,
    .port = &PORTF,
    .portmask = (1<<PF5),
    .min = 520,
    .max = 820
};

/**********************************************************
 * Main function
 **********************************************************/
int main(void){

    //variables
    uint16_t i;
    uint16_t sample[3];

	initialize();
    i = 0;
    //main loop
    while (1)
    {
        i++;
        if (i>10000){
			
            sample[0] = touch_measure(&btn1);
            sample[1] = touch_measure(&btn2);
            sample[2] = touch_measure(&btn3);
            
            if(sample[0] > 790) {
				PORTD &= ~(1 << PIND7);
			} else {
				PORTD |= (1 << PIND7);
			}
    
            i=0;
            
        }


    }
    return 0;
}

/**********************************************************
 * Interrupt vectors
 **********************************************************/


/**********************************************************
 * Other functions
 **********************************************************/

/** Initializes all of the hardware. */
void initialize(void){


    DDRD = 0xFF;
    touch_init();

    sei();
}

/*
 *   Rainbow Scroll
 * 
 *   by @Kimio_kosaka http://make.kosakalab.com/
 *   
 */
 
#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>

//#include "ws2812_config.h" //configure output_port etc. 
/* WS2812_CONFIG_H_ */
///////////////////////////////////////////////////////////////////////
// Define Reset time in µs. 
//
// This is the time the library spends waiting after writing the data.
//
// WS2813 needs 300 µs reset time
// WS2812 and clones only need 50 µs
//
///////////////////////////////////////////////////////////////////////

#define ws2812_resettime  300 

///////////////////////////////////////////////////////////////////////
// Define I/O pin
///////////////////////////////////////////////////////////////////////


#define ws2812_port B     // Data port 
#define ws2812_pin  2     // Data out pin

/* WS2812_CONFIG_H_ */




//#include "light_ws2812.h"
///starting light_ws2812.h
/*
 *  Structure of the LED array
 *
 * cRGB:     RGB  for WS2812S/B/C/D, SK6812, SK6812Mini, SK6812WWA, APA104, APA106
 * cRGBW:    RGBW for SK6812RGBW
 */

struct cRGB  { uint8_t g; uint8_t r; uint8_t b; };
struct cRGBW { uint8_t g; uint8_t r; uint8_t b; uint8_t w;};



/* User Interface
 * 
 * Input:
 *         ledarray:           An array of GRB data describing the LED colors
 *         number_of_leds:     The number of LEDs to write
 *         pinmask (optional): Bitmask describing the output bin. e.g. _BV(PB0)
 *
 * The functions will perform the following actions:
 *         - Set the data-out pin as output
 *         - Send out the LED data 
 *         - Wait 50�s to reset the LEDs
 */

void ws2812_setleds     (struct cRGB  *ledarray, uint16_t number_of_leds);
void ws2812_setleds_pin (struct cRGB  *ledarray, uint16_t number_of_leds,uint8_t pinmask);
void ws2812_setleds_rgbw(struct cRGBW *ledarray, uint16_t number_of_leds);

/* 
 * Old interface / Internal functions
 *
 * The functions take a byte-array and send to the data output as WS2812 bitstream.
 * The length is the number of bytes to send - three per LED.
 */

void ws2812_sendarray     (uint8_t *array,uint16_t length);
void ws2812_sendarray_mask(uint8_t *array,uint16_t length, uint8_t pinmask);


/*
 * Internal defines
 */

#define CONCAT(a, b)            a ## b
#define CONCAT_EXP(a, b)   CONCAT(a, b)

#define ws2812_PORTREG  CONCAT_EXP(PORT,ws2812_port)
#define ws2812_DDRREG   CONCAT_EXP(DDR,ws2812_port)
///end of light_ws2812.h

//#include "light_ws2812.c"
//starting light_ws2812.c
#include <util/delay.h>
 
// Setleds for standard RGB 
void inline ws2812_setleds(struct cRGB *ledarray, uint16_t leds)
{
   ws2812_setleds_pin(ledarray,leds, _BV(ws2812_pin));
}

void inline ws2812_setleds_pin(struct cRGB *ledarray, uint16_t leds, uint8_t pinmask)
{
  ws2812_sendarray_mask((uint8_t*)ledarray,leds+leds+leds,pinmask);
  _delay_us(ws2812_resettime);
}

// Setleds for SK6812RGBW
void inline ws2812_setleds_rgbw(struct cRGBW *ledarray, uint16_t leds)
{
  ws2812_sendarray_mask((uint8_t*)ledarray,leds<<2,_BV(ws2812_pin));
  _delay_us(ws2812_resettime);
}

void ws2812_sendarray(uint8_t *data,uint16_t datlen)
{
  ws2812_sendarray_mask(data,datlen,_BV(ws2812_pin));
}

/*
  This routine writes an array of bytes with RGB values to the Dataout pin
  using the fast 800kHz clockless WS2811/2812 protocol.
*/

// Timing in ns
#define w_zeropulse   350
#define w_onepulse    900
#define w_totalperiod 1250

// Fixed cycles used by the inner loop
#define w_fixedlow    2
#define w_fixedhigh   4
#define w_fixedtotal  8   

// Insert NOPs to match the timing, if possible
#define w_zerocycles    (((F_CPU/1000)*w_zeropulse          )/1000000)
#define w_onecycles     (((F_CPU/1000)*w_onepulse    +500000)/1000000)
#define w_totalcycles   (((F_CPU/1000)*w_totalperiod +500000)/1000000)

// w1 - nops between rising edge and falling edge - low
#define w1 (w_zerocycles-w_fixedlow)
// w2   nops between fe low and fe high
#define w2 (w_onecycles-w_fixedhigh-w1)
// w3   nops to complete loop
#define w3 (w_totalcycles-w_fixedtotal-w1-w2)

#if w1>0
  #define w1_nops w1
#else
  #define w1_nops  0
#endif

// The only critical timing parameter is the minimum pulse length of the "0"
// Warn or throw error if this timing can not be met with current F_CPU settings.
#define w_lowtime ((w1_nops+w_fixedlow)*1000000)/(F_CPU/1000)
#if w_lowtime>550
   #error "Light_ws2812: Sorry, the clock speed is too low. Did you set F_CPU correctly?"
#elif w_lowtime>450
   #warning "Light_ws2812: The timing is critical and may only work on WS2812B, not on WS2812(S)."
   #warning "Please consider a higher clockspeed, if possible"
#endif   

#if w2>0
#define w2_nops w2
#else
#define w2_nops  0
#endif

#if w3>0
#define w3_nops w3
#else
#define w3_nops  0
#endif

#define w_nop1  "nop      \n\t"
#define w_nop2  "rjmp .+0 \n\t"
#define w_nop4  w_nop2 w_nop2
#define w_nop8  w_nop4 w_nop4
#define w_nop16 w_nop8 w_nop8

void inline ws2812_sendarray_mask(uint8_t *data,uint16_t datlen,uint8_t maskhi)
{
  uint8_t curbyte,ctr,masklo;
  uint8_t sreg_prev;
  
  ws2812_DDRREG |= maskhi; // Enable output
  
  masklo  =~maskhi&ws2812_PORTREG;
  maskhi |=        ws2812_PORTREG;
  
  sreg_prev=SREG;
  cli();  

  while (datlen--) {
    curbyte=*data++;
    
    asm volatile(
    "       ldi   %0,8  \n\t"
    "loop%=:            \n\t"
    "       out   %2,%3 \n\t"    //  '1' [01] '0' [01] - re
#if (w1_nops&1)
w_nop1
#endif
#if (w1_nops&2)
w_nop2
#endif
#if (w1_nops&4)
w_nop4
#endif
#if (w1_nops&8)
w_nop8
#endif
#if (w1_nops&16)
w_nop16
#endif
    "       sbrs  %1,7  \n\t"    //  '1' [03] '0' [02]
    "       out   %2,%4 \n\t"    //  '1' [--] '0' [03] - fe-low
    "       lsl   %1    \n\t"    //  '1' [04] '0' [04]
#if (w2_nops&1)
  w_nop1
#endif
#if (w2_nops&2)
  w_nop2
#endif
#if (w2_nops&4)
  w_nop4
#endif
#if (w2_nops&8)
  w_nop8
#endif
#if (w2_nops&16)
  w_nop16 
#endif
    "       out   %2,%4 \n\t"    //  '1' [+1] '0' [+1] - fe-high
#if (w3_nops&1)
w_nop1
#endif
#if (w3_nops&2)
w_nop2
#endif
#if (w3_nops&4)
w_nop4
#endif
#if (w3_nops&8)
w_nop8
#endif
#if (w3_nops&16)
w_nop16
#endif

    "       dec   %0    \n\t"    //  '1' [+2] '0' [+2]
    "       brne  loop%=\n\t"    //  '1' [+3] '0' [+4]
    : "=&d" (ctr)
    : "r" (curbyte), "I" (_SFR_IO_ADDR(ws2812_PORTREG)), "r" (maskhi), "r" (masklo)
    );
  }
  
  SREG=sreg_prev;
}
//end of light_ws2812.c


#define LEDS 8            //number of LEDs 1-8
#define BRIGHTNESS 255    //brightness 0-255
#define CD 32             //color step division 1-255
#define DLY 100           //scroll delay [ms]

struct cRGB led[LEDS];

//Show and Shift LED Color
void shift_led(){ 
  ws2812_setleds(led,LEDS);  
  for(int m=LEDS-1;m>=0;m--){   
    led[m+1].r=led[m].r; led[m+1].g=led[m].g;led[m+1].b=led[m].b;       
  }
}

int main(void)
{
  //init();
  #ifdef __AVR_ATtiny10__
  CCP=0xD8;    // configuration change protection, write signature
  CLKPSR=0;   // set cpu clock prescaler =1 (8Mhz) (attiny 4/5/9/10)
  #endif
  int r,g,b;
  while(1)
  {
    // Red vs Green
    b=0;
    for(int n=0;n<BRIGHTNESS+2;n+=CD){
      if(n>BRIGHTNESS) n=BRIGHTNESS;
      g=n;
      r=BRIGHTNESS-g;
      shift_led();                       //call Show and Shift LED Color func.
      led[0].r=r; led[0].g=g;led[0].b=b;    
      _delay_ms(DLY); 
    }
    // Green vs Blue
    r=0;
    for(int n=0;n<BRIGHTNESS+2;n+=CD){
      if(n>BRIGHTNESS) n=BRIGHTNESS;
      b=n;
      g=BRIGHTNESS-b;
      shift_led();
      led[0].r=r; led[0].g=g;led[0].b=b;   
      _delay_ms(DLY); 
    }
    // Blue vs Red
    g=0;
    for(int n=0;n<BRIGHTNESS+2;n+=CD){
      if(n>BRIGHTNESS) n=BRIGHTNESS;
      r=n;
      b=BRIGHTNESS-r;
      shift_led();
      led[0].r=r; led[0].g=g;led[0].b=b;    
      _delay_ms(DLY); 
    }
  }
}

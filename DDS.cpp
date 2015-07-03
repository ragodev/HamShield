#include <Arduino.h>
#include "DDS.h"

// To start the DDS, we use Timer1, set to the reference clock
// We use Timer2 for the PWM output, running as fast as feasible
void DDS::start() {
  // Use the clkIO clock rate
  ASSR &= ~(_BV(EXCLK) | _BV(AS2));
  
  // First, the timer for the PWM output
  // Setup the timer to use OC2B (pin 3) in fast PWM mode with a configurable top
  // Run it without the prescaler
#ifdef DDS_PWM_PIN_3
  TCCR2A = (TCCR2A | _BV(COM2B1)) & ~(_BV(COM2B0) | _BV(COM2A1) | _BV(COM2A0)) |
         _BV(WGM21) | _BV(WGM20);
  TCCR2B = (TCCR2B & ~(_BV(CS22) | _BV(CS21))) | _BV(CS20) | _BV(WGM22);
#else
  // Alternatively, use pin 11
  // Enable output compare on OC2A, toggle mode
  TCCR2A = _BV(COM2A1) | _BV(WGM21) | _BV(WGM20);
  //TCCR2A = (TCCR2A | _BV(COM2A1)) & ~(_BV(COM2A0) | _BV(COM2B1) | _BV(COM2B0)) |
  //     _BV(WGM21) | _BV(WGM20);
  TCCR2B = _BV(CS20);
#endif

  // Set the top limit, which will be our duty cycle accuracy.
  // Setting Comparator Bits smaller will allow for higher frequency PWM,
  // with the loss of resolution.
#ifdef DDS_PWM_PIN_3
  OCR2A = pow(2,COMPARATOR_BITS)-1;
  OCR2B = 0;
#else
  OCR2A = 0;
#endif
  
#ifdef DDS_USE_ONLY_TIMER2
  TIMSK2 |= _BV(TOIE2);
#endif

  // Second, setup Timer1 to trigger the ADC interrupt
  // This lets us use decoding functions that run at the same reference
  // clock as the DDS.
  TCCR1B = _BV(CS11) | _BV(WGM13) | _BV(WGM12);
  TCCR1A = 0;
  ICR1 = ((F_CPU / 8) / refclk) - 1;
  //ICR1 = ((F_CPU / 8) / 9600) - 1;

  // Configure the ADC here to automatically run and be triggered off Timer1
  ADMUX = _BV(REFS0) | _BV(ADLAR) | 0; // Channel 0, shift result left (ADCH used)
  DDRC &= ~_BV(0);
  PORTC &= ~_BV(0);
  DIDR0 |= _BV(0);
  ADCSRB = _BV(ADTS2) | _BV(ADTS1) | _BV(ADTS0);
  ADCSRA = _BV(ADEN) | _BV(ADSC) | _BV(ADATE) | _BV(ADIE) | _BV(ADPS2); // | _BV(ADPS0);    
}

void DDS::stop() {
  // TODO: Stop the timers.
}

// Set our current sine wave frequency in Hz
void DDS::setFrequency(unsigned short freq) {
  // Fo = (M*Fc)/2^N
  // M = (Fo/Fc)*2^N
  if(refclk == DDS_REFCLK_DEFAULT) {
    // Try to use precalculated values if possible
    if(freq == 2200) {
      stepRate = (2200.0 / (DDS_REFCLK_DEFAULT+DDS_REFCLK_OFFSET)) * pow(2,ACCUMULATOR_BITS);
    } else if (freq == 1200) {
      stepRate = (1200.0 / (DDS_REFCLK_DEFAULT+DDS_REFCLK_OFFSET)) * pow(2,ACCUMULATOR_BITS);      
    } else if (freq == 600) {
      stepRate = (600.0 / (DDS_REFCLK_DEFAULT+DDS_REFCLK_OFFSET)) * pow(2,ACCUMULATOR_BITS);      
    }
  } else {
    // BUG: Step rate isn't properly calculated here, it gets the wrong frequency
    stepRate = pow(2,ACCUMULATOR_BITS)*freq / (refclk+DDS_REFCLK_OFFSET);
  }
}

// Degrees should be between -360 and +360 (others don't make much sense)
void DDS::setPhaseDeg(int16_t degrees) {
  accumulator = degrees * (pow(2,ACCUMULATOR_BITS)/360.0);
}
void DDS::changePhaseDeg(int16_t degrees) {
  accumulator += degrees * (pow(2,ACCUMULATOR_BITS)/360.0);
}

// TODO: Clean this up a bit..
void DDS::clockTick() {
/*  if(running) {
    accumulator += stepRate;
    OCR2A = getDutyCycle();
  }
  return;*/
  if(running) {
    accumulator += stepRate;
    if(timeLimited && tickDuration == 0) {
#ifndef DDS_PWM_PIN_3
      OCR2A = 0;
#else
#ifdef DDS_IDLE_HIGH
      // Set the duty cycle to 50%
      OCR2B = pow(2,COMPARATOR_BITS)/2;
#else
      // Set duty cycle to 0, effectively off
      OCR2B = 0;
#endif
#endif
      running = false;
      accumulator = 0;
    } else {
#ifdef DDS_PWM_PIN_3
      OCR2B = getDutyCycle();
#else
      OCR2A = getDutyCycle();
#endif
    }
    // Reduce our playback duration by one tick
    tickDuration--;
  } else {
    // Hold it low
#ifndef DDS_PWM_PIN_3
    OCR2A = 0;
#else
#ifdef DDS_IDLE_HIGH
    // Set the duty cycle to 50%
    OCR2B = pow(2,COMPARATOR_BITS)/2;
#else
    // Set duty cycle to 0, effectively off
    OCR2B = 0;
#endif
#endif
  }
}

uint8_t DDS::getDutyCycle() {
#if ACCUMULATOR_BIT_SHIFT >= 24
  uint16_t phAng;
#else
  uint8_t phAng;
#endif
  phAng = (accumulator >> ACCUMULATOR_BIT_SHIFT);
  int8_t position = pgm_read_byte_near(ddsSineTable + phAng)>>(8-COMPARATOR_BITS);
  // Apply scaling and return
  int16_t scaled = position;
  // output = ((duty * amplitude) / 256) + 128
  // This keeps amplitudes centered around 50% duty
  scaled *= amplitude;
  scaled >>= 8;
  scaled += 128;
  return scaled;
}

/*
  TemperatureZero.h - Arduino library for internal temperature of the family SAMD -
  Copyright (c) 2018 Electronic Cats.  All right reserved.
  Based in the work of Mitchell Pontague https://github.com/arduino/ArduinoCore-samd/pull/277
*/

#include "Arduino.h"
#include "TemperatureZero.h"

#undef TZ_DEBUG

TemperatureZero::TemperatureZero() {
}

void TemperatureZero::init() {
  getFactoryCalibration();
  wakeup();
}

// After sleeping, the temperature sensor seems disabled. So, let's re-enable it.
void TemperatureZero::wakeup() {
  SYSCTRL->VREF.reg |= SYSCTRL_VREF_TSEN; // Enable the temperature sensor  
  while( ADC->STATUS.bit.SYNCBUSY == 1 ); // Wait for synchronization of registers between the clock domains
}

// Reads temperature using internal ADC channel
// Datasheet chapter 37.10.8 - Temperature Sensor Characteristics
float TemperatureZero::readInternalTemperature() {
   uint16_t adcReading = readInternalTemperatureRaw(TZ_SAMPLES_256);
   return raw2temp(adcReading);
}

#define INT1V_DIVIDER_1000                1000.0
#define ADC_12BIT_FULL_SCALE_VALUE_FLOAT  4095.0

// Get all factory calibration parameters and process them
// This includes both the temperature sensor calibration as well as the 1v reference calibration
void TemperatureZero::getFactoryCalibration() {
   // Factory room temperature readings
  uint8_t roomInteger = (*(uint32_t*)FUSES_ROOM_TEMP_VAL_INT_ADDR & FUSES_ROOM_TEMP_VAL_INT_Msk) >> FUSES_ROOM_TEMP_VAL_INT_Pos;
  uint8_t roomDecimal = (*(uint32_t*)FUSES_ROOM_TEMP_VAL_DEC_ADDR & FUSES_ROOM_TEMP_VAL_DEC_Msk) >> FUSES_ROOM_TEMP_VAL_DEC_Pos;
  _roomTemperature = roomInteger + convert_dec_to_frac(roomDecimal);
  _roomReading = ((*(uint32_t*)FUSES_ROOM_ADC_VAL_ADDR & FUSES_ROOM_ADC_VAL_Msk) >> FUSES_ROOM_ADC_VAL_Pos);
   // Factory hot temperature readings
  uint8_t hotInteger = (*(uint32_t*)FUSES_HOT_TEMP_VAL_INT_ADDR & FUSES_HOT_TEMP_VAL_INT_Msk) >> FUSES_HOT_TEMP_VAL_INT_Pos;
  uint8_t hotDecimal = (*(uint32_t*)FUSES_HOT_TEMP_VAL_DEC_ADDR & FUSES_HOT_TEMP_VAL_DEC_Msk) >> FUSES_HOT_TEMP_VAL_DEC_Pos;
  _hotTemperature = hotInteger + convert_dec_to_frac(hotDecimal);
  _hotReading = ((*(uint32_t*)FUSES_HOT_ADC_VAL_ADDR & FUSES_HOT_ADC_VAL_Msk) >> FUSES_HOT_ADC_VAL_Pos);
  // Factory internal 1V voltage reference readings at both room and hot temperatures
  int8_t roomInt1vRefRaw = (int8_t)((*(uint32_t*)FUSES_ROOM_INT1V_VAL_ADDR & FUSES_ROOM_INT1V_VAL_Msk) >> FUSES_ROOM_INT1V_VAL_Pos);
  int8_t hotInt1vRefRaw  = (int8_t)((*(uint32_t*)FUSES_HOT_INT1V_VAL_ADDR & FUSES_HOT_INT1V_VAL_Msk) >> FUSES_HOT_INT1V_VAL_Pos);
  _roomInt1vRef = 1 - ((float)roomInt1vRefRaw/INT1V_DIVIDER_1000);
  _hotInt1vRef  = 1 - ((float)hotInt1vRefRaw/INT1V_DIVIDER_1000);
  // Combining the temperature dependent 1v reference with the ADC readings
  _roomVoltageCompensated = ((float)_roomReading * _roomInt1vRef)/ADC_12BIT_FULL_SCALE_VALUE_FLOAT;
  _hotVoltageCompensated = ((float)_hotReading * _hotInt1vRef)/ADC_12BIT_FULL_SCALE_VALUE_FLOAT;
#ifdef TZ_DEBUG
    Serial.println(F("\n+++ Factory calibration parameters:"));
    Serial.print(F("Room Temperature : "));
    Serial.println(_roomTemperature, 1);
    Serial.print(F("Hot Temperature  : "));
    Serial.println(_hotTemperature, 1);
    Serial.print(F("Room Reading     : "));
    Serial.println(_roomReading);
    Serial.print(F("Hot Reading      : "));
    Serial.println(_hotReading);
    Serial.print(F("Room Voltage ref raw / interpreted : "));
    Serial.print(roomInt1vRefRaw);
    Serial.print(F(" / "));
    Serial.println(_roomInt1vRef, 4);
    Serial.print(F("Hot Voltage ref raw / interpreted  : "));
    Serial.print(hotInt1vRefRaw);
    Serial.print(F(" / "));
    Serial.println(_hotInt1vRef, 4);
    Serial.print(F("Room Reading compensated : "));
    Serial.println(_roomVoltageCompensated, 4);
    Serial.print(F("Hot Reading compensated  : "));
    Serial.println(_hotVoltageCompensated, 4);
#endif
}

// Extra safe decimal to fractional conversion
float TemperatureZero::convert_dec_to_frac(uint8_t val) {
  if (val < 10) {
    return ((float)val/10.0);
  } else if (val <100) {
    return ((float)val/100.0);
  } else {
    return ((float)val/1000.0);
  }
}

uint16_t TemperatureZero::readInternalTemperatureRaw(uint8_t sample_averaging) {
  // Save ADC settings
  uint16_t oldReadResolution = ADC->CTRLB.reg;
  uint16_t oldSampling = ADC->SAMPCTRL.reg;
  uint16_t oldSampleAveraging = ADC->SAMPCTRL.reg;
  uint16_t oldReferenceGain = ADC->INPUTCTRL.bit.GAIN;
  uint16_t oldReferenceSelect = ADC->REFCTRL.bit.REFSEL;

  // Set to 12 bits resolution
  ADC->CTRLB.reg = ADC_CTRLB_RESSEL_12BIT | ADC_CTRLB_PRESCALER_DIV256;
  // Wait for synchronization of registers between the clock domains
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  // Ensure we are sampling slowly
  ADC->SAMPCTRL.reg = ADC_SAMPCTRL_SAMPLEN(0x3f);
  while (ADC->STATUS.bit.SYNCBUSY == 1);
   // Set ADC reference to internal 1v
  ADC->INPUTCTRL.bit.GAIN = ADC_INPUTCTRL_GAIN_1X_Val;
  ADC->REFCTRL.bit.REFSEL = ADC_REFCTRL_REFSEL_INT1V_Val;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
   // Select MUXPOS as temperature channel, and MUXNEG  as internal ground
  ADC->INPUTCTRL.bit.MUXPOS = ADC_INPUTCTRL_MUXPOS_TEMP_Val;
  ADC->INPUTCTRL.bit.MUXNEG = ADC_INPUTCTRL_MUXNEG_GND_Val; 
  while (ADC->STATUS.bit.SYNCBUSY == 1);
   // Enable ADC
  ADC->CTRLA.bit.ENABLE = 1;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  // Start ADC conversion & discard the first sample
  ADC->SWTRIG.bit.START = 1;
  // Wait until ADC conversion is done, prevents the unexpected offset bug
  while (!(ADC->INTFLAG.bit.RESRDY));
   // Clear the Data Ready flag
  ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY;
  // perform averaging
  switch(sample_averaging) {
    case TZ_SAMPLES_1: 
      ADC->AVGCTRL.reg = 0;
      break;
    case TZ_SAMPLES_2: 
      ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_2 | ADC_AVGCTRL_ADJRES(0x1);
      break;
    case TZ_SAMPLES_4: 
      ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_4 | ADC_AVGCTRL_ADJRES(0x2);
      break;
    case TZ_SAMPLES_8: 
      ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_8 | ADC_AVGCTRL_ADJRES(0x3);
      break;
    case TZ_SAMPLES_16: 
      ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_16 | ADC_AVGCTRL_ADJRES(0x4);
      break;
    case TZ_SAMPLES_32: 
      ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_32 | ADC_AVGCTRL_ADJRES(0x4);
      break;
    case TZ_SAMPLES_64: 
      ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_64 | ADC_AVGCTRL_ADJRES(0x4);
      break;
    case TZ_SAMPLES_128: 
      ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_128 | ADC_AVGCTRL_ADJRES(0x4);
      break;
    case TZ_SAMPLES_256: 
      ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_256 | ADC_AVGCTRL_ADJRES(0x4);
      break;
  }
  while (ADC->STATUS.bit.SYNCBUSY == 1);
   // Start conversion again, since The first conversion after the reference is changed must not be used.
  ADC->SWTRIG.bit.START = 1;
   // Wait until ADC conversion is done
  while (!(ADC->INTFLAG.bit.RESRDY));
  while (ADC->STATUS.bit.SYNCBUSY == 1);
   // Get result
  uint16_t adcReading = ADC->RESULT.reg;
   // Clear result ready flag
  ADC->INTFLAG.reg = ADC_INTFLAG_RESRDY; 
    while (ADC->STATUS.bit.SYNCBUSY == 1);
   // Disable ADC
  ADC->CTRLA.bit.ENABLE = 0; 
  while (ADC->STATUS.bit.SYNCBUSY == 1);
   // Restore pervious ADC settings
  ADC->CTRLB.reg = oldReadResolution;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  ADC->SAMPCTRL.reg = oldSampling;
  ADC->SAMPCTRL.reg = oldSampleAveraging;
  while (ADC->STATUS.bit.SYNCBUSY == 1);
  ADC->INPUTCTRL.bit.GAIN = oldReferenceGain;
  ADC->REFCTRL.bit.REFSEL = oldReferenceSelect;
  while (ADC->STATUS.bit.SYNCBUSY == 1); 
  return adcReading;
}

float TemperatureZero::raw2temp (uint16_t adcReading) {
  // Get course temperature first, in order to estimate the internal 1V reference voltage level at this temperature
  float meaurementVoltage = ((float)adcReading)/ADC_12BIT_FULL_SCALE_VALUE_FLOAT;
  float coarse_temp = _roomTemperature + (((_hotTemperature - _roomTemperature)/(_hotVoltageCompensated - _roomVoltageCompensated)) * (meaurementVoltage - _roomVoltageCompensated));
  // Estimate the reference voltage using the course temperature
  float ref1VAtMeasurement = _roomInt1vRef + (((_hotInt1vRef - _roomInt1vRef) * (coarse_temp - _roomTemperature))/(_hotTemperature - _roomTemperature));
  // Now first compensate the raw adc reading using the estimation of the 1V reference output at current temperature 
  float measureVoltageCompensated = ((float)adcReading * ref1VAtMeasurement)/ADC_12BIT_FULL_SCALE_VALUE_FLOAT;
  // Repeat the temperature interpolation using the compensated measurement voltage
  float result = _roomTemperature + (((_hotTemperature - _roomTemperature)/(_hotVoltageCompensated - _roomVoltageCompensated)) * (measureVoltageCompensated - _roomVoltageCompensated));
#ifdef TZ_DEBUG
    Serial.println(F("\n+++ Temperature calculation:"));
    Serial.print(F("raw adc reading : "));
    Serial.println(adcReading);
    Serial.print(F("Course temperature : "));
    Serial.println(coarse_temp, 1);
    Serial.print(F("Estimated 1V ref @Course temperature : "));
    Serial.println(ref1VAtMeasurement, 4);
    Serial.print(F("Temperature compensated measurement voltage : "));
    Serial.println(measureVoltageCompensated, 4);
    Serial.print(F("Refined temperature : "));
    Serial.println(result, 1);
#endif
  return result;
}


#include "LIS2MDL.h"

LIS2MDL::LIS2MDL(uint8_t comm, uint8_t inputAddress) {
  commMode = comm;
  address = inputAddress;
  
  // default settings
  settings.tempCompensationEnabled = LIS2MDL_TEMP_COMPENSATION_ENABLED;
  settings.rebootMode = LIS2MDL_REBOOT_NORMAL_MODE;
  settings.resetMode = LIS2MDL_NO_RESET_MODE;
  settings.powerMode = LIS2MDL_POWERMODE_HIGH;
  settings.magSampleRate = LIS2MDL_MAG_ODR_10Hz;
  settings.operationMode = LIS2MDL_CONTINUOUS_MODE;

  settings.singleModeOffsetCancellationEnabled = LIS2MDL_SINGLE_MODE_OFF_CANC_DISABLED;
  settings.checkDataAfterHardIronCorrectionEnabled = LIS2MDL_HARD_IRON_CORRECTION_CHECK;
  settings.setPulseFrequency = LIS2MDL_RELEASE_EVERY_63_ODR;
  settings.offsetCancellationEnabled = LIS2MDL_OFFSET_CANCELLATION_DISABLED;
  settings.lowPassFilterEnabled = LIS2MDL_LOW_PASS_FILTER_ENABLED;

  settings.interruptEnabled = LIS2MDL_INTERRUPT_DISABLED;
  settings.i2cDisabled = commMode == SPI_MODE ? LIS2MDL_I2C_DISABLED : LIS2MDL_I2C_ENABLED;
  settings.readSafety = LIS2MDL_SAFE_ASYNC_READ;
  settings.endianness = LIS2MDL_BIG_ENDIAN;
  settings.spiConfig = LIS2MDL_4WIRESPI_DISABLED;
  settings.dataReadyEnabled = LIS2MDL_DATA_READY_DISABLED;

  settings.magSensitivity = 0.0015f;

  allOnesCounter = 0;
  nonSuccessCounter = 0;
}

mag_status_t LIS2MDL::begin() {
  mag_status_t status = wireUp();

  // don't go further if it failed to start up
  if (status != MAG_SUCCESS)
    return status;

  // write settings to device
  writeSettings();
}

mag_status_t LIS2MDL::wireUp() {
  mag_status_t result = MAG_SUCCESS;
  switch(commMode) {
    case I2C_MODE:
      Wire.begin();
      break;
    case SPI_MODE:
      SPI.begin();
      SPI.setClockDivider(SPI_CLOCK_DIV4);
      SPI.setBitOrder(MSBFIRST);

      pinMode(address, OUTPUT);
      digitalWrite(address, HIGH);
      break;
  }

  //Spin for a few ms
	volatile uint8_t temp = 0;
	for( uint16_t i = 0; i < 10000; i++ )
	{
		temp++;
	}

  uint8_t readCheck;
  read(LIS2MDL_WHO_AM_I, &readCheck);
  if (readCheck != deviceId) 
    result = MAG_HW_ERROR;

  return result;
}

void LIS2MDL::calibrate(uint32_t reads) {
  int32_t rawMagBias[3] = {0, 0, 0}, rawMagScale[3] = {0, 0, 0};
  int16_t rawMagMax[3] = {-32767, -32767, -32767}, rawMagMin[3] = {32767, 32767, 32767}, temp[3] = {0, 0, 0};

  // calculate mins and maxes for X/Y/Z axes
  for (int i = 0; i < 4000; i++) {
    temp[0] = readRawMagX();
    temp[1] = readRawMagY();
    temp[2] = readRawMagZ();

    for (int j = 0; j < 3; j++) {
      if (temp[j] > rawMagMax[j]) rawMagMax[j] = temp[j];
      if (temp[j] < rawMagMin[j]) rawMagMin[j] = temp[j];
    }
    delay(12);
  }

  rawMagBias[0]  = (rawMagMax[0] + rawMagMin[0])/2;  // get average x mag bias in counts
  rawMagBias[1]  = (rawMagMax[1] + rawMagMin[1])/2;  // get average y mag bias in counts
  rawMagBias[2]  = (rawMagMax[2] + rawMagMin[2])/2;  // get average z mag bias in counts
  
  settings.magBiasX = (float) rawMagBias[0] * settings.magSensitivity;  // save mag biases in G for main program
  settings.magBiasY = (float) rawMagBias[1] * settings.magSensitivity;
  settings.magBiasZ = (float) rawMagBias[2] * settings.magSensitivity;
      
  // Get soft iron correction estimate
  rawMagScale[0]  = (rawMagMax[0] - rawMagMin[0])/2;  // get average x axis max chord length in counts
  rawMagScale[1]  = (rawMagMax[1] - rawMagMin[1])/2;  // get average y axis max chord length in counts
  rawMagScale[2]  = (rawMagMax[2] - rawMagMin[2])/2;  // get average z axis max chord length in counts

  float avg_rad = rawMagScale[0] + rawMagScale[1] + rawMagScale[2];
  avg_rad /= 3.0f;

  settings.magScaleX = avg_rad/((float)rawMagScale[0]);
  settings.magScaleY = avg_rad/((float)rawMagScale[1]);
  settings.magScaleZ = avg_rad/((float)rawMagScale[2]);
}

int16_t LIS2MDL::readRawMagX() {
  return readInt16(LIS2MDL_OUTX_L_REG);
}

int16_t LIS2MDL::readRawMagY() {
  return readInt16(LIS2MDL_OUTY_L_REG);
}

int16_t LIS2MDL::readRawMagZ() {
  return readInt16(LIS2MDL_OUTZ_L_REG);
}

float LIS2MDL::readFloatMagX() {
  // maxBiasX and magScaleX must be set for this to work*
  return ((float)readRawMagX() * settings.magSensitivity - settings.magBiasX) * settings.magScaleX;
}

float LIS2MDL::readFloatMagY() {
  // maxBiasY and magScaleY must be set for this to work*
  return ((float)readRawMagY() * settings.magSensitivity - settings.magBiasY) * settings.magScaleY;
}

float LIS2MDL::readFloatMagZ() {
  // maxBiasZ and magScaleZ must be set for this to work
  return ((float)readRawMagZ() * settings.magSensitivity - settings.magBiasZ) * settings.magScaleZ;
}

int16_t readRawTemp();
float readTempC();
float readTempF();

mag_status_t LIS2MDL::writeSettings() {
  uint8_t configA = 0;
  uint8_t configB = 0;
  uint8_t configC = 0;

  // set data for register A
  configA |= settings.tempCompensationEnabled;
  configA |= settings.rebootMode;
  configA |= settings.resetMode;
  configA |= settings.powerMode;
  configA |= settings.magSampleRate;
  configA |= settings.operationMode;

  // set data for register B
  configB |= settings.singleModeOffsetCancellationEnabled;
  configB |= settings.checkDataAfterHardIronCorrectionEnabled;
  configB |= settings.setPulseFrequency;
  configB |= settings.offsetCancellationEnabled;
  configB |= settings.lowPassFilterEnabled;

  // set data for register C
  configC |= settings.interruptEnabled;
  configC |= settings.i2cDisabled;
  configC |= settings.readSafety;
  configC |= settings.endianness;
  configC |= settings.spiConfig;
  configC |= settings.selfTestEnabled;
  configC |= settings.dataReadyEnabled;

  // write all registers
  mag_status_t status = write(LIS2MDL_CFG_REG_A, configA);
  if (status == MAG_SUCCESS)
    status = write(LIS2MDL_CFG_REG_B, configB);
  if (status == MAG_SUCCESS)
    status = write(LIS2MDL_CFG_REG_C, configC);

  return status;
}

int16_t LIS2MDL::readInt16(lis2mdlRegisters_t offset) {
  uint8_t buffer[2];
  mag_status_t result = readRegion(offset, buffer, 2);
  int16_t output = (int16_t)buffer[0] | int16_t(buffer[1] << 8);

  if (result != MAG_SUCCESS) {
    if (result == MAG_ALL_ONES_WARNING)
      allOnesCounter++;
    else
      nonSuccessCounter++;
  }

  return output;
}

mag_status_t LIS2MDL::read(lis2mdlRegisters_t offset, uint8_t *output) {
  return readRegion(offset, output, 1);
}

mag_status_t LIS2MDL::readRegion(lis2mdlRegisters_t offset, uint8_t *output, uint8_t length) {
  mag_status_t status = MAG_SUCCESS;
  uint8_t i = 0;
  uint8_t c = 0;

  switch (commMode) {
    case I2C_MODE:
      Wire.beginTransmission(address);
      Wire.write(offset);
      if(Wire.endTransmission() != 0)
        status = MAG_HW_ERROR;
      else {
        Wire.requestFrom(address, length);
        while ((Wire.available()) && (i < length)) {
          c = Wire.read();
          *output = c;
          output++;
          i++;
        }
      }
      break;
    case SPI_MODE:
      digitalWrite(address, LOW);

      SPI.transfer(offset | 0x80);
      while (i < length) {
        c = SPI.transfer(0x00);
        if(c == 0xFF)
          allOnesCounter++;

        *output = c;
        output++;
        i++;
      }
      if(allOnesCounter == i)
        status = MAG_ALL_ONES_WARNING;

      digitalWrite(address, HIGH);
      break;
  }
}

mag_status_t LIS2MDL::write(uint8_t offset, uint8_t data) {
  mag_status_t status = MAG_SUCCESS;
  switch (commMode) {
    case I2C_MODE:
      Wire.beginTransmission(address);
      Wire.write(offset);
      Wire.write(data);
      if (Wire.endTransmission() != 0)
        status = MAG_HW_ERROR;
      break;
    case SPI_MODE:
      digitalWrite(address, LOW);
      SPI.transfer(offset);
      SPI.transfer(data);
      digitalWrite(address, HIGH);
      break;
    default:
      break;
  }
  return status;
}
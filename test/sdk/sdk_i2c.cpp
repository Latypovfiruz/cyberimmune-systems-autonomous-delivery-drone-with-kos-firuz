#include "sdk_i2c.h"
#include <bsp/bsp.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

bool i2cInit = false;
I2cHandle i2cH = NULL;
uint8_t compassAddress = 0x0D;
uint32_t compassFrequency = 100000;
float magneticDeclination = 0.0f;
float compassCalibrationOffsets[3] = { 0.0f, 0.0f, 0.0f };
float compassCalibrationScales[3] = { 1.0f, 1.0f, 1.0f };

CompassReading::CompassReading(uint8_t v1, uint8_t v2, uint8_t v3, uint8_t v4, uint8_t v5, uint8_t v6) {
    X = (int)(int16_t)(v1 | (v2 << 8));
	Y = (int)(int16_t)(v3 | (v4 << 8));
	Z = (int)(int16_t)(v5 | (v6 << 8));
}

void CompassReading::calibrate(float* offsets, float* scales) {
    X = (X - offsets[0]) * scales[0];
    Y = (Y - offsets[1]) * scales[1];
    Z = (Z - offsets[2]) * scales[2];
}

int startI2C(char* channelName) {
    if (!i2cInit) {
        char configName[128];
        snprintf(configName, 128, "%s.%s", BOARD_CONFIG, "p0-1");
        Retcode rc = BspEnableModule(channelName);
        if (rc != rcOk) {
            fprintf(stderr, "Failed to enable '%s' module\n", channelName);
            return 0;
        }
        rc = BspSetConfig(channelName, configName);
        if (rc != rcOk) {
            fprintf(stderr, "Failed to set pmux configuration for '%s' channel\n", channelName);
            return 0;
        }
        rc = I2cInit();
        if (rc != rcOk) {
            fprintf(stderr, "Failed to initialize I2C\n");
            return 0;
        }
        i2cInit = true;
    }
    Retcode rc = I2cOpenChannel(channelName, &i2cH);
    if (rc != rcOk)
    {
        fprintf(stderr, "Failed to open '%s' channel\n", channelName);
        return 0;
    }
    return 1;
}

int initializeCompass(uint8_t mode, uint8_t odr, uint8_t rng, uint8_t osr) {
    uint8_t flags = mode | odr | rng | osr;

    I2cMsg messages[2];
    uint8_t bufInit[2] = { 0x0B, 0x01 };
    uint8_t bufMode[2] = { 0x09, flags };

    messages[0].addr = compassAddress;
    messages[0].flags = 0;
    messages[0].buf = bufInit;
    messages[0].len = 2;

    messages[1].addr = compassAddress;
    messages[1].flags = 0;
    messages[1].buf = bufMode;
    messages[1].len = 2;

    Retcode rc = I2cXfer(i2cH, compassFrequency, messages, 2);
    if (rc != rcOk) {
        fprintf(stderr, "Failed to write to compass\n");
        return 0;
    }

    return 1;
}

int readCompass(CompassReading& res) {
    I2cMsg messages[2];
    uint8_t writeBuffer[] = { 0x00 };
    uint8_t readBuffer[6];

    messages[0].addr = compassAddress;
    messages[0].flags = 0;
    messages[0].buf = writeBuffer;
    messages[0].len = 1;

    messages[1].addr = compassAddress;
    messages[1].flags = I2C_FLAG_RD;
    messages[1].buf = readBuffer;
    messages[1].len = 6;

    I2cError rc = I2cXfer(i2cH, compassFrequency, messages, 2);
    if (rc != I2C_EOK) {
        fprintf(stderr, "Failed to write\n");
        return 0;
    }

    res = CompassReading(readBuffer[0], readBuffer[1], readBuffer[2], readBuffer[3], readBuffer[4], readBuffer[5]);

    res.calibrate(compassCalibrationOffsets, compassCalibrationScales);

    return 1;
}

void setCompassAddress(uint8_t address) {
    compassAddress = address;
}

void setCompassFrequency(uint32_t frequency) {
    compassFrequency = frequency;
}

void setMagneticDeclination(int degrees, int minutes) {
    magneticDeclination = degrees + minutes / 60.0f;
}

void calibrate() {
    for (int i = 0; i < 3; i++) {
        compassCalibrationOffsets[i] = 0.0f;
        compassCalibrationScales[i] = 1.0f;
    }

    CompassReading r;
    if (!readCompass(r)) {
        fprintf(stderr, "Failed to calibrate the compass\n");
        return;
    }

    int32_t calibrationData[3][2] = { { r.X, r.X }, { r.Y, r.Y }, { r.Z, r.Z } };
    clock_t startTime = clock();
    while (((double)(clock() - startTime) / CLOCKS_PER_SEC * 1000) < 10000) {
        if (!readCompass(r)) {
            fprintf(stderr, "Failed to calibrate the compass\n");
            return;
        }

		if(r.X < calibrationData[0][0])
			calibrationData[0][0] = r.X;
		if(r.X > calibrationData[0][1])
			calibrationData[0][1] = r.X;
		if(r.Y < calibrationData[1][0])
			calibrationData[1][0] = r.Y;
		if(r.Y > calibrationData[1][1])
			calibrationData[1][1] = r.Y;
		if(r.Z < calibrationData[2][0])
			calibrationData[2][0] = r.Z;
		if(r.Z > calibrationData[2][1])
			calibrationData[2][1] = r.Z;
    }

    float delta = 0;
    for (int i = 0; i < 3; i++) {
        compassCalibrationOffsets[i] = (calibrationData[i][0] + calibrationData[i][1]) / 2.0f;
        compassCalibrationScales[i] = (calibrationData[i][1] - calibrationData[i][0]) / 2.0f;
        delta += compassCalibrationScales[i];
    }
    delta /= 3;
    for (int i = 0; i < 3; i++)
        compassCalibrationScales[i] = delta / compassCalibrationScales[i];

    fprintf(stderr, "DEBUG: Calibration offsets: %f, %f, %f\n", compassCalibrationOffsets[0], compassCalibrationOffsets[1], compassCalibrationOffsets[2]);
    fprintf(stderr, "DEBUG: Calibration scales: %f, %f, %f\n", compassCalibrationScales[0], compassCalibrationScales[1], compassCalibrationScales[2]);
}

int getAzimuth() {
    CompassReading r;
    if (!readCompass(r)) {
        fprintf(stderr, "Failed to get an azimuth\n");
        return NULL;
    }
    float azimuth = (float)(atan2(r.Y, r.X) * 180.0 / M_PI);
    azimuth += magneticDeclination;
    while (azimuth < 0)
        azimuth += 360;
	return ((int)azimuth % 360);
}
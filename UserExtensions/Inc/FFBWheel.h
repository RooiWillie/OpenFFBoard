/*
 * FFBWheel.h
 *
 *  Created on: 31.01.2020
 *      Author: Yannick
 */

#ifndef SRC_FFBWHEEL_H_
#define SRC_FFBWHEEL_H_
#include <FFBoardMain.h>
#include "TMC4671.h"
#include "flash_helpers.h"
#include "ButtonSource.h"
#include "LocalButtons.h"
#include "SPIButtons.h"
#include "cppmain.h"
#include "HidFFB.h"

enum class EncoderType : uint8_t{
	ABN_LOCAL = 0,ABN_TMC=1,HALL_TMC=2,NONE
};

enum class MotorDriverType : uint8_t{
	TMC4671_type=0,PPM_type=1,NONE // Only tmc implemented
};

enum class ButtonSourceType : uint8_t{
	LOCAL=0,SPI_TM=1,I2C=2,NONE // Only local implemented
};

struct FFBWheelConfig{
	MotorDriverType drvtype=MotorDriverType::TMC4671_type;
	EncoderType enctype=EncoderType::ABN_TMC;
	ButtonSourceType btcsrctype=ButtonSourceType::LOCAL;
};

enum class AnalogOffset : uint8_t{
	FULL=0,LOWER=1,UPPER=2,NONE
};

struct FFBWheelAnalogConfig{
	uint8_t analogmask = 0xff;
	AnalogOffset offsetmode;

};

class FFBWheel: public FFBoardMain , public UsbHidHandler {
public:
	FFBWheel();
	virtual ~FFBWheel();

	static FFBoardMainIdentifier info;
	const FFBoardMainIdentifier getInfo();

	void setupTMC4671();
	void setupTMC4671_enc(EncoderType enctype);
	bool executeUserCommand(ParsedCommand* cmd,std::string* reply);

	void setDrvType(MotorDriverType drvtype);
	void setEncType(EncoderType enctype);
	void setBtnType(ButtonSourceType btntype);

	void SOF();
	void usbInit(); // initialize a composite usb device

	void saveFlash();

	void update();

	static FFBWheelConfig decodeConfFromInt(uint16_t val);
	static uint16_t encodeConfToInt(FFBWheelConfig conf);
	static FFBWheelAnalogConfig decodeAnalogConfFromInt(uint16_t val);
	static uint16_t encodeAnalogConfToInt(FFBWheelAnalogConfig conf);


	void adcUpd(volatile uint32_t* ADC_BUF);
	void timerElapsed(TIM_HandleTypeDef* htim);

	bool usb_update_flag = false;
	bool update_flag = false;

	uint16_t degreesOfRotation = 900; // TODO save in flash
	int32_t getEncValue(Encoder* enc,uint16_t degrees);

	uint16_t power = 0xffff;


private:
	void send_report();
	int16_t updateEndstop();

	HidFFB* ffb;
	TIM_HandleTypeDef* timer_update;
	int32_t torque = 0; // last torque
	int32_t endstopTorque = 0; // last torque
	FFBWheelConfig conf;
	MotorDriver* drv;
	Encoder* enc;
	ButtonSource* btn = nullptr;
	uint16_t buttonMask = 0xffff;
	FFBWheelAnalogConfig aconf;
	volatile uint16_t adc_buf[ADC_PINS];
	reportHID_t reportHID;

	int32_t lastScaledEnc = 0;
	int32_t scaledEnc = 0;
	int32_t speed = 0;
	bool tmcFeedForward = true;


	TMC4671PIDConf tmcpids = TMC4671PIDConf({
		.fluxI		= 1300,
		.fluxP		= 256,
		.torqueI	= 2000,
		.torqueP	= 700,
		.velocityI	= 0,
		.velocityP	= 128,
		.positionI	= 0,
		.positionP	= 64
	});
	int32_t torqueFFgain = 500000;
	int32_t torqueFFconst = 0;
	int32_t velocityFFgain = 300000;
	int32_t velocityFFconst = 0;
};

#endif /* SRC_FFBWHEEL_H_ */

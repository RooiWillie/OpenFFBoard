/*
 * FFBWheel.cpp
 *
 *  Created on: 31.01.2020
 *      Author: Yannick
 */

#include "FFBWheel.h"
#include "FFBWheel_usb_init.h"
#include "voltagesense.h"

FFBoardMainIdentifier FFBWheel::info = {
		 .name = "FFB Wheel" ,
		 .id=1
 };

const FFBoardMainIdentifier FFBWheel::getInfo(){
	return info;
}


FFBWheel::FFBWheel() {
	// Setup a timer
	extern TIM_HandleTypeDef htim4;
	this->timer_update = &htim4; // Timer setup with prescaler of sysclock
	this->timer_update->Instance->ARR = 100;
	this->timer_update->Instance->CR1 = 1;
	HAL_TIM_Base_Start_IT(this->timer_update);

	// read all constants
	uint16_t confint;
	if(Flash_Read(ADR_FFBWHEEL_CONFIG, &confint)){
		this->conf = FFBWheel::decodeConfFromInt(confint);
	}else{
		pulseErrLed();
	}


	setDrvType(this->conf.drvtype);
	setBtnType(this->conf.btcsrctype);

	uint16_t ppr = 0;
	if(Flash_Read(ADR_TMC1_PPR, &ppr)){
		this->enc->setPpr(ppr);
	}else{
		pulseErrLed();
	}

	Flash_Read(ADR_FFBWHEEL_POWER, &this->power);
	Flash_Read(ADR_FFBWHEEL_BUTTONMASK, &this->buttonMask);
	uint16_t aconfint;
	if(Flash_Read(ADR_FFBWHEEL_ANALOGCONF,&aconfint)){
		this->aconf = FFBWheel::decodeAnalogConfFromInt(aconfint);
	}


	// Home


}

void FFBWheel::saveFlash(){
	if(this->conf.drvtype == MotorDriverType::TMC4671_type){
		TMC4671* drv = static_cast<TMC4671*>(this->drv);
		//save motor config
		uint16_t motint = TMC4671::encodeMotToInt(drv->conf.motconf);
		Flash_Write(ADR_TMC1_POLES_MOTTYPE_PHIE, motint);


	}
	Flash_Write(ADR_TMC1_PPR, enc->getPpr());
	Flash_Write(ADR_FFBWHEEL_POWER, this->power);
	Flash_Write(ADR_FFBWHEEL_BUTTONMASK,this->buttonMask);
	Flash_Write(ADR_FFBWHEEL_ANALOGCONF, FFBWheel::encodeAnalogConfToInt(this->aconf));

}

void FFBWheel::update(){
	torque = 0;
	bool updateTorque = false;

	if(usb_update_flag){
		updateTorque = true;
		usb_update_flag = false;
		torque = calculateEffects();

		// Read buttons
		// Read Encoder
		// Send report
		this->send_report();

		// Check if motor driver is even ready
		if(this->conf.drvtype == MotorDriverType::TMC4671_type){
			TMC4671* drv = static_cast<TMC4671*>(this->drv);

			if(getIntV() < 8000){ // low voltage
				drv->initialized = false;
			}else{
				if(!drv->initialized){
					drv->initialize();
				}
			}
		}
	}
	// Calculate endstop effect
	if(update_flag){
		update_flag = false;

		lastRawEnc = getEncValue();
		if(abs(lastRawEnc) > 0xffff){
			// We are way off. Shut down
			drv->stop();
			pulseErrLed();
		}
		int16_t endstopTorque = updateEndstop();
		if(endstopTorque != 0){
			updateTorque = true;
			torque += endstopTorque;
		}
	}

	if(updateTorque){
		// Update torque
		if(abs(torque) >= this->power){
			pulseClipLed();
		}
		//Invert direction for now
		torque = -clip<int16_t,int16_t>(torque, -this->power, this->power);
		drv->turn(torque);
	}
}


int16_t FFBWheel::updateEndstop(){
	int8_t clipval = cliptest(lastRawEnc, -0x7fff, 0x7fff);
	if(clipval == 0){
		return 0;
	}
	int32_t addtorque = 500;

	addtorque = clip<int32_t,int32_t>(abs(lastRawEnc) - 0x7fff,-0x7fff,0x7fff);
	addtorque *= 10;
	addtorque *= clipval;


	return clip(addtorque,-0x7fff,0x7fff);
}



void FFBWheel::setupTMC4671(){
	this->conf.drvtype = MotorDriverType::TMC4671_type;
	TMC4671MainConfig tmcconf;
	uint16_t mconfint;
	TMC4671MotConf motconf;
	if(Flash_Read(ADR_TMC1_POLES_MOTTYPE_PHIE, &mconfint)){
		motconf = TMC4671::decodeMotFromInt(mconfint);
	}

	tmcconf.motconf = motconf;
	this->drv = new TMC4671(&HSPIDRV,SPI1_SS1_GPIO_Port,SPI1_SS1_Pin,tmcconf);
	TMC4671* drv = static_cast<TMC4671*>(this->drv);
	drv->initialize();

	setupTMC4671_enc(this->conf.enctype);

	drv->setMotionMode(MotionMode::stop);

}
FFBWheelConfig FFBWheel::decodeConfFromInt(uint16_t val){
	// 0-2 ENC, 3-5 DRV, 6-8 BTN
	FFBWheelConfig conf;
	conf.enctype = EncoderType((val) & 0x07);
	conf.drvtype = MotorDriverType((val >> 3) & 0x07);
	conf.btcsrctype = ButtonSourceType((val >> 6) & 0x07);
	return conf;
}
uint16_t FFBWheel::encodeConfToInt(FFBWheelConfig conf){
	uint16_t val = (uint8_t)conf.enctype & 0x7;
	val |= ((uint8_t)conf.drvtype & 0x7) << 3;
	val |= ((uint8_t)conf.btcsrctype & 0x7) << 6;
	return val;
}
FFBWheelAnalogConfig FFBWheel::decodeAnalogConfFromInt(uint16_t val){
	FFBWheelAnalogConfig aconf;
	aconf.analogmask = val & 0xff;
	aconf.offsetmode = AnalogOffset((val >> 8) & 0x3);
	return aconf;
}
uint16_t FFBWheel::encodeAnalogConfToInt(FFBWheelAnalogConfig conf){
	uint16_t val = conf.analogmask & 0xff;
	val |= (conf.analogmask & 0x3) << 8;
	return val;
}



void FFBWheel::setupTMC4671_enc(EncoderType enctype){
	this->conf.enctype = enctype;
	TMC4671* drv = static_cast<TMC4671*>(this->drv);
	this->enc = static_cast<Encoder*>(drv);

	//TODO
	if(this->conf.enctype == EncoderType::ABN_TMC){
		uint16_t ppr = 0;
		Flash_Read(ADR_TMC1_PPR, &ppr);
		TMC4671ABNConf encconf;
		encconf.ppr = ppr;
		bool npol = drv->findABNPol();
		encconf.apol = npol;
		encconf.bpol = npol;
		encconf.npol = npol;
		drv->setup_ABN_Enc(encconf);
		drv->setPhiEtype(PhiE::abn);

	}else if(this->conf.enctype == EncoderType::HALL_TMC){
		TMC4671HALLConf hallconf;
		drv->setup_HALL(hallconf);
		drv->setPhiEtype(PhiE::hall);
	}
}

void FFBWheel::setDrvType(MotorDriverType drvtype){
	if(drvtype < MotorDriverType::NONE){
		this->conf.drvtype = drvtype;
	}
	if(this->conf.drvtype == MotorDriverType::TMC4671_type){
		setupTMC4671();
	}
}

void FFBWheel::setEncType(EncoderType enctype){
	if(this->conf.drvtype == MotorDriverType::TMC4671_type){
		setupTMC4671_enc(enctype);
	}
}

void FFBWheel::setBtnType(ButtonSourceType btntype){
	this->conf.btcsrctype = btntype;
	if(btntype == ButtonSourceType::LOCAL){
		this->btn = new LocalButtons();
	}
}



FFBWheel::~FFBWheel() {

}

bool FFBWheel::executeUserCommand(ParsedCommand* cmd,std::string* reply){
	bool flag = true;
	// ------------ General commands ----------------
	if(cmd->cmd == "save"){
		this->saveFlash();
	}else if(cmd->cmd == "drvtype"){
		if(cmd->type == get){
			*reply+=std::to_string((uint8_t)this->conf.drvtype);
		}else if(cmd->type == set && cmd->val < (uint8_t)MotorDriverType::NONE){
			setDrvType(MotorDriverType(cmd->val));
		}else{
			*reply += "TMC4671_type=0,PPM_type=1";
		}
	}else if(cmd->cmd == "power"){
		if(cmd->type == get){
			*reply+=std::to_string(power);
		}else if(cmd->type == set){
			this->power = cmd->val;
		}
	}else if(cmd->cmd == "degrees"){
		if(cmd->type == get){
			*reply+=std::to_string(this->degreesOfRotation);
		}else if(cmd->type == set){
			this->degreesOfRotation = cmd->val;
		}
	}else if(cmd->cmd == "enctype"){
		if(cmd->type == get){
			*reply+=std::to_string((uint8_t)this->conf.enctype);
		}else if(cmd->type == set && cmd->val < (uint8_t)EncoderType::NONE){
			setEncType(EncoderType(cmd->val));
		}else{
			*reply += "ABN_LOCAL=0,ABN_TMC=1,HALL_TMC=2";
		}
	}else if(cmd->cmd == "ppr"){
		if(cmd->type == get){
			*reply+=std::to_string(this->enc->getPpr());
		}else if(cmd->type == set && this->enc != nullptr){
			this->enc->setPpr(cmd->val);
		}else{
			*reply += "Err. Setup enctype first";
		}
	}else if(cmd->cmd == "pos"){
		if(cmd->type == get){
			*reply+=std::to_string(this->enc->getPos());
		}else if(cmd->type == set && this->enc != nullptr){
			this->enc->setPos(cmd->val);
		}else{
			*reply += "Err. Setup enctype first";
		}
	}else if(cmd->cmd == "btntype"){
		if(cmd->type == get){
			*reply+=std::to_string((uint8_t)this->conf.btcsrctype);
		}else if(cmd->type == set && cmd->val < (uint8_t)ButtonSourceType::NONE){
			setBtnType(ButtonSourceType(cmd->val));
		}else{
			*reply += "LOCAL=0,SPI_TM=1,I2C=2";
		}
	}else if(cmd->cmd == "help"){
		*reply += "<HELP>"; // TODO
	}

	// ----------- TMC 4671 specific commands-------------
	if(this->conf.drvtype == MotorDriverType::TMC4671_type){
		TMC4671* drv = static_cast<TMC4671*>(this->drv);
		if(cmd->cmd == "mtype"){
			if(cmd->type == get){
				*reply+=std::to_string((uint8_t)drv->conf.motconf.motor_type);
			}else if(cmd->type == set && cmd->type < (uint8_t)MotorType::ERR){
				drv->setMotorType((MotorType)cmd->val, drv->conf.motconf.pole_pairs);
			}

		}else if(cmd->cmd == "encalign"){
			if(cmd->type == get){
				drv->bangInitABN(3000);
			}else if(cmd->type == set){
				drv->bangInitABN(cmd->val);
			}
		}else if(cmd->cmd == "poles"){
			if(cmd->type == get){
				*reply+=std::to_string(drv->conf.motconf.pole_pairs);
			}else if(cmd->type == set){
				drv->setMotorType(drv->conf.motconf.motor_type,cmd->val);
			}

		}else if(cmd->cmd == "phiesrc"){
			if(cmd->type == get){
				*reply+=std::to_string((uint8_t)drv->conf.motconf.phiEsource);
			}else if(cmd->type == set){
				drv->setPhiEtype((PhiE)cmd->val);
			}

		}else if(cmd->cmd == "reg"){
			if(cmd->type == getat){
				*reply+=std::to_string(drv->readReg(cmd->val));
			}else if(cmd->type == setat){
				drv->writeReg(cmd->adr,cmd->val);
			}

		}else{
			flag=false;
		}
	}

	return flag;
}

void FFBWheel::adcUpd(volatile uint32_t* ADC_BUF){
	for(uint8_t i = 0;i<ADC_PINS;i++){
		this->adc_buf[i] = ADC_BUF[i+ADC_CHAN_FPIN];

		if(this->aconf.offsetmode == AnalogOffset::LOWER){
			this->adc_buf[i] = clip(this->adc_buf[i] << 5, 0, 0xfff);
		}else if(this->aconf.offsetmode == AnalogOffset::UPPER){
			this->adc_buf[i] = clip(this->adc_buf[i] - 0x7ff, 0, 0xfff) << 5;
		}

	}
}

int32_t FFBWheel::getEncValue(){
	float angle = 360.0*((float)enc->getPos()/(float)enc->getPosCpr());
	int32_t val = (0xffff / (float)degreesOfRotation) * angle;
	return val;
}


void FFBWheel::send_report(){
	extern USBD_HandleTypeDef hUsbDeviceFS;

	// Read buttons
	uint16_t buttons = 0;
	btn->readButtons((uint8_t*)&buttons, btn->getBtnNum());
	reportHID.buttons = buttons & buttonMask;
	// Encoder
	reportHID.X = clip(lastRawEnc,-0x7fff,0x7fff);
	// Analog values read by DMA
	uint16_t analogMask = this->aconf.analogmask;
	reportHID.Y 	=  (analogMask & 0x01) ? ((adc_buf[0] & 0xFFF) << 4)-0x7fff : 0;
	reportHID.Z		=  (analogMask & 0x01<<1) ? ((adc_buf[1] & 0xFFF) << 4)-0x7fff : 0;
	reportHID.RX	=  (analogMask & 0x01<<2) ? ((adc_buf[2] & 0xFFF) << 4)-0x7fff : 0;
	reportHID.RY	=  (analogMask & 0x01<<3) ? ((adc_buf[3] & 0xFFF) << 4)-0x7fff : 0;
	reportHID.RZ	=  (analogMask & 0x01<<4) ? ((adc_buf[4] & 0xFFF) << 4)-0x7fff : 0;
	reportHID.Slider=  (analogMask & 0x01<<5) ? ((adc_buf[5] & 0xFFF) << 4)-0x7fff : 0;

	USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, reinterpret_cast<uint8_t*>(&reportHID), sizeof(reportHID_t));

}

void FFBWheel::timerElapsed(TIM_HandleTypeDef* htim){
	if(htim == this->timer_update){
		update_flag = true;
	}
}

void FFBWheel::usbInit(){
	usbInit_HID_Wheel();
}
void FFBWheel::SOF(){
	usb_update_flag = true;
	// USB clocked update callback
}

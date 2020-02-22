/*
 * FFBoardMain.cpp
 *
 *  Created on: 23.01.2020
 *      Author: Yannick
 */

#include "FFBoardMain.h"

#include "usbd_core.h"
#include "usbd_cdc.h"
#include "constants.h"
#include "usbd_composite.h"
#include "usbd_desc.h"
#include "mainclass_chooser.h"
#include "eeprom_addresses.h"
#include "flash_helpers.h"
#include "eeprom.h"
#include "voltagesense.h"

#include "ClassChooser.h"
extern ClassChooser<FFBoardMain> mainchooser;


ClassIdentifier FFBoardMain::info ={.name = "Basic" , .id=0};

FFBoardMain::FFBoardMain() {


}

const ClassIdentifier FFBoardMain::getInfo(){
	return info;
}




void FFBoardMain::cdcRcv(char* Buf, uint32_t *Len){
	if(this->parser.add(Buf, Len)){
		executeCommands(this->parser.parse());
	}
}

bool FFBoardMain::executeUserCommand(ParsedCommand *cmd,std::string* reply){

	return false;
}

bool FFBoardMain::executeSysCommand(ParsedCommand* cmd,std::string* reply){
	bool flag = true;
	if(cmd->cmd == "help"){
		*reply += parser.helpstring;
		*reply += "\nSystem Commands: reboot,help,swver (Version),lsconf (List configs),id,config (Set config),vint,vext,format (Erase flash)\n";
		flag = false; // Continue to user commands
	}else if(cmd->cmd == "reboot"){
		NVIC_SystemReset();

	}else if(cmd->cmd == "vint"){
		if(cmd->type==CMDtype::get){
			*reply+=std::to_string(getIntV());
		}

	}else if(cmd->cmd == "vext"){
		if(cmd->type==CMDtype::get){
			*reply+=std::to_string(getExtV());
		}

	}else if(cmd->cmd == "swver"){
		*reply += std::to_string(SW_VERSION);

	}else if(cmd->type!=CMDtype::set &&cmd->cmd == "lsconf"){
		*reply += mainchooser.printAvailableClasses();

	}else if(cmd->cmd == "id"){
		*reply+=std::to_string(this->getInfo().id);
	}else if(cmd->cmd == "config"){
		if(cmd->type == CMDtype::get || cmd->type == CMDtype::none){
			uint16_t buf=this->getInfo().id;
			Flash_Read(ADR_CURRENT_CONFIG, &buf);
			*reply+=std::to_string(buf);

		}else if(cmd->type == CMDtype::set){
			if(mainchooser.isValidClassId(cmd->val)){
				Flash_Write(ADR_CURRENT_CONFIG, (uint16_t)cmd->val);
				if(cmd->val != this->getInfo().id){
					NVIC_SystemReset(); // Reboot
				}
			}else if(cmd->type == CMDtype::err){
				*reply += "Err";
			}
		}
	}else if(cmd->cmd == "format"){
		if(cmd->type == CMDtype::set && cmd->val==1){
			EE_Format();
		}else{
			*reply += "format=1 will ERASE ALL stored variables. Be careful!";
		}
	}else{
		flag = false;
	}
	// Append newline if reply is not empty

	return flag;
}

void FFBoardMain::executeCommands(std::vector<ParsedCommand> commands){
	std::string reply;
	for(ParsedCommand cmd : commands){
		if(!executeSysCommand(&cmd,&reply)){
			executeUserCommand(&cmd,&reply);
		}
		if(!reply.empty() && reply.back()!='\n'){
			reply+='\n';
		}
	}
	if(reply.length()>0){
		CDC_Transmit_FS(reply.c_str(), reply.length());
	}
}


void FFBoardMain::usbInit(){

	USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
	USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC);
	USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS);
	USBD_Start(&hUsbDeviceFS);
}

// Virtual stubs
void FFBoardMain::update(){

}


void FFBoardMain::SOF(){

}


FFBoardMain::~FFBoardMain() {
	// TODO Auto-generated destructor stub
}


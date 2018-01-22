#include <SPI.h>
#include <WiFi101.h>
#include <WiFiUdp.h>

unsigned long timeToGoAround = 9000; 

int currentByte = 0;
int currentFrame = 0;
int currentImage = 0;

byte numOfImages = 1;
byte FPR = 90;
byte pauseTime = 0;

byte*** Images;

byte* inputBuffer;

char ssid[] = "(SSID)";
char pass[] = "(PASSORD)";		
unsigned int port = 1337;			
WiFiUDP Udp;

void setup() {

	Serial.begin(9600);
	int status = WL_IDLE_STATUS;
	while ( status != WL_CONNECTED) {
		status = WiFi.begin(ssid, pass);
		Serial.println("Trying to connect...");
		delay(10000);
	}
	IPAddress ip = WiFi.localIP();
	Serial.println(ip);
	
	Udp.begin(1337);


	// ARM C for timer setup

	REG_GCLK_CLKCTRL = (GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TCC0_TCC1) ; 
	while ( GCLK->STATUS.bit.SYNCBUSY == 1 ); 

	TCC0->CTRLA.reg |= TCC_CTRLA_PRESCALER_DIV256;	 
	TCC0->CTRLA.reg |= TCC_CTRLA_PRESCSYNC_RESYNC; 

	TCC0->WAVE.reg |= TCC_WAVE_WAVEGEN_MFRQ;
	while (TCC0->SYNCBUSY.bit.WAVE == 1);

	TCC0->CC[0].reg = 46875 / 3;
	while (TCC0->SYNCBUSY.bit.CC0 == 1); 

	TCC0->INTENSET.reg = 0;
	TCC0->INTENSET.bit.MC0 = 1;

	NVIC_EnableIRQ(TCC0_IRQn);

	TCC1->CTRLA.reg |= TCC_CTRLA_PRESCALER_DIV256;	
	TCC1->CTRLA.reg |= TCC_CTRLA_PRESCSYNC_RESYNC;	

	TCC1->WAVE.reg |= TCC_WAVE_WAVEGEN_MFRQ;	
	while (TCC1->SYNCBUSY.bit.WAVE == 1);

	TCC1->CC[0].reg = 46875 / 2;
	while (TCC1->SYNCBUSY.bit.CC0 == 1); 

	TCC1->INTENSET.reg = 0;
	TCC1->INTENSET.bit.MC0 = 1;

	NVIC_EnableIRQ(TCC1_IRQn);

	REG_GCLK_CLKCTRL = (GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TCC2_TC3) ;
	while ( GCLK->STATUS.bit.SYNCBUSY == 1 );

	TCC2->CTRLA.reg |= TCC_CTRLA_PRESCALER_DIV256;	 
	TCC2->CTRLA.reg |= TCC_CTRLA_PRESCSYNC_RESYNC; 

	TCC2->WAVE.reg |= TCC_WAVE_WAVEGEN_MFRQ;	
	while (TCC2->SYNCBUSY.bit.WAVE == 1); 

	TCC2->CC[0].reg = 0; 
	while (TCC2->SYNCBUSY.bit.CC0 == 1); 

	TCC2->INTENSET.reg = 0;
	TCC2->INTENSET.bit.MC0 = 1;

	NVIC_EnableIRQ(TCC2_IRQn);
	
	// End of ARM C for timer setup
	

	for (int pin = 0; pin < 6; pin++) {
		pinMode(pin, OUTPUT);
		digitalWrite(pin, LOW);
	}

	Images = new byte**[numOfImages];
	for (int image = 0; image < numOfImages; image++) {
		Images[image] = new byte*[FPR];
		for (int pos = 0; pos < FPR; pos++) {
			Images[image][pos] = new byte[6];
			for (int value = 0; value < 6; value++) {
				Images[image][pos][value]= 0;
			}
		}
	}

	inputBuffer = new byte[256];
	for (int b = 0; b < 256; b++) {
		inputBuffer[b] = 0;
	}
	
	SPI.begin();
	SPI.beginTransaction(SPISettings(1000000, LSBFIRST, SPI_MODE0));

	attachInterrupt(6, reset, FALLING);
}

void enableTimers() {
	TCC0->CTRLA.reg |= TCC_CTRLA_ENABLE ;
	while (TCC0->SYNCBUSY.bit.ENABLE == 1); 

	TCC1->CTRLA.reg |= TCC_CTRLA_ENABLE ;
	while (TCC1->SYNCBUSY.bit.ENABLE == 1); 

	TCC2->CTRLA.reg |= TCC_CTRLA_ENABLE ;
	while (TCC2->SYNCBUSY.bit.ENABLE == 1); 

	attachInterrupt(6, reset, FALLING);
}

void disableTimers() {
	TCC0->CTRLA.bit.ENABLE = 0;
	while (TCC0->SYNCBUSY.bit.ENABLE == 1);
	
	TCC1->CTRLA.bit.ENABLE = 0; 
	while (TCC1->SYNCBUSY.bit.ENABLE == 1);
	
	TCC2->CTRLA.bit.ENABLE = 0;
	while (TCC2->SYNCBUSY.bit.ENABLE == 1);
	detachInterrupt(6);
}


void TCC0_Handler() {
	if (currentFrame == FPR) {
		currentFrame = 0;
	}
	outputData(Images[currentImage][currentFrame]);
	currentFrame++;
	TCC0->INTFLAG.bit.MC0 = 1;
}

void TCC1_Handler() {
	if (currentImage == numOfImages - 1) {
		currentImage = 0;
	}
	else {
		currentImage++;
	}
	TCC1->INTFLAG.bit.MC0 = 1;
}

void TCC2_Handler() {
	timeToGoAround++;
	TCC2->INTFLAG.bit.MC0 = 1;
}


void outputData(byte* data) {
	for (int dataNum = 0; dataNum < 6; ++dataNum) {
		digitalWrite(dataNum, HIGH);
		SPI.transfer(data[dataNum]);
		digitalWrite(dataNum, LOW);
	}
}

void reset() {
	TCC0->CTRLBSET.reg |= TCC_CTRLBSET_CMD_RETRIGGER;
	TCC0->CC[0].reg = timeToGoAround / FPR + 48000000 / 256 / 14 / 900; 
	currentFrame = 0;
	TCC2->CTRLBSET.reg |= TCC_CTRLBSET_CMD_RETRIGGER;
	timeToGoAround = 0;
}

void awaitNextPacketByte() {
	int packetSize = Udp.parsePacket();
	while (true) {
		packetSize = Udp.parsePacket();
		if (packetSize)
		{
			disableTimers();
			delete(inputBuffer);
			inputBuffer = new byte[256];
			Udp.read(inputBuffer, 256);
			break;
		}
	}
}

void deleteOldImages() {
	for (int image = 0; image < numOfImages; image++) {
		for (int pos = 0; pos < FPR; pos++) {
			delete(Images[image][pos]);
		}
		delete(Images[image]);
	}
	delete(Images); 
}

void getImages() {
	awaitNextPacketByte();
	deleteOldImages();
	FPR = inputBuffer[0];
	TCC0->CC[1].reg = (int)(48000000 * (((double)inputBuffer[1] + 1.0) / 128) / 1024);
	numOfImages = inputBuffer[2];
	int dataNum = 2;
	Images = new byte**[numOfImages];
	for (int image = 0; image < numOfImages; image++) {
		Images[image] = new byte*[FPR];
		for (int pos = 0; pos < FPR; pos++) {
			Images[image][pos] = new byte[6];
		}
	}
	for (int image = 0; image < numOfImages; image++) {
		for (int pos = 0; pos < FPR; pos++) {
			for (int value = 0; value < 6; value++) {
				dataNum++;
				if (dataNum == 256) {
					dataNum = 0;
					awaitNextPacketByte();
				}
				Images[image][pos][value] = inputBuffer[dataNum];
			}
		}
	}
	enableTimers();
}

void loop() {
	while (true) {
		getImages();
	}
}

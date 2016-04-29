/*************************************************** 
  This is an example for the Adafruit VS1053 Codec Breakout

  Designed specifically to work with the Adafruit VS1053 Codec Breakout 
  ----> https://www.adafruit.com/products/1381

  Adafruit invests time and resources providing this open source code, 
  please support Adafruit and open-source hardware by purchasing 
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.  
  BSD license, all text above must be included in any redistribution
 ****************************************************/

// include SPI, MP3 and SD libraries
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>


unsigned char regPtr=0;
unsigned char ModuleAction=0;
unsigned char ModuleExtra=0;
unsigned char intState=0;
unsigned char cntMinute=0;
unsigned char cntSecond=0;

unsigned int getModuleAction(){
    unsigned int i=0;
    cli();
    i=(ModuleExtra<<8)|ModuleAction;
    ModuleAction=0;
    ModuleExtra=0;
    sei();
    return i;
}

// These are the pins used for the breakout example
#define BREAKOUT_RESET  9      // VS1053 reset pin (output)
#define BREAKOUT_CS     10     // VS1053 chip select pin (output)
#define BREAKOUT_DCS    8      // VS1053 Data/command select pin (output)
// These are common pins between breakout and shield
#define CARDCS 4     // Card chip select pin
// DREQ should be an Int pin, see http://arduino.cc/en/Reference/attachInterrupt
#define DREQ 3       // VS1053 Data request, ideally an Interrupt pin


#define BREAKOUT_SDCD 2
#define BREAKOUT_5VON 5
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(BREAKOUT_RESET, BREAKOUT_CS, BREAKOUT_DCS, DREQ, CARDCS);

unsigned char TWIByteCounter=0;

/*I2C 

*/

#define TWI_SLAVE_ADDR 0x44
//with prescaler 4 means 100khz@16Mhz
#define TWI_TWBR 18
unsigned char TWI_state=0x00;

#define TWI_SLAVE 0
#define TWI_MASTER 1
unsigned char TWI_mode=TWI_SLAVE;


void TWI_Slave_Init(void)
{
    TWI_mode=TWI_SLAVE;
    TWBR = TWI_TWBR;                                  // Set bit rate register (Baudrate).
    TWSR = 0x01;					//prescaler = 4
    TWAR = TWI_SLAVE_ADDR|1;				//address will also react to general call
    TWDR = 0xFF;                                      // Default content = SDA released.
    TWCR = (1<<TWEN)|                                 // Enable TWI-interface and release TWI pins.
         (1<<TWIE)|(1<<TWINT)|                      // Enable Interupts, clear flag
         (1<<TWEA)|(0<<TWSTA)|(0<<TWSTO)|           //
         (0<<TWWC);                                 //
    TWIByteCounter=0;
    sei();
}

unsigned char TWI_master_addr=0x00;
unsigned int TWI_send_data=0;

void TWI_start_send(unsigned char addr,unsigned int v){
    TWI_mode=TWI_MASTER;
    TWI_master_addr=addr|0;	//Write to the master
    TWI_send_data=v;

    TWCR = (1<<TWEN)|                             // TWI Interface enabled.
         (1<<TWIE)|(1<<TWINT)|                  // Enable TWI Interupt and clear the flag.
         (0<<TWEA)|(1<<TWSTA)|(0<<TWSTO)|       // Initiate a START condition.
         (0<<TWWC);
    TWIByteCounter=0;
}



ISR(TWI_vect)
{
    unsigned char stopCond=0;
    TWI_state = TWSR&0xFC;                            // Store TWSR and automatically sets clears noErrors bit.
    switch (TWSR&(0xFC))
    {
	//master transmitter

	case 0x08: //A START condition has been transmitted
		    TWDR=TWI_master_addr;
		    break;
	case 0x10: //A repeated START condition has been transmitted
		    break;
	case 0x18: //SLA+W has been transmitted; ACK has been received
		    TWIByteCounter=0;
		    TWDR=TWI_send_data>>8;
		    break;
	case 0x20: //SLA+W has been transmitted; NOT ACK has been received
		    stopCond=1;
		    break;
	case 0x28: //Data byte has been transmitted; ACK has been received
		    TWIByteCounter++;
		    if(TWIByteCounter==2){
			stopCond=1;
		    }else{
			TWDR=TWI_send_data&0xFF;
		    }
		    break;
	case 0x30: //Data byte has been transmitted; NOT ACK has been received
		    stopCond=1;
		    break;
	case 0x38: //Arbitration lost in SLA+W or data bytes
		    stopCond=1;
		    break;

	//slave receiver
	case 0x60: //Own SLA+W has been received; ACK has been returned
		TWIByteCounter=0;
		break;
	case 0x68: //Arbitration lost in SLA+R/W as Master; own SLA+W has been received; ACK has been returned
		break;
	case 0x70: //General call address has been received; ACK has been returned
		break;
	case 0x78: //Arbitration lost in SLA+R/W as Master; General call address has been received; ACK has been returned
		break;
	case 0x80: //Previously addressed with own SLA+W; data has been received; ACK has been returned
		if(TWIByteCounter==0){
		    regPtr=TWDR;
		}else{
		    ModuleAction=regPtr;
		    ModuleExtra=TWDR;
		}
		TWIByteCounter++;
		break;
	case 0x88: //Previously addressed with own SLA+W; data has been received; NOT ACK has been returned
		break;
	case 0x90: //Previously addressed with general call; data has been received; ACK has been returned
		break;
	case 0x98: //Previously addressed with general call; data has been received; NOT ACK has been returned
		break;
	case 0xA0: //A STOP condition or repeated START condition has been received while still addressed as Slave
		break;

	//slave transmitter
	case 0xA8: //Own SLA+R has been received; ACK has been returned
		TWIByteCounter=0;
		if(regPtr==0x01){
		    if(musicPlayer.playingMusic) {
			TWDR=intState|0x08;;
		    }else{
			TWDR=intState;
		    }
		}else if(regPtr==0x10){
		    TWDR=cntSecond;
		}else if(regPtr==0x11){
		    TWDR=cntMinute;
		}else{
		    TWDR=0;
		}
		break;
	case 0xB0: //Arbitration lost in SLA+R/W as Master; own SLA+R has been received; ACK has been returned
		break;
	case 0xB8: //Data byte in TWDR has been transmitted; ACK has been received
		TWDR=0x00;
		break;
	case 0xC0: //Data byte in TWDR has been transmitted; NOT ACK has been received
		break;
	case 0xC8: //Last data byte in TWDR has been transmitted (TWEA = “0”); ACK has been received
		break;

	//general
	case 0xF8: //No relevant state information available; TWINT = “0”
		break;
	case 0x00: //Bus error due to an illegal START or STOP condition
		while(1);//wdt reset the module as this should not happen
		break;
	default:
		break;
    }
    //TWCR &= ~((1 << TWSTO) | (1 << TWEN));
    //TWCR |= (1 << TWEN);
    if(TWI_mode==TWI_SLAVE){
	TWCR = (1<<TWEN)|                                 // Enable TWI-interface and release TWI pins
        (1<<TWIE)|(1<<TWINT)|                      // Enable Interupts and clear flag
	(1<<TWEA)|(0<<TWSTA)|(0<<TWSTO)|           // No Signal requests
        (0<<TWWC);                                 //
    }else{
	if(stopCond){
	    TWI_mode=TWI_SLAVE;
	    TWCR = (1<<TWEN)|                                 // Enable TWI-interface and release TWI pins
	    (1<<TWIE)|(1<<TWINT)|                      // Enable Interupts and clear flag
	    (1<<TWEA)|(0<<TWSTA)|(1<<TWSTO)|           // No Signal requests
	    (0<<TWWC);                                 //
	}else{
	    TWCR = (1<<TWEN)|                                 // Enable TWI-interface and release TWI pins
	    (1<<TWIE)|(1<<TWINT)|                      // Enable Interupts and clear flag
	    (0<<TWEA)|(0<<TWSTA)|(0<<TWSTO)|           // No Signal requests
	    (0<<TWWC);                                 //
	}
    }

}
/************* END OF I2C functions ****************/


unsigned char secondCounter=0;
unsigned int countdown=0;
unsigned char repeatFlag=0;
unsigned int TWI_counter=0;

ISR(TIMER2_COMPA_vect){
    secondCounter++;
    if(secondCounter==125){//1s is over
	secondCounter=0;
	if(countdown==1){
	    repeatFlag=0;
	    musicPlayer.stopPlaying();
	}
	if(countdown>0){
	    countdown--;
	}
	TWI_counter++;
	if(TWI_counter==10){
	    TWI_counter=0;
	    TWI_start_send(0x32,0x1306);
	}
    }
}

void setup() {
    unsigned char i=0;

    TWI_Slave_Init();

    TCCR2A=2; //CTC mode
    TCCR2B=7;//prescaler 1024
    OCR2A=256-125;
    TIMSK2|=0x02;


    pinMode(CARDCS,OUTPUT);
    digitalWrite(CARDCS,HIGH);
    pinMode(BREAKOUT_CS,OUTPUT);
    digitalWrite(BREAKOUT_CS,HIGH);

    TCCR0A&=~0xF0; //give D5 back stupid timer lib
    pinMode(BREAKOUT_5VON,OUTPUT);
    digitalWrite(BREAKOUT_5VON,LOW);


    pinMode(BREAKOUT_SDCD,INPUT);

    Serial.begin(9600);
    Serial.print("Starting...");

  // initialise the music player
    if (! musicPlayer.begin()) { // initialise the music player
    	intState&=~0x01;
    }else{
    	Serial.println("VS1053 ok");
    	intState|=0x01;
    }
    while(!SD.begin(CARDCS)){
	    musicPlayer.sineTest(0x44, 500);
    }
    Serial.println("SD card ok");
    intState|=0x02;

    wdt_enable(WDTO_2S);
    musicPlayer.setVolume(0,0);

    if (! musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)){
    	intState&=~0x04;
	    //Serial.println(F("DREQ pin is not an interrupt pin"));
    }else{
    	Serial.println("VS1053 interrupt ok");
    	intState|=0x04;
    }
}

void Number2PaddedString(unsigned char nr,char *c){
    c[0]=(nr/10)+48;
    c[1]=(nr%10)+48;
    c[2]='.';
    c[3]='m';
    c[4]='p';
    c[5]='3';
    c[6]=0;
}

void Number2String(unsigned char nr,char *c){
    if(nr<10){
	c[0]=(nr)+48;
	c[1]=0;
    }else{
	c[0]=(nr/10)+48;
	c[1]=(nr%10)+48;
	c[2]=0;
    }
}

unsigned char checkFileExists(char *c){
    if(musicPlayer.playingMusic){
	if(musicPlayer.checkExists(c)){
	    return 1;
	}
    }else{
	SD.begin(CARDCS);
	if(SD.exists(c)){
	    return 1;
	}
    }
    return 0;
}

/*SD card structure
/alarm
  01.mp3
  02.mp3
  03.mp3
  ....
  default.mp3

/event
  01.mp3
  02.mp3
  03.mp3
  ....
  default.mp3

/sched
  01.mp3
  02.mp3
  03.mp3
  ....
  default.mp3

/amb
  01.mp3
  02.mp3
  03.mp3
  ....
  default.mp3

/talk  (contains the mp3 used for talking time, format: h_m.mp3)
  0_0.mp3
  ...
  23_59.mp3

/cont  (contains the mp3 used for continous play, format: h_m_dow.mp3 or h_m_day_month.mp3(this will have priority) )
  0_0_0.mp3
  ...
  23_59_6.mp3
  0_0_1_1.mp3
  ...
  23_59_31_12.mp3

*/

    //00: NOP
    //01: get state (initial states, is playing)
    //02: play a specific alarm sound (01...99), if MSB is set repeat the song
    //03: play a specific schedule sound (01...99), if MSB is set repeat the song
    //04: play a specific event sound (01...99), if MSB is set repeat the song
    //05: play a specific ambient sound (01...99), if MSB is set repeat the song
    //06: setup volume (0...255)
    //07: stop playing whatever you playing (any value)
    //08: setup the time (hour setup) for talking
    //09: setup the time (minute setup) for talking, starts talking after setup

    //0A: setup the time (hour+dow setup) for continous play (binary value dow dow dow h h h h h)
    //0B: setup the time (day setup) for continous play
    //0C: setup the time (month setup) for continous play
    //0D: setup the time (minute setup) for continous play, starts playing after setup


    //10: setup play timer seconds (00:00 means infinite)
    //11: setup play timer minutes (00:00 means infinite)

    //AA: WDT reset module (for any value written)

    //talking date
    //talking time+date
    //talk temperature
    //


void loop() {

    char fn[25];
    char nr[13];
    unsigned char hour=0;
    unsigned char min=0;
    unsigned char dow=0;
    unsigned char day=0;
    unsigned char month=0;

    set_sleep_mode(SLEEP_MODE_IDLE);
    //Serial.println("Starting.");
    while(1){

    unsigned int action=getModuleAction();
	sleep_mode();
	//SD.begin(CARDCS);
	switch(action&0xFF){
	    case 2: //play alarm sound
		    fn[0]=0;nr[0]=0;
		    strcat(fn,"/alarm/");
		    if((action>>8)&0x80){
			repeatFlag=1;
		    }else{
			repeatFlag=0;
		    }
		    Number2PaddedString((action>>8)&0x7F,&nr[0]);
		    strcat(fn,nr);
		    wdt_reset();
		    if(musicPlayer.playingMusic) {
			musicPlayer.stopPlaying();
			wdt_reset();delay(50);wdt_reset();
		    }
		    wdt_reset();SD.begin(CARDCS);
		    if(musicPlayer.startPlayingFile(fn)==0){ //file does not exist
			fn[0]=0;
			strcat(fn,"/alarm/default.mp3");
			musicPlayer.startPlayingFile(fn);
		    }
		    if((cntMinute>0)||(cntSecond>0)){
			countdown=(cntMinute*60)+cntSecond;
		    }
		    break;
	    case 3: //play schedule sound
		    fn[0]=0;nr[0]=0;
		    strcat(fn,"/sched/");
		    if((action>>8)&0x80){
			repeatFlag=1;
		    }else{
			repeatFlag=0;
		    }
		    Number2PaddedString((action>>8)&0x7F,&nr[0]);
		    strcat(fn,nr);
		    wdt_reset();
		    if(musicPlayer.playingMusic) {
			musicPlayer.stopPlaying();
			wdt_reset();delay(50);wdt_reset();
		    }
		    wdt_reset();SD.begin(CARDCS);
		    if(musicPlayer.startPlayingFile(fn)==0){ //file does not exist
			fn[0]=0;
			strcat(fn,"/sched/default.mp3");
			musicPlayer.startPlayingFile(fn);
		    }
		    if((cntMinute>0)||(cntSecond>0)){
			countdown=(cntMinute*60)+cntSecond;
		    }
		    break;
	    case 4: //play event sound
		    fn[0]=0;nr[0]=0;
		    strcat(fn,"/event/");
		    if((action>>8)&0x80){
			repeatFlag=1;
		    }else{
			repeatFlag=0;
		    }
		    Number2PaddedString((action>>8)&0x7F,&nr[0]);
		    strcat(fn,nr);
		    wdt_reset();
		    if(musicPlayer.playingMusic) {
			musicPlayer.stopPlaying();
			wdt_reset();delay(50);wdt_reset();
		    }
		    wdt_reset();SD.begin(CARDCS);
		    if(musicPlayer.startPlayingFile(fn)==0){ //file does not exist
			fn[0]=0;
			strcat(fn,"/event/default.mp3");
			musicPlayer.startPlayingFile(fn);
		    }
		    if((cntMinute>0)||(cntSecond>0)){
			countdown=(cntMinute*60)+cntSecond;
		    }
		    break;
	    case 5: //play ambient sound
		    fn[0]=0;nr[0]=0;
		    strcat(fn,"/amb/");
		    if((action>>8)&0x80){
			repeatFlag=1;
		    }else{
			repeatFlag=0;
		    }
		    Number2PaddedString((action>>8)&0x7F,&nr[0]);
		    strcat(fn,nr);
		    wdt_reset();
		    if(musicPlayer.playingMusic) {
			musicPlayer.stopPlaying();
			wdt_reset();delay(50);wdt_reset();
		    }
		    wdt_reset();SD.begin(CARDCS);
		    if(musicPlayer.startPlayingFile(fn)==0){ //file does not exist
			fn[0]=0;
			strcat(fn,"/amb/default.mp3");
			musicPlayer.startPlayingFile(fn);
		    }
		    if((cntMinute>0)||(cntSecond>0)){
			countdown=(cntMinute*60)+cntSecond;
		    }
		    break;
	    case 6: //volume
		    musicPlayer.setVolume((action>>8)&0xFF,(action>>8)&0xFF);
		    break;
	    case 7: //stop
		    repeatFlag=0;
		    musicPlayer.stopPlaying();
		    wdt_reset();delay(50);wdt_reset();
		    break;
	    case 8: //setup hour
		    hour=(action>>8)&0xFF;
		    break;
	    case 9: //setup minute+talk the time
		    min=(action>>8)&0xFF;
		    fn[0]=0;nr[0]=0;
		    strcat(fn,"/talk/");
		    Number2String(hour,&nr[0]);
		    strcat(fn,nr);
		    strcat(fn,"_");
		    Number2String(min,&nr[0]);
		    strcat(fn,nr);
		    strcat(fn,".mp3");
		    wdt_reset();
		    if(checkFileExists(fn)){
			repeatFlag=0;
			musicPlayer.stopPlaying();
			wdt_reset();delay(50);wdt_reset();
			musicPlayer.startPlayingFile(fn);
		    }
		    break;

	    case 0x0A: //setup hour+dow
		    hour=(action>>8)&0x1F;
		    dow=(action>>(8+5))&0x07;
		    break;
	    case 0x0B: //setup day
		    day=(action>>8)&0x1F;
		    break;
	    case 0x0C: //setup month
		    month=(action>>8)&0x0F;
		    break;
	    case 0x0D: //setup minute+talk the time
		    min=(action>>8)&0xFF;
		    fn[0]=0;nr[0]=0;
		    strcat(fn,"/cont/");
		    Number2String(hour,&nr[0]);
		    strcat(fn,nr);
		    strcat(fn,"_");
		    Number2String(min,&nr[0]);
		    strcat(fn,nr);
		    strcat(fn,"_");
		    Number2String(day,&nr[0]);
		    strcat(fn,nr);
		    strcat(fn,"_");
		    Number2String(month,&nr[0]);
		    strcat(fn,nr);
		    strcat(fn,".mp3");
		    wdt_reset();
		    if(checkFileExists(fn)){
			repeatFlag=0;
			musicPlayer.stopPlaying();
			wdt_reset();delay(50);wdt_reset();
			musicPlayer.startPlayingFile(fn);
		    }else{
			fn[0]=0;nr[0]=0;
			strcat(fn,"/cont/");
			Number2String(hour,&nr[0]);
			strcat(fn,nr);
			strcat(fn,"_");
			Number2String(min,&nr[0]);
			strcat(fn,nr);
			strcat(fn,"_");
			Number2String(dow,&nr[0]);
			strcat(fn,nr);
			strcat(fn,".mp3");
			wdt_reset();
			if(checkFileExists(fn)){
			    repeatFlag=0;
			    musicPlayer.stopPlaying();
			    wdt_reset();delay(50);wdt_reset();
			    musicPlayer.startPlayingFile(fn);
			}
		    }
		    break;

	    case 0x10: //setup play timer seconds
		    cntSecond=(action>>8)&0xFF;
		    break;
	    case 0x11: //setup play timer minutes
		    cntMinute=(action>>8)&0xFF;
		    break;
	    case 0xAA: //reset
		    wdt_enable(WDTO_1S);
		    while(1); //let the watchdog bark
		    break;
	}
	wdt_reset();
	if((repeatFlag)&&(musicPlayer.playingMusic==0)){
	    musicPlayer.startPlayingFile(fn);
	}

    }
}

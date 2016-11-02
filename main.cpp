#include "mbed.h"
 
//------------------------------------------------------------------------------------
/* This example was tested on C027-U20 and C027-G35 with the on board modem. 
   
   Additionally it was tested with a shield where the SARA-G350/U260/U270 RX/TX/PWRON 
   is connected to D0/D1/D4 and the GPS SCL/SDA is connected D15/D15. In this 
   configuration the following platforms were tested (it is likely that others 
   will work as well)
   - U-BLOX:    C027-G35, C027-U20, C027-C20 (for shield set define C027_FORCE_SHIELD)
   - NXP:       LPC1549v2, LPC4088qsb
   - Freescale: FRDM-KL05Z, FRDM-KL25Z, FRDM-KL46Z, FRDM-K64F
   - STM:       NUCLEO-F401RE, NUCLEO-F030R8
                mount resistors SB13/14 1k, SB62/63 0R
*/
#include "GPS.h"
#include "MDM.h"
//------------------------------------------------------------------------------------
// You need to configure these cellular modem / SIM parameters.
// These parameters are ignored for LISA-C200 variants and can be left NULL.
//------------------------------------------------------------------------------------
//! Set your secret SIM pin here (e.g. "1234"). Check your SIM manual.
#define SIMPIN      "1922"
/*! The APN of your network operator SIM, sometimes it is "internet" check your 
    contract with the network operator. You can also try to look-up your settings in 
    google: https://www.google.de/search?q=APN+list */
#define APN         NULL
//! Set the user name for your APN, or NULL if not needed
#define USERNAME    NULL
//! Set the password for your APN, or NULL if not needed
#define PASSWORD    NULL 
//------------------------------------------------------------------------------------
 
//#define CELLOCATE
DigitalOut led1(LED1);

int cbString(int type, const char* buf, int len, char* str)
{
    //printf("--> buf: %s <--\n", buf);
    if (sscanf(buf, "\r\n%[^\r\n]s\r\n", str) == 1) {
            /*nothing*/;
    }
    //printf("--> str: %s <--\n", str);
    return 0;
}

int formatSocketData(char* buf, char* method, char* name, char* data)
{
    char header[128];

    sprintf(header, "%s /%s HTTP/1.0\r\nAccept: */*\r\nContent-Type: application/plain", method, name);
    if(strcmp(method, "GET")==0)
        sprintf(buf, "%s\r\n\r\n", header);
    else
        sprintf(buf, "%s\r\nContent-Length: %d\r\n\r\n%s\r\n", header, strlen(data), data);
    //printf("buf: (%s)\n", buf);
}

int main(void)
{
    int ret;
#ifdef LARGE_DATA
    char buf[2048] = "";
#else
    char buf[512] = "";
#endif
    int port = 8007;
    char gpsdata[2048];
    char response[512];
    const char* host = "ubloxsingapore.ddns.net";
    MDMParser::IP hostip;
    char hostipstr[16];
    char constdata[250]="fdsajklre2ndvfklasfkeqlw2bnjkvfdsakflewqjrk32ljfkdelsajk32l4j32rkljfkdeswalrj32klr43j2kljfekalrj32klrjfekwlafjkdesalrje2klfjdkslajrk2lrj32kljfekwlrje2klrj32klrjkewlfjeklrj2klrj32kl4j32klrjewkrjewkrelwjffdskalfjdkewr32kljfelj";

 
    printf("C027_Support Base\n");

    // Create the GPS object
#if 1   // use GPSI2C class
    GPSI2C gps;
#else   // or GPSSerial class 
    GPSSerial gps; 
#endif
    // Create the modem object
    MDMSerial mdm; // use mdm(D1,D0) if you connect the cellular shield to a C027
    //mdm.setDebug(4); // enable this for debugging issues 

    // initialize the modem 
    MDMParser::DevStatus devStatus = {};
    MDMParser::NetStatus netStatus = {};
    bool mdmOk = mdm.init(SIMPIN, &devStatus);

    mdm.dumpDevStatus(&devStatus);
    
#if 0
    while (1) 
    {
	printf("test\n");
	ret = gps.getMessage(buf, sizeof(buf));
	if(ret>0) {
        int len = LENGTH(ret);
        printf("NMEA: %.*s\r\n", len-2, buf); 
	}
            Thread::wait(1000);
    }
#endif

    if (mdmOk) {
        // wait until we are connected
        mdmOk = mdm.registerNet(&netStatus);
        mdm.dumpNetStatus(&netStatus);
    }

    MDMParser::IP ip = mdm.join(APN,USERNAME,PASSWORD);
    if (ip == NOIP)
        printf("Not able to join network");
    else
    {
	hostip = mdm.gethostbyname(host);
	sprintf(hostipstr, IPSTR "\n", IPNUM(hostip));
	printf("server IP: %s\n", hostipstr);
	printf("Make a Http Post Request to post base IP address\r\n");
        int socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
        if(socket>=0)
        {
//            mdm.socketSetBlocking(socket, 1000);

	    if (mdm.socketConnect(socket, hostipstr, port))
	    {
		char ipinfo[100];
		//sprintf(ipinfo, "{IPServer: " IPSTR ", IPClient: 0, read: 0}", IPNUM(ip));
		sprintf(ipinfo, IPSTR, IPNUM(ip));
		formatSocketData(buf, "POST", "ipbase", ipinfo);
		mdm.socketSend(socket, buf, strlen(buf));
#if 0
		ret = mdm.socketRecv(socket, response, sizeof(response)-1);
		if(ret>0)
		    printf("Socket Recv \"%*s\"\r\n", ret, response);
#endif
		mdm.socketClose(socket);
	    }
	    mdm.socketFree(socket);
        }

	printf("Make a Http Post Request to post gpsdata\r\n");
        for(int count=0; count<72000; count++) {
	    ret = gps.getMessage(gpsdata, sizeof(gpsdata));
	    if(ret>0) {
            	printf("Post GPS data %s\r\n", gpsdata);
            	int socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
            	if(socket>=0)
	    	{
//                mdm.socketSetBlocking(socket, 1000);
  	            if (mdm.socketConnect(socket, hostipstr, port))
	            {
		    	formatSocketData(buf, "POST", "gpsdata", gpsdata);
	            	mdm.socketSend(socket, buf, strlen(buf));
#if 0 /* Skipp reading response to save time */
	            ret = mdm.socketRecv(socket, buf, sizeof(buf)-1);
	            if(ret>0)
	                printf("Socket Recv \"%*s\"\r\n", ret, buf);
#endif
	            	mdm.socketClose(socket);
	            }

	            mdm.socketFree(socket);
	    	}
	    }
            //Thread::wait(1000);
        }
        
   
    }
    mdm.disconnect();


    // Blocking code
    while (true) {
	led1 = !led1;
        
        mdm.sendFormated("AT+COPS?\r\n");
        mdm.waitFinalResp(cbString,buf, 60*1000);
        printf("buf is (%s)\n", buf);
	Thread::wait(500);
    }

#if 0
    printf("SMS and GPS Loop\r\n");
    char link[128] = "";
    const int wait = 100;
    bool abort = false;

    //DigitalOut led(LED1);
    while (!abort) {
    //    led = !led;
        while ((ret = gps.getMessage(buf, sizeof(buf))) > 0)
        {
            int len = LENGTH(ret);
            //printf("NMEA: %.*s\r\n", len-2, msg); 
            if ((PROTOCOL(ret) == GPSParser::NMEA) && (len > 6))
            {
                // talker is $GA=Galileo $GB=Beidou $GL=Glonass $GN=Combined $GP=GPS
                if ((buf[0] == '$') || buf[1] == 'G') {
                    #define _CHECK_TALKER(s) ((buf[3] == s[0]) && (buf[4] == s[1]) && (buf[5] == s[2]))
                    if (_CHECK_TALKER("GLL")) {
                        double la = 0, lo = 0;
                        char ch;
                        if (gps.getNmeaAngle(1,buf,len,la) && 
                            gps.getNmeaAngle(3,buf,len,lo) && 
                            gps.getNmeaItem(6,buf,len,ch) && ch == 'A')
                        {
                            printf("GPS Location: %.5f %.5f\r\n", la, lo); 
                            sprintf(link, "I am here!\n"
                                          "https://maps.google.com/?q=%.5f,%.5f", la, lo); 
                        }
                    } else if (_CHECK_TALKER("GGA") || _CHECK_TALKER("GNS") ) {
                        double a = 0; 
                        if (gps.getNmeaItem(9,buf,len,a)) // altitude msl [m]
                            printf("GPS Altitude: %.1f\r\n", a); 
                    } else if (_CHECK_TALKER("VTG")) {
                        double s = 0; 
                        if (gps.getNmeaItem(7,buf,len,s)) // speed [km/h]
                            printf("GPS Speed: %.1f\r\n", s); 
                    }
                }
            }
        }

#ifdef RTOS_H
        Thread::wait(wait);
#else
        ::wait_ms(wait);
#endif
    }
#endif

    gps.powerOff();
    mdm.powerOff();
    return 0;
}

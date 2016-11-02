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
    printf("--> buf: %s <--\n", buf);
    if (sscanf(buf, "\r\n%[^\r\n]s\r\n", str) == 1) {
            /*nothing*/;
    }
    printf("--> str: %s <--\n", str);
    return 0;
}

int main(void)
{
    int ret;
#ifdef LARGE_DATA
    char buf[2048] = "";
#else
    char buf[512] = "";
#endif
 
    printf("C027_Support test code\n");

    // Create the GPS object
#if 1   // use GPSI2C class
    GPSI2C gps;
#else   // or GPSSerial class 
    GPSSerial gps; 
#endif
    // Create the modem object
    MDMSerial mdm; // use mdm(D1,D0) if you connect the cellular shield to a C027
    mdm.setDebug(4); // enable this for debugging issues 

    // initialize the modem 
    MDMParser::DevStatus devStatus = {};
    MDMParser::NetStatus netStatus = {};
    bool mdmOk = mdm.init(SIMPIN, &devStatus);
    mdm.dumpDevStatus(&devStatus);
    
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
	printf("Make a Http Post Request\r\n");
        int socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
        if(socket>=0)
        {
            int port = 8007;
	    char gpsdata[256];
	    char response[512];
	    const char* host = "ubloxsingapore.ddns.net";

            mdm.socketSetBlocking(socket, 10000);

	    if (mdm.socketConnect(socket, host, port))
	    {
		char ipinfo[100];
		sprintf(ipinfo, "{IPServer: " IPSTR ", IPClient: 0, read: 0}", IPNUM(ip));
		char header[] = "POST /ipinfo HTTP/1.0\r\nAccept: */*\r\nContent-Type: application/plain";
		sprintf(buf, "%s\r\nContent-Length: %d\r\n\r\n%s\r\n", header, strlen(ipinfo), ipinfo);
		printf("buf: (%s)\n", buf);
		mdm.socketSend(socket, buf, strlen(buf));

		ret = mdm.socketRecv(socket, response, sizeof(response)-1);
		if(ret>0)
		    printf("Socket Recv \"%*s\"\r\n", ret, response);

		mdm.socketClose(socket);

     		for(int count=0; count<300; count++) {
		printf("---> count: %d buf: (%s)\n", count, buf);
		mdm.socketConnect(socket, host, port);
		mdm.socketSend(socket, buf, strlen(buf));

		ret = mdm.socketRecv(socket, response, sizeof(response)-1);
		if(ret>0)
		    printf("Socket Recv \"%*s\"\r\n", ret, response);
		mdm.socketClose(socket);
      		Thread::wait(1000);
		}

	    }
	    mdm.socketFree(socket);
	}
	mdm.disconnect();

     for(int count=0; count<300; count++) {
	printf("Post GPS data %d\r\n", count);
        int socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
        if(socket>=0)
	{
            int port = 8007;
	    char gpsdata[256];
	    const char* host = "ubloxsingapore.ddns.net";

            mdm.socketSetBlocking(socket, 10000);
  	        if (mdm.socketConnect(socket, host, port))
	        {
		    char header[] = "POST /gpsdata HTTP/1.0\r\nContent-Type: application/plain\r\n";
	 	    sprintf(gpsdata, "gpsdata count %d\n", count);
		    sprintf(buf, "%s%s\r\n\r\n", header, count);
		    printf("buf: (%s)\n", buf);
		    mdm.socketSend(socket, buf, strlen(buf));

		    ret = mdm.socketRecv(socket, buf, strlen(buf));
		    if(ret>0)
		        printf("Socket Recv \"%*s\"\r\n", ret, buf);

		    mdm.socketClose(socket);
	        }

	    mdm.socketFree(socket);
	}
	mdm.disconnect();
      Thread::wait(1000);
      }
    }
#if 0
	printf("Make a Http Post Request\r\n");
        int socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
        if(socket>=0)
        {
            int port = 8007;
	    char gpsdata[256];
	    const char* host = "ubloxsingapore.ddns.net";

            mdm.socketSetBlocking(socket, 10000);

	    if (mdm.socketConnect(socket, host, port))
	    {
		char ipinfo[100];
		char header[] = "POST /ipinfo HTTP/1.0\r\nContent-Type: application/plain\r\n";
		char buf[256];
		sprintf(ipinfo, "{IPServer: " IPSTR ", IPClient: 0, read: 0}", IPNUM(ip));
		sprintf(buf, "%s%s\r\n\r\n", header, ipinfo);
		printf("buf: (%s)\n", buf);
		mdm.socketSend(socket, buf, sizeof(buf)-1);

		ret = mdm.socketRecv(socket, buf, sizeof(buf)-1);
		if(ret>0)
		    printf("Socket Recv \"%*s\"\r\n", ret, buf);

		for(int count=0; count<300; count++) {
		   char headergps[] = "POST /gpsdata HTTP/1.0\r\nContent-Type: application/plain\r\n";
		   sprintf(gpsdata, "gps rawdata %d\n", count);
		   printf("Sending %s\n", gpsdata);
		   sprintf(buf, "%s%s\r\n\r\n", headergps, gpsdata);
		   mdm.socketSend(socket, buf, sizeof(buf)-1);
		   ret = mdm.socketRecv(socket, buf, sizeof(buf)-1);
		   if(ret>0)
		       printf("Socket Recv \"%*s\"\r\n", ret, buf);

		   Thread::wait(1000);
		}
		
		mdm.socketClose(socket);
	    }
	    mdm.socketFree(socket);
	}
	mdm.disconnect();
    }
#endif

    while (true) {
	led1 = !led1;
        
        mdm.sendFormated("AT+COPS?\r\n");
        mdm.waitFinalResp(cbString,buf, 60*1000);
        printf("buf is (%s)\n", buf);
	Thread::wait(500);
    }

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
    gps.powerOff();
    mdm.powerOff();
    return 0;
}

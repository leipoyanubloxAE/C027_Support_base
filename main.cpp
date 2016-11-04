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
 
#define BUFSIZE 1024
#define GPSSIZE 1024

int cbString(int type, const char* buf, int len, char* str)
{
    if (sscanf(buf, "\r\n%[^\r\n]s\r\n", str) == 1) {
            /*nothing*/;
    }
    return 0;
}

int formatSocketData(char* buf, char* method, char* name, char* data, int length)
{
    char header[128];
    int pos;

    memset(buf, 0, sizeof(buf));
    sprintf(header, "%s /%s HTTP/1.0\r\nAccept: */*\r\nContent-Type: application/plain", method, name);
    if(strcmp(method, "GET")==0) {
        sprintf(buf, "%s\r\n\r\n", header);
	return (strlen(buf));
    } else {
        //sprintf(buf, "%s\r\nContent-Length: %d\r\n\r\n%s\r\n", header, length, data);
        sprintf(buf, "%s\r\nContent-Length: %d\r\n\r\n", header, length);
        pos = strlen(buf);
        if((pos+length) < BUFSIZE-2) {
	    memcpy(&buf[pos], data, length);
	    sprintf(&buf[pos+length], "\r\n");
        } else {
	    printf("too much data\n");
	}
	return ((pos+length) < BUFSIZE -2 ?  pos + length + 2 : BUFSIZE );
    }
}

int main(void)
{
    int ret;
    char* buf = NULL;
    int port = 8007;
    char* gpsdata=NULL;
    const char* host = "ubloxsingapore.ddns.net";
    MDMParser::IP hostip;
    char hostipstr[16];
 
    printf("C027_Support Base\n");
    buf = (char*)malloc(sizeof(char) * BUFSIZE);
    gpsdata = (char*)malloc(sizeof(char) * GPSSIZE);
    if(gpsdata==NULL)
    {
        printf("Failed to allocate memory\n");
	return 0;
    }
    memset(gpsdata, 0, sizeof(GPSSIZE));

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
    if (mdmOk) {
 
        // wait until we are connected
        mdmOk = mdm.registerNet(&netStatus);
        mdm.dumpNetStatus(&netStatus);
    }
    if (mdmOk)
    {
        // join the internet connection 
        MDMParser::IP ip = mdm.join(APN,USERNAME,PASSWORD);
        if (ip == NOIP)
            printf("Not able to join network");
        else
        {
            mdm.dumpIp(ip);
            hostip = mdm.gethostbyname(host);
            sprintf(hostipstr, IPSTR "\n", IPNUM(hostip));
            printf("server IP: %s\n", hostipstr);

            printf("Make a Http Post Request\r\n");
            int socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
            if (socket >= 0)
            {
                //mdm.socketSetBlocking(socket, 10000);
                if (mdm.socketConnect(socket, hostipstr, port))
                {
	 	    char ipinfo[20];
		    int bufLength = 0;
		    sprintf(ipinfo, IPSTR, IPNUM(ip));
		    bufLength = formatSocketData(buf, "POST", "ipbase", ipinfo, strlen(ipinfo));
					
                    mdm.socketSend(socket, buf, bufLength);
                
#if 0
                    ret = mdm.socketRecv(socket, buf, sizeof(buf)-1);
                    if (ret > 0)
                        printf("Socket Recv \"%*s\"\r\n", ret, buf);
#endif
                    mdm.socketClose(socket);
                }
                mdm.socketFree(socket);
            }
            
            while (1)
            {
		memset(gpsdata, 0, GPSSIZE);
	        ret = gps.getMessage(gpsdata, GPSSIZE);
		if (ret == GPSParser::WAIT) {
		    Thread::wait(500);
		    continue;
		}

		if (LENGTH(ret)>0) 
                {
                    int len = LENGTH(ret);
                    //printf("NMEA: %.*s\r\n", len-2, gpsdata); 
                    printf("RET: 0x%x protocol: %d len: %d strlen: %d\n", ret, PROTOCOL(ret), len, strlen(gpsdata)); 
                    int socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
                    if (socket >= 0)
                    {
                        //mdm.socketSetBlocking(socket, 10000);
                	if (mdm.socketConnect(socket, hostipstr, port))
                	{
			    int bufLength = 0;
		    	    bufLength = formatSocketData(buf, "POST", "gpsdata", gpsdata, len);
					
                    	    mdm.socketSend(socket, buf, bufLength);
#if 0
                    	    ret = mdm.socketRecv(socket, buf, sizeof(buf)-1);
                    	    if (ret > 0)
                        	printf("Socket Recv \"%*s\"\r\n", ret, buf);
#endif
                    	    mdm.socketClose(socket);
                	}
                	mdm.socketFree(socket);
            	    }
                } else
                {
                    printf("Unable to read gps message\n");
                }
	    }

            // disconnect  
            mdm.disconnect();
        }
    
    }

    gps.powerOff();
    mdm.powerOff();
    return 0;
}

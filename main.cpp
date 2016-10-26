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
 
int main(void)
{
    int ret;
#ifdef LARGE_DATA
    char buf[2048] = "";
#else
    char buf[512] = "";
#endif
 
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
#if 0
        // file system API
        const char* filename = "File";
        char buf[] = "Hello World";
        printf("writeFile \"%s\"\r\n", buf);
        if (mdm.writeFile(filename, buf, sizeof(buf)))
        {
            memset(buf, 0, sizeof(buf));
            int len = mdm.readFile(filename, buf, sizeof(buf));
            if (len >= 0) 
                printf("readFile %d \"%.*s\"\r\n", len, len, buf);
            mdm.delFile(filename);
        }
#endif
 
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
            printf("Make a Http Post Request\r\n");
            int socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
            if (socket >= 0)
            {
                mdm.socketSetBlocking(socket, 10000);
                if (mdm.socketConnect(socket, "mbed.org", 80))
                {
                    const char http[] = "GET /media/uploads/mbed_official/hello.txt HTTP/1.0\r\n\r\n";
                    mdm.socketSend(socket, http, sizeof(http)-1);
                
                    ret = mdm.socketRecv(socket, buf, sizeof(buf)-1);
                    if (ret > 0)
                        printf("Socket Recv \"%*s\"\r\n", ret, buf);
                    mdm.socketClose(socket);
                }
                mdm.socketFree(socket);
            }
            
            int port = 7;
            const char* host = "echo.u-blox.com";
            MDMParser::IP ip = mdm.gethostbyname(host);
            char data[] = "\r\nxxx Socket Hello World\r\n"
#ifdef LARGE_DATA
                        "00  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "01  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "02  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "03  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "04  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        
                        "05  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "06  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "07  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "08  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "09  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
            
                        "10  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "11  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "12  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "13  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "14  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        
                        "15  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "16  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "17  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "18  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
                        "19  0123456789 0123456789 0123456789 0123456789 0123456789 \r\n"
#endif            
                        "End\r\n";
                
            printf("Testing TCP sockets with ECHO server\r\n");
            socket = mdm.socketSocket(MDMParser::IPPROTO_TCP);
            if (socket >= 0)
            {
                mdm.socketSetBlocking(socket, 10000);
                if (mdm.socketConnect(socket, host, port)) {
                    memcpy(data, "\r\nTCP", 5); 
                    ret = mdm.socketSend(socket, data, sizeof(data)-1);
                    if (ret == sizeof(data)-1) {
                        printf("Socket Send %d \"%s\"\r\n", ret, data);
                    }
                    ret = mdm.socketRecv(socket, buf, sizeof(buf)-1);
                    if (ret >= 0) {
                        printf("Socket Recv %d \"%.*s\"\r\n", ret, ret, buf);
                    }
                    mdm.socketClose(socket);
                }
                mdm.socketFree(socket);
            }
 
            printf("Testing UDP sockets with ECHO server\r\n");
            socket = mdm.socketSocket(MDMParser::IPPROTO_UDP, port);
            if (socket >= 0)
            {
                mdm.socketSetBlocking(socket, 10000);
                memcpy(data, "\r\nUDP", 5); 
                ret = mdm.socketSendTo(socket, ip, port, data, sizeof(data)-1);
                if (ret == sizeof(data)-1) {
                    printf("Socket SendTo %s:%d " IPSTR " %d \"%s\"\r\n", host, port, IPNUM(ip), ret, data);
                }
                ret = mdm.socketRecvFrom(socket, &ip, &port, buf, sizeof(buf)-1);
                if (ret >= 0) {
                    printf("Socket RecvFrom " IPSTR ":%d %d \"%.*s\" \r\n", IPNUM(ip),port, ret, ret,buf);
                }
                mdm.socketFree(socket);
            }
            
            // disconnect  
            mdm.disconnect();
        }
    
        // http://www.geckobeach.com/cellular/secrets/gsmcodes.php
        // http://de.wikipedia.org/wiki/USSD-Codes
        const char* ussd = "*130#"; // You may get answer "UNKNOWN APPLICATION"
        printf("Ussd Send Command %s\r\n", ussd);
        ret = mdm.ussdCommand(ussd, buf);
        if (ret > 0) 
            printf("Ussd Got Answer: \"%s\"\r\n", buf);
    }
 
    printf("SMS and GPS Loop\r\n");
    char link[128] = "";
    unsigned int i = 0xFFFFFFFF;
    const int wait = 100;
    bool abort = false;
#ifdef CELLOCATE    
    const int sensorMask = 3;  // Hybrid: GNSS + CellLocate       
    const int timeoutMargin = 5; // seconds
    const int submitPeriod = 60; // 1 minutes in seconds
    const int targetAccuracy = 1; // meters
    unsigned int j = submitPeriod * 1000/wait;
    bool cellLocWait = false;
    MDMParser::CellLocData loc;
    
    //Token can be released from u-blox site, when you got one replace "TOKEN" below 
    if (!mdm.cellLocSrvHttp("TOKEN"))
            mdm.cellLocSrvUdp();        
    mdm.cellLocConfigSensor(1);   // Deep scan mode
    //mdm.cellUnsolIndication(1);
#endif
    //DigitalOut led(LED1);
    while (!abort) {
    //    led = !led;
#ifndef CELLOCATE
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
#endif        
#ifdef CELLOCATE
        if (mdmOk && (j++ == submitPeriod * 1000/wait)) {   
            j=0;
            printf("CellLocate Request\r\n");
            mdm.cellLocRequest(sensorMask, submitPeriod-timeoutMargin, targetAccuracy);
            cellLocWait = true;
        }
        if (cellLocWait && mdm.cellLocGet(&loc)){           
            cellLocWait = false;     
            printf("CellLocate position received, sensor_used: %d,  \r\n", loc.sensorUsed );           
            printf("  latitude: %0.5f, longitude: %0.5f, altitute: %d\r\n", loc.latitue, loc.longitude, loc.altitutude);
            if (loc.sensorUsed == 1)
                printf("  uncertainty: %d, speed: %d, direction: %d, vertical_acc: %d, satellite used: %d \r\n", loc.uncertainty,loc.speed,loc.direction,loc.verticalAcc,loc.svUsed);        
            if (loc.sensorUsed == 1 || loc.sensorUsed == 2)
            sprintf(link, "I am here!\n"
                        "https://maps.google.com/?q=%.5f,%.5f", loc.latitue, loc.longitude);       
        }
        if (cellLocWait && (j%100 == 0 ))
            printf("Waiting for CellLocate...\r\n");                
#endif        
        if (mdmOk && (i++ == 5000/wait)) {
            i = 0;
            // check the network status
            if (mdm.checkNetStatus(&netStatus)) {
                mdm.dumpNetStatus(&netStatus, fprintf, stdout);
            }
                
            // checking unread sms
            int ix[8];
            int n = mdm.smsList("REC UNREAD", ix, 8);
            if (8 < n) n = 8;
            while (0 < n--)
            {
                char num[32];
                printf("Unread SMS at index %d\r\n", ix[n]);
                if (mdm.smsRead(ix[n], num, buf, sizeof(buf))) {
                    printf("Got SMS from \"%s\" with text \"%s\"\r\n", num, buf);
                    printf("Delete SMS at index %d\r\n", ix[n]);
                    mdm.smsDelete(ix[n]);
                    // provide a reply
                    const char* reply = "Hello my friend";
                    if (strstr(buf, /*w*/"here are you"))
                        reply = *link ? link : "I don't know"; // reply wil location link
                    else if (strstr(buf, /*s*/"hutdown"))
                        abort = true, reply = "bye bye";
                    printf("Send SMS reply \"%s\" to \"%s\"\r\n", reply, num);
                    mdm.smsSend(num, reply);
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

#include "mbed.h"
#include "MDM.h"
#ifdef TARGET_UBLOX_C027
 #include "C027_api.h"
#endif
#include "MDMAPN.h"
                
#define PROFILE         "0"   //!< this is the psd profile used
#define MAX_SIZE        128   //!< max expected messages
// num sockets
#define NUMSOCKETS      (sizeof(_sockets)/sizeof(*_sockets))
//! test if it is a socket is ok to use
#define ISSOCKET(s)     (((s) >= 0) && ((s) < NUMSOCKETS) && (_sockets[s].handle != SOCKET_ERROR))
//! check for timeout
#define TIMEOUT(t, ms)  ((ms != TIMEOUT_BLOCKING) && (ms < t.read_ms())) 
//! registration ok check helper
#define REG_OK(r)       ((r == REG_HOME) || (r == REG_ROAMING)) 
//! registration done check helper (no need to poll further)
#define REG_DONE(r)     ((r == REG_HOME) || (r == REG_ROAMING) || (r == REG_DENIED)) 
//! helper to make sure that lock unlock pair is always balaced 
#define LOCK()         { lock() 
//! helper to make sure that lock unlock pair is always balaced 
#define UNLOCK()       } unlock()

#ifdef MDM_DEBUG
 #if 1 // colored terminal output using ANSI escape sequences
  #define COL(c) "\033[" c
 #else
  #define COL(c) 
 #endif
 #define DEF COL("39m")
 #define BLA COL("30m")
 #define RED COL("31m")
 #define GRE COL("32m")
 #define YEL COL("33m")
 #define BLU COL("34m")
 #define MAG COL("35m")
 #define CYA COL("36m")
 #define WHY COL("37m")
 
void dumpAtCmd(const char* buf, int len)
{
    ::printf(" %3d \"", len);
    while (len --) {
        char ch = *buf++;
        if ((ch > 0x1F) && (ch != 0x7F)) { // is printable
            if      (ch == '%')  ::printf("%%");
            else if (ch == '"')  ::printf("\\\"");
            else if (ch == '\\') ::printf("\\\\");
            else putchar(ch);
        } else {
            if      (ch == '\a') ::printf("\\a"); // BEL (0x07)
            else if (ch == '\b') ::printf("\\b"); // Backspace (0x08)
            else if (ch == '\t') ::printf("\\t"); // Horizontal Tab (0x09)
            else if (ch == '\n') ::printf("\\n"); // Linefeed (0x0A)
            else if (ch == '\v') ::printf("\\v"); // Vertical Tab (0x0B)
            else if (ch == '\f') ::printf("\\f"); // Formfeed (0x0C)
            else if (ch == '\r') ::printf("\\r"); // Carriage Return (0x0D)
            else                 ::printf("\\x%02x", (unsigned char)ch);
        }
    }
    ::printf("\"\r\n");
}
 
 #define ERROR(...)     (_debugLevel < 0) ? : ::printf(RED), ::printf(__VA_ARGS__), ::printf(DEF) 
 #define TEST(...)                            ::printf(CYA), ::printf(__VA_ARGS__), ::printf(DEF)  
 #define INFO(...)      (_debugLevel < 1) ? : ::printf(GRE), ::printf(__VA_ARGS__), ::printf(DEF) 
 #define TRACE(...)     (_debugLevel < 2) ? : ::printf(__VA_ARGS__)
 
#else
 
 #define ERROR(...) (void)0 // no tracing
 #define TEST(...)  (void)0 // no tracing
 #define INFO(...)  (void)0 // no tracing
 #define TRACE(...) (void)0 // no tracing

#endif

MDMParser* MDMParser::inst;

MDMParser::MDMParser(void)
{
    inst = this;
    memset(&_dev, 0, sizeof(_dev));
    memset(&_net, 0, sizeof(_net));
    _net.lac = 0xFFFF;
    _net.ci = 0xFFFFFFFF;
    _ip        = NOIP;
    _init      = false;
    memset(_sockets, 0, sizeof(_sockets));
    for (int socket = 0; socket < NUMSOCKETS; socket ++)
        _sockets[socket].handle = SOCKET_ERROR;
#ifdef MDM_DEBUG
    _debugLevel = 1;
    _debugTime.start();
#endif
}

int MDMParser::send(const char* buf, int len)
{
#ifdef MDM_DEBUG
    if (_debugLevel >= 3) {
        ::printf("%10.3f AT send    ", _debugTime.read_ms()*0.001);
        dumpAtCmd(buf,len);
    }
#endif
    return _send(buf, len);
}

int MDMParser::sendFormated(const char* format, ...) {
    char buf[MAX_SIZE];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf,sizeof(buf), format, args);
    va_end(args);
    return send(buf, len);
}

int MDMParser::waitFinalResp(_CALLBACKPTR cb /* = NULL*/, 
                             void* param /* = NULL*/, 
                             int timeout_ms /*= 5000*/)
{
    char buf[MAX_SIZE + 64 /* add some more space for framing */]; 
    Timer timer;
    timer.start();
    do {
        int ret = getLine(buf, sizeof(buf));
#ifdef MDM_DEBUG
        if ((_debugLevel >= 3) && (ret != WAIT) && (ret != NOT_FOUND))
        {
            int len = LENGTH(ret);
            int type = TYPE(ret);
            const char* s = (type == TYPE_UNKNOWN)? YEL "UNK" DEF : 
                            (type == TYPE_TEXT)   ? MAG "TXT" DEF : 
                            (type == TYPE_OK   )  ? GRE "OK " DEF : 
                            (type == TYPE_ERROR)  ? RED "ERR" DEF : 
                            (type == TYPE_PLUS)   ? CYA " + " DEF : 
                            (type == TYPE_PROMPT) ? BLU " > " DEF : 
                                                        "..." ;
            ::printf("%10.3f AT read %s", _debugTime.read_ms()*0.001, s);
            dumpAtCmd(buf, len);
        }
#endif        
        if ((ret != WAIT) && (ret != NOT_FOUND))
        {
            int type = TYPE(ret);
            // handle unsolicited commands here
            if (type == TYPE_PLUS) {
                const char* cmd = buf+3;
                int a, b, c, d, r;
                char s[32];

                // SMS Command ---------------------------------
                // +CNMI: <mem>,<index>
                if (sscanf(cmd, "CMTI: \"%*[^\"]\",%d", &a) == 1) { 
                    TRACE("New SMS at index %d\r\n", a);
                // Socket Specific Command ---------------------------------
                // +UUSORD: <socket>,<length>
                } else if ((sscanf(cmd, "UUSORD: %d,%d", &a, &b) == 2)) {
                    int socket = _findSocket(a);
                    TRACE("Socket %d: handle %d has %d bytes pending\r\n", socket, a, b);
                    if (socket != SOCKET_ERROR)
                        _sockets[socket].pending = b;
                // +UUSORF: <socket>,<length>
                } else if ((sscanf(cmd, "UUSORF: %d,%d", &a, &b) == 2)) {
                    int socket = _findSocket(a);
                    TRACE("Socket %d: handle %d has %d bytes pending\r\n", socket, a, b);
                    if (socket != SOCKET_ERROR)
                        _sockets[socket].pending = b;
                // +UUSOCL: <socket>
                } else if ((sscanf(cmd, "UUSOCL: %d", &a) == 1)) {
                    int socket = _findSocket(a);
                    TRACE("Socket %d: handle %d closed by remote host\r\n", socket, a);
                    if ((socket != SOCKET_ERROR) && _sockets[socket].connected)
                        _sockets[socket].connected = false;
                }
                if (_dev.dev == DEV_LISA_C200) {
                    // CDMA Specific -------------------------------------------
                    // +CREG: <n><SID>,<NID>,<stat>
                    if (sscanf(cmd, "CREG: %*d,%d,%d,%d",&a,&b,&c) == 3) {
                        // _net.sid = a;
                        // _net.nid = b;
                        if      (c == 0) _net.csd = REG_NONE;     // not registered, home network
                        else if (c == 1) _net.csd = REG_HOME;     // registered, home network
                        else if (c == 2) _net.csd = REG_NONE;     // not registered, but MT is currently searching a new operator to register to
                        else if (c == 3) _net.csd = REG_DENIED;   // registration denied
                        else if (c == 5) _net.csd = REG_ROAMING;  // registered, roaming
                        _net.psd = _net.csd; // fake PSD registration (CDMA is always registered)
                        _net.act = ACT_CDMA;
                        // +CSS: <mode>[,<format>,<oper>[,<AcT>]]
                    } else if (sscanf(cmd, "CSS %*c,%2s,%*d",s) == 1) {
                        //_net.reg = (strcmp("Z", s) == 0) ? REG_UNKNOWN : REG_HOME;
                    }
                } else {
                    // GSM/UMTS Specific -------------------------------------------
                    // +UUPSDD: <profile_id> 
                    if (sscanf(cmd, "UUPSDD: %d",&a) == 1) {
                        if (*PROFILE == a) _ip = NOIP;
                    } else {
                        // +CREG|CGREG: <n>,<stat>[,<lac>,<ci>[,AcT[,<rac>]]] // reply to AT+CREG|AT+CGREG
                        // +CREG|CGREG: <stat>[,<lac>,<ci>[,AcT[,<rac>]]]     // URC
                        b = 0xFFFF; c = 0xFFFFFFFF; d = -1;
                        r = sscanf(cmd, "%s %*d,%d,\"%X\",\"%X\",%d",s,&a,&b,&c,&d);
                        if (r <= 1)
                            r = sscanf(cmd, "%s %d,\"%X\",\"%X\",%d",s,&a,&b,&c,&d);
                        if (r >= 2) {
                            Reg *reg = !strcmp(s, "CREG:")  ? &_net.csd : 
                                       !strcmp(s, "CGREG:") ? &_net.psd : NULL;
                            if (reg) {
                                // network status
                                if      (a == 0) *reg = REG_NONE;     // 0: not registered, home network
                                else if (a == 1) *reg = REG_HOME;     // 1: registered, home network
                                else if (a == 2) *reg = REG_NONE;     // 2: not registered, but MT is currently searching a new operator to register to
                                else if (a == 3) *reg = REG_DENIED;   // 3: registration denied
                                else if (a == 4) *reg = REG_UNKNOWN;  // 4: unknown
                                else if (a == 5) *reg = REG_ROAMING;  // 5: registered, roaming
                                if ((r >= 3) && (b != 0xFFFF))      _net.lac = b; // location area code
                                if ((r >= 4) && (c != 0xFFFFFFFF))  _net.ci  = c; // cell ID
                                // access technology
                                if (r >= 5) {
                                    if      (d == 0) _net.act = ACT_GSM;      // 0: GSM
                                    else if (d == 1) _net.act = ACT_GSM;      // 1: GSM COMPACT
                                    else if (d == 2) _net.act = ACT_UTRAN;    // 2: UTRAN
                                    else if (d == 3) _net.act = ACT_EDGE;     // 3: GSM with EDGE availability
                                    else if (d == 4) _net.act = ACT_UTRAN;    // 4: UTRAN with HSDPA availability
                                    else if (d == 5) _net.act = ACT_UTRAN;    // 5: UTRAN with HSUPA availability
                                    else if (d == 6) _net.act = ACT_UTRAN;    // 6: UTRAN with HSDPA and HSUPA availability
                                }
                            }
                        }
                    }
                }
            }
            if (cb) {
                int len = LENGTH(ret);
                int ret = cb(type, buf, len, param);
                if (WAIT != ret)
                    return ret; 
            }
            if (type == TYPE_OK)
                return RESP_OK;
            if (type == TYPE_ERROR)
                return RESP_ERROR;
            if (type == TYPE_PROMPT)    
                return RESP_PROMPT;
        }
        // relax a bit
        wait_ms(10); 
    }
    while (!TIMEOUT(timer, timeout_ms));
    return WAIT;
}

int MDMParser::_cbString(int type, const char* buf, int len, char* str)
{
    if (str && (type == TYPE_UNKNOWN)) {
        if (sscanf(buf, "\r\n%s\r\n", str) == 1)
            /*nothing*/;
    }
    return WAIT;
}

int MDMParser::_cbInt(int type, const char* buf, int len, int* val)
{
    if (val && (type == TYPE_UNKNOWN)) {
        if (sscanf(buf, "\r\n%d\r\n", val) == 1)
            /*nothing*/;
    }
    return WAIT;
}

// ----------------------------------------------------------------

bool MDMParser::connect(
            const char* simpin, 
            const char* apn, const char* username, 
            const char* password, Auth auth,
            PinName pn)
{
    bool ok = init(simpin, NULL, pn);  
#ifdef MDM_DEBUG
    if (_debugLevel >= 1) dumpDevStatus(&_dev);
#endif
    if (!ok)
        return false;
    ok = registerNet();
#ifdef MDM_DEBUG
    if (_debugLevel >= 1) dumpNetStatus(&_net);
#endif
    if (!ok)
        return false;
    IP ip = join(apn,username,password,auth);
#ifdef MDM_DEBUG
    if (_debugLevel >= 1) dumpIp(ip);
#endif
    if (ip == NOIP)
        return false; 
    return true;
}

bool MDMParser::init(const char* simpin, DevStatus* status, PinName pn)
{
    int i = 10;
    LOCK();
    memset(&_dev, 0, sizeof(_dev));
    if (pn != NC) {
        INFO("Modem::wakeup\r\n");
        DigitalOut pin(pn, 1);
        while (i--) {
            // SARA-U2/LISA-U2 50..80us
            pin = 0; ::wait_us(50);
            pin = 1; ::wait_ms(10); 
            
            // SARA-G35 >5ms, LISA-C2 > 150ms, LEON-G2 >5ms
            pin = 0; ::wait_ms(150);
            pin = 1; ::wait_ms(100);
            
            // purge any messages 
            purge();
            
            // check interface
            sendFormated("AT\r\n");
            int r = waitFinalResp(NULL,NULL,1000);
            if(RESP_OK == r) break;
        }
        if (i < 0) {
            ERROR("No Reply from Modem\r\n");
            goto failure;
        }
    }
    _init = true;
    
    INFO("Modem::init\r\n");
    // echo off
    sendFormated("AT E0\r\n");
    if(RESP_OK != waitFinalResp())
        goto failure; 
    // enable verbose error messages
    sendFormated("AT+CMEE=2\r\n");
    if(RESP_OK != waitFinalResp())
        goto failure;
    // set baud rate
    sendFormated("AT+IPR=115200\r\n");
    if (RESP_OK != waitFinalResp())
        goto failure;
    wait_ms(40);
    // identify the module 
    sendFormated("ATI\r\n");
    if (RESP_OK != waitFinalResp(_cbATI, &_dev.dev))
        goto failure;
    if (_dev.dev == DEV_UNKNOWN)
        goto failure;
    // device specific init
    if (_dev.dev == DEV_LISA_C200) {
        // get the manufacturer
        sendFormated("AT+GMI\r\n");
        if (RESP_OK != waitFinalResp(_cbString, _dev.manu))
            goto failure;
        // get the model identification
        sendFormated("AT+GMM\r\n");
        if (RESP_OK != waitFinalResp(_cbString, _dev.model))
            goto failure;
        // get the sw version
        sendFormated("AT+GMR\r\n");
        if (RESP_OK != waitFinalResp(_cbString, _dev.ver))
            goto failure;
        // get the pseudo ESN or MEID
        sendFormated("AT+GSN\r\n");
        if (RESP_OK != waitFinalResp(_cbString, _dev.meid))
            goto failure;
#if 0
        // enable power saving
        if (_dev.lpm != LPM_DISABLED) {
             // enable power saving (requires flow control, cts at least)
            sendFormated("AT+UPSV=1,1280\r\n");
            if (RESP_OK != waitFinalResp())
                goto failure;  
            _dev.lpm = LPM_ACTIVE;
        }
#endif
    } else {
        if ((_dev.dev == DEV_LISA_U200) || (_dev.dev == DEV_LEON_G200)) {
            // enable the network identification feature 
            sendFormated("AT+UGPIOC=20,2\r\n");
            if (RESP_OK != waitFinalResp())
                goto failure;
        } else if ((_dev.dev == DEV_SARA_U260) || (_dev.dev == DEV_SARA_U270) || 
                   (_dev.dev == DEV_SARA_G350)) {
            // enable the network identification feature 
            sendFormated("AT+UGPIOC=16,2\r\n");
            if (RESP_OK != waitFinalResp())
                goto failure;
        }
        // check the sim card
        for (int i = 0; (i < 5) && (_dev.sim != SIM_READY); i++) {
            sendFormated("AT+CPIN?\r\n");
            int ret = waitFinalResp(_cbCPIN, &_dev.sim);
            // having an error here is ok (sim may still be initializing)
            if ((RESP_OK != ret) && (RESP_ERROR != ret))
                goto failure;
            // Enter PIN if needed
            if (_dev.sim == SIM_PIN) {
                if (!simpin) {
                    ERROR("SIM PIN not available\r\n");
                    goto failure;
                }
                sendFormated("AT+CPIN=%s\r\n", simpin);
                if (RESP_OK != waitFinalResp(_cbCPIN, &_dev.sim))
                    goto failure;
            } else if (_dev.sim != SIM_READY) {
                wait_ms(1000);
            }
        }
        if (_dev.sim != SIM_READY) {
            if (_dev.sim == SIM_MISSING)
                ERROR("SIM not inserted\r\n");
            goto failure;
        }
        // get the manufacturer
        sendFormated("AT+CGMI\r\n");
        if (RESP_OK != waitFinalResp(_cbString, _dev.manu))
            goto failure;
        // get the model identification
        sendFormated("AT+CGMM\r\n");
        if (RESP_OK != waitFinalResp(_cbString, _dev.model))
            goto failure;
        // get the 
        sendFormated("AT+CGMR\r\n");
        if (RESP_OK != waitFinalResp(_cbString, _dev.ver))
            goto failure;            
        // Returns the ICCID (Integrated Circuit Card ID) of the SIM-card. 
        // ICCID is a serial number identifying the SIM.
        sendFormated("AT+CCID\r\n");
        if (RESP_OK != waitFinalResp(_cbCCID, _dev.ccid))
            goto failure;
        // Returns the product serial number, IMEI (International Mobile Equipment Identity)
        sendFormated("AT+CGSN\r\n");
        if (RESP_OK != waitFinalResp(_cbString, _dev.imei))
            goto failure;
        // enable power saving
        if (_dev.lpm != LPM_DISABLED) {
             // enable power saving (requires flow control, cts at least)
            sendFormated("AT+UPSV=1\r\n");
            if (RESP_OK != waitFinalResp())
                goto failure;  
            _dev.lpm = LPM_ACTIVE;
        }
        // enable the psd registration unsolicited result code
        sendFormated("AT+CGREG=2\r\n");
        if (RESP_OK != waitFinalResp())
            goto failure;
    } 
    // enable the network registration unsolicited result code
    sendFormated("AT+CREG=%d\r\n", (_dev.dev == DEV_LISA_C200) ? 1 : 2);
    if (RESP_OK != waitFinalResp())
        goto failure;
    // Setup SMS in text mode 
    sendFormated("AT+CMGF=1\r\n");
    if (RESP_OK != waitFinalResp())
        goto failure;
    // setup new message indication
    sendFormated("AT+CNMI=2,1\r\n");
    if (RESP_OK != waitFinalResp())
        goto failure;
    // Request IMSI (International Mobile Subscriber Identification)
    sendFormated("AT+CIMI\r\n");
    if (RESP_OK != waitFinalResp(_cbString, _dev.imsi))
        goto failure;
    if (status)
        memcpy(status, &_dev, sizeof(DevStatus));
    UNLOCK();
    return true; 
failure:
    unlock();
    return false; 
}

bool MDMParser::powerOff(void)
{
    bool ok = false;
    if (_init) {
        LOCK();
        INFO("Modem::powerOff\r\n");
        sendFormated("AT+CPWROFF\r\n");
        if (RESP_OK == waitFinalResp(NULL,NULL,120*1000)) {
            _init = false;
            ok = true;
        }
        UNLOCK();
    }
    return ok;
}

int MDMParser::_cbATI(int type, const char* buf, int len, Dev* dev)
{
    if ((type == TYPE_UNKNOWN) && dev) {
        if      (strstr(buf, "SARA-G350")) *dev = DEV_SARA_G350;
        else if (strstr(buf, "LISA-U200")) *dev = DEV_LISA_U200;
        else if (strstr(buf, "LISA-C200")) *dev = DEV_LISA_C200;
        else if (strstr(buf, "SARA-U260")) *dev = DEV_SARA_U260;
        else if (strstr(buf, "SARA-U270")) *dev = DEV_SARA_U270;
        else if (strstr(buf, "LEON-G200")) *dev = DEV_LEON_G200;
    }
    return WAIT;
}

int MDMParser::_cbCPIN(int type, const char* buf, int len, Sim* sim)
{
    if (sim) {
        if (type == TYPE_PLUS){
            char s[16];
            if (sscanf(buf, "\r\n+CPIN: %[^\r]\r\n", s) >= 1)
                *sim = (0 == strcmp("READY", s)) ? SIM_READY : SIM_PIN;
        } else if (type == TYPE_ERROR) {
            if (strstr(buf, "+CME ERROR: SIM not inserted"))
                *sim = SIM_MISSING;
        }
    }
    return WAIT;
}

int MDMParser::_cbCCID(int type, const char* buf, int len, char* ccid)
{
    if ((type == TYPE_PLUS) && ccid){
        if (sscanf(buf, "\r\n+CCID: %[^\r]\r\n", ccid) == 1)
            /*TRACE("Got CCID: %s\r\n", ccid)*/;
    }
    return WAIT;
}

bool MDMParser::registerNet(NetStatus* status /*= NULL*/, int timeout_ms /*= 180000*/) 
{
    Timer timer;
    timer.start();
    INFO("Modem::register\r\n");
    while (!checkNetStatus(status) && !TIMEOUT(timer, timeout_ms))
        wait_ms(1000);
    if (_net.csd == REG_DENIED) ERROR("CSD Registration Denied\r\n");
    if (_net.psd == REG_DENIED) ERROR("PSD Registration Denied\r\n");
    return REG_OK(_net.csd) || REG_OK(_net.psd);
}

bool MDMParser::checkNetStatus(NetStatus* status /*= NULL*/)
{
    bool ok = false;
    LOCK();
    memset(&_net, 0, sizeof(_net));
    _net.lac = 0xFFFF;
    _net.ci = 0xFFFFFFFF;
    // check registration
    sendFormated("AT+CREG?\r\n");
    waitFinalResp();     // don't fail as service could be not subscribed 
    if (_dev.dev != DEV_LISA_C200) {
        // check PSD registration
        sendFormated("AT+CGREG?\r\n");
        waitFinalResp(); // don't fail as service could be not subscribed 
    }
    if (REG_OK(_net.csd) || REG_OK(_net.psd))
    {
        // check modem specific status messages 
        if (_dev.dev == DEV_LISA_C200) {
            sendFormated("AT+CSS?\r\n");
            if (RESP_OK != waitFinalResp())
                goto failure;
            while (1) {
                // get the Telephone number
                sendFormated("AT$MDN?\r\n");
                if (RESP_OK != waitFinalResp(_cbString, _net.num))
                    goto failure;
                // check if we have a Mobile Directory Number
                if (*_net.num && (memcmp(_net.num, "000000", 6) != 0))
                    break;
                    
                INFO("Device not yet activated\r\n");
                INFO("Make sure you have a valid contract with the network operator for this device.\r\n");
                // Check if the the version contains a V for Verizon 
                // Verizon: E0.V.xx.00.xxR, 
                // Sprint E0.S.xx.00.xxR
                if (_dev.ver[3] == 'V') { 
                    int i;
                    INFO("Start device over-the-air activation (this can take a few minutes)\r\n");
                    sendFormated("AT+CDV=*22899\r\n");
                    i = 1;
                    if ((RESP_OK != waitFinalResp(_cbUACTIND, &i, 120*1000)) || (i == 1)) {
                        ERROR("Device over-the-air activation failed\r\n");
                        goto failure;
                    }
                    INFO("Device over-the-air activation successful\r\n");
                    
                    INFO("Start PRL over-the-air update (this can take a few minutes)\r\n");
                    sendFormated("AT+CDV=*22891\r\n");
                    i = 1;
                    if ((RESP_OK != waitFinalResp(_cbUACTIND, &i, 120*1000)) || (i == 1)) {
                        ERROR("PRL over-the-air update failed\r\n");
                        goto failure;
                    }
                    INFO("PRL over-the-air update successful\r\n");
                    
                } else { 
                    // Sprint or Aeris 
                    INFO("Wait for OMA-DM over-the-air activation (this can take a few minutes)\r\n");
                    wait_ms(120*1000);
                }
            }
            // get the the Network access identifier string
            char nai[64];
            sendFormated("AT$QCMIPNAI?\r\n");
            if (RESP_OK != waitFinalResp(_cbString, nai))
                goto failure;
        } else {
            sendFormated("AT+COPS?\r\n");
            if (RESP_OK != waitFinalResp(_cbCOPS, &_net))
                goto failure;
            // get the MSISDNs related to this subscriber
            sendFormated("AT+CNUM\r\n");
            if (RESP_OK != waitFinalResp(_cbCNUM, _net.num))
                goto failure;
        }  
        // get the signal strength indication
        sendFormated("AT+CSQ\r\n");
        if (RESP_OK != waitFinalResp(_cbCSQ, &_net))
            goto failure;
    }
    if (status) {
        memcpy(status, &_net, sizeof(NetStatus));
    }
    ok = REG_DONE(_net.csd) && REG_DONE(_net.psd);
    UNLOCK();
    return ok;
failure:
    unlock();
    return false;
}

int MDMParser::_cbCOPS(int type, const char* buf, int len, NetStatus* status)
{
    if ((type == TYPE_PLUS) && status){
        int act = 99;
        // +COPS: <mode>[,<format>,<oper>[,<AcT>]]
        if (sscanf(buf, "\r\n+COPS: %*d,%*d,\"%[^\"]\",%d",status->opr,&act) >= 1) {
            if      (act == 0) status->act = ACT_GSM;      // 0: GSM, 
            else if (act == 2) status->act = ACT_UTRAN;    // 2: UTRAN
        }
    }
    return WAIT;
}

int MDMParser::_cbCNUM(int type, const char* buf, int len, char* num)
{
    if ((type == TYPE_PLUS) && num){
        int a;
        if ((sscanf(buf, "\r\n+CNUM: \"My Number\",\"%31[^\"]\",%d", num, &a) == 2) && 
            ((a == 129) || (a == 145))) {
        }
    }
    return WAIT;
}
                    
int MDMParser::_cbCSQ(int type, const char* buf, int len, NetStatus* status)
{
    if ((type == TYPE_PLUS) && status){
        int a,b;
        char _ber[] = { 49, 43, 37, 25, 19, 13, 7, 0 }; // see 3GPP TS 45.008 [20] subclause 8.2.4
        // +CSQ: <rssi>,<qual>
        if (sscanf(buf, "\r\n+CSQ: %d,%d",&a,&b) == 2) {
            if (a != 99) status->rssi = -113 + 2*a;  // 0: -113 1: -111 ... 30: -53 dBm with 2 dBm steps
            if ((b != 99) && (b < sizeof(_ber))) status->ber = _ber[b];  // 
        }
    }
    return WAIT;
}

int MDMParser::_cbUACTIND(int type, const char* buf, int len, int* i)
{
    if ((type == TYPE_PLUS) && i){
        int a;
        if (sscanf(buf, "\r\n+UACTIND: %d", &a) == 1) {
            *i = a;
        }
    }
    return WAIT;
}

// ----------------------------------------------------------------
// internet connection 

MDMParser::IP MDMParser::join(const char* apn /*= NULL*/, const char* username /*= NULL*/, 
                              const char* password /*= NULL*/, Auth auth /*= AUTH_DETECT*/)
{
    LOCK();
    INFO("Modem::join\r\n");
    _ip = NOIP;
    if (_dev.dev == DEV_LISA_C200) {
        // make a dumy dns lookup (which will fail, so ignore the result) 
        sendFormated("AT+UDNSRN=0,\"u-blox.com\"\r\n");
        waitFinalResp(); 
        // This fake lookup will enable the IP connection and we 
        // should have an IP after this, so we check it
        
        //Get local IP address
        sendFormated("AT+CMIP?\r\n");
        if (RESP_OK != waitFinalResp(_cbCMIP, &_ip))
            goto failure;
    } else { 
        // check gprs attach status 
        sendFormated("AT+CGATT=1\r\n");
        if (RESP_OK != waitFinalResp(NULL,NULL,3*60*1000)) 
            goto failure;
        
        // Check the profile
        int a = 0;
        bool force = true;
        sendFormated("AT+UPSND=" PROFILE ",8\r\n");
        if (RESP_OK != waitFinalResp(_cbUPSND, &a))
            goto failure;
        if (a == 1 && force) {
            // disconnect the profile already if it is connected 
            sendFormated("AT+UPSDA=" PROFILE ",4\r\n");
            if (RESP_OK != waitFinalResp(NULL,NULL,40*1000))
                goto failure;
            a = 0;
        }
        if (a == 0) {
            bool ok = false;
            // try to lookup the apn settings from our local database by mccmnc
            const char* config = NULL;
            if (!apn && !username && !password)
                config = apnconfig(_dev.imsi);
            
            // Set up the dynamic IP address assignment.
            sendFormated("AT+UPSD=" PROFILE ",7,\"0.0.0.0\"\r\n");
            if (RESP_OK != waitFinalResp())
                goto failure;
 
            do {
                if (config) {
                    apn      = _APN_GET(config);
                    username = _APN_GET(config);
                    password = _APN_GET(config);
                    TRACE("Testing APN Settings(\"%s\",\"%s\",\"%s\")\r\n", apn, username, password);
                }
                // Set up the APN
                if (apn && *apn) {
                    sendFormated("AT+UPSD=" PROFILE ",1,\"%s\"\r\n", apn);
                    if (RESP_OK != waitFinalResp())
                        goto failure;
                }
                if (username && *username) {
                    sendFormated("AT+UPSD=" PROFILE ",2,\"%s\"\r\n", username);
                    if (RESP_OK != waitFinalResp())
                        goto failure;
                }
                if (password && *password) {
                    sendFormated("AT+UPSD=" PROFILE ",3,\"%s\"\r\n", password);
                    if (RESP_OK != waitFinalResp())
                        goto failure;
                }
                // try different Authentication Protocols
                // 0 = none 
                // 1 = PAP (Password Authentication Protocol)
                // 2 = CHAP (Challenge Handshake Authentication Protocol)
                for (int i = AUTH_NONE; i <= AUTH_CHAP && !ok; i ++) {
                    if ((auth == AUTH_DETECT) || (auth == i)) {
                        // Set up the Authentication Protocol
                        sendFormated("AT+UPSD=" PROFILE ",6,%d\r\n", i);
                        if (RESP_OK != waitFinalResp())
                            goto failure;
                        // Activate the profile and make connection
                        sendFormated("AT+UPSDA=" PROFILE ",3\r\n");
                        if (RESP_OK == waitFinalResp(NULL,NULL,150*1000))
                            ok = true;
                    }
                }
            } while (!ok && config && *config); // maybe use next setting ? 
            if (!ok) {
                ERROR("Your modem APN/password/username may be wrong\r\n");
                goto failure;
            }
        }
        //Get local IP address
        sendFormated("AT+UPSND=" PROFILE ",0\r\n");
        if (RESP_OK != waitFinalResp(_cbUPSND, &_ip))
            goto failure;
    }
    UNLOCK();
    return _ip;
failure: 
    unlock();
    return NOIP;
}

int MDMParser::_cbUDOPN(int type, const char* buf, int len, char* mccmnc)
{
    if ((type == TYPE_PLUS) && mccmnc) {
        if (sscanf(buf, "\r\n+UDOPN: 0,\"%[^\"]\"", mccmnc) == 1)
            ;
    }
    return WAIT;
}

int MDMParser::_cbCMIP(int type, const char* buf, int len, IP* ip)
{
    if ((type == TYPE_UNKNOWN) && ip) {
        int a,b,c,d;
        if (sscanf(buf, "\r\n" IPSTR, &a,&b,&c,&d) == 4)
            *ip = IPADR(a,b,c,d);
    }
    return WAIT;
}
        
int MDMParser::_cbUPSND(int type, const char* buf, int len, int* act)
{
    if ((type == TYPE_PLUS) && act) {
        if (sscanf(buf, "\r\n+UPSND: %*d,%*d,%d", act) == 1)
            /*nothing*/;
    }
    return WAIT;
}

int MDMParser::_cbUPSND(int type, const char* buf, int len, IP* ip)
{
    if ((type == TYPE_PLUS) && ip) {
        int a,b,c,d;
        // +UPSND=<profile_id>,<param_tag>[,<dynamic_param_val>]
        if (sscanf(buf, "\r\n+UPSND: " PROFILE ",0,\"" IPSTR "\"", &a,&b,&c,&d) == 4)
            *ip = IPADR(a,b,c,d);
    }
    return WAIT;
}

int MDMParser::_cbUDNSRN(int type, const char* buf, int len, IP* ip)
{
    if ((type == TYPE_PLUS) && ip) {
        int a,b,c,d;
        if (sscanf(buf, "\r\n+UDNSRN: \"" IPSTR "\"", &a,&b,&c,&d) == 4)
            *ip = IPADR(a,b,c,d);
    }
    return WAIT;
}

bool MDMParser::disconnect(void)
{
    bool ok = false;
    LOCK();
    INFO("Modem::disconnect\r\n");
    if (_ip != NOIP) {
        if (_dev.dev == DEV_LISA_C200) {
            // There something to do here
            _ip = NOIP;
            ok = true;
        } else { 
            sendFormated("AT+UPSDA=" PROFILE ",4\r\n");
            if (RESP_OK != waitFinalResp()) {
                _ip = NOIP;
                ok = true;
            }
        }
    }
    UNLOCK();
    return ok;
}

MDMParser::IP MDMParser::gethostbyname(const char* host)
{
    IP ip = NOIP; 
    int a,b,c,d;
    if (sscanf(host, IPSTR, &a,&b,&c,&d) == 4)
        ip = IPADR(a,b,c,d);
    else {
        LOCK();
        sendFormated("AT+UDNSRN=0,\"%s\"\r\n", host);
        if (RESP_OK != waitFinalResp(_cbUDNSRN, &ip))
            ip = NOIP;
        UNLOCK();
    }
    return ip;
}

// ----------------------------------------------------------------
// sockets

int MDMParser::_cbUSOCR(int type, const char* buf, int len, int* handle)
{
    if ((type == TYPE_PLUS) && handle) {
        const char* p = strstr(buf,"+USOCR: "); 
        if (p)
            *handle = atoi(p+8);
    }
    return WAIT;
}

int MDMParser::socketSocket(IpProtocol ipproto, int port)
{
    int socket;
    LOCK();
    // find an free socket
    socket = _findSocket();
    TRACE("socketSocket(%d)\r\n", ipproto);
    if (socket != SOCKET_ERROR) {
        if ((ipproto == IPPROTO_UDP) && (port == -1)){
            sendFormated("AT+USOCR=17\r\n");
        } else if (ipproto == IPPROTO_UDP){
            sendFormated("AT+USOCR=17,%d\r\n", port);
        } else /*(ipproto == IPPROTO_TCP)*/ {
            sendFormated("AT+USOCR=6\r\n");
        } 
        int handle = SOCKET_ERROR;
        if ((RESP_OK == waitFinalResp(_cbUSOCR, &handle)) && 
            (handle != SOCKET_ERROR)) {
            TRACE("Socket %d: handle %d was created\r\n", socket, handle);
            _sockets[socket].handle     = handle;
            _sockets[socket].timeout_ms = TIMEOUT_BLOCKING;
            _sockets[socket].connected  = false;
            _sockets[socket].pending    = 0;
        }
        else
            socket = SOCKET_ERROR;
    }
    UNLOCK();
    return socket;
}

bool MDMParser::socketConnect(int socket, const char * host, int port)
{
    IP ip = gethostbyname(host);
    if (ip == NOIP)
        return false;
    // connect to socket
    bool ok = false; 
    LOCK();
    if (ISSOCKET(socket) && (!_sockets[socket].connected)) {
        TRACE("socketConnect(%d,%s,%d)\r\n", socket,host,port);
        sendFormated("AT+USOCO=%d,\"" IPSTR "\",%d\r\n", _sockets[socket].handle, IPNUM(ip), port);
        if (RESP_OK == waitFinalResp())
            ok = _sockets[socket].connected = true;
    }
    UNLOCK();
    return ok;
}

bool MDMParser::socketIsConnected(int socket)
{
    bool ok = false;
    LOCK();
    TRACE("socketIsConnected(%d)\r\n", socket);
    ok = ISSOCKET(socket) && _sockets[socket].connected;
    UNLOCK();
    return ok;
}

bool MDMParser::socketSetBlocking(int socket, int timeout_ms)
{
    bool ok = false;
    LOCK();
    TRACE("socketSetBlocking(%d,%d)\r\n", socket,timeout_ms);
    if (ISSOCKET(socket)) {
        _sockets[socket].timeout_ms = timeout_ms;
        ok = true;
    }
    UNLOCK();
    return ok;
}

bool  MDMParser::socketClose(int socket)
{
    bool ok = false;
    LOCK();
    if (ISSOCKET(socket) && _sockets[socket].connected) {
        TRACE("socketClose(%d)\r\n", socket);
        sendFormated("AT+USOCL=%d\r\n", _sockets[socket].handle);
        if (RESP_OK == waitFinalResp()) {
            _sockets[socket].connected = false;
            ok = true;
        }
    }
    UNLOCK();
    return ok;
}

bool  MDMParser::socketFree(int socket)
{
    // make sure it is closed
    socketClose(socket);
    bool ok = true;
    LOCK();
    if (ISSOCKET(socket)) {
        TRACE("socketFree(%d)\r\n",  socket);
        _sockets[socket].handle     = SOCKET_ERROR;
        _sockets[socket].timeout_ms = TIMEOUT_BLOCKING;
        _sockets[socket].connected  = false;
        _sockets[socket].pending    = 0;
        ok = true;
    }
    UNLOCK();
    return ok;
}

#define USO_MAX_WRITE 1024 //!< maximum number of bytes to write to socket

int MDMParser::socketSend(int socket, const char * buf, int len)
{
    TRACE("socketSend(%d,,%d)\r\n", socket,len);
    int cnt = len;
    while (cnt > 0) {
        int blk = USO_MAX_WRITE;
        if (cnt < blk) 
            blk = cnt;
        bool ok = false;
        LOCK();
        if (ISSOCKET(socket)) {
            sendFormated("AT+USOWR=%d,%d\r\n",_sockets[socket].handle,blk);
            if (RESP_PROMPT == waitFinalResp()) {
                wait_ms(50);
                send(buf, blk);
                if (RESP_OK == waitFinalResp()) 
                    ok = true;
            }
        }
        UNLOCK();
        if (!ok) 
            return SOCKET_ERROR;
        buf += blk;
        cnt -= blk;
    }
    return (len - cnt);
}

int MDMParser::socketSendTo(int socket, IP ip, int port, const char * buf, int len)
{
    TRACE("socketSendTo(%d," IPSTR ",%d,,%d)\r\n", socket,IPNUM(ip),port,len);
    int cnt = len;
    while (cnt > 0) {
        int blk = USO_MAX_WRITE;
        if (cnt < blk) 
            blk = cnt;
        bool ok = false;
        LOCK();
        if (ISSOCKET(socket)) {
            sendFormated("AT+USOST=%d,\"" IPSTR "\",%d,%d\r\n",_sockets[socket].handle,IPNUM(ip),port,blk);
            if (RESP_PROMPT == waitFinalResp()) {
                wait_ms(50);
                send(buf, blk);
                if (RESP_OK == waitFinalResp())
                    ok = true;
            }
        }
        UNLOCK();
        if (!ok)
            return SOCKET_ERROR;
        buf += blk;
        cnt -= blk;
    }
    return (len - cnt);
}

int MDMParser::socketReadable(int socket)
{
    int pending = SOCKET_ERROR;
    LOCK();
    if (ISSOCKET(socket) && _sockets[socket].connected) {
        TRACE("socketReadable(%d)\r\n", socket);
        // allow to receive unsolicited commands 
        waitFinalResp(NULL, NULL, 0);
        if (_sockets[socket].connected)
           pending = _sockets[socket].pending; 
    }
    UNLOCK();
    return pending;
}

int MDMParser::_cbUSORD(int type, const char* buf, int len, char* out)
{
    if ((type == TYPE_PLUS) && out) {
        int sz, sk;
        if ((sscanf(buf, "\r\n+USORD: %d,%d,", &sk, &sz) == 2) && 
            (buf[len-sz-2] == '\"') && (buf[len-1] == '\"')) {
            memcpy(out, &buf[len-1-sz], sz);
        }
    }
    return WAIT;
}

int MDMParser::socketRecv(int socket, char* buf, int len)
{
    int cnt = 0;
    TRACE("socketRecv(%d,,%d)\r\n", socket, len);
#ifdef MDM_DEBUG
    memset(buf, '\0', len);
#endif
    Timer timer;
    timer.start();
    while (len) {
        int blk = MAX_SIZE; // still need space for headers and unsolicited  commands 
        if (len < blk) blk = len;
        bool ok = false;        
        LOCK();
        if (ISSOCKET(socket)) {
            if (_sockets[socket].connected) {
                if (_sockets[socket].pending < blk)
                    blk = _sockets[socket].pending;
                if (blk > 0) {
                    sendFormated("AT+USORD=%d,%d\r\n",_sockets[socket].handle, blk);
                    if (RESP_OK == waitFinalResp(_cbUSORD, buf)) {
                        _sockets[socket].pending -= blk;
                        len -= blk;
                        cnt += blk;
                        buf += blk;
                        ok = true;
                    }
                } else if (!TIMEOUT(timer, _sockets[socket].timeout_ms)) {
                    ok = (WAIT == waitFinalResp(NULL,NULL,0)); // wait for URCs
                } else {
                    len = 0;
                    ok = true;
                }
            } else {
                len = 0;
                ok = true;
            }
        }
        UNLOCK();
        if (!ok)
            return SOCKET_ERROR;
    }
    return cnt;
}

int MDMParser::_cbUSORF(int type, const char* buf, int len, USORFparam* param)
{
    if ((type == TYPE_PLUS) && param) {
        int sz, sk, p, a,b,c,d;
        int r = sscanf(buf, "\r\n+USORF: %d,\"" IPSTR "\",%d,%d,", 
            &sk,&a,&b,&c,&d,&p,&sz);
        if ((r == 7) && (buf[len-sz-2] == '\"') && (buf[len-1] == '\"')) {
            memcpy(param->buf, &buf[len-1-sz], sz);
            param->ip = IPADR(a,b,c,d);
            param->port = p;
        }
    }
    return WAIT;
}

int MDMParser::socketRecvFrom(int socket, IP* ip, int* port, char* buf, int len)
{
    int cnt = 0;
    TRACE("socketRecvFrom(%d,,%d)\r\n", socket, len);
#ifdef MDM_DEBUG
    memset(buf, '\0', len);
#endif
    Timer timer;
    timer.start();
    while (len) {
        int blk = MAX_SIZE; // still need space for headers and unsolicited commands 
        if (len < blk) blk = len;
        bool ok = false;        
        LOCK();
        if (ISSOCKET(socket)) {
            if (_sockets[socket].pending < blk)
                blk = _sockets[socket].pending;
            if (blk > 0) {
                sendFormated("AT+USORF=%d,%d\r\n",_sockets[socket].handle, blk);
                USORFparam param;
                param.buf = buf;
                if (RESP_OK == waitFinalResp(_cbUSORF, &param)) {
                    _sockets[socket].pending -= blk;
                    *ip = param.ip;
                    *port = param.port;
                    len -= blk;
                    cnt += blk;
                    buf += blk;
                    len = 0; // done 
                    ok = true;
                }
            } else if (!TIMEOUT(timer, _sockets[socket].timeout_ms)) {
                ok = (WAIT == waitFinalResp(NULL,NULL,0)); // wait for URCs
            } else {
                len = 0; // no more data and socket closed or timed-out
                ok = true;
            }
        }
        UNLOCK();
        if (!ok)
            return SOCKET_ERROR;
    }
    timer.stop();
    timer.reset();
    return cnt;
}

int MDMParser::_findSocket(int handle) {
    for (int socket = 0; socket < NUMSOCKETS; socket ++) {
        if (_sockets[socket].handle == handle)
            return socket;
    }
    return SOCKET_ERROR;
}

// ----------------------------------------------------------------

int MDMParser::_cbCMGL(int type, const char* buf, int len, CMGLparam* param)
{ 
    if ((type == TYPE_PLUS) && param && param->num) {
        // +CMGL: <ix>,...
        int ix;
        if (sscanf(buf, "\r\n+CMGL: %d,", &ix) == 1)
        {
            *param->ix++ = ix;
            param->num--;
        }
    }
    return WAIT;
}

int MDMParser::smsList(const char* stat /*= "ALL"*/, int* ix /*=NULL*/, int num /*= 0*/) {
    int ret = -1;
    LOCK();
    sendFormated("AT+CMGL=\"%s\"\r\n", stat);
    CMGLparam param;
    param.ix = ix;
    param.num = num;
    if (RESP_OK == waitFinalResp(_cbCMGL, &param))
        ret = num - param.num;
    UNLOCK();
    return ret;
}

bool MDMParser::smsSend(const char* num, const char* buf)
{
    bool ok = false;
    LOCK();
    sendFormated("AT+CMGS=\"%s\"\r\n",num);
    if (RESP_PROMPT == waitFinalResp(NULL,NULL,150*1000)) {
        send(buf, strlen(buf));
        const char ctrlZ = 0x1A;
        send(&ctrlZ, sizeof(ctrlZ));
        ok = (RESP_OK == waitFinalResp());
    }
    UNLOCK();
    return ok;
}

bool MDMParser::smsDelete(int ix)
{
    bool ok = false;
    LOCK();
    sendFormated("AT+CMGD=%d\r\n",ix);
    ok = (RESP_OK == waitFinalResp());
    UNLOCK();
    return ok;
}

bool MDMParser::smsDelete()
{
    bool ok = false;
    LOCK();
    sendFormated("AT+CMGD=4\r\n");
    ok = (RESP_OK == waitFinalResp());
    UNLOCK();
    return ok;
}

int MDMParser::_cbCMGR(int type, const char* buf, int len, CMGRparam* param)
{
    if (param) {
        if (type == TYPE_PLUS) {
            if (sscanf(buf, "\r\n+CMGR: \"%*[^\"]\",\"%[^\"]", param->num) == 1) {
            }
        } else if ((type == TYPE_UNKNOWN) && (buf[len-2] == '\r') && (buf[len-1] == '\n')) {
            memcpy(param->buf, buf, len-2);
            param->buf[len-2] = '\0';
        }
    }
    return WAIT;
}

bool MDMParser::smsRead(int ix, char* num, char* buf, int len)
{
    bool ok = false;
    LOCK();
    CMGRparam param;
    param.num = num;
    param.buf = buf;
    sendFormated("AT+CMGR=%d\r\n",ix);
    ok = (RESP_OK == waitFinalResp(_cbCMGR, &param));
    UNLOCK();
    return ok;
}
   
// ----------------------------------------------------------------
  
int MDMParser::_cbCUSD(int type, const char* buf, int len, char* resp)
{
    if ((type == TYPE_PLUS) && resp) {
        // +USD: \"%*[^\"]\",\"%[^\"]\",,\"%*[^\"]\",%d,%d,%d,%d,\"*[^\"]\",%d,%d"..);
        if (sscanf(buf, "\r\n+CUSD: %*d,\"%[^\"]\",%*d", resp) == 1) {
            /*nothing*/            
        }
    }
    return WAIT;
}  

bool MDMParser::ussdCommand(const char* cmd, char* buf)
{
    bool ok = false;
    LOCK();
    *buf = '\0';
    if (_dev.dev != DEV_LISA_C200) {
        sendFormated("AT+CUSD=1,\"%s\"\r\n",cmd);
        ok = (RESP_OK == waitFinalResp(_cbCUSD, buf));
    }
    UNLOCK();
    return ok;
}

// ----------------------------------------------------------------
   
bool MDMParser::delFile(const char* filename)
{
    bool ok = false;
    LOCK();
    sendFormated("AT+UDELFILE=\"%s\"\r\n", filename);
    ok = (RESP_OK == waitFinalResp());
    UNLOCK();
    return ok;
}

int MDMParser::writeFile(const char* filename, const char* buf, int len)
{
    bool ok = false;
    LOCK();
    sendFormated("AT+UDWNFILE=\"%s\",%d\r\n", filename, len);
    if (RESP_PROMPT == waitFinalResp()) {
        send(buf, len);
        ok = (RESP_OK == waitFinalResp());
    }
    UNLOCK();
    return ok ? len : -1;
}

int MDMParser::readFile(const char* filename, char* buf, int len)
{
    URDFILEparam param;
    param.filename = filename;
    param.buf = buf; 
    param.sz = len; 
    param.len = 0;
    LOCK();
    sendFormated("AT+URDFILE=\"%s\"\r\n", filename, len);
    if (RESP_OK != waitFinalResp(_cbURDFILE, &param));
        param.len = -1;
    UNLOCK();
    return param.len;
}

int MDMParser::_cbURDFILE(int type, const char* buf, int len, URDFILEparam* param)
{
    if ((type == TYPE_PLUS) && param && param->filename && param->buf) {
        char filename[48];
        int sz;
        if ((sscanf(buf, "\r\n+URDFILE: \"%[^\"]\",%d,", filename, &sz) == 2) && 
            (0 == strcmp(param->filename, filename)) &&
            (buf[len-sz-2] == '\"') && (buf[len-1] == '\"')) {
            param->len = (sz < param->sz) ? sz : param->sz;
            memcpy(param->buf, &buf[len-1-sz], param->len);
        }
    }
    return WAIT;
}
  
// ----------------------------------------------------------------
bool MDMParser::setDebug(int level) 
{
#ifdef MDM_DEBUG
    if ((_debugLevel >= 0) && (level >= 0)) {
        _debugLevel = level;
        return true;
    }
#endif
    return false;
}

void MDMParser::dumpDevStatus(MDMParser::DevStatus* status, 
            _DPRINT dprint, void* param) 
{
    dprint(param, "Modem::devStatus\r\n");
    const char* txtDev[] = { "Unknown", "SARA-G350", "LISA-U200", "LISA-C200", "SARA-U260", "SARA-U270", "LEON-G200" };
    if (status->dev < sizeof(txtDev)/sizeof(*txtDev) && (status->dev != DEV_UNKNOWN))
        dprint(param, "  Device:       %s\r\n", txtDev[status->dev]);
    const char* txtLpm[] = { "Disabled", "Enabled", "Active" };
    if (status->lpm < sizeof(txtLpm)/sizeof(*txtLpm))
        dprint(param, "  Power Save:   %s\r\n", txtLpm[status->lpm]);
    const char* txtSim[] = { "Unknown", "Missing", "Pin", "Ready" };
    if (status->sim < sizeof(txtSim)/sizeof(*txtSim) && (status->sim != SIM_UNKNOWN))
        dprint(param, "  SIM:          %s\r\n", txtSim[status->sim]);
    if (*status->ccid)  
        dprint(param, "  CCID:         %s\r\n", status->ccid);
    if (*status->imei) 
        dprint(param, "  IMEI:         %s\r\n", status->imei);
    if (*status->imsi)  
        dprint(param, "  IMSI:         %s\r\n", status->imsi);
    if (*status->meid) 
        dprint(param, "  MEID:         %s\r\n", status->meid); // LISA-C
    if (*status->manu) 
        dprint(param, "  Manufacturer: %s\r\n", status->manu);
    if (*status->model)  
        dprint(param, "  Model:        %s\r\n", status->model);
    if (*status->ver)  
        dprint(param, "  Version:      %s\r\n", status->ver);
}

void MDMParser::dumpNetStatus(MDMParser::NetStatus *status,
            _DPRINT dprint, void* param)
{
    dprint(param, "Modem::netStatus\r\n");
    const char* txtReg[] = { "Unknown", "Denied", "None", "Home", "Roaming" };
    if (status->csd < sizeof(txtReg)/sizeof(*txtReg) && (status->csd != REG_UNKNOWN))
        dprint(param, "  CSD Registration:   %s\r\n", txtReg[status->csd]);
    if (status->psd < sizeof(txtReg)/sizeof(*txtReg) && (status->psd != REG_UNKNOWN))
        dprint(param, "  PSD Registration:   %s\r\n", txtReg[status->psd]);
    const char* txtAct[] = { "Unknown", "GSM", "Edge", "3G", "CDMA" };
    if (status->act < sizeof(txtAct)/sizeof(*txtAct) && (status->act != ACT_UNKNOWN))
        dprint(param, "  Access Technology:  %s\r\n", txtAct[status->act]);
    if (status->rssi) 
        dprint(param, "  Signal Strength:    %d dBm\r\n", status->rssi);
    if (status->ber) 
        dprint(param, "  Bit Error Rate:     %d\r\n", status->ber);
    if (*status->opr)  
        dprint(param, "  Operator:           %s\r\n", status->opr);
    if (status->lac != 0xFFFF)  
        dprint(param, "  Location Area Code: %04X\r\n", status->lac);
    if (status->ci != 0xFFFFFFFF)  
        dprint(param, "  Cell ID:            %08X\r\n", status->ci);
    if (*status->num)  
        dprint(param, "  Phone Number:       %s\r\n", status->num);
}

void MDMParser::dumpIp(MDMParser::IP ip,
            _DPRINT dprint, void* param) 
{
    if (ip != NOIP)
        dprint(param, "Modem:IP " IPSTR "\r\n", IPNUM(ip));
}
    
// ----------------------------------------------------------------
int MDMParser::_parseMatch(Pipe<char>* pipe, int len, const char* sta, const char* end)
{
    int o = 0;
    if (sta) {
        while (*sta) {
            if (++o > len)                  return WAIT;
            char ch = pipe->next();
            if (*sta++ != ch)               return NOT_FOUND;
        }
    }
    if (!end)                               return o; // no termination
    // at least any char
    if (++o > len)                      return WAIT;
    pipe->next();
    // check the end     
    int x = 0;
    while (end[x]) {
        if (++o > len)                      return WAIT;
        char ch = pipe->next();
        x = (end[x] == ch) ? x + 1 : 
            (end[0] == ch) ? 1 : 
                            0;
    }
    return o;
}

int MDMParser::_parseFormated(Pipe<char>* pipe, int len, const char* fmt)
{
    int o = 0;
    int num = 0;
    if (fmt) {
        while (*fmt) {
            if (++o > len)                  return WAIT;
            char ch = pipe->next();
            if (*fmt == '%') {
                fmt++;
                if (*fmt == 'd') { // numeric
                    fmt ++;
                    num = 0;
                    while (ch >= '0' && ch <= '9') {
                        num = num * 10 + (ch - '0'); 
                        if (++o > len)      return WAIT;
                        ch = pipe->next();
                    }
                }   
                else if (*fmt == 'c') { // char buffer (takes last numeric as length)
                    fmt ++;
                    while (num --) {
                        if (++o > len)      return WAIT;
                        ch = pipe->next();
                    }   
                }
                else if (*fmt == 's') {
                    fmt ++;
                    if (ch != '\"')         return NOT_FOUND;
                    do {
                        if (++o > len)      return WAIT;
                        ch = pipe->next();
                    } while (ch != '\"');
                    if (++o > len)          return WAIT;
                    ch = pipe->next();
                }
            }
            if (*fmt++ != ch)               return NOT_FOUND;
        }
    }
    return o; 
}

int MDMParser::_getLine(Pipe<char>* pipe, char* buf, int len)
{
    int unkn = 0;
    int sz = pipe->size();
    int fr = pipe->free();
    if (len > sz)
        len = sz;
    while (len > 0)
    {
        static struct { 
              const char* fmt;                              int type; 
        } lutF[] = {
            { "\r\n+USORD: %d,%d,\"%c\"",                   TYPE_PLUS       },
            { "\r\n+USORF: %d,\"" IPSTR "\",%d,%d,\"%c\"",  TYPE_PLUS       },
            { "\r\n+URDFILE: %s,%d,\"%c\"",                 TYPE_PLUS       },
        };
        static struct { 
              const char* sta;          const char* end;    int type; 
        } lut[] = {
            { "\r\nOK\r\n",             NULL,               TYPE_OK         },
            { "\r\nERROR\r\n",          NULL,               TYPE_ERROR      },
            { "\r\n+CME ERROR:",        "\r\n",             TYPE_ERROR      }, 
            { "\r\n+CMS ERROR:",        "\r\n",             TYPE_ERROR      },
            { "\r\nRING\r\n",           NULL,               TYPE_RING       },
            { "\r\nCONNECT\r\n",        NULL,               TYPE_CONNECT    },
            { "\r\nNO CARRIER\r\n",     NULL,               TYPE_NOCARRIER  },
            { "\r\nNO DIALTONE\r\n",    NULL,               TYPE_NODIALTONE },
            { "\r\nBUSY\r\n",           NULL,               TYPE_BUSY       },
            { "\r\nNO ANSWER\r\n",      NULL,               TYPE_NOANSWER   },
            { "\r\n+",                  "\r\n",             TYPE_PLUS       },
            { "\r\n@",                  NULL,               TYPE_PROMPT     }, // Sockets
            { "\r\n>",                  NULL,               TYPE_PROMPT     }, // SMS
            { "\n>",                    NULL,               TYPE_PROMPT     }, // File
        };
        for (int i = 0; i < sizeof(lutF)/sizeof(*lutF); i ++) {
            pipe->set(unkn);
            int ln = _parseFormated(pipe, len, lutF[i].fmt);
            if (ln == WAIT && fr)                       
                return WAIT;
            if ((ln != NOT_FOUND) && (unkn > 0))  
                return TYPE_UNKNOWN | pipe->get(buf, unkn);
            if (ln > 0)
                return lutF[i].type  | pipe->get(buf, ln);
        }
        for (int i = 0; i < sizeof(lut)/sizeof(*lut); i ++) {
            pipe->set(unkn);
            int ln = _parseMatch(pipe, len, lut[i].sta, lut[i].end);
            if (ln == WAIT && fr)                       
                return WAIT;
            if ((ln != NOT_FOUND) && (unkn > 0))  
                return TYPE_UNKNOWN | pipe->get(buf, unkn);
            if (ln > 0)
                return lut[i].type | pipe->get(buf, ln);
        }
        // UNKNOWN
        unkn ++;
        len--;
    }
    return WAIT;
}

// ----------------------------------------------------------------
// Serial Implementation 
// ----------------------------------------------------------------

/*! Helper Dev Null Device 
    Small helper class used to shut off stderr/stdout. Sometimes stdin/stdout
    is shared with the serial port of the modem. Having printfs inbetween the 
    AT commands you cause a failure of the modem.
*/
class DevNull : public Stream {
public: 
    DevNull() : Stream(_name+1) { }             //!< Constructor
    void claim(const char* mode, FILE* file) 
        { freopen(_name, mode, file); }         //!< claim a stream
protected:
    virtual int _getc()         { return EOF; } //!< Nothing
    virtual int _putc(int c)    { return c; }   //!< Discard
    static const char* _name;                   //!< File name
};
const char* DevNull::_name = "/null";  //!< the null device name
static      DevNull null;              //!< the null device

MDMSerial::MDMSerial(PinName tx /*= MDMTXD*/, PinName rx /*= MDMRXD*/, 
            int baudrate /*= MDMBAUD*/,
#if DEVICE_SERIAL_FC
            PinName rts /*= MDMRTS*/, PinName cts /*= MDMCTS*/, 
#endif
            int rxSize /*= 256*/, int txSize /*= 128*/) : 
            SerialPipe(tx, rx, rxSize, txSize) 
{
    if (rx == USBRX) 
        null.claim("r", stdin);
    if (tx == USBTX) {
        null.claim("w", stdout);
        null.claim("w", stderr);
#ifdef MDM_DEBUG
        _debugLevel = -1;
#endif
    }
#ifdef TARGET_UBLOX_C027
    _onboard = (tx == MDMTXD) && (rx == MDMRXD);
    if (_onboard)
       c027_mdm_powerOn(false);
#endif
    baud(baudrate);
#if DEVICE_SERIAL_FC
    if ((rts != NC) || (cts != NC))
    {
        Flow flow = (cts == NC) ? RTS :
                    (rts == NC) ? CTS : RTSCTS ;
        set_flow_control(flow, rts, cts);
        if (cts != NC) _dev.lpm = LPM_ENABLED;
    }
#endif
}

MDMSerial::~MDMSerial(void)
{
    powerOff();
#ifdef TARGET_UBLOX_C027
    if (_onboard)
        c027_mdm_powerOff();
#endif
}

int MDMSerial::_send(const void* buf, int len)   
{ 
    return put((const char*)buf, len, true/*=blocking*/);
}

int MDMSerial::getLine(char* buffer, int length)
{
    return _getLine(&_pipeRx, buffer, length);
}

// ----------------------------------------------------------------
// USB Implementation 
// ----------------------------------------------------------------

#ifdef HAVE_MDMUSB
MDMUsb::MDMUsb(void)                             
{ 
#ifdef MDM_DEBUG
    _debugLevel = 1;
#endif    
#ifdef TARGET_UBLOX_C027
    _onboard = true;
    c027_mdm_powerOn(true);
#endif
}

MDMUsb::~MDMUsb(void)
{
    powerOff();
#ifdef TARGET_UBLOX_C027
    if (_onboard)
        c027_mdm_powerOff();
#endif
}

int MDMUsb::_send(const void* buf, int len)      { return 0; }

int MDMUsb::getLine(char* buffer, int length)    { return NOT_FOUND; }

#endif

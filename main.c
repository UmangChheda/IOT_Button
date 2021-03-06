/*
*******************************************************************************
			IFTTT IOT BUTTON
*******************************************************************************
*/
// Standard includes
#include <stdlib.h>
#include <string.h>

// Simplelink includes
#include "simplelink.h"

//Driverlib includes
#include "hw_types.h"
#include "hw_ints.h"
#include "rom.h"
#include "rom_map.h"
#include "interrupt.h"
#include "prcm.h"
#include "utils.h"
#include "stdbool.h"
#include "stdint.h"
#include "gpio.h"
#include "hw_memmap.h"
#include "hw_common_reg.h"
#include "hw_apps_rcm.h"

//TI-rtos includes
#include "osi.h"

//Common interface includes
#include "gpio_if.h"
#ifndef NOTERM
#include "uart_if.h"
#endif
#include "common.h"
#include "pinmux.h"


#define APPLICATION_NAME        "IOT_BUTTON"
#define APPLICATION_VERSION     "1.1.1"
//Post Data Text
#define POST_TEXT "POST /trigger/cc3200_iot_btn_event/with/key/YOUR_KEY HTTP/1.1\r\n"
//Post Data Value for SW3 button
#define POST_VALUE "{\"value1\" : \"\", \"value2\" : \"SW3\", \"value3\" : \"\" }"

#define WLAN_DEL_ALL_PROFILE	0xFF
#define TASK1_STACK_SIZE	1024
#define TASK2_STACK_SIZE	6000
#define TASK1_PRIORITY		1
#define TASK2_PRIORITY        	2

// Application specific status/error codes
typedef enum{
    // Choosing -0x7D0 to avoid overlap w/ host-driver's error codes
    LAN_CONNECTION_FAILED = -0x7D0,
    INTERNET_CONNECTION_FAILED = LAN_CONNECTION_FAILED - 1,
    DEVICE_NOT_IN_STATION_MODE = INTERNET_CONNECTION_FAILED - 1,
    STATUS_CODE_MAX = -0xBB8
}e_AppStatusCodes;

// Push Button Events
typedef enum events{
    PUSH_BUTTON_SW2_PRESSED,
    PUSH_BUTTON_SW3_PRESSED
}osi_messages;

//*****************************************************************************
//                 GLOBAL VARIABLES
//*****************************************************************************
unsigned long  g_ulStatus = 0;//SimpleLink Status
unsigned long  g_ulGatewayIP = 0; //Network Gateway IP address
unsigned char  g_ucConnectionSSID[SSID_LEN_MAX+1]; //Connection SSID
unsigned char  g_ucConnectionBSSID[BSSID_LEN_MAX]; //Connection BSSID
SlSockAddrIn_t  Addr;							//Structure for Socket Details
int iAddrSize;
int iSockID;								//Socket Descriptor
int iStatus;								//Signed to check the status of sl_connect
bool deviceConnected = false;						//Variable indicating the device connection status
bool ipAcquired = false;						//Variable indicating IP address acquiring status
OsiMsgQ_t Int_Msg;							//Message Queue

#if defined(gcc)
extern void (* const g_pfnVectors[])(void);
#endif
#if defined(ewarm)
extern uVectorEntry __vector_table;
#endif
//*****************************************************************************
//                GLOBAL VARIABLES -- End
//*****************************************************************************



//****************************************************************************
//             	 LOCAL FUNCTION PROTOTYPES
//****************************************************************************
static long WlanConnect();
void Wlan_Task( void *pvParameters );
static void InitializeAppVariables();
static void IFTTT_Trigger(void);
static void InitializeInterrupts(void);
void SW2IntHandler(void);
void SW3IntHandler(void);
int Smart_Config(void);
void SwitchToStaMode(int iMode);


//*******************************************************************************************
//	\brief This Function Handles the asynchoronous events generated by the WiFi Subsystem
//	Two Events which might generate are:
//	-WiFi is Sucessfully Connected to the AP
//	-WiFi is Disconnected from the AP
//
//	param[in] pArgs - Contains relavant WiFi event information
//
//	\return None
//*******************************************************************************************
void SimpleLinkWlanEventHandler(SlWlanEvent_t *pArgs)
{
	switch (pArgs->Event) {
	        case SL_WLAN_CONNECT_EVENT:
	            deviceConnected = true;
	            break;

	        case SL_WLAN_DISCONNECT_EVENT:
	            deviceConnected = false;
	            break;

	        default:
	            break;
	    }
}
//*********************************************************************************
//	\brief This Function handles the asynchronous events of the network events.

//	This function is invoked when a IP is acquired from the AP through DHCP
//
//	param[in]  pNetAppEvent - Contains the relevant event information
//
//	\return None
//**********************************************************************************
void SimpleLinkNetAppEventHandler(SlNetAppEvent_t *pNetAppEvent)
{
    switch(pNetAppEvent->Event)
    {
        case SL_NETAPP_IPV4_IPACQUIRED_EVENT:

           ipAcquired = true;
           break;

        default:
           break;
    }
}

//*****************************************************************************
//	\breif This Function Initialises the Variables

//	param[in] None

//	\return None
//*****************************************************************************
static void InitializeAppVariables()
{
    g_ulStatus = 0;
    g_ulGatewayIP = 0;
    memset(g_ucConnectionSSID,0,sizeof(g_ucConnectionSSID));
    memset(g_ucConnectionBSSID,0,sizeof(g_ucConnectionBSSID));
}

//*****************************************************************************
//	\brief This Function Initializes the Interrupts
//	Register SW3 on  the launchpad as interrupt
//	param[in] None
//
//	\return None
//*****************************************************************************
static void InitializeInterrupts()
{
	MAP_GPIOIntTypeSet(GPIOA1_BASE, GPIO_PIN_5,GPIO_FALLING_EDGE);				//Set the Interrupt as Falling Edge and on GPIO_PIN_5
	osi_InterruptRegister(INT_GPIOA1,(P_OSI_INTR_ENTRY)SW2IntHandler, \
	                                INT_PRIORITY_LVL_1);
	MAP_GPIOIntClear(GPIOA1_BASE,GPIO_PIN_5);						//Initally Clear any pending Interrupt
	MAP_GPIOIntEnable(GPIOA1_BASE,GPIO_INT_PIN_5);						//Enable the Interrupt

	MAP_GPIOIntTypeSet(GPIOA2_BASE, GPIO_PIN_6,GPIO_FALLING_EDGE);				//Set the Interrupt as Falling Edge and on GPIO_PIN_6
	osi_InterruptRegister(INT_GPIOA2,(P_OSI_INTR_ENTRY)SW3IntHandler, \
		                                INT_PRIORITY_LVL_1);
	MAP_GPIOIntClear(GPIOA2_BASE,GPIO_PIN_6);						//Initally Clear any pending Interrupt
	MAP_GPIOIntEnable(GPIOA2_BASE,GPIO_INT_PIN_6);						//Enable the Interrupt
}


//*****************************************************************************
// \brief Interrupt Handler for SW3
// Writes a message to message queue when an interrupt occurs
// Interrupt to Trigger IFTTT receipe
//*****************************************************************************
void SW2IntHandler(void)
{
	osi_messages var = PUSH_BUTTON_SW3_PRESSED;
	osi_MsgQWrite(&Int_Msg,&var,OSI_NO_WAIT);
	MAP_IntPendClear(INT_GPIOA1);
	MAP_GPIOIntClear(GPIOA1_BASE,GPIO_PIN_5);
}


//*****************************************************************************
// \brief Interrupt Handler for SW2
// Writes a message to message queue when an interrupt Occurs
// Interrupt to start Smart Config mode
//*****************************************************************************
void SW3IntHandler(void)
{
	osi_messages var = PUSH_BUTTON_SW2_PRESSED;
	osi_MsgQWrite(&Int_Msg,&var,OSI_NO_WAIT);
	MAP_IntPendClear(INT_GPIOA2);
	MAP_GPIOIntClear(GPIOA2_BASE,GPIO_PIN_6);
}

//*****************************************************************************
//
//! \brief This function handles HTTP server events
//!
//! \param[in]  pServerEvent - Contains the relevant event information
//! \param[in]    pServerResponse - Should be filled by the user with the
//!                                      relevant response information
//!
//! \return None
//!
//****************************************************************************
void SimpleLinkHttpServerCallback(SlHttpServerEvent_t *pHttpEvent,
                                  SlHttpServerResponse_t *pHttpResponse)
{
    // Unused in this application
}

//***************************************************************************
// \brief This Function configures the following:
//	-Switches the device to Station Mode
//	-Sets the Scan Policy
//	-Sets the Power Policy
//	-Sets the Connection Policy
//	-Enables DHCP client
//
//	params[in] - Return value of sl_start
//
//	\return value None
//****************************************************************************
void SwitchToStaMode(int iMode)
{
	unsigned char ucVal = 1;
	unsigned char ucConfigOpt = 0;
	long lRetVal = -1;

    if(iMode != ROLE_STA)
    {
        sl_WlanSetMode(ROLE_STA);
        MAP_UtilsDelay(80000);
        sl_Stop(10);
        MAP_UtilsDelay(80000);
        sl_Start(NULL,NULL,NULL);
    }

    //Set Connection Policy to Auto+SmartConfig
    lRetVal = sl_WlanPolicySet(SL_POLICY_CONNECTION,SL_CONNECTION_POLICY(1, 0, 0, 0, 1), NULL, 0);

    //Set Power Policy to Normal
    lRetVal = sl_WlanPolicySet(SL_POLICY_PM , SL_NORMAL_POLICY, NULL, 0);

    //Enable DHCP Client
    lRetVal = sl_NetCfgSet(SL_IPV4_STA_P2P_CL_DHCP_ENABLE,1,1,&ucVal);

    //Disable Scan Policy
    ucConfigOpt = SL_SCAN_POLICY(0);
    lRetVal = sl_WlanPolicySet(SL_POLICY_SCAN , ucConfigOpt, NULL, 0);
}


//*****************************************************************************
// \brief  This function handles asynchorous socket events
//
// \param[in] pSock - Contains relavant information about the socket error
//
// \return None
//
//*****************************************************************************

void SimpleLinkSockEventHandler(SlSockEvent_t *pSock)
{
    //
    // This application doesn't work w/ socket - Events are not expected
    //
    switch( pSock->Event )
    {
        case SL_SOCKET_TX_FAILED_EVENT:
            switch( pSock->socketAsyncEvent.SockTxFailData.status)
            {
                case SL_ECLOSE: 
                    UART_PRINT("[SOCK ERROR] - close socket (%d) operation "
                                "failed to transmit all queued packets\n\n", 
                                    pSock->socketAsyncEvent.SockTxFailData.sd);
                    break;
                default: 
                    UART_PRINT("[SOCK ERROR] - TX FAILED  :  socket %d , reason "
                                "(%d) \n\n",
                                pSock->socketAsyncEvent.SockTxFailData.sd, pSock->socketAsyncEvent.SockTxFailData.status);
                  break;
            }
            break;

        default:
        	UART_PRINT("[SOCK EVENT] - Unexpected Event [%x0x]\n\n",pSock->Event);
          break;
    }

}

//****************************************************************************
//
//! \brief Connecting to a WLAN Accesspoint
//!
//!  This function connects to the required AP (SSID_NAME) with Security
//!  parameters specified in te form of macros at the top of this file
//!
//! \param  None
//!
//! \return  None
//!
//
//****************************************************************************
static long WlanConnect()
{
    SlSecParams_t secParams = {0};
    long lRetVal = 0;
    secParams.Key = (signed char*)SECURITY_KEY;
    secParams.KeyLen = strlen(SECURITY_KEY);
    secParams.Type = SECURITY_TYPE;
    lRetVal = sl_WlanConnect((signed char*)SSID_NAME, strlen(SSID_NAME), 0, &secParams, 0);
    // Wait for WLAN Event
    while((deviceConnected != true) || (ipAcquired != true))
    { 
        // Toggle LEDs to Indicate Connection Progress
        GPIO_IF_LedOff(MCU_IP_ALLOC_IND);
        MAP_UtilsDelay(800000);
        GPIO_IF_LedOn(MCU_IP_ALLOC_IND);
        MAP_UtilsDelay(800000);
    }
     return SUCCESS;
}

//****************************************************************************
//
//! //! This Task starts the simplelink, connects to the ap and
//! checks for SmartConfig Event.
//!
//!
//! \param[in]  pvParameters - Pointer to the list of parameters that 
//!             can be passed to the task while creating it
//!
//! \return  None
//
//****************************************************************************
void Wlan_Task( void *pvParameters )
{
	osi_messages RecvQue;
	long lRetVal = -1;
	InitializeAppVariables();
	lRetVal = sl_Start(NULL, NULL, NULL);
		if (lRetVal < 0)
		{
			UART_PRINT("Failed to start the device \n\r");
			LOOP_FOREVER();
		}
	SwitchToStaMode(lRetVal);
    UART_PRINT("Device started as STATION \n\r");
    //Connecting to WLAN AP

    lRetVal = WlanConnect();
    if(lRetVal < 0){
         LOOP_FOREVER();
    }
    UART_PRINT("Connection established w/ AP and IP is aquired \n\r");

    while (1)
    {
     	osi_MsgQRead( &Int_Msg, &RecvQue, OSI_WAIT_FOREVER);

     	if(PUSH_BUTTON_SW2_PRESSED == RecvQue){
     		UART_PRINT("SW2 Pressed\r\n");
     		deviceConnected = false;
     		ipAcquired = false;
     		//Start Smart Config Mode
     		Smart_Config();
     	}

     	osi_Sleep(50);
    }
}

//*************************************************************************
//	\brief This  task listens for any messages in the mesaage queue and
// if relavant message is there then it triggers POST request accordingly
//
// \param[in] pvParameters - Pointer to the list of parameters that
//!             			can be passed to the task while creating it
//
// \return None
//*************************************************************************
void Event( void *pvParameters )
{
	 osi_messages RecvQue;
	 while(1)
	 {
		osi_MsgQRead( &Int_Msg, &RecvQue, OSI_WAIT_FOREVER);
		if(PUSH_BUTTON_SW3_PRESSED == RecvQue){
			IFTTT_Trigger();
		}
		 osi_Sleep(50);
	 }
}

//**********************************************************
// \brief This Function connects to the IFTTT Server and
//	makes HTTP Post Request

// param[in] None
//
// return None
//
// warning! Error checking on the data sent is not done
//**********************************************************
void IFTTT_Trigger(void)
{
	int iTXStatus;
	char acSendBuff[512];
	Addr.sin_family = SL_AF_INET;
	Addr.sin_port = sl_Htons(80); // default port for http
	Addr.sin_addr.s_addr = sl_Htonl((unsigned int)0x3210F92E);		//Hex Value of IP address of IFTTT Server
	iAddrSize = sizeof(SlSockAddrIn_t);
	iSockID = sl_Socket(SL_AF_INET,SL_SOCK_STREAM,0);

	if( iSockID < 0 ){
  	GPIO_IF_LedOn(MCU_RED_LED_GPIO);
	LOOP_FOREVER();
	}
	//Establish Connection with IFTTT server
	iStatus = sl_Connect(iSockID, ( SlSockAddr_t *)&Addr, iAddrSize);

	if( iStatus < 0 ){
		sl_Close(iSockID);
	}

	strcpy(acSendBuff, POST_TEXT);
	iTXStatus = sl_Send(iSockID, acSendBuff, strlen(acSendBuff), 0);

	iTXStatus = sl_Send(iSockID,"Host: maker.ifttt.com\r\n", strlen("Host: maker.ifttt.com\r\n"), 0);

	iTXStatus = sl_Send(iSockID, "User-Agent: IOT-Button\r\n", strlen("User-Agent: IOT-Button\r\n"), 0);

	iTXStatus = sl_Send(iSockID, "Connection: close\r\n", strlen("Connection: close\r\n"), 0);

	iTXStatus = sl_Send(iSockID, "Content-Type: application/json\r\n\r\n", strlen("Content-Type: application/json\r\n\r\n"), 0);

	iTXStatus = sl_Send(iSockID, "Content-Length: 65\r\n", strlen("Content-Length: 65\r\n"), 0);

	iTXStatus = sl_Send(iSockID, "\r\n", strlen("\r\n"), 0);

	strcpy(acSendBuff,POST_VALUE);
	iTXStatus = sl_Send(iSockID, acSendBuff, strlen(acSendBuff), 0);

	iTXStatus = sl_Send(iSockID, "\r\n", strlen("\r\n"), 0);

	if(iTXStatus<0){
	sl_Close(iSockID);
	return;
	}

	//Toogle LED to indicate data succesfully sent
	GPIO_IF_LedOff(MCU_IP_ALLOC_IND);
	MAP_UtilsDelay(800000);
	GPIO_IF_LedOn(MCU_IP_ALLOC_IND);
	MAP_UtilsDelay(800000);
	sl_Close(iSockID);
}

//****************************************************************************
// \brief This functions changes the mode to Smart Config Mode and waits for
// the configuration to be completed
//
// param[in] none
//
// \return Status value

//****************************************************************************
int Smart_Config()
{
    unsigned char policyVal;
    long lRetVal = -1;
    lRetVal = sl_WlanProfileDel(WLAN_DEL_ALL_PROFILES);
    ASSERT_ON_ERROR(lRetVal);
    //set AUTO policy
    lRetVal = sl_WlanPolicySet(  SL_POLICY_CONNECTION,
                      SL_CONNECTION_POLICY(1,0,0,0,1),
                      &policyVal,
                      1 /*PolicyValLen*/);

    // Start SmartConfig
    //
    lRetVal = sl_WlanSmartConfigStart(0,                /*groupIdBitmask*/
                           SMART_CONFIG_CIPHER_NONE,    /*cipher*/
                           0,                           /*publicKeyLen*/
                           0,                           /*group1KeyLen*/
                           0,                           /*group2KeyLen */
                           NULL,                        /*publicKey */
                           NULL,                        /*group1Key */
                           NULL);                       /*group2Key*/


    // Wait for WLAN Event
    while((deviceConnected != true) || (ipAcquired != true))
    {
        // Toggle LEDs to Indicate Connection Progress
        GPIO_IF_LedOff(MCU_IP_ALLOC_IND);
        MAP_UtilsDelay(800000);
        GPIO_IF_LedOn(MCU_IP_ALLOC_IND);
        MAP_UtilsDelay(800000);
    }
     // Turn ON the RED LED to indicate connection success
     //
     GPIO_IF_LedOn(MCU_RED_LED_GPIO);
     //wait for few moments
     MAP_UtilsDelay(80000000);
     //reset to default AUTO policy
     lRetVal = sl_WlanPolicySet(  SL_POLICY_CONNECTION,
                           SL_CONNECTION_POLICY(1,0,0,0,0),
                           &policyVal,
                           1 /*PolicyValLen*/);
     	 return SUCCESS;
}

//*****************************************************************************
//
//! \brief  Board Initialization & Configuration
//!
//! \param  None
//!
//! \return None
//
//*****************************************************************************
static void
BoardInit(void)
{
// In case of TI-RTOS vector table is initialize by OS itself
#ifndef USE_TIRTOS
    //
    // Set vector table base
    //
#if defined(ccs) || defined(gcc)
    MAP_IntVTableBaseSet((unsigned long)&g_pfnVectors[0]);
#endif
#if defined(ewarm)
    MAP_IntVTableBaseSet((unsigned long)&__vector_table);
#endif
#endif //USE_TIRTOS

    // Enable Processor
    MAP_IntMasterEnable();
    MAP_IntEnable(FAULT_SYSTICK);
    PRCMCC3200MCUInit();
}


//*****************************************************************************
//                            MAIN FUNCTION
//*****************************************************************************
void main()
{
    long lRetVal = -1;
    // Board Initialization
    BoardInit();
    // configure the GPIO pins for LEDs,UART
    PinMuxConfig();
   // Configure the UART
    //
#ifndef NOTERM
    InitTerm();
#endif  //NOTERM
    
     GPIO_IF_LedConfigure(LED1|LED2|LED3);

    // switch off all LEDs
    GPIO_IF_LedOff(MCU_ALL_LED_IND);
    InitializeAppVariables();

    //Initialise Interrupt
    InitializeInterrupts();
    //
    // Start the SimpleLink Host
    //
    lRetVal = VStartSimpleLinkSpawnTask(SPAWN_TASK_PRIORITY);
    if(lRetVal < 0){
        ERR_PRINT(lRetVal);
        LOOP_FOREVER();
    }

    //Create Message Queue
    osi_MsgQCreate(&Int_Msg,"PBQueue",sizeof(osi_messages),10);

    // Start the Wlan_Task task
    lRetVal = osi_TaskCreate( Wlan_Task, \
                                (const signed char*)"WlanTask", \
                                TASK1_STACK_SIZE, NULL, TASK1_PRIORITY, NULL );
    if(lRetVal < 0){
        ERR_PRINT(lRetVal);
        LOOP_FOREVER();
    }

    //Start the Event Task
     lRetVal = osi_TaskCreate( Event,(const signed char*)"Event", \
                                       TASK2_STACK_SIZE, NULL, TASK2_PRIORITY, NULL );
    if(lRetVal < 0){
    	ERR_PRINT(lRetVal);
    	LOOP_FOREVER();
    }
        // Start the task scheduler
    	osi_start();
 }

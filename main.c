/***************************************************************************//**
* \file main.c
* \version 1.0
*
* \details This is the source code for the USB-QSPI Bridge Application 
*           for ModusToolbox.
*
*           See \ref ./README.md ["README.md"]
*
*
*******************************************************************************
* \copyright
* (c) (2026), Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.
*
* SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "FreeRTOS.h"
#include "cy_hbdma_mgr.h"
#include "queue.h"
#include "task.h"
#include "cy_pdl.h"
#include "cybsp.h"
#include <string.h>
#include "cy_usb_common.h"
#include "cy_usb_usbd.h"
#include "usb_app.h"
#include "cy_debug.h"
#include "cy_usbd_version.h"
#include "cy_hbdma_version.h"
#include "app_version.h"
#include "spi.h"
#include "msc_demo.h"

/* Select SCB interface used for UART based logging. */
#define LOGGING_SCB             (SCB4)
#define LOGGING_SCB_IDX         (4)
#define DEBUG_LEVEL             (3u)

/* Debug log related initialisation */
#if DEBUG_INFRA_EN

#define LOGBUF_SIZE (1024u)
uint8_t logBuff[LOGBUF_SIZE];


#if USBFS_LOGS_ENABLE
    cy_stc_debug_config_t dbgCfg = {logBuff, DEBUG_LEVEL, LOGBUF_SIZE, CY_DEBUG_INTFCE_USBFS_CDC, true};
#else
    cy_stc_debug_config_t dbgCfg = {logBuff, DEBUG_LEVEL, LOGBUF_SIZE, CY_DEBUG_INTFCE_UART_SCB4, true};
#endif /* USBFS_LOGS_ENABLE */ 

TaskHandle_t printLogTaskHandle;
#endif /* DEBUG_INFRA_EN */

/* CPU DMA register pointers. */
DMAC_Type *pCpuDmacBase;
DW_Type   *pCpuDw0Base;
DW_Type   *pCpuDw1Base;

cy_stc_usb_usbd_ctxt_t  usbdCtxt;
cy_stc_usb_app_ctxt_t   appCtxt;
cy_stc_usb_cal_ctxt_t hsCalCtxt;
cy_en_smif_slave_select_t glSlaveSelect = CY_SMIF_SLAVE_SELECT_0;

/* USB HS related descriptors */
extern const uint8_t CyFxUSB20DeviceDscr[];
extern const uint8_t CyFxUSBFSConfigDscr[];
extern const uint8_t CyFxUSBHSConfigDscr[];

/* Common descriptors shared across speed. */
extern const uint8_t CyFxUSBStringLangIDDscr[];
extern const uint8_t CyFxUSBManufactureDscr[];
extern const uint8_t CyFxUSBProductDscr[];
extern const uint8_t CyFxUSBSerNumString[];
extern const uint8_t CyFxUSBDeviceQualDscr[];
extern const uint8_t CyFxUSBBOSDscr[];

extern void xPortPendSVHandler( void );
extern void xPortSysTickHandler( void );
extern void vPortSVCHandler( void );

/* Extern declaration for MSC state variables */
extern bool     glCswPending;
extern uint32_t glMscTag;
extern uint32_t glMscResidue;
extern uint8_t  glCswStatus;

/* Extern declaration for the global write buffer used for CBW reads */
extern uint8_t *glWriteBuffer;
extern uint8_t *glCbwBuffer;

/**
 * \name vPortSetupTimerInterrupt
 * \retval None
 */
void vPortSetupTimerInterrupt(void)
{
    /* Register the exception vectors. */
    Cy_SysInt_SetVector(PendSV_IRQn, xPortPendSVHandler);
    Cy_SysInt_SetVector(SVCall_IRQn, vPortSVCHandler);
    Cy_SysInt_SetVector(SysTick_IRQn, xPortSysTickHandler);

    /* Start the SysTick timer with a period of 1 ms. */
    Cy_SysTick_SetClockSource(CY_SYSTICK_CLOCK_SOURCE_CLK_CPU);
    Cy_SysTick_SetReload(Cy_SysClk_ClkFastGetFrequency() / 1000U);
    Cy_SysTick_Clear();
    Cy_SysTick_Enable();
}

/**
 * \name Cy_Fx2g3_InitPeripheralClocks
 * \brief Function used to enable clocks to different peripherals on the FX2G3 device.
 * \param adcClkEnable Whether to enable clock to the ADC in the USBSS block.
 * \param usbfsClkEnable Whether to enable bus reset detect clock input to the USBFS block.
 * \retval None
 */
void Cy_Fx2g3_InitPeripheralClocks (
        bool adcClkEnable,
        bool usbfsClkEnable)
{
    if (adcClkEnable) {
        /* Divide PERI clock at 75 MHz by 75 to get 1 MHz clock using 16-bit divider #1. */
        Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 1, 74);
        Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 1);
        Cy_SysLib_DelayUs(10U);
        Cy_SysClk_PeriphAssignDivider(PCLK_LVDS2USB32SS_CLOCK_SAR, CY_SYSCLK_DIV_16_BIT, 1);
    }

    if (usbfsClkEnable) {
        /* Divide PERI clock at 75 MHz by 750 to get 100 KHz clock using 16-bit divider #2. */
        Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_16_BIT, 2, 749);
        Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_16_BIT, 2);
        Cy_SysLib_DelayUs(10U);
        Cy_SysClk_PeriphAssignDivider(PCLK_USB_CLOCK_DEV_BRS, CY_SYSCLK_DIV_16_BIT, 2);
    }
}

/**
 * \name Cy_Fx2g3_OnResetInit
 * \details This function performs initialization that is required to enable scatter
 * loading of data into the High BandWidth RAM during device boot-up. The FX2G3
 * device comes up with the High BandWidth RAM disabled and hence any attempt
 * to read/write the RAM will cause the processor to hang. The RAM needs to
 * be enabled with default clock settings to allow scatter loading to work.
 * \note This function needs to be called from Cy_OnResetUser.
 * \retval None
 */
void
Cy_Fx2g3_OnResetInit (
        void)
{
    /* Enable clk_hf4 with IMO as input. */
    SRSS->CLK_ROOT_SELECT[4] = SRSS_CLK_ROOT_SELECT_ENABLE_Msk;

    /* Enable LVDS2USB32SS IP and select clk_hf[4] as clock input. */
    MAIN_REG->CTRL = (
            MAIN_REG_CTRL_IP_ENABLED_Msk |
            (1UL << MAIN_REG_CTRL_NUM_FAST_AHB_STALL_CYCLES_Pos) |
            (1UL << MAIN_REG_CTRL_NUM_SLOW_AHB_STALL_CYCLES_Pos) |
            (3UL << MAIN_REG_CTRL_DMA_SRC_SEL_Pos));
}

/**
 * \name ConfigurePeripheralClocks
 * \brief Configure the clock applied to USBFS IP block.
 * \retval None
 */
void ConfigurePeripheralClocks()
{
    /* Configure PERI 16 bit clock divider for 100 KHz operation and enable it. */
    Cy_SysClk_PeriphSetDivider (CY_SYSCLK_DIV_16_BIT, 1, 749);
    Cy_SysClk_PeriphEnableDivider (CY_SYSCLK_DIV_16_BIT, 1);
    Cy_SysLib_DelayUs(10);

    /* Connect the PERI clock to the USBFS input. */
    Cy_SysClk_PeriphAssignDivider(PCLK_USB_CLOCK_DEV_BRS, CY_SYSCLK_DIV_16_BIT, 1);
}

#if DEBUG_INFRA_EN
void PrintTaskHandler(void *pTaskParam)
{
    while(1)
    {
        /* Print any pending logs to the output console. */
        Cy_Debug_PrintLog();

        /* Put the thread to sleep for 5 ms */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
#endif /* DEBUG_INFRA_EN */

/**
 * \name Cy_USB_HS_ISR
 * \brief Handler for USB-HS Interrupts.
 * \retval None
 */
void Cy_USB_HS_ISR(void)
{
    if (Cy_USBHS_Cal_IntrHandler(&hsCalCtxt))
    {
        portYIELD_FROM_ISR(true);
    }
}

/**
 * \name Cy_PrintVersionInfo
 * \brief Function to print version information to UART console.
 * \param type Type of version string.
 * \param version Version number including major, minor, patch and build number.
 * \retval None
 */
void Cy_PrintVersionInfo(const char *type, uint32_t version)
{
    char tString[32];
    uint16_t vBuild;
    uint8_t vMajor, vMinor, vPatch;
    uint8_t typeLen = strlen(type);

    vMajor = (version >> 28U);
    vMinor = ((version >> 24U) & 0x0FU);
    vPatch = ((version >> 16U) & 0xFFU);
    vBuild = (uint16_t)(version & 0xFFFFUL);

    memcpy(tString, type, typeLen);
    tString[typeLen++] = '0' + (vMajor / 10);
    tString[typeLen++] = '0' + (vMajor % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vMinor / 10);
    tString[typeLen++] = '0' + (vMinor % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vPatch / 10);
    tString[typeLen++] = '0' + (vPatch % 10);
    tString[typeLen++] = '.';
    tString[typeLen++] = '0' + (vBuild / 1000);
    tString[typeLen++] = '0' + ((vBuild % 1000) / 100);
    tString[typeLen++] = '0' + ((vBuild % 100) / 10);
    tString[typeLen++] = '0' + (vBuild % 10);
    tString[typeLen++] = '\r';
    tString[typeLen++] = '\n';
    tString[typeLen] = 0;

    Cy_Debug_AddToLog(1,"%s", tString);
}

/**
 * \name Cy_USB_VbusDetGpio_ISR
 * \brief Interrupt handler for the Vbus detect GPIO transition detection.
 * \retval None
 */
static void Cy_USB_VbusDetGpio_ISR(void)
{
    BaseType_t xHigherPriorityTaskWoken;
    cy_stc_usbd_app_msg_t xMsg;

    /* Send VBus changed message to the task thread. */
    xMsg.type = CY_USB_VBUS_CHANGE_INTR;
    xQueueSendFromISR(appCtxt.xQueue, &(xMsg), &(xHigherPriorityTaskWoken));

    /* Remember that VBus change has happened and disable the interrupt. */
    appCtxt.vbusChangeIntr = true;
    Cy_GPIO_SetInterruptMask(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN, 0);
}

/**
 * \name Cy_USB_USBHSInit
 * \brief Initialize USBHS block and attempt device enumeration.
 * \retval None
 */
void Cy_USB_USBHSInit (void)
{
    cy_stc_gpio_pin_config_t pinCfg;
    cy_stc_sysint_t intrCfg;

    /* Do all the relevant clock configuration */
    Cy_Fx2g3_InitPeripheralClocks(false, true);

    /* Unlock and then disable the watchdog. */
    Cy_WDT_Unlock();
    Cy_WDT_Disable();

    /* Enable interrupts. */
    __enable_irq();

    memset((void *)&pinCfg, 0, sizeof(pinCfg));

    /* Configure VBus detect GPIO. */
    pinCfg.driveMode = CY_GPIO_DM_HIGHZ;
    pinCfg.hsiom     = HSIOM_SEL_GPIO;
    pinCfg.intEdge   = CY_GPIO_INTR_BOTH;
    pinCfg.intMask   = 0x01UL;
    Cy_GPIO_Pin_Init(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN, &pinCfg);

    /* CM0+: Interrupt source to NVIC Mux map
       VBUS(GPIO#4) - NVIC Mux #2
       USBHS Active & DeepSleep - NVIC Mux #5
    */
    /* Register edge detect interrupt for Vbus detect GPIO. */
#if CY_CPU_CORTEX_M4
    intrCfg.intrSrc = VBUS_DETECT_GPIO_INTR;
    intrCfg.intrPriority = 7;
#else
    intrCfg.cm0pSrc = VBUS_DETECT_GPIO_INTR;
    intrCfg.intrSrc = NvicMux2_IRQn;
    intrCfg.intrPriority = 3;
#endif /* CY_CPU_CORTEX_M4 */
    Cy_SysInt_Init(&intrCfg, Cy_USB_VbusDetGpio_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /*
     * Enable the DMA buffer RAM
     * need to be enabled before we try to do High BandWidth DMA initialization.
     */
    MAIN_REG->CTRL |= MAIN_REG_CTRL_IP_ENABLED_Msk;
    /* Register ISR for and enable USBHS Interrupt. */
#if CY_CPU_CORTEX_M4
    intrCfg.intrSrc      = usbhsdev_interrupt_u2d_active_o_IRQn;
    intrCfg.intrPriority = 4;
#else
    intrCfg.cm0pSrc      = usbhsdev_interrupt_u2d_active_o_IRQn;
    intrCfg.intrSrc      = NvicMux5_IRQn;
    intrCfg.intrPriority = 2;
#endif /* CY_CPU_CORTEX_M4 */
    Cy_SysInt_Init(&intrCfg, Cy_USB_HS_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* Register ISR for and enable USBHS Interrupt. */
#if CY_CPU_CORTEX_M4
    intrCfg.intrSrc      = usbhsdev_interrupt_u2d_dpslp_o_IRQn;
    intrCfg.intrPriority = 4;
#else
    intrCfg.cm0pSrc      = usbhsdev_interrupt_u2d_dpslp_o_IRQn;
    intrCfg.intrSrc      = NvicMux5_IRQn;
    intrCfg.intrPriority = 2;
#endif /* CY_CPU_CORTEX_M4 */
    Cy_SysInt_Init(&intrCfg, Cy_USB_HS_ISR);
    NVIC_EnableIRQ(intrCfg.intrSrc);
}

/**
 * \name Cy_USB_DisableUsbBlock
 * \brief   Function to disable the USB IP block after terminating current
 * connection.
 * \retval None
 */
static void Cy_USB_DisableUsbBlock (void)
{
    DBG_APP_INFO("USB DISABLE\r\n");
}

/**
 * \name Cy_USB_ConnectionDisable
 * \brief Function which disables the USB connection.
 * \param pAppCtxt Application context structure pointer.
 * \retval None
 */
void Cy_USB_ConnectionDisable (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    Cy_USBD_DisconnectDevice(pAppCtxt->pUsbdCtxt);
    pAppCtxt->devState = CY_USB_DEVICE_STATE_DISABLE;
    Cy_USB_DisableUsbBlock();
}

/**
 * \name Cy_USB_ConnectionEnable
 * \brief Function used to enable USB connection.
 * \param pAppCtxt Application context structure.
 * \retval None
 */
bool Cy_USB_ConnectionEnable (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    DBG_APP_INFO("Cy_USB_ConnectionEnable >>\r\n");
    DBG_APP_INFO("HS\r\n");
    Cy_USBD_ConnectDevice(pAppCtxt->pUsbdCtxt, CY_USBD_USB_DEV_HS);
    pAppCtxt->usbConnected = true;

    Cy_SysLib_DelayUs(10);
    DBG_APP_INFO("Cy_USB_ConnectionEnable <<\r\n");
    return true;
}

/**
 * \name InEpDma_ISR
 * \brief Handler for DMA transfer completion on IN endpoint.
 * This is now the key to sequencing the Data and Status phases.
 * \retval None
 */
void InEpDma_ISR (uint8_t endpNum)
{
    DBG_APP_TRACE("...ISR...InEpDma...endpNum:0x%x\r\n",endpNum);
    Cy_USB_AppClearCpuDmaInterrupt(&appCtxt, endpNum, CY_USB_ENDP_DIR_IN);


    /* Check if a CSW is pending after this data transfer. */
    if (glCswPending)
    {
        glCswPending = false;
        Cy_USB_MSC_BOT_SendCSW(&appCtxt, glMscTag, glMscResidue, glCswStatus);

        /* Now that the full transaction is complete, queue a read for the next CBW. */
        if (glCbwBuffer != NULL) {
            DBG_APP_TRACE("Queueing next CBW read, buffer: 0x%08X\r\n", (unsigned int)glCbwBuffer);
            Cy_USB_AppQueueRead(&appCtxt, 1, glCbwBuffer, sizeof(CBW_t));
        } else {
            DBG_APP_ERR("glCbwBuffer is NULL in InEpDma_ISR\r\n");
        }
    }

    portYIELD_FROM_ISR(true);
}


/**
 * \name OutEpDma_ISR
 * \brief Handler for DMA transfer completion on OUT endpoint.
 * \retval None
 */
void
OutEpDma_ISR (uint8_t endpNum)
{
    /*
     * For this application, a single BULK_OUT endpoint is considered. So, hardcoded
     * value is used here for `endpNum`.
     */

    if (endpNum == 0x00) {
        Cy_USB_AppClearCpuDmaInterrupt(&appCtxt, endpNum, CY_USB_ENDP_DIR_OUT);
        Cy_USB_Endp0ReadComplete((void *)&appCtxt);
    } else {
        Cy_USB_AppClearCpuDmaInterrupt(&appCtxt, endpNum, CY_USB_ENDP_DIR_OUT);
        Cy_USB_OutEpDma_READ_Complete(&appCtxt);
    }
    portYIELD_FROM_ISR(true);
}

/* Global variables associated with High BandWidth DMA setup. */
cy_stc_hbdma_context_t HBW_DrvCtxt;     /* High BandWidth DMA driver context. */
cy_stc_hbdma_dscr_list_t HBW_DscrList;  /* High BandWidth DMA descriptor free list. */
cy_stc_hbdma_buf_mgr_t HBW_BufMgr;      /* High BandWidth DMA buffer manager. */
cy_stc_hbdma_mgr_context_t HBW_MgrCtxt; /* High BandWidth DMA manager context. */

uint8_t *rw_buffer;

/**
 * \name Cy_App_HbDmaInit
 * \brief Initialize HBDMA block.
 * \retval None
 */
bool Cy_App_HbDmaInit(void)
{
    cy_en_hbdma_status_t drvstat;
    cy_en_hbdma_mgr_status_t mgrstat;

    /* Initialize the HBW DMA driver layer. */
    drvstat = Cy_HBDma_Init(LVDSSS_LVDS, USB32DEV, &HBW_DrvCtxt, 0, 0);
    if (drvstat != CY_HBDMA_SUCCESS)
    {
        return false;
    }

    /* Setup a HBW DMA descriptor list. */
    mgrstat = Cy_HBDma_DscrList_Create(&HBW_DscrList, 256U);
    if (mgrstat != CY_HBDMA_MGR_SUCCESS)
    {
        return false;
    }

    /* Initialize the DMA buffer manager. We will use 512 KB of space from 0x1C030000 onwards. */
    mgrstat = Cy_HBDma_BufMgr_Create(&HBW_BufMgr, (uint32_t *)0x1C030000UL, 0x80000UL);
    if (mgrstat != CY_HBDMA_MGR_SUCCESS)
    {
        return false;
    }

    /* Initialize the HBW DMA channel manager. */
    mgrstat = Cy_HBDma_Mgr_Init(&HBW_MgrCtxt, &HBW_DrvCtxt, &HBW_DscrList, &HBW_BufMgr);
    if (mgrstat != CY_HBDMA_MGR_SUCCESS)
    {
        return false;
    }

    return true;
}

/**
 * \name main
 * \brief Entry to the application.
 * \retval None
 */
int main (void)
{
    pCpuDmacBase = ((DMAC_Type *)DMAC_BASE);
    pCpuDw0Base = ((DW_Type *)DW0_BASE);
    pCpuDw1Base = ((DW_Type *)DW1_BASE);

    /* Initialize the PDL driver library and set the clock variables. */
    /* Note: All FX devices,  share a common configuration structure. */
    Cy_PDL_Init(&cy_deviceIpBlockCfgFX3G2);

    /* Initialize the device and board peripherals */
    cybsp_init();

    /* Initialize the PDL and register ISR for USB block. */
    Cy_USB_USBHSInit();

#if DEBUG_INFRA_EN
#if !USBFS_LOGS_ENABLE
    /* Initialize the UART for logging. */
    InitUart(LOGGING_SCB_IDX);
#endif /* USBFS_LOGS_ENABLE */

    Cy_Debug_LogInit(&dbgCfg);

    /* Create task for printing logs and check status. */
    xTaskCreate(PrintTaskHandler, "PrintLogTask", 512, NULL, 5, &printLogTaskHandle);

    Cy_SysLib_Delay(500);
    Cy_Debug_AddToLog(1, "********** FX2G3: USB-QSPI Bridge ********** \r\n");
    
    /* Print application, USBD stack and HBDMA version information. */
    Cy_PrintVersionInfo("APP_VERSION: ", APP_VERSION_NUM);
    Cy_PrintVersionInfo("USBD_VERSION: ", USBD_VERSION_NUM);
    Cy_PrintVersionInfo("HBDMA_VERSION: ", HBDMA_VERSION_NUM);
#endif /* DEBUG_INFRA_EN */

    Cy_App_HbDmaInit();
    /* Allocate 256 Bytes to the buffer */
    rw_buffer = Cy_HBDma_BufMgr_Alloc(&HBW_BufMgr, 256);
    DBG_APP_INFO("HbDmaInit done\r\n");

    memset((void *)&usbdCtxt, 0, sizeof(cy_stc_usb_usbd_ctxt_t));
    memset((void *)&hsCalCtxt, 0, sizeof(cy_stc_usb_cal_ctxt_t));
    memset((void *)&appCtxt, 0, sizeof(cy_stc_usb_app_ctxt_t));

    /* Store IP base address in CAL context. */
    hsCalCtxt.pCalBase = MXS40USBHSDEV_USBHSDEV;
    hsCalCtxt.pPhyBase = MXS40USBHSDEV_USBHSPHY;

    Cy_SysLib_Delay(500);

    /* Initialize the USBD layer */
    Cy_USB_USBD_Init(&appCtxt, &usbdCtxt, pCpuDmacBase, &hsCalCtxt, NULL, NULL);
    DBG_APP_INFO("USBD_Init done\r\n");

    Cy_USBD_SetDmaClkFreq(&usbdCtxt, CY_HBDMA_CLK_240_MHZ);

    /* Enable stall cycles between back-to-back AHB accesses to high bandwidth RAM. */
    MAIN_REG->CTRL = (MAIN_REG->CTRL & 0xF00FFFFFUL) | 0x09900000UL;

    /* USB 2.0 related descriptors. */
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_DEVICE_DSCR,
            0, (uint8_t *)CyFxUSB20DeviceDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_FS_CONFIG_DSCR,
            0, (uint8_t *)CyFxUSBFSConfigDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_CONFIG_DSCR,
            0, (uint8_t *)CyFxUSBHSConfigDscr);
     Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_DEVICE_QUAL_DSCR,
            0, (uint8_t *)CyFxUSBDeviceQualDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_HS_BOS_DSCR,
            0, (uint8_t *)CyFxUSBBOSDscr);

    /* Register USB descriptors with the stack. */
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 2, (uint8_t *)CyFxUSBProductDscr);
    Cy_USBD_SetDscr(&usbdCtxt, CY_USB_SET_STRING_DSCR, 3, (uint8_t *)CyFxUSBSerNumString);

    /* Initialize the application. */
    Cy_USB_AppInit(&appCtxt, &usbdCtxt, pCpuDmacBase, pCpuDw0Base, pCpuDw1Base);

    DBG_APP_INFO("Scheduler start done\r\n");
    /* Invokes scheduler: Not expected to return. */
    vTaskStartScheduler();
    while (1)
    {
        Cy_SysLib_Delay(10000);
        DBG_APP_INFO("Task Idle\r\n");
    }

    return 0;
}

/**
 * \name Cy_OnResetUser
 * \details Init function which is executed before the load regions in RAM are updated.
 * The High BandWidth subsystem needs to be enable here to allow variables
 * placed in the High BandWidth SRAM to be updated.
 * \retval None
 */
void Cy_OnResetUser(void)
{
    Cy_Fx2g3_OnResetInit();
}

/* [] END OF FILE */

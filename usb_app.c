/***************************************************************************//**
* \file usb_app.c
* \version 1.0
*
* \details This file contains the application layer implementation for the USB
* Mass Storage Class (MSC) for demo purposes.
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

#include "usb_app.h"
#include "cy_smif.h"
#include "msc_demo.h"
#include "FreeRTOS.h"
#include "cy_debug.h"
#include "cy_hbdma_mgr.h"
#include "cy_usbhs_dw_wrapper.h"
#include "portable.h"
#include "queue.h"
#include "task.h"
#include "cy_pdl.h"
#include "cy_device.h"
#include "cy_usb_common.h"
#include "cy_usbhs_cal_drv.h"
#include "cy_usb_usbd.h"
#include "spi.h"
#include <stdint.h>
#include <stdio.h>

/* Global variables for MSC operation */
/* Sense Key for SCSI Request Sense command. Default is 0x00 (NO SENSE). */
uint8_t  glSenseKey = 0x00;
/* Additional Sense Code (ASC) */
uint8_t  glAsc = 0x00;
/* Additional Sense Code Qualifier (ASCQ) */
uint8_t  glAscq = 0x00;

/* Buffers for read and write operations, allocated from HB DMA memory */
HBDMA_BUF_ATTRIBUTES uint8_t *gl_ReadBuffer;
HBDMA_BUF_ATTRIBUTES uint8_t *glWriteBuffer;
HBDMA_BUF_ATTRIBUTES uint8_t *glCbwBuffer;  /* Dedicated buffer for CBW commands to avoid collision */

#if !USE_RAM_STORAGE
extern uint8_t* MSC_SPI_WriteBuffer;
extern uint8_t* MSC_SPI_ReadBuffer;
#endif /* !USE_RAM_STORAGE */

/* State management for multi-stage SCSI commands like WRITE10 */
static uint32_t glWriteLba = 0;
static uint16_t glWriteBlocks = 0;
static bool glWritePending = false;

/* State management for BOT protocol (CBW -> Data -> CSW) */
bool glCswPending = false;
uint32_t glMscTag = 0;
uint32_t glMscResidue = 0;
uint8_t  glCswStatus = CSW_STATUS_PASSED;

/* External DMA context variables */
extern cy_stc_hbdma_context_t HBW_DrvCtxt;
extern cy_stc_hbdma_dscr_list_t HBW_DscrList;
extern cy_stc_hbdma_buf_mgr_t HBW_BufMgr;
extern cy_stc_hbdma_mgr_context_t HBW_MgrCtxt;

/**
 * \name Cy_USB_HandleCtrlSetup
 * \brief Handle control command given to application
 * \param pApp Application layer conetxt pointer
 * \param pMsg App message queue
 * \retval None
 */
void
Cy_USB_HandleCtrlSetup (void *pApp, cy_stc_usbd_app_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt = (cy_stc_usb_app_ctxt_t *)pApp;

    bool isReqHandled = false;
    uint8_t   reqType;
    uint8_t bmRequest, bRequest, bTarget;
    uint16_t wValue, wIndex, wLength;
    (void)wValue; 
    uint32_t  setupData0 = pMsg->data[0];
    uint32_t  setupData1 = pMsg->data[1];
    uint8_t maxLun = 0; 

    /* Decode the fields from the setup request. */
    bmRequest = (uint8_t)((setupData0 & CY_USB_BMREQUEST_SETUP0_MASK) >>
                           CY_USB_BMREQUEST_SETUP0_POS);
    bRequest =  (uint8_t)((setupData0 & CY_USB_BREQUEST_SETUP0_MASK) >>
                           CY_USB_BREQUEST_SETUP0_POS);
    wValue = (uint16_t)((setupData0 & CY_USB_WVALUE_SETUP0_MASK) >>
                         CY_USB_WVALUE_SETUP0_POS);
    wIndex = (uint16_t)((setupData1 & CY_USB_WINDEX_SETUP1_MASK) >>
                         CY_USB_WINDEX_SETUP1_POS);
    wLength = (uint16_t)((setupData1 & CY_USB_WLENGTH_SETUP1_MASK) >>
                          CY_USB_WLENGTH_SETUP1_POS);

    reqType = ((bmRequest & CY_USB_CTRL_REQ_TYPE_MASK) >>
                                                CY_USB_CTRL_REQ_TYPE_POS);
    bTarget = (bmRequest & CY_USB_CTRL_REQ_RECIPENT_MASK);

    switch (reqType)
    {
        case CY_USB_CTRL_REQ_STD:
            /* Standard requests are handled by the USBD driver layer. */
            isReqHandled = false;
            break;

        case CY_USB_CTRL_REQ_CLASS:
            Cy_Debug_AddToLog(3, "Info:USB CLASS request: bRequest=0x%x\r\n", bRequest);
            if ((bTarget == CY_USB_CTRL_REQ_RECIPENT_INTF) && (wIndex == pAppCtxt->currentAltSetting))
            {
                /* Handle Get MAX LUN Request */
                if (bRequest == SCSI_GET_MAX_LUN)
                {
                    if (wLength == 1)
                    {
                        isReqHandled = true;
                        Cy_USB_USBD_SendEndp0Data(pAppCtxt->pUsbdCtxt, &maxLun, wLength);
                    }
                }
                /* Handle Bulk-Only Mass Storage Reset */
                else if (bRequest == SCSI_BULK_ONLY_MASS_STORAGE_RESET)
                {
                    isReqHandled = true;
                    if (wLength == 0)
                    {
                         /* Flush endpoints and reset state if needed */
                         Cy_USBD_FlushEndp(pAppCtxt->pUsbdCtxt, 1, CY_USB_ENDP_DIR_OUT);
                         Cy_USBD_FlushEndp(pAppCtxt->pUsbdCtxt, 1, CY_USB_ENDP_DIR_IN);
                    }
                }
            }
            break;

        case CY_USB_CTRL_REQ_VENDOR:
            Cy_Debug_AddToLog(1, "USB VENDOR request: bRequest=0x%x\r\n", bRequest);
            isReqHandled = false;
            break;

        default:
            Cy_Debug_AddToLog(1, "USB UNKNOWN request: reqType=0x%x bRequest=0x%x\r\n", reqType, bRequest);
            isReqHandled = false;
            break;
    }

    if(!isReqHandled) {
        static uint32_t stall_count = 0;
        stall_count++;
        if ((stall_count % 10) == 1) {
            Cy_Debug_AddToLog(1, "Err:USB request not handled, stalling endpoint 0 (count: %lu)\r\n", stall_count);
        }
        Cy_USB_USBD_EndpSetClearStall(pAppCtxt->pUsbdCtxt, 0x00, CY_USB_ENDP_DIR_IN, TRUE);
    }
    return;
}


/**
 * \name Cy_USB_TaskHandler
 * \brief This function handles events for device.
 * \param pTaskParam Task param
 * \retval None
 */
void 
Cy_USB_TaskHandler (void *pTaskParam)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt;
    cy_stc_usbd_app_msg_t queueMsg;
    pAppCtxt = (cy_stc_usb_app_ctxt_t *)pTaskParam;

    /* Enable USB-2 connection and wait until it is stable. */
    vTaskDelay(250);

    BaseType_t xStatus;
    uint32_t idleLoopCnt = 0;

    /* Configure and initialize SPI flash memroy for operation in quad mode */
    if(Cy_QSPI_Start(pAppCtxt,SPI_FLASH_0)==CY_SMIF_SUCCESS){
        Cy_Debug_AddToLog(3, "Info:SPI Flash configuration Success\r\n");
    } else {
        Cy_Debug_AddToLog(3, "Info:SPI Flash configuration Failed\r\n");
    }
    Cy_SPI_FlashInit(SPI_FLASH_0, false, false);

    /* Check flash protection status early to identify potential write conflicts */
#if !USE_RAM_STORAGE
    LOG_MSC_INFO("Checking flash protection status for MSC operations\r\n");
    Cy_USB_MSC_CheckFlashProtection();
#endif /* USE_RAM_STORAGE */

    /* Enable USB-2 connection and wait until it is stable. */
    vTaskDelay(100);

    /* If VBus is present, enable the USB connection. */
    pAppCtxt->vbusPresent =
    (Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN) == VBUS_DETECT_STATE);

#if USBFS_LOGS_ENABLE
    vTaskDelay(500);
#endif /* USBFS_LOGS_ENABLE */

    if (pAppCtxt->vbusPresent) {
        Cy_USB_ConnectionEnable(pAppCtxt);
    }

    DBG_APP_INFO("ThreadActive\r\n");

    do {
#if WATCHDOG_RESET_EN
        /* Kick The WDT to prevent RESET */
        KickWDT();
#endif /* WATCHDOG_RESET_EN */

        /*
         * Wait until some data is received from the queue.
         * Timeout after 100 ms.
         */
        xStatus = xQueueReceive(pAppCtxt->xQueue, &queueMsg, 1);
        if (xStatus != pdPASS) {
            idleLoopCnt++;
            if (idleLoopCnt >= 100000UL) {  
                idleLoopCnt = 0;
                DBG_APP_INFO("TaskIdle\r\n");
            }

            continue;
        }

        idleLoopCnt = 0;
        /*
         * Make sure that the USB link is brought into active state
         * periodically to avoid stuck data transfers.
         */
        /*
        if ((Cy_USBD_GetTimerTick() & 0x1FF) == 0x1FF) {
            Cy_USBD_GetUSBLinkActive(pAppCtxt->pUsbdCtxt);
        }
        */


        switch (queueMsg.type) {

            case CY_USB_VBUS_CHANGE_INTR:
              /* Start the debounce timer. */
              xTimerStart(pAppCtxt->vbusDebounceTimer, 0);
              break;

            case CY_USB_VBUS_CHANGE_DEBOUNCED:
              /* Check whether VBus state has changed. */
              pAppCtxt->vbusPresent = (Cy_GPIO_Read(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN) == VBUS_DETECT_STATE);
              if (pAppCtxt->vbusPresent) {
                  if (!pAppCtxt->usbConnected) {
                      DBG_APP_INFO("Enabling USB connection due to VBus detect\r\n");
                      Cy_USB_ConnectionEnable(pAppCtxt);
                  }
              } else {
                  DBG_APP_INFO("Disabling USB connection due to VBus removal\r\n");
                  Cy_USB_ConnectionDisable(pAppCtxt);
              }
              break;
                  
            case CY_USB_MSG_CTRL_XFER_SETUP:
                DBG_APP_TRACE("CY_USB_MSG_CTRL_XFER_SETUP\r\n");
                Cy_USB_HandleCtrlSetup((void *)pAppCtxt, &queueMsg);
                break;

            case CY_USB_MSG_CTRL_XFER_DATA:
                DBG_APP_TRACE("CY_USB_MSG_CTRL_XFER_DATA\r\n");
                break;

            case CY_USB_SLP_OUT_MSG:
                /* prepare to listen for short packet */
                Cy_USB_AppReadShortPacket(pAppCtxt, 0x01, queueMsg.data[1]);
                Cy_TrigMux_SwTrigger(TRIG_IN_MUX_0_USBHSDEV_TR_OUT0 + 0x01, CY_TRIGGER_TWO_CYCLES);
                break;

            case CY_USB_ENDP0_READ_TIMEOUT:
                DBG_APP_INFO("Endp0ReadTimeout\r\n");
                /*
                 * When application layer wants to recieve data from
                 * host through endpoint 0 then device initiate RcvEndp0
                 * function call and start timer. When timer ends and still
                 * data is not recieved then TIMER interrupt will send
                 * CY_USB_ENDP0_READ_TIMEOUT message. If data is recieved then
                 * case which handles data should stop timer.
                 */
                Cy_USB_USBD_RetireRecvEndp0Data(pAppCtxt->pUsbdCtxt);
                break;


            default:
                DBG_APP_ERR("Default %d\r\n", queueMsg.type);
                break;
        }   /* end of switch() */
    
    } while (1);
}   /* End of function  */

/**
 * \name Cy_USB_Endp0ReadComplete
 * \brief Handler for DMA transfer completion on endpoint 0 OUT Transfer.
 * \param pApp application layer context pointer
 * \retval None
 */
void
Cy_USB_Endp0ReadComplete (void *pApp)
{
    (void)pApp;
    DBG_APP_TRACE("Sent CY_USB_ENDP0_READ_COMPLETE\r\n");
    return;
}   /* end of function */

/**
 * \name Cy_USB_VbusDebounceTimerCallback
 * \brief Timer used to do debounce on VBus changed interrupt notification.
 * \param xTimer timer handle
 * \return None
 */
void
Cy_USB_VbusDebounceTimerCallback (TimerHandle_t xTimer)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt = (cy_stc_usb_app_ctxt_t *)pvTimerGetTimerID(xTimer);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    cy_stc_usbd_app_msg_t xMsg;

    DBG_APP_INFO("VbusDebounce_CB\r\n");
    if (pAppCtxt->vbusChangeIntr) {
        /* Notify the VCOM task that VBus debounce is complete. */
        xMsg.type = CY_USB_VBUS_CHANGE_DEBOUNCED;
        xQueueSendFromISR(pAppCtxt->xQueue, &(xMsg), &(xHigherPriorityTaskWoken));

        /* Clear and re-enable the interrupt. */
        pAppCtxt->vbusChangeIntr = false;
        Cy_GPIO_ClearInterrupt(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN);
        Cy_GPIO_SetInterruptMask(VBUS_DETECT_GPIO_PORT, VBUS_DETECT_GPIO_PIN, 1);
    }
}   /* end of function  */

/**
 * \name Cy_USB_AppInit
 * \details This function Initializes application related data structures,
 * register callback and creates queue and task for device
 * function. Common function for FS/HS.
 * \param pAppCtxt application layer conetxt pointer
 * \param pUsbdCtxt USBD layer conetxt pointer
 * \param pCpuDmacBase DMA controller base address pointer
 * \param pCpuDw0Base Datawire 0 base address pointer
 * \param pCpuDw1Base Datawire 1 base address pointer
 * \retval None
 */
void
Cy_USB_AppInit (cy_stc_usb_app_ctxt_t *pAppCtxt,
                cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, DMAC_Type *pCpuDmacBase,
                DW_Type *pCpuDw0Base, DW_Type *pCpuDw1Base)
{
    BaseType_t status = pdFALSE;
    pAppCtxt->devState = CY_USB_DEVICE_STATE_DISABLE;
    pAppCtxt->prevDevState = CY_USB_DEVICE_STATE_DISABLE;

    DBG_APP_INFO("Cy_USB_AppInit\r\n");
    /*
     * Initially application sees device speed as USBFS and during set
     * configuration application will update actual device speed.
     */
    pAppCtxt->devSpeed = CY_USBD_USB_DEV_FS;
    pAppCtxt->devAddr = 0x00;
    pAppCtxt->activeCfgNum = 0x00;
    pAppCtxt->currentAltSetting = 0x00;
    pAppCtxt->enumMethod = CY_USB_ENUM_METHOD_FAST;
    
    cy_stc_app_endp_dma_set_t *pEndpInDma;
    cy_stc_app_endp_dma_set_t *pEndpOutDma;

    pEndpInDma = &(pAppCtxt->endpInDma[0x01]);

    memset((void *)pEndpInDma, 0, sizeof(cy_stc_app_endp_dma_set_t));
    pEndpInDma->channel = 0x01;
    pEndpInDma->firstRqtDone = false;

    /* Time to handle OUT endpoints. */
    pEndpOutDma = &(pAppCtxt->endpOutDma[0x01]);

    memset((void *)pEndpOutDma, 0, sizeof(cy_stc_app_endp_dma_set_t));
    pEndpOutDma->channel = 0x01;
    pEndpOutDma->firstRqtDone = false;

    pAppCtxt->pCpuDmacBase = pCpuDmacBase;
    pAppCtxt->pCpuDw0Base = pCpuDw0Base;
    pAppCtxt->pCpuDw1Base = pCpuDw1Base;
    pAppCtxt->pUsbdCtxt = pUsbdCtxt;

    /*
     * Callbacks registered with USBD layer. These callbacks will be called
     * based on appropriate event.
     */
    Cy_USB_AppRegisterCallback(pAppCtxt);

    if (!(pAppCtxt->firstInitDone)) {
       
        /* create queue and register it to kernel. */
        pAppCtxt->xQueue = xQueueCreate(CY_USB_MSG_QUEUE_SIZE,
                                        CY_USB_MSG_SIZE);
        DBG_APP_INFO("CreatedQueue\r\n");
        vQueueAddToRegistry(pAppCtxt->xQueue, "MsgQueue");

        /* Create task and check status to confirm task created properly. */
        status = xTaskCreate(Cy_USB_TaskHandler, "Task", 2048,
                        (void *)pAppCtxt, 5, &(pAppCtxt->taskHandle));
        if (status != pdPASS) {
            DBG_APP_ERR("TaskcreateFail\r\n");
            return;
        }
        
        pAppCtxt->vbusDebounceTimer = xTimerCreate("VbusDebounceTimer", 200, pdFALSE,
                (void *)pAppCtxt, Cy_USB_VbusDebounceTimerCallback);
        if (pAppCtxt->vbusDebounceTimer == NULL) {
            DBG_APP_ERR("TimerCreateFail\r\n");
            return;
        }
        DBG_APP_INFO("VBus debounce timer created\r\n");
        pAppCtxt->firstInitDone = 0x01;
        
    }


    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppConfigureEndp
 * \brief    This Function is used by application to configure endpoints after set
 * configuration. This function should be used for all endpoints except endp0.
 * \param pUsbdCtxt USBD layer context pointer.
 * \param pEndpDscr pointer to endpoint descriptor.
 * \retval None
 */
void
Cy_USB_AppConfigureEndp (cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, uint8_t *pEndpDscr)
{
    cy_stc_usb_endp_config_t endpConfig;
    cy_en_usb_endp_dir_t endpDirection;
    bool valid;
    uint32_t endpType;
    uint32_t endpNum, dir;
    uint16_t maxPktSize;
    uint32_t isoPkts = 0x00;
    uint8_t burstSize = 0x00;
    uint8_t maxStream = 0x00;
    uint8_t interval = 0x00;

    DBG_APP_TRACE("Cy_USB_AppConfigureEndp >> \r\n");
    /* If it is not endpoint descriptor then return */
    if (!Cy_USBD_EndpDscrValid(pEndpDscr)) {
        DBG_APP_ERR("EndpDscrNotValid \r\n");
        return;
    }
    Cy_USBD_GetEndpNumMaxPktDir(pEndpDscr, &endpNum, &maxPktSize, &dir);
    
    if (dir) {
        DBG_APP_TRACE("DIR-IN endpNum:%d \n", endpNum);
        endpDirection = CY_USB_ENDP_DIR_IN;
    } else {
        DBG_APP_TRACE("DIR-OUT endpNum:%d \n", endpNum);
        endpDirection = CY_USB_ENDP_DIR_OUT;
    }
    Cy_USBD_GetEndpType(pEndpDscr, &endpType);

    if ((CY_USB_ENDP_TYPE_ISO == endpType) ||
        (CY_USB_ENDP_TYPE_INTR == endpType)) {
        /*
         * The ISOINPKS setting in the USBHS register is the actual
         * packets per microframe value.
         */
        isoPkts = 
        ((*((uint8_t *)(pEndpDscr + CY_USB_ENDP_DSCR_OFFSET_MAX_PKT + 1)) & CY_USB_ENDP_ADDL_XN_MASK)
        >> CY_USB_ENDP_ADDL_XN_POS) + 1;
    }

    valid = 0x01;
    Cy_USBD_GetEndpInterval(pEndpDscr, &interval);

    /* Prepare endpointConfig parameter. */
    endpConfig.endpType = (cy_en_usb_endp_type_t)endpType;
    endpConfig.endpDirection = endpDirection;
    endpConfig.valid = valid;
    endpConfig.endpNumber = endpNum;
    endpConfig.maxPktSize = (uint32_t)maxPktSize;
    endpConfig.isoPkts = isoPkts;
    endpConfig.burstSize = burstSize;
    endpConfig.streamID = (maxStream & 0x1F);
    endpConfig.interval = interval;
    /*
     * allowNakTillDmaRdy = true means device will send NAK
     * till DMA setup is ready. This field is applicable to only
     * ingress direction ie OUT transfer/OUT endpoint.
     * For Egress ie IN transfer, this field is ignored.
     */
    endpConfig.allowNakTillDmaRdy = TRUE;
    Cy_USB_USBD_EndpConfig(pUsbdCtxt, endpConfig);

    DBG_APP_TRACE("Cy_USB_AppConfigureEndp << \r\n");
    return;
} /* end of function. */

/**
 * \name Cy_USB_AppInitCpuDmaIntr
 * \brief Function to register an ISR for a USB endpoint DMA channel and enable
 * the interrupt.
 * \param endpNum endpoint number.
 * \param endpDir endpoint direction
 * \param userIsr user provided ISR function pointer.
 * \retval None
 */
void 
Cy_USB_AppInitCpuDmaIntr (uint32_t endpNum, cy_en_usb_endp_dir_t endpDir,
                          cy_israddress userIsr)
{
    cy_stc_sysint_t intrCfg;
    
    DBG_APP_TRACE("Cy_USB_AppInitCpuDmaIntr >>\r\n");

    if ((endpNum == 0x00) && (endpDir == CY_USB_ENDP_DIR_OUT)) {
        /* To make "RcevEndp0Data" non blocking, register ISR */
#if (!CY_CPU_CORTEX_M4)
        intrCfg.intrPriority = 3;
        intrCfg.intrSrc = NvicMux4_IRQn;
        /* DW0 channels 0 onwards are used for OUT endpoints. */
        intrCfg.cm0pSrc = (cy_en_intr_t)(cpuss_interrupts_dmac_1_IRQn);

#else
        /* Only for OUT transfer interrupt registration required. */
        intrCfg.intrPriority = 5;
        intrCfg.intrSrc = (IRQn_Type)(cpuss_interrupts_dmac_1_IRQn);
#endif

       if (userIsr != NULL)  {
            /* If an ISR is provided, register it and enable the interrupt. */
            Cy_SysInt_Init(&intrCfg, userIsr);
            NVIC_EnableIRQ(intrCfg.intrSrc);
        } else {
            /* ISR is NULL. Disable the interrupt. */
            NVIC_DisableIRQ(intrCfg.intrSrc);
        }

    }

    if ((endpNum > 0) && (endpNum < 2)) {
        DBG_APP_TRACE("Registering ISR for endp:%d endpDir %d \r\n",endpNum,endpDir);
#if (!CY_CPU_CORTEX_M4)
        if (endpDir == CY_USB_ENDP_DIR_IN) {
            intrCfg.intrPriority = 3;
            intrCfg.intrSrc = NvicMux0_IRQn;
            /* DW1 channels 0 onwards are used for IN endpoints. */
            intrCfg.cm0pSrc = (cy_en_intr_t)(cpuss_interrupts_dw1_0_IRQn + endpNum);
        } else {
            intrCfg.intrPriority = 3;
            intrCfg.intrSrc = NvicMux1_IRQn;
            /* DW0 channels 0 onwards are used for OUT endpoints. */
            intrCfg.cm0pSrc = (cy_en_intr_t)(cpuss_interrupts_dw0_0_IRQn + endpNum);
        }
#else
        intrCfg.intrPriority = 5;
        if (endpDir == CY_USB_ENDP_DIR_IN) {
            DBG_APP_TRACE("DIR-IN \r\n");
            /* DW1 channels 0 onwards are used for IN endpoints. */
            intrCfg.intrSrc =
                          (IRQn_Type)(cpuss_interrupts_dw1_0_IRQn + endpNum);
        } else {
            /* DW0 channels 0 onwards are used for OUT endpoints. */
            DBG_APP_TRACE("DIR-OUT \r\n");
            intrCfg.intrSrc =
                          (IRQn_Type)(cpuss_interrupts_dw0_0_IRQn + endpNum);
        }
#endif /* (CY_CPU_CORTEX_M4) */

        if (userIsr != NULL)  {
            /* If an ISR is provided, register it and enable the interrupt. */
            DBG_APP_TRACE("Registering ISR \r\n");
            Cy_SysInt_Init(&intrCfg, userIsr);
            NVIC_EnableIRQ(intrCfg.intrSrc);
        } else {
            /* ISR is NULL. Disable the interrupt. */
            DBG_APP_TRACE("Disabling ISR\r\n");
            NVIC_DisableIRQ(intrCfg.intrSrc);
        }
    }
    DBG_APP_TRACE("Cy_USB_AppInitCpuDmaIntr << \r\n");
} /* end of function. */

extern void InEpDma_ISR (uint8_t endpNum);

/**
 * \name Cy_USB_AppClearCpuDmaInterrupt
 * \brief Function to clear the pending DMA interrupt associated with an endpoint.
 * \param pAppCtxt application layer context pointer.
 * \param endpNum endpoint number.
 * \param endpDir endpoint direction
 * \retval None
 */
void 
Cy_USB_AppClearCpuDmaInterrupt (cy_stc_usb_app_ctxt_t *pAppCtxt,
                                uint32_t endpNum, cy_en_usb_endp_dir_t endpDir)
{

    if ((pAppCtxt != NULL) && (endpNum == 0x00) &&
        (endpDir == CY_USB_ENDP_DIR_OUT)) {
        Cy_DMAC_Channel_ClearInterrupt(pAppCtxt->pCpuDmacBase,
                                       pAppCtxt->pUsbdCtxt->channel1,
                                       CY_DMAC_INTR_COMPLETION);
    }

    if ((pAppCtxt != NULL) && (endpNum > 0) && 
        (endpNum < 2)) {
        if (endpDir == CY_USB_ENDP_DIR_IN) {
            Cy_DMA_Channel_ClearInterrupt(pAppCtxt->pCpuDw1Base, 
                                      pAppCtxt->endpInDma[endpNum].channel);
        } else  {
            Cy_DMA_Channel_ClearInterrupt(pAppCtxt->pCpuDw0Base,
                                     pAppCtxt->endpOutDma[endpNum].channel);
        }
    }
} /* end of function. */

cy_israddress GetEPInDmaIsr(uint8_t epNum);
cy_israddress GetEPOutDmaIsr(uint8_t epNum);

/**
* \name Cy_USB_AppSetupEndpDmaParamsHs
* \brief    This Function will setup Endpoint and DMA related parameters for high speed
* device before transfer initiated.
* \param pAppCtxt application layer context pointer.
* \param pEndpDscr pointer to endpoint descriptor.
* \retval None
*/
void
Cy_USB_AppSetupEndpDmaParamsHs (cy_stc_usb_app_ctxt_t *pUsbApp,
    uint8_t *pEndpDscr)
{
    DW_Type *pDW;
    cy_stc_app_endp_dma_set_t *pEndpDmaSet;
    uint32_t endpNum, channelNum;
    uint16_t maxPktSize = 0x00;
    bool stat;
    cy_en_usb_endp_dir_t endpDir;

    DBG_APP_TRACE("Cy_USB_AppSetupEndpDmaParamsHs >> \r\n");

    /* Configure endpoint in USB-IP */
    Cy_USB_AppConfigureEndp(pUsbApp->pUsbdCtxt, pEndpDscr);

    endpNum = ((*(pEndpDscr+CY_USB_ENDP_DSCR_OFFSET_ADDRESS)) & 0x7F);
    Cy_USBD_GetEndpMaxPktSize(pEndpDscr, &maxPktSize);

    channelNum = endpNum;
    if (*(pEndpDscr + CY_USB_ENDP_DSCR_OFFSET_ADDRESS) & CY_USBD_ENDP_DIR_MASK) {
        endpDir = CY_USB_ENDP_DIR_IN;
        pEndpDmaSet   = &(pUsbApp->endpInDma[endpNum]);
        pDW           = pUsbApp->pCpuDw1Base;
    } else {
        endpDir = CY_USB_ENDP_DIR_OUT;
        pEndpDmaSet   = &(pUsbApp->endpOutDma[endpNum]);
        pDW           = pUsbApp->pCpuDw0Base;
    }

    stat = Cy_USBHS_App_EnableEpDmaSet(pEndpDmaSet, pDW, channelNum,
                                       endpNum, endpDir, maxPktSize);
    DBG_APP_TRACE("Enable EPDmaSet: endp=%x dir=%x stat=%x\r\n",
                  endpNum, endpDir, stat);

    pEndpDmaSet->endpType = (cy_en_usb_endp_type_t)
                    ((*(pEndpDscr + CY_USB_ENDP_DSCR_OFFSET_ATTRIBUTE)) & 0x03);

    /* Make the ISR registration and trigger connections from EPM to DMAC. */
    if (endpDir == CY_USB_ENDP_DIR_IN) {
        DBG_APP_TRACE("DIR-IN, ChannelNum:0x%x\r\n",pEndpDmaSet->channel);
        Cy_USB_AppInitCpuDmaIntr(endpNum, CY_USB_ENDP_DIR_IN,
                                 GetEPInDmaIsr(endpNum));
    } else {
        DBG_APP_TRACE("DIR-OUT, ChannelNum:0x%x\r\n",pEndpDmaSet->channel);
        Cy_USB_AppInitCpuDmaIntr(endpNum, CY_USB_ENDP_DIR_OUT,
                                 GetEPOutDmaIsr(endpNum));
    }

    DBG_APP_TRACE("Cy_USB_AppSetupEndpDmaParamsHs << \r\n");
    return;
} /* end of function. */

/**
 * \name Cy_USB_AppTerminateCpuDma
 * \brief Function will disable associate central DMA channel.
 * \param pAppCtxt application layer context pointer.
 * \param endpNum endpoint number.
 * \param endpDir endpoint direction
 * \retval None
 */
void
Cy_USB_AppTerminateCpuDma (cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t endpNum,
                           cy_en_usb_endp_dir_t endpDir)
{
    cy_stc_app_endp_dma_set_t *pEndpDmaSet;

    DBG_APP_TRACE("Cy_USB_AppTerminateCpuDma >>\r\n");

    if (endpDir == CY_USB_ENDP_DIR_OUT) {
        /* Parameter validity checks. */
        if ((pAppCtxt == NULL) || (pAppCtxt->pCpuDw0Base == NULL)) {
            DBG_APP_ERR("TerminateCpuDma: BadParam\r\n");
            return;
        }

        if (pAppCtxt->devSpeed <= CY_USBD_USB_DEV_HS) {
            /*
             * while disabling DMA channel for OUT endpoint, enable sending
             * NAK also.
             */
                Cy_USB_USBD_EndpSetClearNakNrdy(pAppCtxt->pUsbdCtxt,
                                                endpNum,
                                                CY_USB_ENDP_DIR_OUT, true);
            }
            pEndpDmaSet = &(pAppCtxt->endpOutDma[endpNum]);
            Cy_DMA_Channel_Disable(pAppCtxt->pCpuDw0Base, pEndpDmaSet->channel);
    } else {
        /* Parameter validity checks. */
        if ((pAppCtxt == NULL) || (pAppCtxt->pCpuDw1Base == NULL)) {
            DBG_APP_ERR("TerminateCpuDma: BadParam\r\n");
            return;
        }
        /* If the DMA channel is already enabled, disable it. */
        pEndpDmaSet = &(pAppCtxt->endpInDma[endpNum]);
        Cy_DMA_Channel_Disable(pAppCtxt->pCpuDw1Base, pEndpDmaSet->channel);
    }
    DBG_APP_TRACE("Cy_USB_AppTerminateCpuDma <<\r\n");
    return;
}   /* end of function */

/**
 * \name Cy_USB_AppDestroyEndpDmaParamsHs
 * \details This Function de-couple endpoint and DMA channel for HS controller. It also
 * destroys DMA channel.
 * \param pAppCtxt application layer context pointer.
 * \param pEndpDscr pointer to endpoint descriptor.
 * \retval None
 */
void
Cy_USB_AppDestroyEndpDmaParamsHs (cy_stc_usb_app_ctxt_t *pUsbApp,
                                  uint8_t *pEndpDscr)
{
    cy_stc_app_endp_dma_set_t *pEndpDmaSet;
    uint32_t endpNum, endpDir;
    uint16_t maxPktSize;

    DBG_APP_TRACE("Cy_USB_AppDestroyEndpDmaParamsHs >> \r\n");
    Cy_USBD_GetEndpNumMaxPktDir(pEndpDscr, &endpNum, &maxPktSize, &endpDir);

    Cy_USB_AppInitCpuDmaIntr(endpNum,
                             endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT),
                             NULL);

    /* Updated endpoint related functions in CAL layer through USBD */
    Cy_USBD_EnableEndp(pUsbApp->pUsbdCtxt, endpNum,
                       endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT),
                       false);
    Cy_USBD_FlushEndp(pUsbApp->pUsbdCtxt, endpNum,
                      endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT));
    Cy_USBD_ResetEndp(pUsbApp->pUsbdCtxt, endpNum,
                      endpDir ? (CY_USB_ENDP_DIR_IN):(CY_USB_ENDP_DIR_OUT), false);

    /* This function takes care of retrieving channel from endpDmaSet */
    if (endpDir) {
        Cy_USB_AppTerminateCpuDma(pUsbApp, endpNum, CY_USB_ENDP_DIR_IN);
    } else {
        Cy_USB_AppTerminateCpuDma(pUsbApp, endpNum, CY_USB_ENDP_DIR_OUT);
    }

    if (endpDir) {
        pEndpDmaSet = &(pUsbApp->endpInDma[endpNum]);
        memset(pEndpDmaSet, 0, sizeof(cy_stc_app_endp_dma_set_t));
        pEndpDmaSet->channel = endpNum;

    } else {
        pEndpDmaSet = &(pUsbApp->endpOutDma[endpNum]);
        memset(pEndpDmaSet, 0, sizeof(cy_stc_app_endp_dma_set_t));
        pEndpDmaSet->channel = endpNum;
    }
    DBG_APP_TRACE("Cy_USB_AppDestroyEndpDmaParamsHs << \r\n");
    return;
}   /* end of function() */

/**
 * \name Cy_USB_AppQueueRead
 * \brief Function to queue read operation on an OUT endpoint.
 * \param pAppCtxt application layer context pointer.
 * \param endpNum endpoint number.
 * \param endpDir endpoint direction
 * \param pBuffer pointer to buffer where data will be stored.
 * \param dataSize expected data size.
 * \retval None
 */
void
Cy_USB_AppQueueRead(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t endpNum,
                     uint8_t *pBuffer, uint32_t dataSize)
{
    cy_stc_app_endp_dma_set_t      *pEndpDmaSet;

    DBG_APP_TRACE("Cy_USB_AppQueueRead >>\r\n");
    DBG_APP_TRACE("pBuffer:0x%x \r\n",pBuffer);

    /* Null pointer checks. */
    if ((pAppCtxt == NULL) || (pAppCtxt->pUsbdCtxt == NULL) ||
       (pAppCtxt->pCpuDw0Base == NULL) || (pBuffer == NULL) ||
       (dataSize == 0)) {

        DBG_APP_ERR("QueueRead: BadParam NULL\r\n");
        return;
    }

    pEndpDmaSet  = &(pAppCtxt->endpOutDma[endpNum]);
    /* If endpoint not valid then dont go ahead. */
    if (pEndpDmaSet->valid == 0) {
        DBG_APP_ERR("QueueRead: EndpSetNotValid\r\n");
        return;
    }

    /* USB HS-FS data recieve case */
    DBG_APP_TRACE("CALLING Cy_USBHS_App_QueueRead\r\n");
    Cy_USBHS_App_QueueRead(pEndpDmaSet, pBuffer, dataSize);
    /* Update xfer count and then disable NAK for the endpoint. */
    Cy_USBD_UpdateXferCount(pAppCtxt->pUsbdCtxt, endpNum,
                            CY_USB_ENDP_DIR_OUT, dataSize);
    /*
        * When device not ready then it will enable NAK.
        * Now device is ready to recieve data so disable NAK.
        */
    Cy_USB_USBD_EndpSetClearNakNrdy(pAppCtxt->pUsbdCtxt, endpNum,
                                    CY_USB_ENDP_DIR_OUT, false);
    

    pEndpDmaSet->firstRqtDone = true;
    DBG_APP_TRACE("Cy_USB_AppQueueRead << \r\n");
    return;

} /* end of function */

/**
 * \name Cy_USB_AppQueueWrite
 * \brief Queue USBHS Write on the USB endpoint
 * \param pAppCtxt application layer context pointer.
 * \param endpNumber Endpoint number
 * \param pBuffer Data Buffer Pointer
 * \param dataSize DataSize to send on USB bus
 * \retval None
 */
void Cy_USB_AppQueueWrite(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t endpNumber,
                          uint8_t *pBuffer, uint32_t dataSize)
{
    cy_stc_app_endp_dma_set_t *dmaset_p=NULL;

    /* Null pointer checks. */
    if ((pAppCtxt == NULL) || (pAppCtxt->pUsbdCtxt == NULL) ||
            (pAppCtxt->pCpuDw1Base == NULL) || (pBuffer == NULL)) 
    {
        DBG_APP_ERR("QueueWrite Err0\r\n");
        return;
    }

    /*
     * Verify that the selected endpoint is valid and the dataSize
     * is non-zero.
     */
    dmaset_p = &(pAppCtxt->endpInDma[endpNumber]);
    if ((dmaset_p->valid == 0) || (dataSize == 0)) 
    {
        DBG_APP_ERR("QueueWrite Err1 %d %d\r\n",dmaset_p->valid,dataSize);
        return;
    }

    Cy_USBHS_App_QueueWrite(dmaset_p, pBuffer, dataSize);
} /* end of function */



/**
 * \name Cy_USB_MSC_BOT_SendCSW
 * \brief Constructs and sends a Command Status Wrapper (CSW)
 * \param pUsbApp USB application context
 * \param tag The tag from the corresponding CBW
 * \param residue The difference between data expected and data transferred
 * \param status The status of the command execution
 * \retval None
 */
void Cy_USB_MSC_BOT_SendCSW(cy_stc_usb_app_ctxt_t *pUsbApp, uint32_t tag, uint32_t residue, uint8_t status)
{
    static CSW_t csw; /* Made static to ensure that it's in scope for DMA */
    csw.dCSWSignature = CSW_SIGNATURE;
    csw.dCSWTag = tag;
    csw.dCSWDataResidue = residue;
    csw.bCSWStatus = status;

    static uint32_t csw_count = 0;
    if (residue == 188 && (++csw_count % 20 == 1)) {
        LOG_MSC_TRACE("Info:CSW:Sending Tag=0x%x, Residue=%u (count=%lu), Status=%u\r\n", 
                         tag, residue, csw_count, status);
    } else if (status != 0 || (residue > 0 && residue != 188)) {
        LOG_MSC_TRACE("Info:CSW:Sending Tag=0x%x, Residue=%u, Status=%u\r\n", tag, residue, status);
    }
    Cy_USB_AppQueueWrite(pUsbApp, 0x01, (uint8_t*)&csw, sizeof(CSW_t));
    if (status != 0) {
        LOG_MSC_ERROR("[CSW] Queued for transmission (ERROR)\r\n");
    }
}

/**
 * \name Cy_USB_AppSetConfigCallback
 * \brief Callback function to handle Set Configuration request.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer.
 * \param pMsg message containing setup request data.
 * \retval None
 */
void Cy_USB_AppSetConfigCallback(void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                            cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    cy_stc_usb_setup_req_t *pSetupReq = (cy_stc_usb_setup_req_t *)(&(pMsg->data[0]));
    uint8_t *pActiveCfg, *pIntfDscr, *pEndpDscr;
    uint8_t index, numOfIntf, numOfEndp;

    DBG_APP_TRACE("Set Configuration Callback\r\n");

    /* Free existing DMA buffers if they were previously allocated */
    if (gl_ReadBuffer) {
        Cy_HBDma_BufMgr_Free(&HBW_BufMgr, gl_ReadBuffer);
        gl_ReadBuffer = NULL;
    }
    if (glWriteBuffer) {
        Cy_HBDma_BufMgr_Free(&HBW_BufMgr, glWriteBuffer);
        glWriteBuffer = NULL;
    }
    if (glCbwBuffer) {
        Cy_HBDma_BufMgr_Free(&HBW_BufMgr, glCbwBuffer);
        glCbwBuffer = NULL;
    }
#if !USE_RAM_STORAGE
    if (MSC_SPI_WriteBuffer) {
        Cy_HBDma_BufMgr_Free(&HBW_BufMgr, MSC_SPI_WriteBuffer);
        MSC_SPI_WriteBuffer = NULL;
    }
    if (MSC_SPI_ReadBuffer) {
        Cy_HBDma_BufMgr_Free(&HBW_BufMgr, MSC_SPI_ReadBuffer);
        MSC_SPI_ReadBuffer = NULL;
    }
#endif /* !USE_RAM_STORAGE */
    /* Allcoate memory for QSPI read/write buffers */
    gl_ReadBuffer = Cy_HBDma_BufMgr_Alloc(&HBW_BufMgr, SECTOR_LENGTH);
    glWriteBuffer = Cy_HBDma_BufMgr_Alloc(&HBW_BufMgr, SECTOR_LENGTH);
    glCbwBuffer = Cy_HBDma_BufMgr_Alloc(&HBW_BufMgr, 1024);  /* 1KB for CBW commands */

#if !USE_RAM_STORAGE
    MSC_SPI_WriteBuffer = Cy_HBDma_BufMgr_Alloc(&HBW_BufMgr, SECTOR_LENGTH);
    MSC_SPI_ReadBuffer = Cy_HBDma_BufMgr_Alloc(&HBW_BufMgr, SECTOR_LENGTH);
#endif /* !USE_RAM_STORAGE */

    /* Check buffer allocation */
    if (!gl_ReadBuffer || !glWriteBuffer || !glCbwBuffer
#if !USE_RAM_STORAGE
        || !MSC_SPI_WriteBuffer || !MSC_SPI_ReadBuffer
#endif /* !USE_RAM_STORAGE */
    ) {
        DBG_APP_ERR("Failed to allocate DMA buffers\r\n");
        return;
    }
    
    /* Initialize buffers to ensure clean state */
    memset(gl_ReadBuffer, 0, 1024*64);
    memset(glWriteBuffer, 0, 1024*64);
    memset(glCbwBuffer, 0, 1024);

#if !USE_RAM_STORAGE
    memset(MSC_SPI_WriteBuffer, 0, 1024*64);
    memset(MSC_SPI_ReadBuffer, 0, 1024*64);
#endif /* !USE_RAM_STORAGE */

    DBG_APP_INFO("DMA buffers allocated and initialized successfully\r\n");

    pUsbApp->devSpeed = Cy_USBD_GetDeviceSpeed(pUsbdCtxt);

    DBG_APP_INFO("SetCfg:devSpeed:%d\r\n",pUsbApp->devSpeed);
    /* Handle de-configuration request (wValue = 0) */
    if(pSetupReq->wValue == 0) {
        DBG_APP_INFO("De-configured\r\n");
        pUsbApp->devState = CY_USB_DEVICE_STATE_ADDRESS;
        return;
    }

    /* Enable Datawire. This application uses data wire for data transfer. */
    Cy_DMA_Enable(pUsbApp->pCpuDw0Base);
    Cy_DMA_Enable(pUsbApp->pCpuDw1Base);

    pActiveCfg = Cy_USB_USBD_GetActiveCfgDscr(pUsbdCtxt);
    if (!pActiveCfg) {
        /* Set config should be called when active config value > 0x00. */
        DBG_APP_ERR("Active config not available\r\n");
        DBG_APP_ERR("CY_USB_APP_STATUS_FAILURE");
        return;
    }

    numOfIntf = Cy_USBD_FindNumOfIntf(pActiveCfg);
    if (numOfIntf == 0x00) {
        DBG_APP_ERR("numOfIntf-0x00\r\n");
        DBG_APP_ERR("CY_USB_APP_STATUS_FAILURE");
        return;
    }
    DBG_APP_TRACE("NumOfIntf:%d\r\n", numOfIntf);

    for (index = 0x00; index < numOfIntf; index++) {
        /* During Set Config command always altSetting 0 will be active. */
        pIntfDscr = Cy_USBD_GetIntfDscr(pUsbdCtxt, index, 0x00);
        if (pIntfDscr == NULL) {
            DBG_APP_INFO("pIntfDscrNull\r\n");
            DBG_APP_ERR("CY_USB_APP_STATUS_FAILURE");
            return;
        }

        numOfEndp = Cy_USBD_FindNumOfEndp(pIntfDscr);
        DBG_APP_TRACE("numOfEndp:%d\r\n", numOfEndp);
        if (numOfEndp == 0x00) {
            /* If current interface has 0 endpoint then move to next intf */
            DBG_APP_ERR("numOfEndp-0x00\r\n");
            continue;
        }

        pEndpDscr = Cy_USBD_GetEndpDscr(pUsbdCtxt, pIntfDscr);
        while (numOfEndp != 0x00) {
            /* first cleanup all channels related info then configure new. */
            Cy_USB_AppDestroyEndpDmaParamsHs(pUsbApp, pEndpDscr);
            Cy_USB_AppSetupEndpDmaParamsHs(pUsbApp, pEndpDscr);
            numOfEndp--;

            pEndpDscr = (pEndpDscr + (*(pEndpDscr + CY_USB_DSCR_OFFSET_LEN)));
            
        }
    }
    DBG_APP_TRACE("All endpoint Configured\r\n");

    pUsbApp->prevDevState = CY_USB_DEVICE_STATE_CONFIGURED;
    pUsbApp->devState = CY_USB_DEVICE_STATE_CONFIGURED;
    
    /* Initialize MSC storage after buffers are allocated */
    uint8_t msc_init_attempts = 0;
    bool msc_init_success = false;
    
    while (!msc_init_success && msc_init_attempts < 3) {
        msc_init_attempts++;
        
        if (Cy_USB_MSC_Init()) {
            msc_init_success = true;
            DBG_APP_INFO("MSC initialization successful on attempt %d\r\n", msc_init_attempts);
        } else {
            DBG_APP_ERR("MSC initialization failed on attempt %d\r\n", msc_init_attempts);
            if (msc_init_attempts < 3) {
                Cy_SysLib_Delay(100); /* 100ms delay before retry */
            }
        }
    }
    
    if (!msc_init_success) {
        DBG_APP_ERR("MSC initialization failed after %d attempts\r\n", msc_init_attempts);
    }
    
    glWritePending = false;
    glCswPending = false;

    /* Small delay before queuing first CBW to ensure host is ready */
    Cy_SysLib_Delay(10);

    /* Queue a read for the first CBW */
    LOG_MSC_INFO("[USB-MOUNT] Device configured and ready for SCSI commands\r\n");
    LOG_MSC_TRACE("Queuing first CBW read - buffer: %p\r\n", glCbwBuffer);
    Cy_USB_AppQueueRead(pAppCtxt, 0x01, glCbwBuffer, sizeof(CBW_t));

    DBG_APP_TRACE("Cy_USB_AppHandleSetCfgCommon: Endpoints configured <<\r\n");
} /* end of function. */

/**
 * \name Cy_USB_OutEpDma_READ_Complete
 * \brief Callback for OUT endpoint DMA completion. This is where CBWs are processed.
 * \param pAppCtxt Application context pointer.
 * \retval None
 */
void Cy_USB_OutEpDma_READ_Complete(void *pAppCtxt)
{
    cy_stc_usb_app_ctxt_t *pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    uint32_t residue = 0;
       
    /* Check if glWriteBuffer is valid */
    if (!glWriteBuffer) {
        LOG_MSC_ERROR("glWriteBuffer is NULL!\r\n");
        return;
    }

#if CY_CPU_CORTEX_M4
    Cy_HBDma_EvictReadCache(true);
#else
    Cy_HBDma_EvictReadCache(false);
#endif /* CY_CPU_CORTEX_M4 */

    /* Handle the data phase of a pending WRITE10 command */
    if (glWritePending)
    {
        glWritePending = false;
        uint32_t dataSize = glWriteBlocks * MSC_DUT_BLOCK_SIZE;

        /* Log write operations to track System Volume Information writes */
        LOG_MSC_INFO("[WRITE] LBA:%lu, Blocks:%u, Size:%lu bytes\r\n", 
                          glWriteLba, glWriteBlocks, dataSize);

        if (Cy_USB_MSC_WriteBlocks(glWriteLba, glWriteBlocks, glWriteBuffer))
        {
            glCswStatus = CSW_STATUS_PASSED;
            residue = glMscResidue;
        }
        else
        {
            LOG_MSC_ERROR("WRITE10: Failed to write data\r\n");
            glCswStatus = CSW_STATUS_FAILED;
            residue = glMscResidue + dataSize; /* Failed to write, so all data is residue */
            glSenseKey = 0x03; /* MEDIUM ERROR */
            glAsc = 0x0C;      /* WRITE ERROR */
            glAscq = 0x00;
        }

        Cy_USB_MSC_BOT_SendCSW(pUsbApp, glMscTag, residue, glCswStatus);
        
        /* Queue read for the next CBW immediately */
        Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));
        return;
    }


    /* Process CBW directly - no debugging interference */
    CBW_t *cbw = (CBW_t *)glCbwBuffer;

    /* Simple CBW validation - just check signature */
    if (cbw->dCBWSignature != CBW_SIGNATURE) {
        /* Re-queue immediately for maximum performance */
        Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));

        /* Stall both Bulk IN and OUT endpoints on phase error */
        Cy_USB_USBD_EndpSetClearStall(pUsbApp->pUsbdCtxt, 1, CY_USB_ENDP_DIR_IN, true);
        Cy_USB_USBD_EndpSetClearStall(pUsbApp->pUsbdCtxt, 1, CY_USB_ENDP_DIR_OUT, true);
        return;
    }

    /* Set up state for potential IN data transfer */
    glMscTag = cbw->dCBWTag;
    glCswStatus = CSW_STATUS_PASSED;
    residue = cbw->dCBWDataTransferLength;
    glCswPending = false; /* Default to no data phase */

    /* Process the SCSI command from the CBW */
    uint8_t scsiCommand = cbw->CBWCB[0];

    /* Track SCSI command sequence for mount analysis */
    static uint32_t scsi_cmd_count = 0;
    scsi_cmd_count++;
    if (scsi_cmd_count <= 20 || scsiCommand == SCSI_WRITE10 || scsiCommand == SCSI_READ10) {
        Cy_Debug_AddToLog(2, "[SCSI-%lu] Cmd:0x%02X %s\r\n", scsi_cmd_count, scsiCommand,
                          (scsiCommand == SCSI_INQUIRY) ? "INQUIRY" :
                          (scsiCommand == SCSI_READ_CAPACITY_10) ? "READ_CAPACITY" :
                          (scsiCommand == SCSI_TEST_UNIT_READY) ? "TEST_UNIT_READY" :
                          (scsiCommand == SCSI_READ10) ? "READ10" :
                          (scsiCommand == SCSI_WRITE10) ? "WRITE10" :
                          (scsiCommand == SCSI_MODE_SENSE6) ? "MODE_SENSE6" :
                          (scsiCommand == SCSI_REQUEST_SENSE) ? "REQUEST_SENSE" : "OTHER");
    }

    switch(scsiCommand)
    {
        case SCSI_TEST_UNIT_READY:
            {
                if (!Cy_USB_MSC_IsReady())
                {
                    glCswStatus = CSW_STATUS_FAILED;
                    glSenseKey = 0x02; /* NOT READY */
                    glAsc = 0x3A;      /* MEDIUM NOT PRESENT */
                    glAscq = 0x00;
                }
                Cy_USB_MSC_BOT_SendCSW(pUsbApp, cbw->dCBWTag, 0, glCswStatus);
                
                /* Queue immediately for maximum performance */
                Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));
            }
            break;

        case SCSI_INQUIRY:
            {
                /*
                 * SCSI Inquiry Standard Data (36 bytes):
                 * Byte 0: Peripheral Device Type (0x00 = Direct-access block device)
                 * Byte 1: Removable Medium Bit (0x80 = Removable)
                 * Byte 2: Version (0x02 = SCSI-2/SPC-2)
                 * Byte 3: Response Data Format (0x02 = SPC-2 compliant)
                 * Byte 4: Additional Length (0x1F = 31 bytes follow)
                 * Byte 5: Reserved (0x00)
                 * Byte 6: Reserved (0x00)
                 * Byte 7: Reserved (0x00)
                 * Bytes 8-15: Vendor Identification ('Infineon')
                 * Bytes 16-31: Product Identification ('FX2G3 MSC DEVICE')
                 * Bytes 32-35: Product Revision Level ('1.00')
                 */
            /* Standard MSC (SBC) INQUIRY response (36 bytes) */
            const uint8_t inquiry_response[36] = {
                0x00, /* Peripheral Device Type: Direct-access block device */
                0x80, /* Removable Medium Bit: Removable */
                0x02, /* Version: SPC-2 */
                0x02, /* Response Data Format: SPC-2 compliant */
                0x1F,   /* Additional Length (36-5=31) */
                0x00, 0x00, 0x00,
                'I','n','f','i','n','e','o','n',  /* Vendor ID */
                'F','X','2','G','3',' ','M','S','C',' ','D','E','V','I','C','E', /* Product ID */
                '1','.','0','0'                   /* Product Revision */
            };
            uint16_t alloc_len = cbw->CBWCB[4];
            uint16_t len_to_send = (alloc_len < sizeof(inquiry_response)) ? alloc_len : sizeof(inquiry_response);

            residue -= len_to_send;
            glMscResidue = residue;
            glCswPending = true; /* CSW will be sent after this data transfer */
            Cy_USB_AppQueueWrite(pUsbApp, 0x01, (uint8_t*)inquiry_response, len_to_send);
        }
        break;
        
        case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
        case SCSI_START_STOP_UNIT:
        case SCSI_SYNCHRONIZE_CACHE:
        case SCSI_VERIFY10:
            {
                /* VERIFY10 command - just acknowledge success for RAW device */
                static uint32_t verify_count = 0;
                verify_count++;
                if ((verify_count % 50) == 1) {
                    LOG_MSC_INFO("[SCSI] VERIFY10 - OK (count: %lu)\r\n", verify_count);
                }
                Cy_USB_MSC_BOT_SendCSW(pUsbApp, cbw->dCBWTag, 0, CSW_STATUS_PASSED);
                
                /* Small delay to ensure CSW transmission before queuing next CBW */
                for (volatile uint32_t i = 0; i < 500; i++) { /* Brief delay */ }
                
                Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));
            }
            break;

        case SCSI_FORMAT_UNIT:
            {
            /* FORMAT_UNIT command for RAW block device */
                LOG_MSC_INFO("[SCSI] FORMAT_UNIT command received (FmtData=%d, CmpLst=%d)\r\n", 
                (cbw->CBWCB[1] & 0x10) ? 1 : 0, (cbw->CBWCB[1] & 0x08) ? 1 : 0);
            
            /* Check if write protection is enabled */
            if (Cy_USB_MSC_IsWriteProtected())
            {
                LOG_MSC_INFO("[SCSI] FORMAT_UNIT failed - Write protected\r\n");
                glCswStatus = CSW_STATUS_FAILED;
                glSenseKey = 0x07; /* DATA PROTECT */
                glAsc = 0x27;      /* WRITE PROTECTED */
                glAscq = 0x00;
            }
            else
            {
                /* Check if host is sending format parameter data */
                if ((cbw->CBWCB[1] & 0x10) && cbw->dCBWDataTransferLength > 0) {
                    /* Host is sending format data - queue read for parameter list */
                    LOG_MSC_TRACE("Info:SCSI:FORMAT_UNIT - Reading format parameters (%u bytes)\r\n", 
                        cbw->dCBWDataTransferLength);
                    residue = cbw->dCBWDataTransferLength;
                    glMscResidue = residue;
                    glCswPending = true; /* CSW will be sent after data phase */
                    
                    /* Queue read for format parameter data */
                    Cy_USB_AppQueueRead(pUsbApp, 0x01, glWriteBuffer, cbw->dCBWDataTransferLength);
                } else {
                    /* Quick format without parameter data */
                    LOG_MSC_INFO("[SCSI] FORMAT_UNIT - Quick format accepted\r\n");
                    glCswStatus = CSW_STATUS_PASSED;
                    Cy_USB_MSC_BOT_SendCSW(pUsbApp, cbw->dCBWTag, 0, glCswStatus);
                    
                    /* Small delay to ensure CSW transmission before queuing next CBW */
                    for (volatile uint32_t i = 0; i < 500; i++) { /* Brief delay */ }
                    
                    Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));
                }
            }
            }
            break;

        case SCSI_REQUEST_SENSE:
        {
            /* Fixed format, 18-byte response */
            uint8_t sense_response[18] = {
                0x70, 0x00, /* Response Code, Obsolete */
                glSenseKey, /* Sense Key */
                0x00, 0x00, 0x00, 0x00, /* Information */
                10,   /* Additional Sense Length (18-8=10) */
                0x00, 0x00, 0x00, 0x00, /* Command Specific Info */
                glAsc,  /* Additional Sense Code */
                glAscq, /* Additional Sense Code Qualifier */
                0x00, 0x00, 0x00, 0x00 /* FRU, SKSV */
            };
            uint16_t len_to_send = (cbw->CBWCB[4] < sizeof(sense_response)) ? cbw->CBWCB[4] : sizeof(sense_response);

            residue -= len_to_send;
            glMscResidue = residue;
            glCswPending = true;
            Cy_USB_AppQueueWrite(pUsbApp, 0x01, sense_response, len_to_send);

            /* Clear sense data after it has been reported */
            glSenseKey = 0x00;
            glAsc = 0x00;
            glAscq = 0x00;
        }
        break;

        case SCSI_MODE_SENSE6:
        {
            /* Standard MSC response with write-protect bit */
            uint8_t mode_sense_response[4] = {
                0x03, /* Mode data length (n-1) */
                0x00, /* Medium type */
                Cy_USB_MSC_IsWriteProtected() ? 0x80 : 0x00, /* WP bit */
                0x00  /* Block descriptor length */
            };
            residue -= sizeof(mode_sense_response);
            glMscResidue = residue;
            glCswPending = true;
            Cy_USB_AppQueueWrite(pUsbApp, 0x01, mode_sense_response, sizeof(mode_sense_response));
        }
        break;

        case SCSI_READ_FORMAT_CAPACITIES:
        {
            uint32_t totalBlocks;
            uint32_t blockSize;
            Cy_USB_MSC_GetCapacity(&totalBlocks, &blockSize);
            
            /* For RAW block device, provide multiple format descriptors with unformatted media */
            uint8_t format_capacities_response[20] = {
                0x00, 0x00, 0x00, 0x10, /* Capacity List Length = 16 bytes (2 descriptors) */
                
                /* Current/Maximum Format Capacity Descriptor (8 bytes) */
                (uint8_t)(totalBlocks >> 24), (uint8_t)(totalBlocks >> 16),
                (uint8_t)(totalBlocks >> 8), (uint8_t)totalBlocks, /* Number of Blocks */
                0x03, /* Descriptor Code: No Cartridge in Drive / Unformatted Media */
                (uint8_t)(blockSize >> 16), (uint8_t)(blockSize >> 8), (uint8_t)blockSize, /* Block Length */
                
                /* Formattable Capacity Descriptor (8 bytes) - Same capacity but formatted */
                (uint8_t)(totalBlocks >> 24), (uint8_t)(totalBlocks >> 16),
                (uint8_t)(totalBlocks >> 8), (uint8_t)totalBlocks, /* Number of Blocks */
                0x02, /* Descriptor Code: Formatted Media - what it will be after format */
                (uint8_t)(blockSize >> 16), (uint8_t)(blockSize >> 8), (uint8_t)blockSize /* Block Length */
            };
            LOG_MSC_INFO("[SCSI] READ_FORMAT_CAPACITIES - Unformatted media (%u blocks)\r\n", totalBlocks);
            residue -= sizeof(format_capacities_response);
            glMscResidue = residue;
            glCswPending = true;
            Cy_USB_AppQueueWrite(pUsbApp, 0x01, format_capacities_response, sizeof(format_capacities_response));
        }
        break;
        case SCSI_READ_CAPACITY_10:
        {
            uint32_t totalBlocks;
            uint32_t blockSize;
            Cy_USB_MSC_GetCapacity(&totalBlocks, &blockSize);
            /*
            * SCSI Read Capacity(10) response (8 bytes):
            * Bytes 0-3: Last Logical Block Address (big-endian)
            * Bytes 4-7: Block Length in bytes (big-endian)
            *
            * Ex: if totalBlocks_cap = 64, blockSize_cap = 512 then
            *   Last LBA = 63 (0x00 0x00 0x00 0x3F)
            *   Block size = 512 (0x00 0x00 0x02 0x00)
            */
            uint8_t capacity_response[8];
            /* Last LBA = totalBlocks - 1 */
            capacity_response[0] = (uint8_t)((totalBlocks - 1) >> 24);
            capacity_response[1] = (uint8_t)((totalBlocks - 1) >> 16);
            capacity_response[2] = (uint8_t)((totalBlocks - 1) >> 8);
            capacity_response[3] = (uint8_t)(totalBlocks - 1);
            /* Block Size */
            capacity_response[4] = (uint8_t)(blockSize >> 24);
            capacity_response[5] = (uint8_t)(blockSize >> 16);
            capacity_response[6] = (uint8_t)(blockSize >> 8);
            capacity_response[7] = (uint8_t)(blockSize);

            residue -= sizeof(capacity_response);
            glMscResidue = residue;
            glCswPending = true;
            Cy_USB_AppQueueWrite(pUsbApp, 0x01, capacity_response, sizeof(capacity_response));
        }
        break;
        case SCSI_READ10:
        {
            uint32_t lba = (cbw->CBWCB[2] << 24) | (cbw->CBWCB[3] << 16) | (cbw->CBWCB[4] << 8) | cbw->CBWCB[5];
            uint16_t blocks = (cbw->CBWCB[7] << 8) | cbw->CBWCB[8];
            uint32_t dataSize = blocks * MSC_DUT_BLOCK_SIZE;
#if USE_RAM_STORAGE
            uint8_t* readPtr = Cy_USB_MSC_GetStoragePtr(lba);
#else
            uint8_t* readPtr = Cy_USB_MSC_GetStoragePtr(lba, dataSize);
#endif /* USE_RAM_STORAGE */

            if (readPtr != NULL && blocks > 0)
            {
                residue -= dataSize;
                glCswStatus = CSW_STATUS_PASSED;
                glCswPending = true;
                glMscResidue = residue;
                Cy_USB_AppQueueWrite(pUsbApp, 0x01, readPtr, dataSize);
            }
            else
            {
                glCswStatus = CSW_STATUS_FAILED;
                glSenseKey = 0x05; /* ILLEGAL REQUEST */
                glAsc = 0x21;      /* LOGICAL BLOCK ADDRESS OUT OF RANGE */
                glAscq = 0x00;
                Cy_USB_MSC_BOT_SendCSW(pUsbApp, cbw->dCBWTag, cbw->dCBWDataTransferLength, glCswStatus);
                Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));
            }
        }
        break;

        case SCSI_WRITE10:
        {
            glWriteLba = (cbw->CBWCB[2] << 24) | (cbw->CBWCB[3] << 16) | (cbw->CBWCB[4] << 8) | cbw->CBWCB[5];
            glWriteBlocks = (cbw->CBWCB[7] << 8) | cbw->CBWCB[8];
            uint32_t dataSize = glWriteBlocks * MSC_DUT_BLOCK_SIZE;

            if (Cy_USB_MSC_IsWriteProtected())
            {
                glCswStatus = CSW_STATUS_FAILED;
                glSenseKey = 0x07; /* DATA PROTECT */
                glAsc = 0x27;      /* WRITE PROTECTED */
                glAscq = 0x00;
                Cy_USB_MSC_BOT_SendCSW(pUsbApp, cbw->dCBWTag, cbw->dCBWDataTransferLength, glCswStatus);
                Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));
                break;
            }

            if (dataSize > 0)
            {
                /* Prepare for data phase */
                glWritePending = true;
                glMscTag = cbw->dCBWTag;
                glMscResidue = cbw->dCBWDataTransferLength - dataSize;
                Cy_USB_AppQueueRead(pUsbApp, 0x01, glWriteBuffer, dataSize);
            }
            else /* Zero-length write is valid, just send CSW */
            {
                Cy_USB_MSC_BOT_SendCSW(pUsbApp, cbw->dCBWTag, 0, CSW_STATUS_PASSED);
                Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));
            }
        }
        break;
        


        default:
            /* Unsupported command */
            {
                char cmdBytes[32];
                int pos = 0;
                for (int i = 0; i < 10 && pos < (int)sizeof(cmdBytes) - 4; i++) {
                    pos += sprintf(cmdBytes + pos, "%02X ", cbw->CBWCB[i]);
                }
                LOG_MSC_ERROR("[SCSI] Unsupported command 0x%02X, bytes: %s\r\n", 
                                  cbw->CBWCB[0], cmdBytes);
            }
            
            /* Set proper sense data for unsupported command */
            glCswStatus = CSW_STATUS_FAILED;
            glSenseKey = 0x05; /* ILLEGAL REQUEST */
            glAsc = 0x20;      /* INVALID COMMAND OPERATION CODE */
            glAscq = 0x00;
            
            /* Send CSW with proper tag and data transfer length */
            Cy_USB_MSC_BOT_SendCSW(pUsbApp, cbw->dCBWTag, cbw->dCBWDataTransferLength, glCswStatus);
            
            /* Small delay to ensure CSW transmission */
            for (volatile uint32_t i = 0; i < 500; i++) { /* Brief delay */ }
            
            Cy_USB_AppQueueRead(pUsbApp, 0x01, glCbwBuffer, sizeof(CBW_t));
            break;
    }
} /* end of function. */

/**
 * \name Cy_USB_AppReadShortPacket
 * \brief Function to modify an ongoing DMA read operation to take care of a short packet.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USB endpoint number
 * \param pktSize USB data size
 * \retval Total size of data in the DMA buffer including data which was already read by the channel.
 */
uint16_t
Cy_USB_AppReadShortPacket(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t endpNumber, uint16_t pktSize)
{
    cy_stc_app_endp_dma_set_t *dmaset_p=NULL;
    uint16_t dataSize = 0;

    /* Null pointer checks. */
    if ((pAppCtxt == NULL) || (pAppCtxt->pUsbdCtxt == NULL) || (pAppCtxt->pCpuDw0Base == NULL))
    {
        DBG_APP_ERR("ReadSLP:NULL\r\n");
        return 0;
    }

    /* Verify that the selected endpoint is valid. */
    if (pAppCtxt->endpOutDma[endpNumber].valid == 0)
    {
        DBG_APP_ERR("ReadSLP:BadParam\r\n");
        return 0;
    }

    dmaset_p = &(pAppCtxt->endpOutDma[endpNumber]);

    if (dmaset_p->endpType != CY_USB_ENDP_TYPE_ISO) {
        /* NAK the endpoint until we queue a new DMA request. */
        Cy_USB_USBD_EndpSetClearNakNrdy(pAppCtxt->pUsbdCtxt, endpNumber, CY_USB_ENDP_DIR_OUT, true);
    }

    /* The code assumes that the channel is active. */
    dataSize = Cy_USBHS_App_ReadShortPacket(dmaset_p, pktSize);
    
    return dataSize;
} /* end of function */

/**
 * \name Cy_USB_AppSlpCallback
 * \brief Callback function will be invoked by USBD when SLP message comes.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer
 * \param pMsg USB Message
 * \retval None
 */
void
Cy_USB_AppSlpCallback (void *pUsbApp, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                       cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pAppCtxt;
    BaseType_t status;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    cy_stc_usbd_app_msg_t xMsg;

    DBG_APP_TRACE("AppSlpCb\r\n");

    /*
     * Get cal MSG.
     * Convert CAL msg to usbd/app message.
     * send it through queue.
     */
    pAppCtxt = (cy_stc_usb_app_ctxt_t *)pUsbApp;

    if (pMsg->type == CY_USB_CAL_MSG_OUT_SLP) {
        xMsg.type = CY_USB_SLP_OUT_MSG;
    } else {
        xMsg.type = CY_USB_SLP_IN_MSG;
    }
    xMsg.data[0] = pMsg->data[0];
    xMsg.data[1] = pMsg->data[1];
    status = xQueueSendFromISR(pAppCtxt->xQueue,  &(xMsg),
                               &(xHigherPriorityTaskWoken));

    (void)status;
    return;
}   /* end of function. */


/**
 * \name Cy_USB_AppRegisterCallback
 * \brief This function will register all calback with USBD layer.
 * \param pAppCtxt application layer context pointer
 * \return None
 */
void
Cy_USB_AppRegisterCallback (cy_stc_usb_app_ctxt_t *pAppCtxt)
{
    cy_stc_usb_usbd_ctxt_t *pUsbdCtxt = pAppCtxt->pUsbdCtxt;

    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_RESET, 
                                               Cy_USB_AppBusResetCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SETUP, 
                                                  Cy_USB_AppSetupCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SET_CONFIG, 
                                                  Cy_USB_AppSetConfigCallback);
    Cy_USBD_RegisterCallback(pUsbdCtxt, CY_USB_USBD_CB_SLP, 
    Cy_USB_AppSlpCallback);

    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppBusResetCallback
 * \brief This Function will be called by USBD when bus detects RESET.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer.
 * \retval None
 */
void 
Cy_USB_AppBusResetCallback (void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                            cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp;

    pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;

    DBG_APP_INFO("ResetCallback >>\r\n");
    
    /* Free existing DMA buffers on reset to prevent memory leaks */
    if (gl_ReadBuffer) {
        Cy_HBDma_BufMgr_Free(&HBW_BufMgr, gl_ReadBuffer);
        gl_ReadBuffer = NULL;
    }
    if (glWriteBuffer) {
        Cy_HBDma_BufMgr_Free(&HBW_BufMgr, glWriteBuffer);
        glWriteBuffer = NULL;
    }
    
    /*
     * USBD layer takes care of reseting its own data structure as well as
     * takes care of calling CAL reset APIs. Application needs to take care
     * of reseting its own data structure as well as "device function".
     */
    Cy_USB_AppInit(pUsbApp, pUsbdCtxt, pUsbApp->pCpuDmacBase,
                   pUsbApp->pCpuDw0Base, pUsbApp->pCpuDw1Base);
    pUsbApp->devState = CY_USB_DEVICE_STATE_RESET;
    pUsbApp->prevDevState = CY_USB_DEVICE_STATE_RESET;

    DBG_APP_INFO("ResetCallback <<\r\n\r\n");
    return;
}   /* end of function. */

/**
 * \name Cy_USB_AppSetupCallback
 * \brief Callback function will be invoked by USBD when SETUP packet is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void 
Cy_USB_AppSetupCallback (void *pAppCtxt, cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                         cy_stc_usb_cal_msg_t *pMsg)
{
    cy_stc_usb_app_ctxt_t *pUsbApp = (cy_stc_usb_app_ctxt_t *)pAppCtxt;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    cy_stc_usbd_app_msg_t xMsg;
    BaseType_t status;

    xMsg.type = CY_USB_MSG_CTRL_XFER_SETUP;
    xMsg.data[0] = pMsg->data[0];
    xMsg.data[1] = pMsg->data[1];

    status = xQueueSendFromISR(pUsbApp->xQueue, &(xMsg),
                               &(xHigherPriorityTaskWoken));
    (void)status;

}   /* end of function. */

/**
 * \name Cy_CheckStatus
 * \brief Function that handles prints error log
 * \param function Pointer to function
 * \param line Line number where error is seen
 * \param condition condition of failure
 * \param value error code
 * \param isBlocking blocking function
 * \return None
 */
void 
Cy_CheckStatus(const char *function, uint32_t line, uint8_t condition, uint32_t value, uint8_t isBlocking)
{
    if (!condition)
    {
        /* Application failed with the error code status */
        Cy_Debug_AddToLog(1, "Function %s failed at line %d with status = 0x%x\r\n", function, line, value);
        if (isBlocking)
        {
            /* Loop indefinitely */
            for (;;)
            {
            }
        }
    }
}

/**
 * \name Cy_CheckStatusHandleFailure
 * \brief Function that handles prints error log
 * \param function Pointer to function
 * \param line LineNumber where error is seen
 * \param condition Line number where error is seen
 * \param value error code
 * \param isBlocking blocking function
 * \param failureHandler failure handler function
 * \return None
 */
void 
Cy_CheckStatusHandleFailure(const char *function, uint32_t line, uint8_t condition, uint32_t value, uint8_t isBlocking, void (*failureHandler)(void))
{
    if (!condition)
    {
        /* Application failed with the error code status */
        Cy_Debug_AddToLog(1, "Function %s failed at line %d with status = 0x%x\r\n", function, line, value);

        if(failureHandler != NULL)
        {
            (*failureHandler)();
        }
        if (isBlocking)
        {
            /* Loop indefinitely */
            for (;;)
            {
            }
        }
    }
} /* end of function. */

/**
 * \name Cy_FailHandler
 * \brief Error Handler
 * \retval None
 */
void 
Cy_FailHandler(void)
{
    DBG_APP_ERR("Reset Done\r\n");
}

/* [] END OF FILE */

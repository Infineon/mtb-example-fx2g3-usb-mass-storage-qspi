/***************************************************************************//**
* \file usb_app.h
* \version 1.0
*
* \details Header file for Application data structures and functions declaration.
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
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef _CY_USB_APP_H_
#define _CY_USB_APP_H_

#include "cy_debug.h"
#include "cy_syslib.h"
#include "cy_usbhs_dw_wrapper.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Macros */
#define RED                             "\033[0;31m"
#define CYAN                            "\033[0;36m"
#define COLOR_RESET                     "\033[0m"

#define ASSERT(condition, value)        Cy_CheckStatus(__func__, __LINE__, condition, value, true);
#define ASSERT_NON_BLOCK(condition, value) Cy_CheckStatus(__func__, __LINE__, condition, value, false);
#define ASSERT_AND_HANDLE(condition, value, failureHandler) Cy_CheckStatusHandleFailure(__func__, __LINE__, condition, value, false, Cy_FailHandler);

#define CY_USB_MSG_CTRL_XFER_SETUP          (0x0A)
#define CY_USB_MSG_CTRL_XFER_DATA           (0x02)
#define CY_USB_ENDP0_READ_TIMEOUT           (0x0D)
#define CY_USB_VBUS_CHANGE_INTR             (0x0E)
#define CY_USB_VBUS_CHANGE_DEBOUNCED        (0x0F)

#define CY_USB_SLP_IN_MSG                   (0x07)
#define CY_USB_SLP_OUT_MSG                  (0x09)

#define CY_USB_MSG_QUEUE_SIZE               (16)
#define CY_USB_MSG_SIZE                     (sizeof (cy_stc_usbd_app_msg_t))

#define VBUS_DETECT_GPIO_PORT                (P4_0_PORT) /* P4.0 is used for VBus detect functionality. */
#define VBUS_DETECT_GPIO_PIN                 (P4_0_PIN)
#define VBUS_DETECT_GPIO_INTR                (ioss_interrupts_gpio_dpslp_4_IRQn)
#define VBUS_DETECT_STATE                    (0u)

#define CY_APP_SPI_MAX_USB_TRANSFER_SIZE    (0x800)

#define USB_DESC_ATTRIBUTES __attribute__ ((section(".descSection"), used)) __attribute__ ((aligned (32)))
#define HBDMA_BUF_ATTRIBUTES __attribute__ ((section(".hbBufSection"), used)) __attribute__ ((aligned (32)))

#define MS_VENDOR_CODE                    (0xF0) /* Vendor command code used to return WinUSB specific descriptors. */

/* SCSI command opcodes */

/*
 * SCSI command opcodes for standard Mass Storage Class devices.
 * This includes all commands for MSC compatibility.
 */
#define SCSI_TEST_UNIT_READY                (0x00)
#define SCSI_REQUEST_SENSE                  (0x03)
#define SCSI_FORMAT_UNIT                    (0x04)
#define SCSI_INQUIRY                        (0x12)
#define SCSI_MODE_SELECT6                   (0x15)
#define SCSI_MODE_SENSE6                    (0x1A)
#define SCSI_START_STOP_UNIT                (0x1B)
#define SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL   (0x1E)
#define SCSI_READ_FORMAT_CAPACITIES         (0x23)
#define SCSI_READ_CAPACITY_10               (0x25)
#define SCSI_READ10                         (0x28)
#define SCSI_WRITE10                        (0x2A)
#define SCSI_VERIFY10                       (0x2F)
#define SCSI_SYNCHRONIZE_CACHE              (0x35)
#define SCSI_MODE_SELECT10                  (0x55)
#define SCSI_MODE_SENSE10                   (0x5A)
#define SCSI_READ12                         (0xA8)
#define SCSI_WRITE12                        (0xAA)
#define SCSI_REPORT_LUNS                    (0xA0)

/*
 * These are class-specific requests defined in the Bulk-Only Transport (BOT)
 * specification. They are handled as control requests, not SCSI commands over
 * the bulk endpoints, but are often grouped with SCSI definitions.
 */
#define SCSI_GET_MAX_LUN                    (0xFE)
#define SCSI_BULK_ONLY_MASS_STORAGE_RESET   (0xFF)

/* BOT protocol constants */
#define CBW_SIGNATURE                       (0x43425355)  /* "USBC" */
#define CSW_SIGNATURE                       (0x53425355)  /* "USBS" */
#define CSW_STATUS_PASSED                   (0x00)
#define CSW_STATUS_FAILED                   (0x01)
#define CSW_STATUS_PHASE_ERROR              (0x02)

extern uint8_t glOsString[];
extern uint8_t glOsCompatibilityId[];
extern uint8_t glOsFeature[];

typedef struct cy_stc_usb_app_ctxt_ cy_stc_usb_app_ctxt_t;

/* Structs */
typedef struct Cy_IfxQueue_ Cy_IfxQueue_t;
typedef struct Cy_IfxQueueElem_ Cy_IfxQueueElem_t;

/* QueueElem and IfxQueue data structurs defined for Loopback device. */
struct Cy_IfxQueueElem_ {
    uint8_t  *pData;   /* Based on speed of device pointer will be changed. */
    uint32_t dataLen;
};

/*
 * SRC-SYNC mode dont need to maintain data so next two pointers are used
 * instead complete queue.
 * pReadBuffer: Used when device in SRC-SYNC mode. Gets 0th
 *              location from dataQueue.
 * pWriteBuffer: Used when device in SRC-SYNC mode. Gets 1th
 *               location from dataQueue.
 */
struct Cy_IfxQueue_ {
    Cy_IfxQueueElem_t elem[8]; /* Max 8 elements in queue. */
    uint32_t readIndex;
    uint32_t writeIndex;
    uint32_t numElem;
    uint8_t *pReadBuffer;
    uint32_t readLength;
    uint8_t *pWriteBuffer;
    uint32_t writeLength;
    uint32_t maxPktSizeOut;
    uint32_t maxPktSizeIn;
    uint32_t endpNumOut;
    uint32_t endpNumIn;
    bool readActive;
    bool writeActive;
};

typedef struct cy_spi_simple_ctx{
    uint16_t wValue;        /* wValue from setup packet */
    uint16_t wIndex;        /* wIndex from setup packet */
    uint32_t spiAddress;    /* SPI address to write/read */
    uint16_t numPages;      /* Number of pages to write */
} cy_spi_simple_ctx_t;

/*
 * USB application data structure which is bridge between USB system and device
 * functionality.
 * It maintains some usb system information which comes from USBD and it also
 * maintains information about device functionality.
 */
struct cy_stc_usb_app_ctxt_
{
    bool vbusChangeIntr;
    uint8_t firstInitDone;
    uint8_t devAddr;
    uint8_t activeCfgNum;
    uint8_t currentAltSetting;
    cy_en_usb_device_state_t devState;
    cy_en_usb_device_state_t prevDevState;
    cy_en_usb_speed_t devSpeed;
    cy_en_usb_enum_method_t enumMethod;

    /* added */
    cy_stc_app_endp_dma_set_t endpInDma[16];
    cy_stc_app_endp_dma_set_t endpOutDma[16];
    uint8_t intfAltSetEndp[4][4];               /* Max 4INTF AND EACH INTERFACE 4 alt setting */ 
    cy_spi_simple_ctx_t spictx;                 /* SPI context for simple SPI commands */

    /* Next three are related to central DMA */
    DMAC_Type *pCpuDmacBase;
    DW_Type *pCpuDw0Base;
    DW_Type *pCpuDw1Base;
    cy_stc_usb_usbd_ctxt_t *pUsbdCtxt;

    /* Global Task handles */
    TaskHandle_t taskHandle;
    QueueHandle_t xQueue;
    TimerHandle_t vbusDebounceTimer;

    /* VBus detect status */
    bool vbusPresent;
    bool usbConnected;    

    uint8_t *qspiWriteBuffer;
    uint8_t *qspiReadBuffer;
    uint8_t glpassiveSerialMode;
};

/**
 * \typedef cy_en_fx10_fpga_config_type_t
 * \brief Enum for FPGA Configuration
 */
typedef enum cy_en_fx10_fpga_config_type_t
{
    FPGA_PASSIVE_SERIAL_x4 = 1,
    FPGA_PASSIVE_SERIAL_x8,
    FPGA_CONFIG_INVALID = 0xFF,

}cy_en_fx10_fpga_config_type_t;

/* Parse data against BOT protocol */
typedef struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} __attribute__((packed)) CBW_t;

/* Command Status Wrapper (CSW) structure */
typedef struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} __attribute__((packed)) CSW_t;

/* Function Prototypes */
/**
 * \name Cy_USB_TaskHandler
 * \brief This function handles events for device.
 * \param pTaskParam Task param
 * \retval None
 */
void Cy_USB_TaskHandler(void *pTaskParam);

/**
 * \name Cy_USB_Endp0ReadComplete
 * \brief Handler for DMA transfer completion on endpoint 0 OUT Transfer.
 * \param pApp application layer context pointer
 * \retval None
 */
void Cy_USB_Endp0ReadComplete(void *pApp);

/**
 * \name Cy_USB_AppRegisterCallback
 * \brief This function will register all calback with USBD layer.
 * \param pAppCtxt application layer context pointer
 * \return None
 */
void Cy_USB_AppRegisterCallback(cy_stc_usb_app_ctxt_t *pAppCtxt);

/**
 * \name Cy_USB_AppBusResetCallback
 * \brief This Function will be called by USBD when bus detects RESET.
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD layer context pointer.
 * \retval None
 */
void Cy_USB_AppBusResetCallback(void *pAppCtxt, 
                                cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                                cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_AppSetupCallback
 * \brief Callback function will be invoked by USBD when SETUP packet is received
 * \param pAppCtxt application layer context pointer.
 * \param pUsbdCtxt USBD context
 * \param pMsg USB Message
 * \retval None
 */
void Cy_USB_AppSetupCallback(void *pAppCtxt, 
                             cy_stc_usb_usbd_ctxt_t *pUsbdCtxt,
                             cy_stc_usb_cal_msg_t *pMsg);

/**
 * \name Cy_USB_ConnectionEnable
 * \brief Function used to enable USB connection.
 * \param pAppCtxt Application context structure.
 * \retval None
 */
bool Cy_USB_ConnectionEnable(cy_stc_usb_app_ctxt_t *pAppCtxt);

/**
 * \name Cy_USB_ConnectionDisable
 * \brief Function which disables the USB connection.
 * \param pAppCtxt Application context structure pointer.
 * \retval None
 */
void Cy_USB_ConnectionDisable(cy_stc_usb_app_ctxt_t *pAppCtxt);

/**
 * \name Cy_USB_AppInit
 * \details This function Initializes application related data structures,
 *          register callback and creates queue and task for device
 *          function. Common function for FS/HS.
 * \param pAppCtxt application layer conetxt pointer
 * \param pUsbdCtxt USBD layer conetxt pointer
 * \param pCpuDmacBase DMA controller base address pointer
 * \param pCpuDw0Base Datawire 0 base address pointer
 * \param pCpuDw1Base Datawire 1 base address pointer
 * \retval None
 */
void Cy_USB_AppInit (cy_stc_usb_app_ctxt_t *pAppCtxt,
                cy_stc_usb_usbd_ctxt_t *pUsbdCtxt, DMAC_Type *pCpuDmacBase,
                DW_Type *pCpuDw0Base, DW_Type *pCpuDw1Base);

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
void Cy_CheckStatus(const char *function, uint32_t line, uint8_t condition, 
                    uint32_t value, uint8_t isBlocking);

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
void Cy_CheckStatusHandleFailure(const char *function, uint32_t line, uint8_t condition, 
                        uint32_t value, uint8_t isBlocking, void (*failureHandler)());

/**
 * \name Cy_FailHandler
 * \brief Error Handler
 * \retval None
 */
void Cy_FailHandler(void);

/**
 * \name Cy_USB_AppClearCpuDmaInterrupt
 * \brief Function to clear the pending DMA interrupt associated with an endpoint.
 * \param pAppCtxt application layer context pointer.
 * \param endpNum endpoint number.
 * \param endpDir endpoint direction
 * \retval None
 */
void Cy_USB_AppClearCpuDmaInterrupt (cy_stc_usb_app_ctxt_t *pAppCtxt,
                                uint32_t endpNum, cy_en_usb_endp_dir_t endpDir);

/**
 * \name Cy_USB_OutEpDma_READ_Complete
 * \brief Callback for OUT endpoint DMA completion. This is where CBWs are processed.
 * \param pAppCtxt Application context pointer.
 * \retval None
 */
void Cy_USB_OutEpDma_READ_Complete(void *pAppCtxt);

/**
 * \name Cy_USB_AppQueueRead
 * \brief Function to queue read operation on an OUT endpoint.
 * \param pAppCtxt application layer context pointer.
 * \param endpNum endpoint number.
 * \param pBuffer pointer to buffer where data will be stored.
 * \param dataSize expected data size.
 * \retval None
 */
void Cy_USB_AppQueueRead(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t endpNum,
                     uint8_t *pBuffer, uint32_t dataSize);

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
                          uint8_t *pBuffer, uint32_t dataSize);

/* MSC BOT function declarations */
/**
 * \name Cy_USB_MSC_BOT_SendCSW
 * \brief Constructs and sends a Command Status Wrapper (CSW)
 * \param pUsbApp USB application context
 * \param tag The tag from the corresponding CBW
 * \param residue The difference between data expected and data transferred
 * \param status The status of the command execution
 * \retval None
 */
void Cy_USB_MSC_BOT_SendCSW(cy_stc_usb_app_ctxt_t *pUsbApp, uint32_t tag, uint32_t residue, uint8_t status);

/**
 * \name Cy_USB_AppReadShortPacket
 * \brief Function to modify an ongoing DMA read operation to take care of a short packet.
 * \param pAppCtxt application layer context pointer.
 * \param endpNumber USB endpoint number
 * \param pktSize USB data size
 * \retval Total size of data in the DMA buffer including data which was already read by the channel.
 */
uint16_t Cy_USB_AppReadShortPacket(cy_stc_usb_app_ctxt_t *pAppCtxt, uint8_t endpNumber, uint16_t pktSize);

#if defined(__cplusplus)
}
#endif

#endif /* _CY_USB_APP_H_ */

/* End of File */

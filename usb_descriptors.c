/***************************************************************************//**
* \file usb_descriptors.c
* \version 1.0
*
* \brief Defines the USB descriptors used in the USB QSPI application.
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

#include "cy_pdl.h"
#include "cy_usb_common.h"
#include "cy_usb_usbd.h"
#include "usb_app.h"
#include "spi.h"

/* USB 2.0 descriptors */
/* Standard device descriptor for USB 2.0 */
USB_DESC_ATTRIBUTES uint8_t CyFxUSB20DeviceDscr[] __attribute__ ((aligned (4))) =
{
    0x12,                            /* Descriptor size */
    0x01,                            /* Device descriptor type */
    0x00,0x02,                       /* USB 2.00 */
    0x00,                            /* Device class */
    0x00,                            /* Device sub-class */
    0x00,                            /* Device protocol */
    0x40,                            /* Maxpacket size for EP0 : 64 bytes */
    0xB4,0x04,                       /* Vendor ID */
    0x09,0x49,                      /* FX2G3 Vendor ID. */
    0x00,0x00,                      /* Device release number */
    0x01,                           /* Manufacture string index */
    0x02,                           /* Product string index */
    0x03,                           /* Serial number string index */
    0x01                            /* Number of configurations */
};

/* Device qualifier descriptor. */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBDeviceQualDscr[] __attribute__ ((aligned (4))) =
{
    0x0A,                           /* Descriptor size */
    0x06,                           /* Device qualifier descriptor type */
    0x00,0x02,                      /* USB 2.0 */
    0x00,                           /* Device class */
    0x00,                           /* Device sub-class */
    0x00,                           /* Device protocol */
    0x40,                           /* Maxpacket size for EP0 : 64 bytes */
    0x01,                           /* Number of configurations */
    0x00                            /* Reserved */
};

/* Binary Object Store (BOS) Descriptor. */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBBOSDscr[] __attribute__ ((aligned (4))) =
{
    0x05,                           /* Descriptor size */
    CY_USB_BOS_DSCR,                /* BOS descriptor type */
    0x0C,0x00,                      /* Length of this descriptor and all sub descriptors */
    0x01,                           /* Number of device capability descriptors */

    /* USB 2.0 extension */
    0x07,                           /* Descriptor size */
    CY_DEVICE_CAPB_DSCR,            /* Device capability type descriptor */
    0x02,                           /* USB 2.0 extension capability type */
    0x1E,0x64,0x00,0x00             /* Supported device level features: LPM support, BESL supported,
                                       Baseline BESL=400 us, Deep BESL=1000 us. */
};

USB_DESC_ATTRIBUTES uint8_t CyFxUSBStringLangIDDscr[32] __attribute__ ((aligned (32))) =
{
    0x04,
    0x03,
    0x09,
    0x04
};

/* Standard Manufacturer String descriptor */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBManufactureDscr[32] __attribute__ ((aligned (32))) =
{
    0x12,
    0x03,
    'I',
    0x00,
    'N',
    0x00,
    'F',
    0x00,
    'I',
    0x00,
    'N',
    0x00,
    'E',
    0x00,
    'O',
    0x00,
    'N',
    0x00
};

/* Standard Product String desciptor */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBProductDscr[] __attribute__ ((aligned (32))) =
{
    0x3D,
    0x03,
    'E',0x00,
    'Z',0x00,
    '-',0x00,
    'U',0x00,
    'S',0x00,
    'B',0x00,
    ' ',0x00,
    'F',0x00,
    'X',0x00,
    '2',0x00,
    'G',0x00,
    '3',0x00,
    ':',0x00,
    ' ',0x00,
    'U',0x00,
    'S',0x00,
    'B',0x00,
    ' ',0x00,
    'M',0x00,
    'S',0x00,
    'C',0x00,
    ' ',0x00,
    'D',0x00,
    'E',0x00,
    'V',0x00,
    'I',0x00,
    'C',0x00,
    'E',0x00,
};

USB_DESC_ATTRIBUTES uint8_t CyFxUSBSerNumString[] __attribute__ ((aligned (32))) =
{
    0x1A, 0x03,
    '0', 0x00,
    '1', 0x00,
    '2', 0x00,
    '3', 0x00,
    '0', 0x00,
    '1', 0x00,
    '2', 0x00,
    '3', 0x00,
    '0', 0x00,
    '1', 0x00,
    '2', 0x00,
    '3', 0x00
};

/* Standard full speed configuration descriptor */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBFSConfigDscr[] __attribute__ ((aligned (4))) =
{
    /* Configuration descriptor */
    0x09,                           /* Descriptor size */
    0x02,                           /* Configuration descriptor type */
    0x20,0x00,                      /* Total length: 32 bytes (config + interface + 2 endpoints) */
    0x01,                           /* Number of interfaces */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - bus powered */
    0x32,                           /* Max power consumption: 100mA */

    /* Interface descriptor, alt setting 0, bulk transfer */
    0x09,                            /* Descriptor size */
    0x04,                           /* Interface descriptor type */
    0x00,                           /* Interface number */
    0x00,                           /* Alternate setting number */
    0x02,                           /* Number of endpoints */
    0x08,                           /* Interface class: Mass Storage */
    0x06,                           /* Interface sub class: SCSI */
    0x50,                           /* Interface protocol: Bulk-Only Transport */
    0x01,                           /* Interface descriptor string index */

    /* Endpoint Descriptor for BULK IN 1 */
    0x07,                           /* Descriptor size */
    0x05,                           /* Endpoint descriptor type */
    0x81,                           /* Endpoint address: IN endpoint 1 */
    0x02,                           /* Attributes: BULK */
    0x40, 0x00,                     /* Max packet size: 64 bytes */
    0x00,                           /* Interval */

    /* Endpoint Descriptor for BULK OUT 1 */
    0x07,                           /* Descriptor size */
    0x05,                           /* Endpoint descriptor type */
    0x01,                           /* Endpoint address: OUT endpoint 1 */
    0x02,                           /* Attributes: BULK */
    0x40, 0x00,                     /* Max packet size: 64 bytes */
    0x00                            /* Interval */
};

/* Standard high speed configuration descriptor */
USB_DESC_ATTRIBUTES uint8_t CyFxUSBHSConfigDscr[] __attribute__ ((aligned (4))) =
{
    0x09,                           /* Descriptor size: 9 (config) + 9 (interface) + 7 (EP OUT) + 7 (EP IN) = 32 bytes */
    0x02,                           /* Configuration descriptor type */
    0x20,0x00,                      /* Length of this descriptor and all sub descriptors */
    0x01,                           /* Number of interfaces */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - bus powered */
    0x32,                           /* Max power consumption: 100mA */

    /* Interface descriptor, alt setting 0, bulk transfer */
    0x09,                            /* Descriptor size */
    0x04,                           /* Interface descriptor type */
    0x00,                           /* Interface number */
    0x00,                           /* Alternate setting number */
    0x02,                           /* Number of endpoints */
    0x08,                           /* Interface class: Mass Storage */
    0x06,                           /* Interface sub class: SCSI */
    0x50,                           /* Interface protocol: Bulk-Only Transport */
    0x01,                           /* Interface descriptor string index */

    /* Endpoint Descriptor for BULK IN 1 */
    0x07,                           /* Descriptor size */
    0x05,                           /* Endpoint descriptor type */
    0x81,                           /* Endpoint address: IN endpoint 1 */
    0x02,                           /* Attributes: BULK */
    0x00, 0x02,                     /* Max packet size: 512 bytes (HS bulk requirement) */
    0x00,                           /* Interval */

    /* Endpoint Descriptor for BULK OUT 1 */
    0x07,                           /* Descriptor size */
    0x05,                           /* Endpoint descriptor type */
    0x01,                           /* Endpoint address: OUT endpoint 1 */
    0x02,                           /* Attributes: BULK */
    0x00, 0x02,                     /* Max packet size: 512 bytes (HS bulk requirement) */
    0x00                            /* Interval */
};

/***************************************************************************//**
* \file spi.h
* \version 1.0
*
* \details Header file with SMIF application constants and the function definitions
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

#ifndef _SPI_H_
#define _SPI_H_

#include "cy_pdl.h"
#include "cy_debug.h"
#include "usb_app.h"

/* Macros */
#define SMIF_HW                           (SMIF0)
#define CY_SYSCLK_SPI_CLK_HF1             (1)

#define SMIF_CLK_HSIOM                    (P6_0_SMIF_SPI_CLK)
#define SMIF_CLK_PORT                     (P6_0_PORT)
#define SMIF_CLK_PIN                      (P6_0_PIN)

#define SMIF_SELECT0_HSIOM                (P6_1_SMIF_SPI_SELECT0)
#define SMIF_SELECT0_PORT                 (P6_1_PORT)
#define SMIF_SELECT0_PIN                  (P6_1_PIN)

#define SMIF_SELECT1_HSIOM                (P6_2_SMIF_SPI_SELECT1)
#define SMIF_SELECT1_PORT                 (P6_2_PORT)
#define SMIF_SELECT1_PIN                  (P6_2_PIN)

#define SMIF_DATA0_HSIOM                  (P7_0_SMIF_SPI_DATA0)
#define SMIF_DATA0_PORT                   (P7_0_PORT)
#define SMIF_DATA0_PIN                    (P7_0_PIN)

#define SMIF_DATA1_HSIOM                  (P7_1_SMIF_SPI_DATA1)
#define SMIF_DATA1_PORT                   (P7_1_PORT)
#define SMIF_DATA1_PIN                    (P7_1_PIN)

#define SMIF_DATA2_HSIOM                  (P7_2_SMIF_SPI_DATA2)
#define SMIF_DATA2_PORT                   (P7_2_PORT)
#define SMIF_DATA2_PIN                    (P7_2_PIN)

#define SMIF_DATA3_HSIOM                  (P7_3_SMIF_SPI_DATA3)
#define SMIF_DATA3_PORT                   (P7_3_PORT)
#define SMIF_DATA3_PIN                    (P7_3_PIN)

#define SMIF_DATA4_HSIOM                  (P7_4_SMIF_SPI_DATA4)
#define SMIF_DATA4_PORT                   (P7_4_PORT)
#define SMIF_DATA4_PIN                    (P7_4_PIN)

#define SMIF_DATA5_HSIOM                  (P7_5_SMIF_SPI_DATA5)
#define SMIF_DATA5_PORT                   (P7_5_PORT)
#define SMIF_DATA5_PIN                    (P7_5_PIN)

#define SMIF_DATA6_HSIOM                  (P7_6_SMIF_SPI_DATA6)
#define SMIF_DATA6_PORT                   (P7_6_PORT)
#define SMIF_DATA6_PIN                    (P7_6_PIN)

#define SMIF_DATA7_HSIOM                  (P7_7_SMIF_SPI_DATA7)
#define SMIF_DATA7_PORT                   (P7_7_PORT)
#define SMIF_DATA7_PIN                    (P7_7_PIN)

#define CY_SPI_STATUS_READ_CMD            (0x05)
#define CY_SPI_SECTOR_ERASE_CMD           (0xD8)
#define CY_SPI_HYBRID_SECTOR_ERASE_CMD    (0x20)
#define CY_SPI_PROGRAM_CMD_QUAD           (0x32)
#define CY_SPI_PROGRAM_CMD                (0x02)
#define CY_SPI_WRITE_ENABLE_CMD           (0x06)
#define CY_SPI_READ_CMD                   (0x03)
#define CY_QSPI_READ_CMD                  (0x6B)
#define SPI_ADDRESS_BYTE_COUNT            (3)
#define CY_SPI_RESET_ENABLE_CMD           (0x66)
#define CY_SPI_SW_RESET_CMD               (0x99)
#define CY_SPI_WRITE_ENABLE_LATCH_MASK    (0x02)
#define CY_SPI_WIP_MASK                   (0x01)
#define CY_SPI_WIP_STATUS                 (0x01)

#define CY_SPI_READ_ID_CMD                (0x9F)
#define CY_FLASH_ID_LENGTH                (0x04)
#define CY_APP_SPI_FLASH_ERASE_SIZE       (0x10000)
#define CY_SPI_FLASH_PAGE_SIZE            (0x100)
#define CY_SPI_PROGRAM_TIMEOUT_US         (650000)
#define MAX_BUFFER_SIZE                   (2048u)   /**< Max size for read and write buffers */

#define CY_CFI_DEVICE_SIZE_OFFSET          (0x27)
#define CY_CFI_ERASE_NUM_SECTORS_OFFSET    (0x2D)
#define CY_CFI_ERASE_REGION_SIZE_INFO_SIZE (0x04)
#define CY_CFI_ERASE_SECTOR_SIZE_OFFSET    (0x2F)
#define CY_CFI_MAX_SIZE_NUM_ERASE_SECTORS  (0xFF)
#define CY_CFI_NUM_ERASE_REGION_OFFSET     (0x2C)
#define CY_CFI_TABLE_LENGTH                (0x56)

#define CY_APP_QSPI_CONFIG_REG_READ_CMD    (0x35)
#define CY_APP_QSPI_WRITE_REGISTER_CMD     (0x01)
#define CY_APP_QSPI_STATUS_1_READ_CMD      (0x05)

#define SECTOR_LENGTH                      0x10000
#define SECTOR_START_FOR_GIVEN_ADDR_MASK   (~(SECTOR_LENGTH - 1))
#define QSPI_FLASH_DEVICE_MEMORY_OFFSET    0x20000

/* Enums */
/* Vendor commands sent by USB Host application (eg: EZ-USB FX Control Center) */
typedef enum cy_en_flashProgrammerVendorCmd_t
{
    FLASH_CMD_CHECK_SPI_SUPPORT     = 0xB0,
    FLASH_CMD_CHECK_STATUS          = 0xB5,
    FLASH_CMD_FLASH_WRITE           = 0xB2,
    FLASH_CMD_FLASH_READ            = 0xB3,
    FLASH_CMD_FLASH_SECTOR_ERASE    = 0xB4,
    FLASH_CMD_FLASH_GET_ID          = 0xB1,
    FLASH_CMD_BULK_READ             = 0xB6,
    FLASH_CMD_BULK_WRITE            = 0xB7
}cy_en_flashProgrammerVendorCmd_t;

typedef enum cy_en_flash_index_t
{
  SPI_FLASH_0    = 0,
  SPI_FLASH_1    = 1,
  DUAL_SPI_FLASH = 2,
  NUM_SPI_FLASH,
}cy_en_flash_index_t;

/* Structs */
typedef struct cy_stc_cfi_erase_block_info_t
{
  uint32_t numSectors;
  uint32_t sectorSize;
  uint32_t startingAddress;
  uint32_t lastAddress;
} cy_stc_cfi_erase_block_info_t ;

typedef struct cy_stc_cfi_flash_map_t
{
  uint32_t deviceSizeFactor; /*0x27h*/
  uint32_t deviceSize;
  uint32_t numEraseRegions;    /*0x2c*/
  cy_stc_cfi_erase_block_info_t memoryLayout[CY_CFI_MAX_SIZE_NUM_ERASE_SECTORS]; /* 0x2D onwards*/
  uint32_t num4KBParameterRegions;
}cy_stc_cfi_flash_map_t;

/* Extern Variables */
extern cy_stc_smif_context_t spiContext;

/* Function Prototypes */
/**
 * \name Cy_SPI_Start
 * \brief Function to enable SPI block 
 * \param pAppCtxt
 * \param flashIndex
 * \retval status
 */
cy_en_smif_status_t Cy_SPI_Start(cy_stc_usb_app_ctxt_t *pAppCtxt, cy_en_flash_index_t flashIndex);

/**
 * \name Cy_SPI_Stop
 * \brief Function to stop the SPI block
 * \param pAppCtxt application layer context pointer
 * \param flashIndex flash index
 * \retval status
 */
cy_en_smif_status_t Cy_SPI_Stop(void);

/**
 * \name Cy_SPI_ReadID
 * \brief Function to read device ID
 * \param rxBuffer
 * \param flashIndex
 * \retval status
 */
cy_en_smif_status_t Cy_SPI_ReadID(uint8_t *rxBuffer, cy_en_flash_index_t flashIndex);

/**
 * \name Cy_SPI_IsMemBusy
 * \brief Function to check busy status of flash
 * \param flashIndex
 * \retval boolean true when busy, false when not busy
 */
bool Cy_SPI_IsMemBusy(cy_en_flash_index_t flashIndex);

/**
 * \name Cy_SPI_WriteOperation
 * \brief Function to initiate flash write operation
 * \param address flash address
 * \param txBuffer Data buffer pointer
 * \param length Length of data to write to flash
 * \param numPages Number of Flash pages to write
 * \param flashIndex flash index
 * \retval status
 */
cy_en_smif_status_t Cy_SPI_WriteOperation(uint32_t address, uint8_t *txBuffer, uint32_t length, uint32_t numPages, cy_en_flash_index_t flashIndex);

/**
 * \name Cy_SPI_ReadOperation
 * \brief Function to initiate flash write operation
 * \param address flash address
 * \param rxBuffer Data buffer pointer
 * \param length Length of data to read from flash
 * \param flashIndex flash index
 * \return status
 */
cy_en_smif_status_t Cy_SPI_ReadOperation(uint32_t address, uint8_t *rxBuffer, uint32_t length, cy_en_flash_index_t flashIndex);

/**
 * \name Cy_SPI_SectorErase
 * \brief Function to erase flash sector 
 * \param flashIndex flash index
 * \param address flash address
 * \retval status
 */
cy_en_smif_status_t Cy_SPI_SectorErase(cy_en_flash_index_t flashIndex, uint32_t address);

/**
 * \name Cy_SPI_FlashInit
 * \brief Function to initialize SPI Flash
 * \details Quad Mode - Data in x4 mode, Command in x1 mode
 *          QPI Mode - Data in x4 mode, Command in x4 mode
 *
 *          QPI enabled implies Quad enable.
 *
 *          Enable only Quad mode when writes to flash can be in x1 mode and only reads need to be in x4 mode (eg: Passive x4 mode with one x4 flash memory)
 *          Enable QPI mode when writes and reads should be in x4 mode (eg: Passive x8 mode with two x4 flash memories)
 *
 * \param flashIndex SPI Flash Index
 * \param quadEnable Quad Mode enable
 * \param qpiEnable QPI mode enable
 * \retval status
 */
cy_en_smif_status_t Cy_SPI_FlashInit (cy_en_flash_index_t flashIndex, bool quadEnable, bool qpiEnable);

/**
 * \name Cy_App_ReadConfigRegister
 * \brief Get the operational configuration of a SPI flash device
 * \param slaveSelect Choose the slave flash device
 * \retval 8-bit value from the flash device's config register (CR1)
 */
uint8_t Cy_App_ReadConfigRegister(cy_en_smif_slave_select_t slaveSelect);

/**
 * \name Cy_App_QSPIStatus1Read
 * \brief Read the status register of a SPI flash device
 * \param slaveSelect Choose the slave flash device
 * \retval 8-bit value from the flash device's config register (CR1)
 */
uint8_t Cy_App_QSPIStatus1Read(cy_en_smif_slave_select_t slaveSelect);

/**
 * \name Cy_App_WriteConfigurationRegister
 * \brief Set the operational configuration of a SPI flash device
 * \param slaveSelect Choose the slave flash device
 * \param value to write to the configuration register (CR1)
 * \retval None
 */
void Cy_App_WriteConfigurationRegister(cy_en_smif_slave_select_t slaveSelect, uint8_t value);

/**
 * \name Cy_QSPI_Start
 * \brief Function to enable SPI block 
 * \param pAppCtxt
 * \param flashIndex
 * \retval status
 */
cy_en_smif_status_t Cy_QSPI_Start(cy_stc_usb_app_ctxt_t *pAppCtxt, cy_en_flash_index_t flashIndex);

/**
 * \name Cy_SPI_WriteOperation
 * \brief Function to initiate flash write operation
 * \param address flash address
 * \param txBuffer Data buffer pointer
 * \param length Length of data to write to flash
 * \param numPages Number of Flash pages to write
 * \param flashIndex flash index
 * \retval status
 */
cy_en_smif_status_t Cy_QSPI_WriteOperation(uint32_t address, uint8_t *txBuffer, uint32_t length, cy_en_flash_index_t flashIndex);

/**
 * \name Cy_SPI_ReadOperation
 * \brief Function to initiate flash write operation
 * \param address flash address
 * \param rxBuffer Data buffer pointer
 * \param length Length of data to read from flash
 * \param flashIndex flash index
 * \return status
 */
cy_en_smif_status_t Cy_QSPI_ReadOperation(uint32_t address, uint8_t *rxBuffer, uint32_t length, cy_en_flash_index_t flashIndex);

#endif /* _SPI_H_ */

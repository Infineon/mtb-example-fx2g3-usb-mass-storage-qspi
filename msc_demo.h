/***************************************************************************//**
* \file msc_demo.h
*
* \brief Mass Storage Class QSPI application header
*
* \details This header file defines the interface for a Mass Storage Class (MSC) device.
*
*******************************************************************************/

#ifndef _MSC_DEMO_H_
#define _MSC_DEMO_H_

#include <stdint.h>
#include <stdbool.h>
#include "usb_app.h"

#if USE_RAM_STORAGE
/*
 * RAM storage mode Capacity: 32 KB
 */
#define MSC_DUT_CAPACITY_BYTES             (32 * 1024)
#else
/*
 * QSPI flash mode capacity: ~14.97 MB
 *
 * Since we are using 3-Byte addressing mode for SPI flash, the maximum
 * addressable space is 16MB. We reserve some space at the start to avoid
 * protected flash regions and hybrid sectors (1 MB).
 * This makes the usable capacity 15 MB. We also align the memory start
 * address such that FAT FS table ends exactly before the next closest
 * sector boundary, and actual file data starts at sector boundary. This
 * helps to simplify the write logic in `Cy_MSCD_Write` by skipping
 * read-modify-write cycles when writing full sectors.
 */
#define SECTOR_ALIGNMENT_OFFSET             (0x5000)
#define MSC_DUT_CAPACITY_BYTES              ((15 * 1024 * 1024) - SECTOR_ALIGNMENT_OFFSET)
#endif /* USE_RAM_STORAGE */

#define MSC_DUT_BLOCK_SIZE                  (512)                                           /**< Block size in bytes */
#define MSC_DUT_TOTAL_BLOCKS                (MSC_DUT_CAPACITY_BYTES / MSC_DUT_BLOCK_SIZE)   /**< Total number of logical blocks */

#if !USE_RAM_STORAGE
#include "spi.h"

/* QSPI flash device details for QSPI flash mode implementation */
#define MSC_SPI_BASE_ADDRESS                (0x100000 + SECTOR_ALIGNMENT_OFFSET)            /**< Base address in SPI flash - Start at 1MB to avoid protected regions */
#define MSC_SPI_TEMP_BUFFER_SIZE            (16384)                                         /**< Size of buffer that holds data read from the QSPI flash device */
#define MSC_SPI_SECTOR_SIZE                 (SECTOR_LENGTH)                                 /**< Erase block size or sector size */
#define MSC_SPI_PAGE_SIZE                   (CY_SPI_FLASH_PAGE_SIZE)                        /**< Page size for flash programming */
#endif /* !USE_RAM_STORAGE */

/* Logging function macros */
#define LOG_MSC_ERROR(msg, ...) \
{ \
    Cy_Debug_AddToLog(2, "[MSC][ERR] "msg"%s", ##__VA_ARGS__, "\r\n"); \
}

#define LOG_MSC_INFO(msg, ...) \
{ \
    Cy_Debug_AddToLog(3, "[MSC][INFO] "msg"%s", ##__VA_ARGS__, "\r\n"); \
}

#define LOG_MSC_TRACE(msg, ...) \
{ \
    Cy_Debug_AddToLog(4, "[MSC][T] "msg"%s", ##__VA_ARGS__, "\r\n"); \
}

/* Function Prototypes */
#if !USE_RAM_STORAGE
/**
 * \brief Checks and logs flash protection status for debugging.
 * \details This function helps identify if write operations are targeting 
 * protected flash regions that could cause USB resets during format operations.
 * \retval true if flash status check is successful, false otherwise.
 */
bool Cy_USB_MSC_CheckFlashProtection(void);
#endif /* !USE_RAM_STORAGE */

#if !USE_RAM_STORAGE
/**
 * \brief Initializes the storage device.
 * \retval true if successful, false otherwise.
 */
bool Cy_USB_MSC_Init(void);
#else
/**
 * \brief Initializes the storage medium in RAM mode.
 * \details Initialize the array to simulate FAT FS table and a demo file "README.txt"
 * \retval true if successful, false otherwise.
 */
bool Cy_USB_MSC_Init(void);
#endif

/**
 * \brief Reads data from storage.
 * \param lba The starting logical block address to read from.
 * \param blocks The number of blocks to read.
 * \param buffer A pointer to the buffer where the read data will be stored.
 * \retval true if successfully read data into buffer, false otherwise.
 */
bool Cy_USB_MSC_ReadBlocks(uint32_t lba, uint16_t blocks, uint8_t *buffer);

/**
 * \brief Writes data to storage.
 * \param lba The starting logical block address to write to.
 * \param blocks The number of blocks to write.
 * \param buffer Buffer containing the data to be written.
 * \retval true if successfully wrote data into storage, false otherwise.
 */
bool Cy_USB_MSC_WriteBlocks(uint32_t lba, uint16_t blocks, const uint8_t *buffer);

/**
 * \brief Set the capacity of the device.
 * \param totalBlocks Pointer to store the total number of blocks.
 * \param blockSize Pointer to store the size of each block in bytes.
 * \retval true on setting capacity values, false if provided capacity paramters are NULL.
 */
bool Cy_USB_MSC_GetCapacity(uint32_t *totalBlocks, uint32_t *blockSize);

/**
 * \brief Check if device is ready for operations
 * \note This implementation always returns true as the device is always ready.
 * \retval true if ready, false otherwise.
 */
bool Cy_USB_MSC_IsReady(void);

/**
 * \brief Gets the current write-protection status of the storage.
 * \details This is used to respond to SCSI commands like MODE SENSE.
 * \retval true if the device is write-protected, false otherwise.
 */
bool Cy_USB_MSC_IsWriteProtected(void);

/**
 * \brief Sets the write-protection status.
 * \param writeProtected true to enable write-protection, false to disable.
 * \retval None
 */
void Cy_USB_MSC_SetWriteProtection(bool writeProtected);


#if USE_RAM_STORAGE
/**
 * \brief Gets a direct pointer to a location within the storage array.
 * \details This is an optimization for the READ10 command handler to avoid an
 * intermediate buffer copy. The DMA can be pointed directly to this memory location.
 * \param lba The Logical Block Address (LBA) for which to get a pointer.
 * \retval A valid pointer to the start of the LBA's data, or NULL if the LBA is out of bounds.
 */
uint8_t* Cy_USB_MSC_GetStoragePtr(uint32_t lba);
#else
/**
 * \brief Provides the pointer to a buffer containing data read from the QSPI flash device.
 * \param lba The Logical Block Address (LBA) for which to get a pointer.
 * \param dataSize Size of data to read into the buffer.
 * \retval A valid pointer to the start of the LBA's data, or NULL if the LBA is out of bounds.
 */
uint8_t* Cy_USB_MSC_GetStoragePtr(uint32_t lba, uint32_t dataSize);
#endif /* USE_RAM_STORAGE */

#if !USE_RAM_STORAGE
/**
 * \name Cy_MSCD_Read
 * \brief Reads data from the SPI flash.
 * \param address Flash address offset to read from
 * \param rxBuffer Buffer to store the read data
 * \param length Length of data to read
 * \return Status of the read operation
 */
cy_en_smif_status_t Cy_MSCD_Read(
    uint32_t address,
    uint8_t *rxBuffer,
    uint32_t length
);
#endif /* !USE_RAM_STORAGE */

#if !USE_RAM_STORAGE
/**
 * \name Cy_MSCD_Write
 * \brief Writes data to the SPI flash using a sector-based approach.
 * \param address Flash address offset to write to
 * \param txBuffer Buffer containing data to be written
 * \param length Length of data to write
 * \return Status of the write operation
 */
cy_en_smif_status_t Cy_MSCD_Write(
    uint32_t address,
    uint8_t *txBuffer,
    uint32_t length
);
#endif /* !USE_RAM_STORAGE */

#endif /* _MSC_DEMO_H_ */

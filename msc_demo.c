/***************************************************************************//**
* \file msc_demo.c
* \version 1.0
*
* \brief Mass Storage Class QSPI application implementation
*
* \details This file implements a storage device interface that can operate
* with either the QSPI flash device or a RAM buffer as the storage medium.
*
*******************************************************************************/

#include "msc_demo.h"
#include "cy_debug.h"
#include "usb_app.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#if !USE_RAM_STORAGE
#include "spi.h"
#endif /* !USE_RAM_STORAGE */

#if !USE_RAM_STORAGE
static bool mscInitialized = false;                                                     /* Status variable to avoid re-initialization of QSPI flash device. */
static uint8_t mscTempBuffer[MSC_SPI_TEMP_BUFFER_SIZE] __attribute__((aligned(32)));    /* A temporary buffer to hold data read from the QSPI flash device. */
#endif /* !USE_RAM_STORAGE */

#if !USE_RAM_STORAGE
HBDMA_BUF_ATTRIBUTES uint8_t* MSC_SPI_WriteBuffer;                                      /* Buffer allocated from HB DMA RAM for QSPI write operations. */
HBDMA_BUF_ATTRIBUTES uint8_t* MSC_SPI_ReadBuffer;                                       /* Buffer allocated from HB DMA RAM for QSPI read operations. */
#endif /* !USE_RAM_STORAGE */

/* Device state variables */
static bool mscWriteProtected = false;                                                  /* Storage medium write-protection status */
static bool mscDeviceReady = true;                                                      /* Device readiness status (in this implementation, device is always ready) */

#if !USE_RAM_STORAGE
/**
 * \brief Checks and logs flash protection status for debugging.
 * \retval true if successful, false otherwise.
 */
bool Cy_USB_MSC_CheckFlashProtection(void)
{
    uint8_t statusReg = 0;
    
    /* Read flash status register to check write protection */
    statusReg = Cy_App_QSPIStatus1Read(CY_SMIF_SLAVE_SELECT_0);
    
    LOG_MSC_INFO("Flash Status Register: 0x%02X", statusReg);
    
    /* Check common protection bits */
    if (statusReg & 0x80) {
        LOG_MSC_INFO("Info:MSC:Status Register Protection (SRP) enabled");
    }
    if (statusReg & 0x1C) {
        LOG_MSC_INFO("Block Protection bits set: 0x%02X - Some sectors protected", 
                          (statusReg & 0x1C) >> 2);
    }
    if (statusReg & 0x02) {
        LOG_MSC_INFO("Write Enable Latch (WEL) status: %s", 
                          (statusReg & 0x02) ? "Enabled" : "Disabled");
    }
    if (statusReg & 0x01) {
        LOG_MSC_INFO("Write In Progress (WIP): %s", 
                          (statusReg & 0x01) ? "Busy" : "Ready");
    }
    
    /* Log recommended safe region */
    LOG_MSC_INFO("Safe write region: 0x%06X to 0x%06X (15MB)", 
                      MSC_SPI_BASE_ADDRESS, 
                      MSC_SPI_BASE_ADDRESS + MSC_DUT_CAPACITY_BYTES - 1);
    return true;
}
#endif /* !USE_RAM_STORAGE */

#if !USE_RAM_STORAGE
/**
 * \brief Initializes the storage device.
 * \retval true if successful, false otherwise.
 */
bool Cy_USB_MSC_Init(void)
{
    /* RAW Block Device Mode */
    LOG_MSC_INFO("Initializing RAW block device");
    
    /* Check if already initialized to avoid repeated initialization */
    if (mscInitialized) {
        LOG_MSC_INFO("Already initialized, skipping");
        return true;
    }
    
    /* Ensure SPI buffers are allocated before initialization */
    if (MSC_SPI_WriteBuffer == NULL) {
        LOG_MSC_INFO("SPI buffers not allocated yet, deferring initialization");
        return false;
    }
    
    /* Check and log flash protection status for debugging */
    Cy_USB_MSC_CheckFlashProtection();
        
    #if FORCE_RAW_DEVICE_ERASE
    /* Erase first few sectors to ensure raw device (FS table would be erased) */
    LOG_MSC_INFO("Erasing first 10 sectors to ensure raw device state...");
    for(uint32_t sector = 0; sector < 10; sector++) {
        uint32_t eraseAddr = MSC_SPI_BASE_ADDRESS + (sector * MSC_SPI_SECTOR_SIZE);
        cy_en_smif_status_t eraseStatus = Cy_SPI_SectorErase(SPI_FLASH_0, eraseAddr);
        if (eraseStatus != CY_SMIF_SUCCESS) {
            LOG_MSC_ERROR("Warning - Failed to erase sector at 0x%08lX", eraseAddr);
        }
    }
    LOG_MSC_INFO("Raw device erase complete");
    LOG_MSC_INFO("RAW block device ready - available for host formatting");
#endif /* FORCE_RAW_DEVICE_ERASE */
    
    mscInitialized = true;
    return true;
}
#else

/* In-memory array to simulate the storage device (RAM mode). */
static uint8_t mscStorage[MSC_DUT_CAPACITY_BYTES] __attribute__((aligned(32)));

/**
 * \brief Initializes the storage medium in RAM mode.
 * \details Initialize the array to simulate FAT FS table and a demo file "README.txt"
 * \retval true if successful, false otherwise.
 */
bool Cy_USB_MSC_Init(void)
{
    /* Zero out the entire storage array before populating it. */
    memset(mscStorage, 0, MSC_DUT_CAPACITY_BYTES);

    /*
     * LBA 0: Boot Sector with BIOS Parameter Block (BPB) for FAT12.
     * This structure is critical for the host OS to recognize the file system.
     */
    mscStorage[0x000] = 0xEB; /* JMP instruction */
    mscStorage[0x001] = 0x3C;
    mscStorage[0x002] = 0x90;
    memcpy(&mscStorage[0x003], "MSDOS5.0", 8);      /* OEM Name */
    mscStorage[0x00B] = (uint8_t)(MSC_DUT_BLOCK_SIZE & 0xFF); /* Bytes Per Sector (Low Byte) */
    mscStorage[0x00C] = (uint8_t)(MSC_DUT_BLOCK_SIZE >> 8);   /* Bytes Per Sector (High Byte) */
    mscStorage[0x00D] = 0x01; /* Sectors Per Cluster */
    mscStorage[0x00E] = 0x01; /* Reserved Sectors (low byte) */
    mscStorage[0x00F] = 0x00; /* Reserved Sectors (high byte) */
    mscStorage[0x010] = 0x02; /* Number of FATs */
    mscStorage[0x011] = 0x40; /* Root Entry Count (low byte): 64 */
    mscStorage[0x012] = 0x00; /* Root Entry Count (high byte) */
    mscStorage[0x013] = (uint8_t)(MSC_DUT_TOTAL_BLOCKS & 0xFF); /* Total Sectors (Low byte) */
    mscStorage[0x014] = (uint8_t)(MSC_DUT_TOTAL_BLOCKS >> 8);   /* Total Sectors (High byte) */
    mscStorage[0x015] = 0xF0; /* Media Descriptor (F0 for removable media) */
    mscStorage[0x016] = 0x02; /* Sectors Per FAT */
    mscStorage[0x017] = 0x00;

    mscStorage[0x018] = 0x20; /* Sectors Per Track (low byte) */
    mscStorage[0x019] = 0x00; /* Sectors Per Track (high byte) */
    mscStorage[0x01A] = 0x02; /* Number of Heads (low byte) */
    mscStorage[0x01B] = 0x00; /* Number of Heads (high byte) */
    mscStorage[0x01C] = 0x00; /* Hidden Sectors (byte 0) */
    mscStorage[0x01D] = 0x00; /* Hidden Sectors (byte 1) */
    mscStorage[0x01E] = 0x00; /* Hidden Sectors (byte 2) */
    mscStorage[0x01F] = 0x00; /* Hidden Sectors (byte 3) */
    mscStorage[0x020] = 0x00; /* Large Sectors (byte 0) */
    mscStorage[0x021] = 0x00; /* Large Sectors (byte 1) */
    mscStorage[0x022] = 0x00; /* Large Sectors (byte 2) */
    mscStorage[0x023] = 0x00; /* Large Sectors (byte 3) */
    mscStorage[0x024] = 0x80; /* Physical Drive Number */
    mscStorage[0x025] = 0x00; /* Reserved */
    mscStorage[0x026] = 0x29; /* Extended Boot Signature */
    mscStorage[0x027] = 0x87; /* Volume Serial Number (byte 0) */
    mscStorage[0x028] = 0x65; /* Volume Serial Number (byte 1) */
    mscStorage[0x029] = 0x43; /* Volume Serial Number (byte 2) */
    mscStorage[0x02A] = 0x21; /* Volume Serial Number (byte 3) */
    memcpy(&mscStorage[0x02B], "FX2G3-MSC  ", 11); /* Volume Label */
    memcpy(&mscStorage[0x036], "FAT12   ", 8); /* File System Type */
    mscStorage[0x1FE] = 0x55; /* Boot Signature (low byte) */
    mscStorage[0x1FF] = 0xAA; /* Boot Signature (high byte) */

    /*
     * LBA 1-2: File Allocation Table 1 (FAT1)
     * Defines the cluster chain for files.
     */
    mscStorage[0x200] = 0xF0; /* First byte echoes the media descriptor from the BPB */
    mscStorage[0x201] = 0xFF; /* Reserved cluster entry 0 (low byte) */
    mscStorage[0x202] = 0xFF; /* Reserved cluster entry 1 (high byte) */
    mscStorage[0x203] = 0xFF; /* End of Chain marker (low byte) for cluster 2 */
    mscStorage[0x204] = 0x0F; /* End of Chain marker (high nibble) for cluster 2 */

    /* LBA 3-4: File Allocation Table 2 (FAT2) -backup of FAT1 */
    memcpy(&mscStorage[0x600], &mscStorage[0x200], MSC_DUT_BLOCK_SIZE * 2);

    /*
     * LBA 5-8: Root Directory
     * Contains entries for files and the volume label.
     */
    /* Volume Label Entry */
    memcpy(&mscStorage[0xA00], "FX2G3-MSC  ", 11);

    mscStorage[0xA0B] = 0x08; /* Attribute: Volume Label */
    mscStorage[0xA0B] = 0x08; /* Attribute Byte: Volume Label */
    mscStorage[0xA0C] = 0x00; /* reserved */
    mscStorage[0xA0D] = 0x00; /* creation time (low byte) */
    mscStorage[0xA0E] = 0x00; /* creation time (high byte) */
    mscStorage[0xA0F] = 0x00; /* creation date (low byte) */
    mscStorage[0xA10] = 0x00; /* creation date (high byte) */
    mscStorage[0xA11] = 0x00; /* last access date (low byte) */
    mscStorage[0xA12] = 0x00; /* last access date (high byte) */
    mscStorage[0xA13] = 0x00; /* high cluster */
    mscStorage[0xA14] = 0x00; /* last write time (low byte) */
    mscStorage[0xA15] = 0x00; /* last write time (high byte) */
    mscStorage[0xA16] = 0x00; /* last write date (low byte) */
    mscStorage[0xA17] = 0x00; /* last write date (high byte) */
    mscStorage[0xA18] = 0x00; /* first cluster (low byte) */
    mscStorage[0xA19] = 0x00; /* first cluster (high byte) */
    mscStorage[0xA1A] = 0x00; /* file size (byte 0) */
    mscStorage[0xA1B] = 0x00; /* file size (byte 1) */
    mscStorage[0xA1C] = 0x00; /* file size (byte 2) */
    mscStorage[0xA1D] = 0x00; /* file size (byte 3) */


    /* README.TXT File Directory Entry (8.3 format, all fields set) */
    memset(&mscStorage[0xA20], 0, 32); /* Zero the 32-byte entry */
    memcpy(&mscStorage[0xA20], "README  ", 8); /* 8 chars for name (first byte is 'R'=0x52, not 0x00) */
    memcpy(&mscStorage[0xA28], "txt", 3);    /* 3 chars for extension */
    mscStorage[0xA2B] = 0x20; /* Attribute Byte: Archive (normal file) */
    mscStorage[0xA2C] = 0x00; /* reserved */

    mscStorage[0xA2D] = 0x00; /* Creation time (tenths of sec) */
    /* Set creation, last access and last write date/time to for example: 2026-07-26 12:00:00 (DOS format) */
    /* DOS time: bits 15-11 hour (12), 10-5 min (0), 4-0 sec/2 (0) */
    /* DOS date: bits 15-9 year-1980 (45), 8-5 month (7), 4-0 day (26) */
    mscStorage[0xA2E] = 0x00; /* Creation time (high byte): 0x6000 (12:00:00) */
    mscStorage[0xA2F] = 0x60; /* Creation time (low byte)*/
    mscStorage[0xA30] = 0xDA; /* Creation date (high byte): 0x25DA (2026-07-26) */
    mscStorage[0xA31] = 0x25; /* Creation date (low byte) */
    mscStorage[0xA32] = 0xDA; /* Last access date(high byte): 0x25DA */
    mscStorage[0xA33] = 0x25; /* Last access date(low byte) */
    mscStorage[0xA34] = 0x00; /* High word of first cluster */
    mscStorage[0xA35] = 0x00; /* Low word of first cluster */
    mscStorage[0xA36] = 0x00; /* Last write time(high byte): 0x6000 (12:00:00) */
    mscStorage[0xA37] = 0x60; /* Last write time(low byte)*/
    mscStorage[0xA38] = 0xDA; /* Last write date(high byte): 0x25DA (2026-07-26) */
    mscStorage[0xA39] = 0x25; /* Last write date(low byte): 0x25DA */
    mscStorage[0xA3A] = 0x02; /* First cluster (Cluster 2) (high byte) */
    mscStorage[0xA3B] = 0x00; /* First cluster (Cluster 2) (low byte) */

    /* File content and size */
    const char *readmeContent;
    readmeContent = "This is a USB MSC compliant device\r\n";

    uint16_t readmeLen = (uint16_t)strlen(readmeContent);
    mscStorage[0xA3C] = (uint8_t)(readmeLen & 0xFF);         /* File Size (Low) */
    mscStorage[0xA3D] = (uint8_t)((readmeLen >> 8) & 0xFF);  /* File Size (High) */
    mscStorage[0xA3E] = 0x00;                                /* File Size (Upper 2 bytes, always 0 for small files) */
    mscStorage[0xA3F] = 0x00;

    /* Set data area for cluster 2 (LBA 9, offset 0x1200, size 512 bytes) */
    memset(&mscStorage[0x1200], 0, MSC_DUT_BLOCK_SIZE);

    /* Write README.txt content to data area */
    memcpy(&mscStorage[0x1200], readmeContent, readmeLen);

    LOG_MSC_INFO("Virtual storage initialized with FAT12 file system");

    /* Brief FAT entry verification. */
    uint16_t fatEntry = mscStorage[0x203] | ((mscStorage[0x204] & 0x0F) << 8);
    LOG_MSC_INFO("FAT12 entry for cluster 2: 0x%03X", fatEntry);

    return true;
}
#endif

/**
 * \brief Reads data from storage.
 * \param lba The starting logical block address to read from.
 * \param blocks The number of blocks to read.
 * \param buffer A pointer to the buffer where the read data will be stored.
 * \retval true if successfully read data into buffer, false otherwise.
 */
bool Cy_USB_MSC_ReadBlocks(uint32_t lba, uint16_t blocks, uint8_t *buffer)
{
    /* Verify device ready status */
    if (!mscDeviceReady)
    {
        LOG_MSC_ERROR("Read failed, device not ready.");
        return false;
    }

    /* Ensure that the buffer has a valid address */
    if (buffer == NULL)
    {
        LOG_MSC_ERROR("Read failed, null buffer");
        return false;
    }

    /* Identify the parameters requried for reading */
    const uint32_t startOffset = lba * MSC_DUT_BLOCK_SIZE;
    const uint32_t totalBytes = blocks * MSC_DUT_BLOCK_SIZE;

    /* Ensure that the requested read lies within the memory range of the device */
    if ((startOffset + totalBytes) > MSC_DUT_CAPACITY_BYTES) 
    {
        LOG_MSC_ERROR("Read out of bounds (LBA: %u, Blocks: %u)", lba, blocks);
        return false;
    }

#if USE_RAM_STORAGE
    /* Perform the read by loading the requested data into the buffer */
    memcpy(buffer, &mscStorage[startOffset], totalBytes);
    LOG_MSC_INFO("[MSC-RAM] Read %u blocks from LBA %u", blocks, lba);

#else

    /* Perform the read by loading the requested data from the SPI flash device into the buffer */
    cy_en_smif_status_t readStatus = Cy_MSCD_Read(MSC_SPI_BASE_ADDRESS + startOffset, buffer, totalBytes);
    if (readStatus != CY_SMIF_SUCCESS) {
        LOG_MSC_ERROR("SPI flash read failed (LBA: %u, Status: 0x%x)", lba, readStatus);
        return false;
    }

    LOG_MSC_TRACE("[RAW] Read %u blocks from LBA %u (SPI offset: 0x%08lX)",
        blocks, lba, (unsigned long)(MSC_SPI_BASE_ADDRESS + startOffset));

#endif /* USE_RAM_STORAGE */

    return true;
}

/**
 * \brief Writes data to storage.
 * \param lba The starting logical block address to write to.
 * \param blocks The number of blocks to write.
 * \param buffer Buffer containing the data to be written.
 * \retval true if successfully wrote data into storage, false otherwise.
 */
bool Cy_USB_MSC_WriteBlocks(uint32_t lba, uint16_t blocks, const uint8_t *buffer)
{
    
    /* Verify device ready status */
    if (!mscDeviceReady)
    {
        LOG_MSC_ERROR("Read failed, device not ready.");
        return false;
    }

    /* Ensure that the buffer has a valid address */
    if (buffer == NULL)
    {
        LOG_MSC_ERROR("Read failed, null buffer");
        return false;
    }
    
    /* Ensure that the storage is not write protected */
    if (mscWriteProtected) 
    {
        LOG_MSC_ERROR("Write failed, device is write-protected.");
        return false;
    }

    /* Identify the parameters requried for writing */
    const uint32_t startOffset = lba * MSC_DUT_BLOCK_SIZE;
    const uint32_t totalBytes = blocks * MSC_DUT_BLOCK_SIZE;
    
    /* Ensure that the requested write lies within the memory range of the device */
    if ((startOffset + totalBytes) > MSC_DUT_CAPACITY_BYTES) 
    {
        LOG_MSC_ERROR("Write out of bounds (LBA: %u, Blocks: %u, Size: %u, Max: %u)",
            lba, blocks, (startOffset + totalBytes), MSC_DUT_CAPACITY_BYTES);
        return false;
    }

#if USE_RAM_STORAGE
    /* Perform a write to the storage */
    memcpy(&mscStorage[startOffset], buffer, totalBytes);
    LOG_MSC_INFO("[MSC-RAM] Wrote %u blocks to LBA %u", blocks, lba);
#else

    /* Perform the write by sending the data from the buffer to the SPI flash device */
    cy_en_smif_status_t writeStatus = Cy_MSCD_Write(MSC_SPI_BASE_ADDRESS + startOffset, (uint8_t *)buffer, totalBytes);
    if (writeStatus != CY_SMIF_SUCCESS) {
        LOG_MSC_ERROR("SPI flash write failed (LBA: %u, Status: 0x%x)", lba, writeStatus);
        return false;
    }
#endif /* USE_RAM_STORAGE */

    return true;
}

/**
 * \brief Provide the memory capacity of the device.
 * \param totalBlocks Pointer to store the total number of blocks.
 * \param blockSize Pointer to store the size of each block in bytes.
 * \retval true on setting capacity values, false if provided capacity paramters are NULL.
 */
bool Cy_USB_MSC_GetCapacity(uint32_t *totalBlocks, uint32_t *blockSize)
{
    if (totalBlocks == NULL || blockSize == NULL) 
    {
        return false;
    }
    *totalBlocks = MSC_DUT_TOTAL_BLOCKS;
    *blockSize = MSC_DUT_BLOCK_SIZE;
    return true;
}

/**
 * \brief Check if device is ready for operations
 * \note This implementation always returns true as the device is always ready.
 * \retval true if ready, false otherwise.
 */
bool Cy_USB_MSC_IsReady(void)
{
    return mscDeviceReady;
}

/**
 * \brief Gets the current write-protection status of the storage.
 * \details This is used to respond to SCSI commands like MODE SENSE.
 * \retval true if the device is write-protected, false otherwise.
 */
bool Cy_USB_MSC_IsWriteProtected(void)
{
    return mscWriteProtected;
}

/**
 * \brief Sets the write-protection status.
 * \param writeProtected true to enable write-protection, false to disable.
 * \retval None
 */
void Cy_USB_MSC_SetWriteProtection(bool writeProtected)
{
    mscWriteProtected = writeProtected;
    LOG_MSC_INFO("Write protection %s.", writeProtected ? "enabled" : "disabled");
}

#if USE_RAM_STORAGE
/**
 * \brief Gets a direct pointer to a location within the storage array.
 * \details This is an optimization for the READ10 command handler to avoid an
 * intermediate buffer copy. The DMA can be pointed directly to this memory location.
 * \param lba The Logical Block Address (LBA) for which to get a pointer.
 * \retval A valid pointer to the start of the LBA's data, or NULL if the LBA is out of bounds.
 */
uint8_t* Cy_USB_MSC_GetStoragePtr(uint32_t lba){
    /* Identify the address offset */
    uint32_t offset = lba * MSC_DUT_BLOCK_SIZE;
    
    /* Ensure that the address requested lies within valid memory range */
    if ((offset + MSC_DUT_BLOCK_SIZE) <= MSC_DUT_CAPACITY_BYTES)
    {
        return &mscStorage[offset];
    }
    
    /* Return NULL if the requested LBA is out of bounds. */
    return NULL;
}
#else
/**
 * \brief Provides the pointer to a buffer containing data read from the QSPI flash device.
 * \param lba The Logical Block Address (LBA) for which to get a pointer.
 * \param dataSize Size of data to read into the buffer.
 * \retval A valid pointer to the start of the LBA's data, or NULL if the LBA is out of bounds.
 */
uint8_t* Cy_USB_MSC_GetStoragePtr(uint32_t lba, uint32_t dataSize) {

    /* Identify the address offset */
    uint32_t offset = lba * MSC_DUT_BLOCK_SIZE;

    /* Ensure that the address lies within the memory range of the SPI flash device */
    if ((offset + MSC_DUT_BLOCK_SIZE) <= MSC_DUT_CAPACITY_BYTES)
    {
        /* The target buffer is to which the data read from the flash device is stored in */
        uint8_t* targetBuffer = MSC_SPI_ReadBuffer;

        /* The following check is useful if targetBuffer is not the default from HBDMA RAM */
        if (targetBuffer == NULL) {
            LOG_MSC_INFO("Warning: Using static buffer for direct pointer access");
            targetBuffer = mscTempBuffer;
            /* Limit dataSize to temp buffer size */
            if (dataSize > MSC_SPI_TEMP_BUFFER_SIZE) {
                dataSize = MSC_SPI_TEMP_BUFFER_SIZE;
            }
        }
        
        /* Read from SPI flash device into the buffer */
        cy_en_smif_status_t readStatus = Cy_MSCD_Read(MSC_SPI_BASE_ADDRESS + offset, targetBuffer, dataSize);
        if (readStatus != CY_SMIF_SUCCESS) {
            LOG_MSC_ERROR("Direct storage pointer read failed (LBA: %u)", lba);
            return NULL;
        }
        
        LOG_MSC_TRACE("[RAW] Direct pointer access LBA %x (SPI offset: 0x%x)",
            lba, (unsigned long)(MSC_SPI_BASE_ADDRESS + offset));

        /* Return the pointer to the buffer with data read from SPI flash device */
        return targetBuffer;
    }

    /* Return NULL if the requested LBA is out of bounds. */
    return NULL;
}
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
){
    cy_en_smif_status_t status = CY_SMIF_SUCCESS;

    /* Ensure that the receive buffer is not NULL */
    if (rxBuffer == NULL) {
        LOG_MSC_ERROR("rxBuffer is NULL\r\n");
        return CY_SMIF_BAD_PARAM;
    }
    
    /* Ensure that the read length is a non-zero value */
    if (length == 0) {
        LOG_MSC_ERROR("read length is zero\r\n");
        return CY_SMIF_BAD_PARAM;
    }

    /* Read data from the QSPI flash device into the receive buffer */
    status = Cy_QSPI_ReadOperation(address, rxBuffer, length, SPI_FLASH_0);
    if(status != CY_SMIF_SUCCESS){
        DBG_APP_ERR("[SPI][ERROR] Cy_QSPI_ReadOperation failed at address 0x%x\r\n", address);
        return status;
    }

    LOG_MSC_INFO("[%s] Address: 0x%x, Length: %d\r\n", __func__, address, length);

    return status;
}
#endif /* USE_RAM_STORAGE */

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
){

    cy_en_smif_status_t status = CY_SMIF_SUCCESS;
    
    uint32_t initial_address = address;             /* Store the initial address for later logging */
    uint32_t initial_length = length;               /* Store the initial length for later logging */

    uint8_t *p_mscSpiBuffer = MSC_SPI_WriteBuffer;  /* A buffer to prepare the data for write */

    while(length > 0){

        /*
         * We have to load the data in the target sector into RAM
         * This needs to be performed because we can only erase entire sectors
         * and write data in pages.
         * However, if we want to rewrite the entire sector without preserving
         * its original contents, we can skip this read step.
         */
        if(address==(address&SECTOR_START_FOR_GIVEN_ADDR_MASK) && length>=SECTOR_LENGTH){
            LOG_MSC_TRACE("Skipping read for sector at address 0x%x\r\n", address);
            status = CY_SMIF_SUCCESS;
        }
        else{
            LOG_MSC_TRACE("Sector read at address 0x%x\r\n", address & SECTOR_START_FOR_GIVEN_ADDR_MASK);
            status = Cy_QSPI_ReadOperation(address & SECTOR_START_FOR_GIVEN_ADDR_MASK, p_mscSpiBuffer, SECTOR_LENGTH, SPI_FLASH_0);
        }
        if(status != CY_SMIF_SUCCESS){
            LOG_MSC_ERROR("[SPI][ERROR] Cy_QSPI_ReadOperation failed at address 0x%x\r\n", address);
            return status;
        }
        
        /*
         * We have txBuffer which contains the new data to be written
         * and p_mscSpiBuffer which contains the old data read from the device.
         * Now we calculate the offset from the start of p_mscSpiBuffer until the target write address
         *
         * Example case:
         * p_mscSpiBuffer   {data0, data1, data2, ..., data223,   data224,  data225,    ..., data255  }     <- old data read from device
         *                                             ^ offset from start of p_mscSpiBuffer
         * txBuffer         {^Retain old data here    }{newdata0, newdata1, newdata2,   ..., newdata32}     <- new data to be written
         */
        uint32_t offset = address - (address & SECTOR_START_FOR_GIVEN_ADDR_MASK);



        /* 
         * Now that we have the offset, we can update the p_mscSpiBuffer with the new data.
         * If the data spills over into the next sector, we will handle that in the next iteration
         * of the loop. This is handled by limiting the write length to the remaining space
         * in the current sector (write_len)
         */
        uint32_t write_len = (length > SECTOR_LENGTH-offset) ? (SECTOR_LENGTH-offset) : length;
        memcpy(p_mscSpiBuffer+offset, txBuffer, write_len);

        /*
         * Before we perform the write with the updated sector data, we have to erase the target sector
         */
        status = Cy_SPI_SectorErase(SPI_FLASH_0, (address & SECTOR_START_FOR_GIVEN_ADDR_MASK));
        if(status != CY_SMIF_SUCCESS){
            Cy_Debug_AddToLog(2, "[SPI][ERROR] Cy_SPI_SectorErase failed at address 0x%x\r\n[FAIL RAW]: 0x%x\r\n", (address & SECTOR_START_FOR_GIVEN_ADDR_MASK), address);
            return status;
        }

        /*
         * Once the sector is erased, we can program it with the updated contents
         */
        uint32_t iter_address = address & SECTOR_START_FOR_GIVEN_ADDR_MASK;
        uint32_t iter_length = SECTOR_LENGTH;
        uint8_t *iter_ramBuffer = p_mscSpiBuffer;
        while(iter_length>0){
            status = Cy_QSPI_WriteOperation(
                iter_address,
                iter_ramBuffer,
                iter_length%CY_SPI_FLASH_PAGE_SIZE==0?CY_SPI_FLASH_PAGE_SIZE:iter_length%CY_SPI_FLASH_PAGE_SIZE,
                SPI_FLASH_0
            );
            iter_length-=CY_SPI_FLASH_PAGE_SIZE;
            iter_address +=CY_SPI_FLASH_PAGE_SIZE;
            iter_ramBuffer += CY_SPI_FLASH_PAGE_SIZE;
        }

        length -= write_len;
        txBuffer += write_len;
        address += write_len;
        
        /* The loop iterates until all data is written (length==0) */
    }

    LOG_MSC_INFO("[%s] Address: 0x%x, Length: %d\r\n", __func__, initial_address, initial_length);
    
    return status;
}
#endif /* !USE_RAM_STORAGE */

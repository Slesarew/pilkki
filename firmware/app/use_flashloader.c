/**************************************************************************//**
 * @file use_flashloader.c
 * @brief Handles programming with help of a flashloader
 * @author Silicon Labs
 * @version 1.03
 ******************************************************************************
 * @section License
 * <b>(C) Copyright 2014 Silicon Labs, http://www.silabs.com</b>
 *******************************************************************************
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * DISCLAIMER OF WARRANTY/LIMITATION OF REMEDIES: Silicon Labs has no
 * obligation to support this Software. Silicon Labs is providing the
 * Software "AS IS", with no express or implied warranties of any kind,
 * including, but not limited to, any implied warranties of merchantability
 * or fitness for any particular purpose or warranties against infringement
 * of any proprietary rights of a third party.
 *
 * Silicon Labs will not be liable for any consequential, incidental, or
 * special damages, or any other relief, or for any claim by any third party,
 * arising from your use of this Software.
 *
 ******************************************************************************/
#include <stdbool.h>
#include <stdio.h>
#include "dap.h"
#include "flashloader.h"
#include "utils.h"
#include "use_flashloader.h"
#include "errors.h"
#include "delay.h"

#include "efm32.h"
#include "payload.h"




/* Initializes a global flashLoaderState object 
 * which is placed at the same location in memory 
 * as in the flashloader itself. Using this
 * we can easily get the address of each of the fields */

flashLoaderState_TypeDef *flState = (flashLoaderState_TypeDef *)STATE_LOCATION;
extern uint32_t ErrorFlag;

bool LoadLoader()
{
	haltTarget();
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
    uploadFlashloader((uint32_t *)payload, (uint32_t)payload_size);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	if (!waitForFlashloader()) return false;
	
	verifyFlashloaderReady();
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	return true;
}

/**********************************************************
 * Uploads and runs the flashloader on the target
 * The flashloader is written directly to the start
 * of RAM. Then the PC and SP are loaded from the
 * flashloader image. 
 **********************************************************/
bool uploadFlashloader(uint32_t *flImage, uint32_t size)
{
	int w;
	uint32_t addr;
	uint32_t tarWrap;
	uint32_t numWords = size / 4;
	if (numWords * 4 < size) numWords++;
   
	resetAndHaltTarget();
	
	/* Get the TAR wrap-around period */
	tarWrap = getTarWrap();
  
	if (ErrorFlag != SWD_ERROR_OK) 
	{	
		uPrintf("Error %lu\r\n", ErrorFlag);
		return false;
	}
	
	uPrintf("Uploading flashloader\r\n");
  
	/* Enable autoincrement on TAR */
	writeAP(AP_CSW, AP_CSW_DEFAULT | AP_CSW_AUTO_INCREMENT);
	
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
    // Send zero region of 16x4 bytes
    for (w = 0; w < 16; w++)
    {
        addr = RAM_MEM_BASE + w * 4;
        if ((addr & tarWrap) == 0)
        {
            writeAP(AP_TAR, addr);
        }
        uint32_t zero = 0;
        writeAP(AP_DRW, zero);
        // #error "AAA"
        if (ErrorFlag != SWD_ERROR_OK)
            return false;
    }

//	bool InitAdr = true;
	
	for (w = 0; w < numWords; w++) 
	{

		/* Get address of current word */
		addr = RAM_MEM_BASE + w * 4 + 16 * 4;
//		if (InitAdr) 
//		{
//			InitAdr = false;
//			writeAP(AP_TAR, addr); 
//		}
		
		/* At the TAR wrap boundary we need to reinitialize TAR
		 * since the autoincrement wraps at these */
		if ((addr & tarWrap) == 0)
		{
			writeAP(AP_TAR, addr); 
		}

		writeAP(AP_DRW, *flImage++);
	
		if (ErrorFlag != SWD_ERROR_OK) return false;
	
	}
  
	writeAP(AP_CSW, AP_CSW_DEFAULT);
  
	uPrintf("Booting flashloader\r\n");


	
	
	writeAP(AP_TAR, 0xE000EDF4); //ADDR MASK

	writeDP(DP_SELECT, 0x00000010); //PAGE 0x10

	writeAP(0x02, 0x200000E5); //PC
	writeAP(AP_TAR, 0x0001000F);
	
	writeAP(0x02, 0x20008000);  //SP
	writeAP(AP_TAR, 0x00010011);
	
	writeAP(0x02, 0xF9000000); //xPSR
	writeAP(AP_TAR, 0x00010010);
	
	
	writeDP(DP_ABORT, 0x0000001E);
	writeDP(DP_SELECT, 0x00000000);

	

	/* Load SP (Reg 13) from flashloader image */
	writeCpuReg(13, readMem(RAM_MEM_BASE + 0x40));
	
	/* Load PC (Reg 15) from flashloader image */
	writeCpuReg(15, readMem(RAM_MEM_BASE + 0x44));

	runTarget();
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	return true;
}


/**********************************************************
 * Verifies that the flashloader is ready. Will throw an
 * exception if the flashloader is not ready within a
 * timeout. 
 **********************************************************/
void verifyFlashloaderReady(void)
{
	  uint32_t status;
	  
	  uint32_t timeout = FLASHLOADER_RETRY_COUNT;
	  
	  do {
	    status = readMem( (uint32_t)&(flState->flashLoaderStatus) );
	    timeout--;
	  } while ( status == FLASHLOADER_STATUS_NOT_READY  && timeout > 0 );
	      
	  if ( status == FLASHLOADER_STATUS_READY ) {
	    uPrintf("Flashloader ready\r\n");
	  } else {
	    printf("Flashloader not ready. Status: 0x%.8x\r\n", status);
	  }
}

/**********************************************************
 * Waits until the flashloader reports that it is ready
 **********************************************************/
bool waitForFlashloader(void)
{
	uint32_t status;
	int retry = FLASHLOADER_RETRY_COUNT;
	  
	/* Wait until flashloader has acknowledged the command */
	do {
		HAL_Delay(1);
		status = readMem((uint32_t)&(flState->debuggerStatus));
		retry--;
	} while (status != DEBUGGERCMD_NONE && retry > 0);
	
	//uPrintf("Loader Dbg Status: %u\r\n", status);
	
	/* Wait until command has completed */
	retry = FLASHLOADER_RETRY_COUNT;
	do {
		HAL_Delay(1);
		status = readMem((uint32_t)&(flState->flashLoaderStatus));
		retry--;
	} while (status == FLASHLOADER_STATUS_NOT_READY && retry > 0);
	  
	/* Raise an error if we timed out or flashloader has reported an error */
	if (status == FLASHLOADER_STATUS_NOT_READY) 
	{
		uPrintf("Error Timed out while waiting for flashloader\r\n");
		ErrorFlag = 0;
		return false;
	} 
	else if (status != FLASHLOADER_STATUS_READY) 
	{
		uPrintf("Flashloader returned error code %d\r\n", status);
		return false;
	}
	return true;
}

/**********************************************************
 * Get Buffer size in bytes
 **********************************************************/
uint32_t getBufferSize()
{
	return readMem((uint32_t)&(flState->bufferSize));
}


/**********************************************************
 * Writes a chunk of data to a buffer in the flashloader.
 * This function does not make any checks or assumptions.
 * It simply copies a number of words from the 
 * local to the remote buffer.
 * 
 * @param remoteAddr
 *    Address of the flashloader buffer at the target
 * 
 * @param localBuffer
 *    The local buffer to write from
 * 
 * @param numWords
 *    Number of words to write to buffer
 **********************************************************/
bool writeToFlashloaderBuffer(uint32_t remoteAddr, uint32_t *localBuffer, int numWords)
{
	uint32_t bufferPointer = (uint32_t)remoteAddr;
	int curWord = 0;
	uint32_t tarWrap;

	/* Get the TAR wrap-around period */
	tarWrap = getTarWrap();
    
	/* Set auto increment on TAR to allow faster writes */
	writeAP(AP_CSW, AP_CSW_DEFAULT | AP_CSW_AUTO_INCREMENT);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	/* Initialize TAR with the start of buffer */
	writeAP(AP_TAR, bufferPointer);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	/* Send up to one full buffer of data */    
	while (curWord < numWords)
	{
		/* At TAR wrap boundary we need to reinitialize TAR
		 * since the autoincrement wraps at these */
		if ((bufferPointer & tarWrap) == 0)
		{
			writeAP(AP_TAR, bufferPointer);
		}
      
		/* Write one word */
		writeAP(AP_DRW, localBuffer[curWord]);
		if (ErrorFlag != SWD_ERROR_OK) return false;
		/* Increment local and remote pointers */
		bufferPointer += 4;
		curWord += 1;
	}
            
	/* Disable auto increment on TAR */
	writeAP(AP_CSW, AP_CSW_DEFAULT);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	return true;
	
}

/**********************************************************
 * Get SPI Flash size 
 * Check if SPI flash is available at all
 **********************************************************/
uint32_t getSpiFlashSize()
{
	writeMem((uint32_t)&(flState->numBytes1), 0); //reset size
	if (ErrorFlag != SWD_ERROR_OK) return false;
	writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_EFLASH_INIT_GET_ID);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	if (!waitForFlashloader()) return false;
	
	
	return readMem((uint32_t)&(flState->numBytes1)) * 1024;
}


/**********************************************************
 * Uploads a binary image to the SPi flash.
 **********************************************************/
bool uploadImageToloaderSpiFlash(uint32_t writeAddress, uint32_t *fwImage, uint32_t size)
{   
	uint32_t numPages = size / SPIPageSize;
	uint32_t curPage = 0;
//	if (writeAddress < FLASH_MEM_BASE) 
//	{
//		return false;
//	}
	bool useBuffer1 = true;
  
	/* Get the buffer location (where to temporary store data 
	 * in target SRAM) from flashloader */
	uint32_t bufferLocation1 = readMem((uint32_t)&(flState->bufferAddress1));
	if (ErrorFlag != SWD_ERROR_OK) return false;
	uint32_t bufferLocation2 = readMem((uint32_t)&(flState->bufferAddress2));
	if (ErrorFlag != SWD_ERROR_OK) return false;   
	/* Get size of target buffer */
	uint32_t bufferSize = readMem((uint32_t)&(flState->bufferSize));
  
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	/* Round up to nearest page */
	if (numPages * SPIPageSize < size) numPages++;
	
	/* Fill the buffer in RAM and tell the flashloader to write
	 * this buffer to SPI Flash. Since we are using two buffers
	 * we can fill one buffer while the flashloader is using
	 * the other. 
	 */  
	while (curPage < numPages) 
	{
		/* Calculate the number of words to write */
		int pagesToWrite = numPages - curPage < bufferSize / SPIPageSize ? numPages - curPage : bufferSize / SPIPageSize;
		        
		/* Write one chunk to the currently active buffer */
		writeToFlashloaderBuffer(
		       useBuffer1 ? bufferLocation1 : bufferLocation2,
			&fwImage[curPage * SPIPageSize / 4],
			pagesToWrite * SPIPageSize / 4);
		
		if (ErrorFlag != SWD_ERROR_OK) return false;    
		
		/* Wait until flashloader is done writing to flash */
		if (!waitForFlashloader()) return false;

		
		/* Tell the flashloader to write the data to flash */
		if (useBuffer1) 
		{      
			writeMem((uint32_t)&(flState->numBytes1), pagesToWrite * SPIPageSize);
			writeMem((uint32_t)&(flState->writeAddress1), writeAddress);
			writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_EFLASH_WRITE_DATA1);
		} 
		else
		{
			writeMem((uint32_t)&(flState->numBytes2), pagesToWrite * SPIPageSize);
			writeMem((uint32_t)&(flState->writeAddress2), writeAddress);
			writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_EFLASH_WRITE_DATA2);      
		}
		if (ErrorFlag != SWD_ERROR_OK) return false;
		/* Increase address */
		curPage += pagesToWrite;
		writeAddress += pagesToWrite * SPIPageSize;
    
		/* Flip buffers */
		useBuffer1 = !useBuffer1;
	}
  
	/* Wait until the last flash write operation has completed */
	if (!waitForFlashloader()) return false;
	return true;
}

/**********************************************************
 * Read SPI Flash data
 **********************************************************/
bool sendSpiFlashReadCmd(bool useBuffer1, uint32_t addr, uint32_t size)
{
	if (useBuffer1) 
	{      
		writeMem((uint32_t)&(flState->numBytes1), size);
		writeMem((uint32_t)&(flState->writeAddress1), addr);
		writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_EFLASH_READ_DATA1);
	} 
	else
	{
		writeMem((uint32_t)&(flState->numBytes2), size);
		writeMem((uint32_t)&(flState->writeAddress2), addr);
		writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_EFLASH_READ_DATA2);      
	}
	if (ErrorFlag != SWD_ERROR_OK) return false;

	return true;
}

bool getSpiFlashReadBufferLocation(bool useBuffer1, uint32_t *BufferLocation)
{
	if (!waitForFlashloader()) return false;
	if (useBuffer1) {
		*BufferLocation = readMem((uint32_t)&(flState->bufferAddress1));
	} else {
		*BufferLocation = readMem((uint32_t)&(flState->bufferAddress2));
	}
	if (ErrorFlag != SWD_ERROR_OK) return false;

	return true;
}

/**********************************************************
 * Tells the flashloader to erase one page at the 
 * given address in SPI Flash
 **********************************************************/
bool sendSpiFlashErasePageCmd(uint32_t addr, uint32_t size)
{

	writeMem((uint32_t)&(flState->writeAddress1), addr);
	if (ErrorFlag != SWD_ERROR_OK) return false;

	writeMem((uint32_t)&(flState->numBytes1), size);
	if (ErrorFlag != SWD_ERROR_OK) return false;

	writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_EFLASH_ERASE_PAGE);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	if (!waitForFlashloader()) return false;
	
	if (readMem((uint32_t)&(flState->numBytes1)) == 0) return false;
	return true;
}

/**********************************************************
 * Tells the flashloader to erase one page at the 
 * given address.
 **********************************************************/
bool sendErasePageCmd(uint32_t addr, uint32_t size)
{
	writeMem((uint32_t)&(flState->writeAddress1), addr);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	writeMem((uint32_t)&(flState->numBytes1), size);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_ERASE_PAGE);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	if (!waitForFlashloader()) return false;
	return true;
}




/**********************************************************
 * Uploads a binary image (the firmware) to the flashloader.
 **********************************************************/
bool uploadImageToFlashloader(uint32_t writeAddress, uint32_t *fwImage, uint32_t size)
{   
	uint32_t numWords = size / 4;
	uint32_t curWord = 0;
//	if (writeAddress < FLASH_MEM_BASE) 
//	{
//		return false;
//	}
	bool useBuffer1 = true;
  
	/* Get the buffer location (where to temporary store data 
	 * in target SRAM) from flashloader */
	uint32_t bufferLocation1 = readMem((uint32_t)&(flState->bufferAddress1));
	if (ErrorFlag != SWD_ERROR_OK) return false;
	uint32_t bufferLocation2 = readMem((uint32_t)&(flState->bufferAddress2));
	if (ErrorFlag != SWD_ERROR_OK) return false;   
	/* Get size of target buffer */
	uint32_t bufferSize = readMem((uint32_t)&(flState->bufferSize));
  
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	/* Round up to nearest word */
	if (numWords * 4 < size) numWords++;
	
	uint32_t pageSize = readMem((uint32_t)&(flState->pageSize));
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	/* Calculate number of pages needed to store image */
//	uint32_t usedPages = size / pageSize;
//	if (usedPages * pageSize < size) usedPages++;  
//	  
//	uPrintf("Erasing %d page(s)\r\n", usedPages);
//	
//	
//	sendErasePageCmd(writeAddress, usedPages * pageSize);
//	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	/* Fill the buffer in RAM and tell the flashloader to write
	 * this buffer to Flash. Since we are using two buffers
	 * we can fill one buffer while the flashloader is using
	 * the other. 
	 */  
	while (curWord < numWords) 
	{
		/* Calculate the number of words to write */
		int wordsToWrite = numWords - curWord < bufferSize / 4 ? numWords - curWord : bufferSize / 4;
		        
		/* Write one chunk to the currently active buffer */
		writeToFlashloaderBuffer(
		       useBuffer1 ? bufferLocation1 : bufferLocation2,
			&fwImage[curWord],
			wordsToWrite);
		
		if (ErrorFlag != SWD_ERROR_OK) return false;    
		
		/* Wait until flashloader is done writing to flash */
		if (!waitForFlashloader()) return false;

		
		/* Tell the flashloader to write the data to flash */
		if (useBuffer1) 
		{      
			writeMem((uint32_t)&(flState->numBytes1), wordsToWrite);
			writeMem((uint32_t)&(flState->writeAddress1), writeAddress);
			writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_WRITE_DATA1);
		} 
		else
		{
			writeMem((uint32_t)&(flState->numBytes2), wordsToWrite);
			writeMem((uint32_t)&(flState->writeAddress2), writeAddress);
			writeMem((uint32_t)&(flState->debuggerStatus), DEBUGGERCMD_WRITE_DATA2);      
		}
		if (ErrorFlag != SWD_ERROR_OK) return false;
		/* Increase address */
		curWord += wordsToWrite;
		writeAddress += wordsToWrite * 4;
    
		/* Flip buffers */
		useBuffer1 = !useBuffer1;
	}
  
	/* Wait until the last flash write operation has completed */
	if (!waitForFlashloader()) return false;
	return true;
}


/**************************************************************************//**
 * @file flashloader.c
 * @brief Universal flashloader for EFM32 devices
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
#include <stdint.h>

#include "em_msc.h"
#include "flashloader.h"
#include "AT25.h"


/* Place the state struct in at a defined address. 
 * This struct is used to communicate with the 
 * programmer.
 */



//#pragma location = STATE_LOCATION

volatile flashLoaderState_TypeDef state __attribute__((section(".ram2"))); 



/* This is the buffer to use for writing data to/from.
   It is dynamically resized based on available RAM.
   Therefore it must be placed last by the linker. */

#define BUFFER_SIZE 0x800 //1 page

uint32_t flashBuffer1[BUFFER_SIZE];
uint32_t flashBuffer2[BUFFER_SIZE];

uint16_t SpiFlashSize = 0;
bool SpiFlashFound = false;

/**********************************************************
 * HardFault handler. Sets the status field to tell
 * programmer that we have encountered a HardFault and
 * enter an endless loop 
 **********************************************************/
static void HardFaultHandler(void)
{
	state.flashLoaderStatus = FLASHLOADER_STATUS_ERROR_HARDFAULT;
	while (1) ;
}

/**********************************************************
 * NMI handler. Sets the status field to tell
 * programmer that we have encountered a NMI and
 * enter an endless loop 
 **********************************************************/
static void NmiHandler(void)
{
	state.flashLoaderStatus = FLASHLOADER_STATUS_ERROR_NMI;
	while (1) ;
}

/**********************************************************
 * Handles errors from MSC. Sets the status field to tell
 * programmer which error occured and then
 * enter an endless loop 
 * 
 * @param ret
 *    MSC error code
 **********************************************************/
static void handleMscError(msc_Return_TypeDef ret)
{
	if (ret != mscReturnOk)
	{
		/* Generate error code. */
		switch (ret)
		{
		case (mscReturnTimeOut):      
			state.flashLoaderStatus = FLASHLOADER_STATUS_ERROR_TIMEOUT;
			break;
		case (mscReturnLocked):
			state.flashLoaderStatus = FLASHLOADER_STATUS_ERROR_LOCKED;
			break;
		case (mscReturnInvalidAddr):
			state.flashLoaderStatus = FLASHLOADER_STATUS_ERROR_INVALIDADDR;
			break;
		default:
			state.flashLoaderStatus = FLASHLOADER_STATUS_ERROR_UNKNOWN;
			break;
		}
		while (1) ;
	}
}


/**********************************************************
 * Initalizes the state struct with values from the DI
 * page on current device. This function initializes
 *   - SRAM size
 *   - Flash Size
 *   - Production Revision
 *   - Device Family
 *   - Page Size
 * 
 **********************************************************/
static void setupEFM32(void)
{
	/* Automatically detect device characteristics */
	state.sramSize = (DEVINFO->MSIZE & _DEVINFO_MSIZE_SRAM_MASK) >> _DEVINFO_MSIZE_SRAM_SHIFT;
	state.flashSize = (DEVINFO->MSIZE & _DEVINFO_MSIZE_FLASH_MASK) >> _DEVINFO_MSIZE_FLASH_SHIFT;
	
	state.pageSize = (DEVINFO->MEMINFO & _DEVINFO_MEMINFO_FLASHPAGESIZE_MASK) >> _DEVINFO_MEMINFO_FLASHPAGESIZE_SHIFT;
	state.pageSize = 1U << (state.pageSize + 10U);

	/* Check for unprogrammed flash. */
	if (state.sramSize == 0xFFFFFFFF)
	{
		state.sramSize = 1;
	}

	/* Calculate it in bytes */
	state.sramSize *= 1024;

}


/**********************************************************
 * Waits on the MSC_STATUS register until the selected
 * bits are set or cleared. This function will busy wait
 * until (MSC_STATUS & mask) == value. 
 * Errors are also handled by this function. Errors
 * will cause the flashloader to set the status
 * flag and stop execution by entering a busy loop. 
 * 
 * @param mask
 *    The mask to apply to MSC_STATUS
 * 
 * @param value
 *    The value to compare against, after applying the mask
 * 
 **********************************************************/
static void mscStatusWait(uint32_t mask, uint32_t value)
{
	uint32_t status;
	uint32_t timeOut = MSC_PROGRAM_TIMEOUT;

	while (1)
	{
		status = MSC_NS->STATUS;

		/* Check for errors */
		if (status & (MSC_STATUS_LOCKED | MSC_STATUS_INVADDR))
		{
			/* Disable write access */
			MSC_NS->WRITECTRL &= ~MSC_WRITECTRL_WREN;

			/* Set error flag and enter busy loop */
			if (status & MSC_STATUS_LOCKED)
			{
				handleMscError(mscReturnLocked);
			}

			/* Set error flag and enter busy loop */
			if (status & MSC_STATUS_INVADDR) 
			{
				handleMscError(mscReturnInvalidAddr);
			}
		}
    
		/* Check end condition */
		if ((status & mask) == value)
		{
			/* We are done waiting */
			break;
		}

		timeOut--;
		if (timeOut == 0)
		{
			/* Timeout occured. Set flag and enter busy loop */
			handleMscError(mscReturnTimeOut);
		}
	}
}


/**********************************************************
 * Erases on page of flash. 
 *
 * @param addr
 *    Address of page. Must be a valid flash address.
 * 
 * @param pagesize
 *    Size of one page in bytes
 **********************************************************/
static void eraseSector(uint32_t addr, uint32_t pagesize)
{
	uint32_t *p = (uint32_t*)addr, result;

	/* Check if page already is erased. If so we can
	 * simply return. */
	do
	{
		result    = *p++;
		pagesize -= 4;
	} while (pagesize && (result == 0xFFFFFFFF));

	if (result != 0xFFFFFFFF)
	{
		/* Erase the page */
		MSC_NS->ADDRB    = addr;
		MSC_NS->WRITECMD = MSC_WRITECMD_ERASEPAGE;
		mscStatusWait(MSC_STATUS_BUSY, 0);
	}
}

/**********************************************************
 * Writes multiple words to flash
 *
 * @param addr
 *    Where to start writing. Must be a valid flash address.
 * 
 * @param p
 *    Pointer to data
 * 
 * @param cnt
 *    Number of bytes to write. Must be a multiple
 *    of four.
 **********************************************************/
static void pgmBurst(uint32_t addr, uint32_t *p, uint32_t cnt)
{
	uint32_t ii = 0;
	/* Wait until MSC is ready */
	mscStatusWait(MSC_STATUS_BUSY, 0);
  
	/* Enter start address */
	MSC_NS->ADDRB    = addr;
	/* Write first word. Address will be automatically incremented. */
	MSC_NS->WDATA    = *p++;

	cnt--;

	/* Loop until all words have been written */
	while (cnt)
	{
		mscStatusWait(MSC_STATUS_WDATAREADY, MSC_STATUS_WDATAREADY);
		MSC_NS->WDATA = *p++;
		cnt--;
	}
  
	/* End writing */
	MSC_NS->WRITECMD = MSC_WRITECMD_WRITEEND;
}


bool SpiFlashInit()
{
	SpiFlashDispInit();
	SpiFlashExitSleep();
	SpiFlashSize = SpiFlashGetSize();
	
	SpiFlashFound = (SpiFlashSize) ? true : false;
	state.numBytes1 = SpiFlashSize;
	return SpiFlashFound;
}

void main(void)
{
	uint32_t  *pBuff;
	uint32_t addr, burst, pageMask, DWordCount;

	/* Disable interrupts */
	__disable_irq();
	
	/* Relocate vector table */
	SCB->VTOR = 0x20000040;
	__DSB();

  
	/* Signal setup */
	state.flashLoaderStatus = FLASHLOADER_STATUS_NOT_READY;
	state.debuggerStatus = DEBUGGERCMD_NOT_CONNECTED;

	
	/* Get device info including memory size */
	
	setupEFM32();

  
	/* Calculate size of available buffers. Two buffers are
	 * used. Each buffer will  fill up half of the remaining RAM. 
	 * Round down to nearest word boundry */
	state.bufferSize = (state.sramSize - ((uint32_t) &flashBuffer1 - 0x20000000));
  
	/* Only use full 4 bytes (1 word) */
	state.bufferSize = BUFFER_SIZE << 2;
   
	/* Set the address of both buffers  */
	state.bufferAddress1 = (uint32_t) &flashBuffer1;
	state.bufferAddress2 = (uint32_t) &flashBuffer2;

	/* Signal setup complete. Ready to accept commands from programmer. */
	state.flashLoaderStatus = FLASHLOADER_STATUS_READY;
	state.debuggerStatus = DEBUGGERCMD_NONE;
	
	//SpiFlashReadPage(SpiFlashBuffer, 0x8000);
	
	//MSC->CMD_SET = MSC_CMD_PWRUP; //enable flash power
	CMU->CLKEN1_SET = CMU_CLKEN1_MSC; //enable MSC clock
	

	/* Poll debuggerStatus field to listen for commands
	 * from programmer */
	while (1)
	{
    
		/* Erase page(s) command */
		if (state.debuggerStatus == DEBUGGERCMD_ERASE_PAGE)
		{
			/* Clear the flag to indicate that we are busy */
			state.flashLoaderStatus = FLASHLOADER_STATUS_NOT_READY;
			state.debuggerStatus = DEBUGGERCMD_NONE;
			
			MSC->WRITECTRL = 0x00010001;
			
			/* Enable flash writes */
			MSC->WRITECTRL |= MSC_WRITECTRL_WREN;
			      
			/* Get address of first page to erase */
			uint32_t writeAddress = state.writeAddress1;
			      
			/* Erase all pages in the given range */
			for (addr = writeAddress; addr < writeAddress + state.numBytes1; addr += state.pageSize)
			{
				eraseSector(addr, state.pageSize);
			}
			      
			/* Disable flash writes */
			MSC->WRITECTRL &= ~MSC_WRITECTRL_WREN;

			/* Operation complete. Set flag to ready again. */
			state.flashLoaderStatus = FLASHLOADER_STATUS_READY;
		}
    
		/* Write command */
		if (state.debuggerStatus == DEBUGGERCMD_WRITE_DATA1 || state.debuggerStatus == DEBUGGERCMD_WRITE_DATA2)
		{
			/* Select buffer based on write command */
			uint8_t useBuffer1 = state.debuggerStatus == DEBUGGERCMD_WRITE_DATA1 ? 1 : 0;
			
			/* Clear the flag to indicate that we are busy */
			state.flashLoaderStatus = FLASHLOADER_STATUS_NOT_READY;
			state.debuggerStatus = DEBUGGERCMD_NONE;

			/* Set up buffer, size and destination */
			
			pBuff     = useBuffer1 ? (uint32_t *)state.bufferAddress1 : (uint32_t *)state.bufferAddress2;
			DWordCount = useBuffer1 ? state.numBytes1 : state.numBytes2; //words32
			addr      = useBuffer1 ? state.writeAddress1 : state.writeAddress2;
			/* Enable flash writes */
			MSC_NS->WRITECTRL |= MSC_WRITECTRL_WREN;
			pgmBurst(addr, pBuff, DWordCount);

			/* Wait until operations are complete */
			mscStatusWait(MSC_STATUS_BUSY, 0);
			
			/* Disable flash writes */
			MSC_NS->WRITECTRL &= ~MSC_WRITECTRL_WREN;

			/* Operation complete. Set flag to ready again. */
			state.flashLoaderStatus = FLASHLOADER_STATUS_READY;
		}
		else
			
/*******************************
 *******************************
 *******************************
 *******************************
 *******************************
 ********************************/
			
			/* External flash */
			//Init - get ID
			
		if (state.debuggerStatus == DEBUGGERCMD_EFLASH_INIT_GET_ID)
		{
			/* Clear the flag to indicate that we are busy */
			state.flashLoaderStatus = FLASHLOADER_STATUS_NOT_READY;
			state.debuggerStatus = DEBUGGERCMD_NONE;
			
			SpiFlashInit();
			
			/* Operation complete. Set flag to ready again. */
			state.flashLoaderStatus = FLASHLOADER_STATUS_READY;
		}
		
		else 

		/* Write command */
		if (state.debuggerStatus == DEBUGGERCMD_EFLASH_WRITE_DATA1 || state.debuggerStatus == DEBUGGERCMD_EFLASH_WRITE_DATA2)
		{
			/* Select buffer based on write command */
			uint8_t useBuffer1 = state.debuggerStatus == DEBUGGERCMD_EFLASH_WRITE_DATA1 ? 1 : 0;
			
			/* Clear the flag to indicate that we are busy */
			state.flashLoaderStatus = FLASHLOADER_STATUS_NOT_READY;
			state.debuggerStatus = DEBUGGERCMD_NONE;

			/* Set up buffer, size and destination */
			
			uint8_t *pBuff = useBuffer1 ? (uint8_t*)flashBuffer1 : (uint8_t*)flashBuffer2;
			uint32_t StartAddress = useBuffer1 ? state.writeAddress1 : state.writeAddress2;
			uint32_t ReadSize = useBuffer1 ? state.numBytes1 : state.numBytes2;

			if (!SpiFlashFound) SpiFlashInit();
			
			if (SpiFlashFound)
			{
			    while (SpiFlashReadStatus() & SPI_STATUS_BSY) ; //wait for ready
				/* Erase all pages in the given range */
				for (addr = StartAddress; addr < StartAddress + ReadSize; addr += SPIPageSize)
				{
					SpiFlashCmd(SPICMD_WREN);
					SpiFlashWritePage(pBuff, addr);
					while (SpiFlashReadStatus() & SPI_STATUS_BSY) ; //wait for ready
					pBuff += SPIPageSize;
				}
			}

			/* Operation complete. Set flag to ready again. */
			state.flashLoaderStatus = FLASHLOADER_STATUS_READY;
		}
		else

			//read data
			
		if (state.debuggerStatus == DEBUGGERCMD_EFLASH_READ_DATA1 || state.debuggerStatus == DEBUGGERCMD_EFLASH_READ_DATA2)
		{
			/* Select buffer based on read command */
			uint8_t useBuffer1 = state.debuggerStatus == DEBUGGERCMD_EFLASH_READ_DATA1 ? 1 : 0;
			/* Clear the flag to indicate that we are busy */
			state.flashLoaderStatus = FLASHLOADER_STATUS_NOT_READY;
			state.debuggerStatus = DEBUGGERCMD_NONE;
			
			if (!SpiFlashFound) SpiFlashInit();
			
			if (SpiFlashFound)
			{

				uint8_t *pBuff = useBuffer1 ? (uint8_t*)flashBuffer1 : (uint8_t*)flashBuffer2;
				uint32_t StartAddress = useBuffer1 ? state.writeAddress1 : state.writeAddress2;
				uint32_t ReadSize = useBuffer1 ? state.numBytes1 : state.numBytes2;

				while (SpiFlashReadStatus() & SPI_STATUS_BSY) ; //wait for ready
				/* read all pages in the given range */
				for (addr = StartAddress; addr < StartAddress + ReadSize; addr += SPIPageSize)
				{
					SpiFlashReadPage(pBuff, addr);
					pBuff += SPIPageSize;
				}
				
			}
			
			/* Operation complete. Set flag to ready again. */
			state.flashLoaderStatus = FLASHLOADER_STATUS_READY;
		}
		
		else 
		
			//erase data
			
		if (state.debuggerStatus == DEBUGGERCMD_EFLASH_ERASE_PAGE)
		{
			/* Clear the flag to indicate that we are busy */
			state.flashLoaderStatus = FLASHLOADER_STATUS_NOT_READY;
			state.debuggerStatus = DEBUGGERCMD_NONE;
			
			if (!SpiFlashFound) SpiFlashInit();
			
			if (SpiFlashFound)
			{
				uint8_t *pBuff;
			    while (SpiFlashReadStatus() & SPI_STATUS_BSY) ; //wait for ready
				/* Erase all pages in the given range */
				for (addr = state.writeAddress1; addr < state.writeAddress1 + state.numBytes1; addr += SPIPageSize)
				{
					//check if page is empty already
					pBuff = (uint8_t*)flashBuffer1;
					SpiFlashReadPage(pBuff, addr);
					bool empty = true;
					int len = SPIPageSize;
					do {
						if (*pBuff++ != 0xFF) {
							empty = false;
						}
						len--;
					} while (len && empty);

					if (!empty) {
						SpiFlashCmd(SPICMD_WREN);
						SpiFlashErasePage(addr);
						while (SpiFlashReadStatus() & SPI_STATUS_BSY) ; //wait for ready
					}
				}
			}
			
			/* Operation complete. Set flag to ready again. */
			state.flashLoaderStatus = FLASHLOADER_STATUS_READY;
		}
			
		
	}
}

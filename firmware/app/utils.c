/**************************************************************************//**
 * @file utils.c
 * @brief Various utility functions for the debug interface
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
#include <stdbool.h>
#include <stdio.h>

#include "dap.h"
#include "utils.h"
#include "errors.h"
#include "delay.h"

#include "main.h"

#include "efm32.h"


#include "use_flashloader.h"
extern uint32_t ErrorFlag;


#define _DEVINFO_PART_FAMILY_MG                                  0x00000001UL     
#define _DEVINFO_PART_FAMILY_BG                                  0x00000002UL     


extern bool OutInv;

/**********************************************************
 * Reads the unique ID of the target from the DI page.
 * 
 * @returns
 *    The unique ID of target
 **********************************************************/
uint64_t readUniqueId(void)
{
	uint64_t uniqueId;
  
	/* Retrive high part of unique ID */
	uniqueId = readMem((uint32_t)&(DEVINFO->EUI64H));
	uniqueId = uniqueId << 32;
  
	/* Retrive low part of unique ID */
	uniqueId |= readMem((uint32_t)&(DEVINFO->EUI64L));

	return uniqueId;
}


/**********************************************************
 * Halts the target CPU
 **********************************************************/
bool haltTarget(void)
{

	int timeout = DEBUG_EVENT_TIMEOUT;
	writeAP(AP_TAR, (uint32_t)&(CoreDebug->DHCSR));
	if (ErrorFlag != SWD_ERROR_OK) return false;
	writeAP(AP_DRW, STOP_CMD);
	if (ErrorFlag != SWD_ERROR_OK) return false;
  
	uint32_t dhcrState;
	do {
		HAL_Delay(1);
		writeAP(AP_TAR, (uint32_t)&(CoreDebug->DHCSR));
		if (ErrorFlag != SWD_ERROR_OK) return false;
		readAP(AP_DRW, &dhcrState);
		if (ErrorFlag != SWD_ERROR_OK) return false;
		readDP(DP_RDBUFF, &dhcrState);
		if (ErrorFlag != SWD_ERROR_OK) return false;
		timeout--;
	} while (!(dhcrState & CoreDebug_DHCSR_S_HALT_Msk) && timeout > 0); 
  
	if (!(dhcrState & CoreDebug_DHCSR_S_HALT_Msk)) {
		uPrintf("SWD Error Timeout Halt\r\n");
		//RAISE(SWD_ERROR_TIMEOUT_HALT);
		return false;
	}
	return true;
}


/**********************************************************
 * Lets the target CPU run freely (stops halting)
 **********************************************************/
void runTarget(void)
{
	writeAP(AP_TAR, (uint32_t)&(CoreDebug->DHCSR));
	writeAP(AP_DRW, RUN_CMD);
}

/**********************************************************
 * Single steps the target
 **********************************************************/
void stepTarget(void)
{
	writeAP(AP_TAR, (uint32_t)&(CoreDebug->DHCSR));
	writeAP(AP_DRW, STEP_CMD);
}


/**********************************************************
 * Retrieves total flash size from the DI page of the target
 * 
 * @returns
 *    The flash size in bytes
 **********************************************************/
int getFlashSize(void)
{  
	/* Read memory size from the DI page */
	uint32_t msize = readMem((uint32_t)&(DEVINFO->MSIZE));
  
	/* Retrieve flash size (in kB) */
	uint32_t flashSize = (msize & _DEVINFO_MSIZE_FLASH_MASK) >> _DEVINFO_MSIZE_FLASH_SHIFT;
  
	/* Return value in bytes */
	return flashSize * 1024;
	
	return 0;
}


int getRamSize(void)
{  
	/* Read memory size from the DI page */
	uint32_t msize = readMem((uint32_t)&(DEVINFO->MSIZE));
  
	/* Retrieve flash size (in kB) */
	uint32_t ramsize = (msize & _DEVINFO_MSIZE_SRAM_MASK) >> _DEVINFO_MSIZE_SRAM_SHIFT;
  
	/* Check for unprogrammed flash. */
	if (ramsize == 0xFFFFFFFF)
	{
		ramsize = 1;
	}
	/* Calculate it in bytes */
	ramsize *= 1024;
	return ramsize;
}

/**********************************************************
 * Retrieves page size from the DI page of the target
 * 
 * @returns
 *    The page size in bytes
 **********************************************************/
int getPageSize(void)
{  
	uint32_t minfo = readMem((uint32_t)&(DEVINFO->MEMINFO));
  
	uint32_t pageSize = (minfo & _DEVINFO_MEMINFO_FLASHPAGESIZE_MASK) >> _DEVINFO_MEMINFO_FLASHPAGESIZE_SHIFT;
	return 1U << (pageSize + 10U);
}

/**********************************************************
 * Retrieve the device name from the DI page of the target
 * 
 * @param deviceName[out]
 *    Device name is stored in this buffer when 
 *    the function returns. The calling function is
 *    responsible for allocating memory for the string
 **********************************************************/
void getDeviceName(char deviceName[])
{
	char familyCode[3];
	char FamilyLetter[2] = { 0, 0 };
  
	uint32_t part = readMem((uint32_t)&(DEVINFO->PART));
	uint32_t msize = readMem((uint32_t)&(DEVINFO->MSIZE));
	uint32_t pkginfo = readMem((uint32_t)&(DEVINFO->PKGINFO));
	
	uint32_t flashSize = (msize & _DEVINFO_MSIZE_FLASH_MASK) >> _DEVINFO_MSIZE_FLASH_SHIFT;
	uint32_t family = (part &  _DEVINFO_PART_FAMILY_MASK) >> _DEVINFO_PART_FAMILY_SHIFT;  
	uint32_t partNum = (part &  _DEVINFO_PART_DEVICENUM_MASK) >> _DEVINFO_PART_DEVICENUM_SHIFT;  
	char tGrade = 'C';
	char pack = 'I';
	uint8_t PinCOunt = (pkginfo >> 16) & 0xFF;
	
	switch (pkginfo & 0xFF)
	{
	case 0:
		tGrade = 'G'; break;
	case 1:
		tGrade = 'I'; break;
	case 2:
		tGrade = 'N'; break;
	case 3:
		tGrade = 'F'; break;
	default:
		break;
	}
	
	switch ((pkginfo >> 8) & 0xFF)
	{
	case 74:
		pack = 'W'; break; //not sure, WLCSP
	case 76:
		pack = 'B'; break; //not sure, BGA
	case 77:
		pack = 'M'; break; //QFN
	case 81:
		pack = 'Q'; break; //QFP
	default:
		break;
	}
	
	
	FamilyLetter[0] = partNum / 1000 + 'A';
	
	
	switch (family)
	{
	case _DEVINFO_PART_FAMILY_FG:  
		sprintf(familyCode, "%s", "FG");
		break;
	case _DEVINFO_PART_FAMILY_PG:          
		sprintf(familyCode, "%s", "PG");
		break;
	case _DEVINFO_PART_FAMILY_ZG:          
		sprintf(familyCode, "%s", "ZG");
		break;
	case _DEVINFO_PART_FAMILY_BG:
		sprintf(familyCode, "%s", "BG");
		break;
	default:
		sprintf(familyCode, "%s", "xx"); /* Unknown family */
		break;
	}
  
	sprintf(deviceName,
		"EFM32%s%dF%d%c%c%u", 
		familyCode,
		partNum % 1000,
		flashSize,
		tGrade,
		pack,
		PinCOunt);
}

/**********************************************************
 * Get the wrap-around period for the TAR register. This
 * is device dependent and affects the burst write
 * algorithms. This function hard-codes the values
 * based on information from the DI-page. 
 * 
 * @returns
 *   The wrap-around period of the TAR register
 **********************************************************/
uint32_t getTarWrap(void)
{
	uint32_t part = readMem((uint32_t)&(DEVINFO->PART));
	uint32_t family = (part &  _DEVINFO_PART_FAMILY_MASK) >> _DEVINFO_PART_FAMILY_SHIFT;
  
	/* Hard-code result based on device family. ZG has 1kB 
	 * wrap. G/TG/LG/WG/GG has 4kB. Default to 1 kB on unknown
	 * devices */
	switch (family)
	{

	case _DEVINFO_PART_FAMILY_FG:          
      
		return 0xFFF;
	case _DEVINFO_PART_FAMILY_PG:   
	case _DEVINFO_PART_FAMILY_ZG:          /* Zero Gecko */
		return 0x3FF;
	default:                                      /* Unknown family */
		return 0x3FF;
	}
	
	return 0;
}


/**********************************************************
 * Resets the target CPU by using the AIRCR register. 
 * The target will be halted immediately when coming
 * out of reset. Does not reset the debug interface.
 **********************************************************/
bool resetAndHaltTarget(void)
{
	uint32_t dhcsr;
	int timeout = DEBUG_EVENT_TIMEOUT;
  
	/* Halt target first. This is necessary before setting
	 * the VECTRESET bit */
	
	if (!haltTarget()) return false;
	
	/* Set halt-on-reset bit */
	writeMem((uint32_t)&(CoreDebug->DEMCR), CoreDebug_DEMCR_VC_CORERESET_Msk );
	if (ErrorFlag != SWD_ERROR_OK) return false;
	

	
	/* Clear exception state and reset target */
	writeAP(AP_TAR, (uint32_t)&(SCB->AIRCR));
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	writeAP(AP_DRW,
		(0x05FA << SCB_AIRCR_VECTKEY_Pos) |
	                SCB_AIRCR_VECTCLRACTIVE_Msk |
	                SCB_AIRCR_VECTRESET_Msk);
	
	if (ErrorFlag != SWD_ERROR_OK) return false;
	/* Wait for target to reset */
	do { 
		HAL_Delay(1);
		timeout--;
		dhcsr = readMem((uint32_t)&(CoreDebug->DHCSR));
	} while (dhcsr & CoreDebug_DHCSR_S_RESET_ST_Msk);
  
  
	/* Check if we timed out */
	dhcsr = readMem((uint32_t)&(CoreDebug->DHCSR));
	if (dhcsr & CoreDebug_DHCSR_S_RESET_ST_Msk) 
	{
		if (ErrorFlag != SWD_ERROR_OK) return false;
	}
  
	/* Verify that target is halted */
	if (!(dhcsr & CoreDebug_DHCSR_S_HALT_Msk)) 
	{
		if (ErrorFlag != SWD_ERROR_OK) return false;
	}
	
//	/* Reset CPU */
//	writeMem((uint32_t)&(SCB->AIRCR), AIRCR_RESET_CMD);
//	if (ErrorFlag != SWD_ERROR_OK) return false;
	return true;
}


/**********************************************************
 * Resets the target CPU by using the AIRCR register. 
 * Does not reset the debug interface
 **********************************************************/
bool resetTarget(void)
{  
	uint32_t dhcsr;
	int timeout = DEBUG_EVENT_TIMEOUT;
  
	/* Clear the VC_CORERESET bit */
	writeMem((uint32_t)&(CoreDebug->DEMCR), 0);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	/* Do a dummy read of sticky bit to make sure it is cleared */
	readMem((uint32_t)&(CoreDebug->DHCSR));
	if (ErrorFlag != SWD_ERROR_OK) return false;
	dhcsr = readMem((uint32_t)&(CoreDebug->DHCSR));
	if (ErrorFlag != SWD_ERROR_OK) return false;
  
	/* Reset CPU */
	writeMem((uint32_t)&(SCB->AIRCR), AIRCR_RESET_CMD);
	if (ErrorFlag != SWD_ERROR_OK) return false;
	/* Wait for reset to complete */
  
  
	/* First wait until sticky bit is set. This means we are
	 * or have been in reset */
	HAL_Delay(1);
	do { 
		HAL_Delay(1);
		dhcsr = readMem((uint32_t)&(CoreDebug->DHCSR));
		if (ErrorFlag != SWD_ERROR_OK) return false;
		timeout--;
	} while (!(dhcsr & CoreDebug_DHCSR_S_RESET_ST_Msk) && timeout > 0);
    
	/* Throw error if sticky bit is never set */
	if (!(dhcsr & CoreDebug_DHCSR_S_RESET_ST_Msk)) {
		RAISE(SWD_ERROR_TIMEOUT_WAITING_RESET);
	}
    
	/* Wait for sticky bit to be cleared. When bit is cleared are we out of reset */
	timeout = DEBUG_EVENT_TIMEOUT;
	do { 
		HAL_Delay(1);
		dhcsr = readMem((uint32_t)&(CoreDebug->DHCSR));
		if (ErrorFlag != SWD_ERROR_OK) return false;
		timeout--;
	} while (dhcsr & CoreDebug_DHCSR_S_RESET_ST_Msk && timeout > 0);
  
	/* Throw error if bit is never cleared */
	if (dhcsr & CoreDebug_DHCSR_S_RESET_ST_Msk) {
		RAISE(SWD_ERROR_TIMEOUT_WAITING_RESET);
	}
	if (ErrorFlag != SWD_ERROR_OK) return false;
	
	return true;
}

/**********************************************************
 * Performs a pin reset on the target
 **********************************************************/
void hardResetTarget(void)
{
	HAL_GPIO_WritePin(iRST_GPIO_Port, iRST_Pin, GPIO_PIN_SET);
	HAL_Delay(10);
	HAL_GPIO_WritePin(iRST_GPIO_Port, iRST_Pin, GPIO_PIN_RESET);
}

/**********************************************************
 * Reads one word from internal memory
 * 
 * @param addr 
 *    The address to read from
 * 
 * @returns 
 *    The value at @param addr
 **********************************************************/
uint32_t readMem(uint32_t addr)
{
	uint32_t ret;
	writeAP(AP_TAR, addr);
	readAP(AP_DRW, &ret);
	readDP(DP_RDBUFF, &ret);
	return ret;
}

/**********************************************************
 * Writes one word to internal memory
 * 
 * @param addr 
 *    The address to write to 
 *
 * @param data
 *    The value to write
 * 
 * @returns 
 *    The value at @param addr
 **********************************************************/
void writeMem(uint32_t addr, uint32_t data)
{
	writeAP(AP_TAR, addr);
	writeAP(AP_DRW, data);
}
 
/**********************************************************
 * Waits for the REGRDY bit in DCRSR. This bit indicates
 * that the DCRDR/DCRSR registers are ready to accept
 * new data. 
 **********************************************************/
void waitForRegReady(void)
{
	uint32_t dhcsr;
	do {
		dhcsr = readMem((uint32_t)&CoreDebug->DHCSR);
	} while (!(dhcsr & CoreDebug_DHCSR_S_REGRDY_Msk));
}


/**********************************************************
 * Verifies the current firmware against the locally
 * stored original. This function assumes the firmware
 * has been written to target starting at address 0x00. 
 *
 * @param fwImage
 *    Pointer to locally stored copy of firmware image
 * 
 * @param size
 *    Size (in bytes) of firmware image
 * 
 * @returns 
 *    True if target firmware matches local copy. 
 *    False otherwise. 
 **********************************************************/
bool verifyFirmware(uint32_t *fwImage, uint32_t size)
{
	int i;
	int numWords = size / 4;
	bool ret = true;
	uint32_t value;
	uint32_t addr;
	uint32_t tarWrap =  getTarWrap();
  
	uPrintf("Verifying firmware\n");
  
	/* Set autoincrement on TAR */
	writeAP(AP_CSW, AP_CSW_DEFAULT | AP_CSW_AUTO_INCREMENT);
  
	for (i = 0; i < numWords; i++) 
	{   
		/* Get current address */
		addr = i * 4;
        
		/* TAR must be initialized at every TAR wrap boundary
		 * because the autoincrement wraps around at these */
		if ((addr & tarWrap) == 0)
		{
			writeAP(AP_TAR, addr);
      
			/* Do one dummy read. Subsequent reads will return the 
			 * correct result. */
			readAP(AP_DRW, &value);
		}
    
		/* Read the value from addr */
		readAP(AP_DRW, &value);
    
		/* Verify that the read value matches what is expected */
		if (value != fwImage[i]) 
		{
			uPrintf("Verification failed at address 0x%.8x\n", addr);
			uPrintf("Value is 0x%.8x, should have been 0x%.8x\n", value, fwImage[i]);
			ret = false;
			break;
		}
	}
  
	/* Disable autoincrement on TAR */
	writeAP(AP_CSW, AP_CSW_DEFAULT);
     
	return ret;
}

/**********************************************************
 * Returns true if the @param dpId is a valid
 * IDCODE value. 
 **********************************************************/
bool verifyDpId(uint32_t dpId)
{
	if (dpId == EFM32_DPID_1) 
	{
		return true; // Valid for G, LG, GG, TG, WG
	}
	else if (dpId == EFM32_DPID_2)
	{
		return true; // Valid for ZG
	} 
	else if (dpId == EFM32_DPID_3)
	{
		return true; // Valid for SWMD
	}   
	else if (dpId == EFM32_DPID_4)
	{
		return true; // Valid for PG
	}   
	else 
	{
		return false;
	}
}

/**********************************************************
 * Returns true if the @param apId is a valid
 * IDR value for the AHB-AP. 
 **********************************************************/
bool verifyAhbApId(uint32_t apId)
{
	if (apId == EFM32_AHBAP_ID_1) 
	{
		return true; // Valid for G, LG, GG, TG, WG
	}
	else if (apId == EFM32_AHBAP_ID_2)
	{
		return true; // Valid for ZG
	} 
	else if (apId == EFM32_AHBAP_ID_3)
	{
		return true; // Valid for PG
	} 
	else if (apId == STM32_AHBAP_ID_3)
	{
		return true; // Valid for PG
	} 
	else 
	{
		return false;
	}
}

/**********************************************************
 * This function will check if a Zero Gecko device is
 * locked. The method is different from other MCUs,
 * we have to read the AAP registers from the internal
 * memory space.
 * 
 * This process can fail (we receive a FAULT response) on 
 * other devices so wee need to check for failure on the AP 
 * transaction and clear the STICKYERR flag in CTRL/STAT 
 * before continuing. 
 **********************************************************/
void checkIfZeroGeckoIsLocked(void)
{
	int readError = SWD_ERROR_OK;
	uint32_t readVal;
	uint32_t apId;
  
	/* Try reading the AAP_IDR register on Zero. 
	 * Do in a separate TRY/CATCH block in case to allow
	 * failure in this transaction.
	 */
	//TRY 
	apId = readMem(AAP_IDR_ZERO);
	//CATCH
	  /* If transaction failed. Store error code */
	readError = 0; // errorCode;
  //ENDTRY
  
  /* If the transaction was OK we check if we got
   * access to the AAP registers. If we do, the device
   * is locked. 
   */
	if (readError == SWD_ERROR_OK)
	{
		if (apId == EFM32_AAP_ID) 
		{
			RAISE(SWD_ERROR_MCU_LOCKED);
		}
	} 
	/* We received a FAULT or WAIT error. This is normal on non-ZG devices. 
	 * If this happens we have to clear the STICKYERR flag before continuing. 
	 * If we do not do this all subsequent AP transactions will fail. 
	 */
	else if(readError == SWD_ERROR_FAULT || readError == SWD_ERROR_WAIT)
	{
		/* Read CTRL/STAT register */
		readDP(DP_CTRL, &readVal);
    
		/* See if STICKYERR is set */
		if (readVal & (1 << 5))
		{
			/* Clear sticky error */
			writeDP(DP_ABORT, (1 << 2));
		} 
	} 
	/* We received another error, e.g. protocol error. 
	 * Report the error back to the calling function */
	else  
	{
		RAISE(readError);
	}
}


/**********************************************************
 * Writes a value to a CPU register in the target.
 * 
 * @param reg
 *   The register number to write to
 * 
 * @param value
 *   The value to write to the register
 **********************************************************/
void writeCpuReg(int reg, uint32_t value)
{
	/* Wait until debug register is ready to accept new data */
	waitForRegReady();
  
	/* Write value to Data Register */
	writeAP(AP_TAR, (uint32_t)&(CoreDebug->DCRDR));
	writeAP(AP_DRW, value);
  
	/* Write register number ot Selector Register. 
	 * This will update the CPU register */
	writeAP(AP_TAR, (uint32_t)&(CoreDebug->DCRSR));
	writeAP(AP_DRW, 0x10000 | reg); 
}


uint32_t readCpuReg(int reg)
{
	
}

/**********************************************************
 * Performs the initialization sequence on the SW-DP. 
 * After this completes the debug interface can be used. 
 * Raises an exception on any error during connection. 
 **********************************************************/
bool connectToTarget(void)
{
	uint32_t dpId, apId;
  
	//uPrintf("Connecting to target...\r\n");
  
	//hardResetTarget();
	//HAL_Delay(100);
		
	
	dpId = initDp();

	uPrintf("IDCODE 0x%08X\r\n", dpId);  
	
	/* Verify that the DP returns the correct ID */
	//if (!verifyDpId(dpId)) 
	if ((dpId & 0x2477) != 0x2477)
	{
		uPrintf("SWD Error Invalid IDCODE\r\n");
		return false;
	}
	
	/* Verify that the AP returns the correct ID */
	int retry = AHB_IDR_RETRY_COUNT;
	while (retry > 0)
	{
		apId = readApId();
		if (verifyAhbApId(apId))
		{
			/* Success. AHB-AP registers found */
			break;
		}
		retry--;
	}
	if (!retry)
	{
		return false;
	}

	/* In case we did NOT find the AHB-AP registers check 
		 * if the chip is locked or if some other error ocurred. */  
	if (!verifyAhbApId(apId)) 
	{
		/* If the ID is from AAP, the MCU is locked. 
		 * Debug access is not available. */
		if (apId == EFM32_AAP_ID) 
		{
			uPrintf("SWD Error MCU Locked\r\n");
			return false;
			//RAISE(SWD_ERROR_MCU_LOCKED);
		} 
		else
		{
			/* The AP ID was not from AAP nor AHB-AP. This is an error. */ 
			uPrintf("Error Read IDR = 0x%08X\r\n", apId);
			//
		  //RAISE(SWD_ERROR_INVALID_IDR);  
			return false;
		}
	} 
    

	/* Set up parameters for AHB-AP. This must be done before accessing
	 * internal memory. */
	initAhbAp();
	
	if (ErrorFlag != SWD_ERROR_OK) 
	{	
		uPrintf("Error %lu\r\n", ErrorFlag);
		return false;
	}
	
	/*
	 * On Zero Gecko it is possible that the device is locked
	 * even though we reach this point. We have to see if 
	 * we can access the AAP registers which are memory mapped 
	 * on these devices. The function will raise an exception
	 * if the device is a Zero Gecko and is currently locked.
	 */
	if (apId == EFM32_AHBAP_ID_2)
	{
		checkIfZeroGeckoIsLocked();
	}

	writeAP(AP_TAR, 0xE000EE08);
	
	writeAP(AP_DRW, 0x00010000);

	writeAP(AP_TAR, 0xE000ED0C);
	writeAP(AP_DRW, 0x05FA0004);
	writeDP(DP_STAT, 0xF0000000);
	HAL_Delay(10);
	writeAP(AP_TAR, 0xE000EDFC);
	writeAP(AP_DRW, 0x01000000);
	
	if (!haltTarget()) return false;
   	

	
	uint32_t tarWrap;
 
	/* Get the TAR wrap-around period */
	tarWrap = getTarWrap();
	
	/* Get device name */
	char deviceName[30];
	getDeviceName(deviceName);
	uPrintf("Target %s\r\n", deviceName);

	/* Read the unique ID of the MCU */
	uint64_t uniqueId = readUniqueId();
	uPrintf("UniqueID %08lX%08lX\r\n",
		((uint32_t)((uniqueId >> 32) & 0xFFFFFFFF)),
		((uint32_t)(uniqueId & 0xFFFFFFFF)));
	
	if (ErrorFlag != SWD_ERROR_OK) 
	{	
		uPrintf("Error %lu\r\n", ErrorFlag);
		return false;
	}
	

	writeMem(CMU_BASE + 0x1068, 1 << 16); //Enable CMU clock
	writeDP(0, 0x1E); //abort

	return true;
}

void GPIO_WritePinOut()
{
	if (OutInv)
	{
		SWDIR_GPIO_Port->BSRR = SWDIR_Pin;
	}
	else
	{
		SWDIR_GPIO_Port->BRR = SWDIR_Pin;
	}
}

void GPIO_WritePinIn()
{
	if (OutInv)
	{
		SWDIR_GPIO_Port->BRR = SWDIR_Pin;
	}
	else
	{
		SWDIR_GPIO_Port->BSRR = SWDIR_Pin;
	}
}
/**************************************************************************//**
 * @file use_flashloader.h
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
#ifndef _USE_FLASHLOADER_H_
#define _USE_FLASHLOADER_H_


bool uploadFlashloader(uint32_t *addr, uint32_t size);

bool checkFlashloader(void);

bool erasePage(uint32_t addr);

bool uploadImageToFlashloader(uint32_t writeAddress, uint32_t *fwImage, uint32_t size);
bool uploadImageToloaderSpiFlash(uint32_t writeAddress, uint32_t *fwImage, uint32_t size);

bool flashWithFlashloader(bool verify);
bool waitForFlashloader(void);
bool sendErasePageCmd(uint32_t addr, uint32_t size);
bool LoadLoader();
void verifyFlashloaderReady(void);
uint32_t getBufferSize();
uint32_t getSpiFlashSize();
bool sendSpiFlashErasePageCmd(uint32_t addr, uint32_t size);
bool sendSpiFlashReadCmd(bool useBuffer1, uint32_t addr, uint32_t size);
bool getSpiFlashReadBufferLocation(bool useBuffer1, uint32_t *BufferLocation);

#endif
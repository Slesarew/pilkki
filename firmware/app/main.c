/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "crc.h"
#include "dma.h"
#include "rng.h"
#include "spi.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dap.h"
#include "utils.h"
#include "flash_write.h"
#include "use_flashloader.h"
#include "flashloader.h"

#include "efm32.h"

#include "usbd_cdc_if.h"
#include "usbd_cdc.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_FIFO	128

#define WRITE_BUFFER 4096


//#define SPI1_DR_8bit (*(__IO uint8_t *)((uint32_t)&(SPI1->DR)))
//#define SPI1_DR_16bit (*(__IO uint16_t *)((uint32_t)&(SPI1->DR)))

#define FIRMWARE_REVISION 21

#define USB_FIFO_RX 1024
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

uint32_t Seccount = 0;
char buff[UART_FIFO]; // __attribute__((section(".ram2"))); 
//char SubBuff[UART_FIFO];
//char UartRX[UART_FIFO];

uint32_t WriteBuffer[WRITE_BUFFER];
volatile uint8_t * WriteBufP;
volatile uint8_t UsbRxFifo[USB_FIFO_RX]; // __attribute__((section(".ram2")));
volatile bool DirectBuf = false;
volatile uint32_t BufReceived;
volatile uint32_t sBytes;
uint8_t SpiRead[256];

uint16_t RxFifoWrPointer = 0;
uint16_t RxFifoRdPointer = 0;
uint16_t RxFifoRecords = 0;
uint32_t UsbPackBuffer[128]; // __attribute__((section(".ram2")));
uint16_t UsbPackBufferP = 0;
uint32_t UsbSubBuf[128]; // __attribute__((section(".ram2")));

uint32_t Unique_ID_0;

uint32_t WriteBuffSize = WRITE_BUFFER * 4;
uint32_t PageSize;
uint32_t FlashSize;
uint32_t RamSize;

bool TxBusy = false;
uint32_t tickstart;
uint32_t ErrorFlag = 0;

uint16_t AdcRaw;
struct UartDma_t
{
	size_t head, tail;
	uint8_t RingBuff[128];
} UartDma;// __attribute__((section(".ram2")));

uint32_t IdCode;

struct 
{
	uint8_t CmdSize;
	char CmdIn[32];
	bool CMD;
} usb;

bool OutInv = true;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
bool SwdAdrStart(bool APDP, bool RW, uint8_t ADDR);
bool ReadCrc(uint32_t Addr, uint32_t Len, uint32_t * Crc);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

uint8_t SpiSendByte(uint8_t data)
{
	while (!(SPI1->SR & SPI_SR_TXE)) ;     
	SPI1_DR_8bit = data;                  
	while (!(SPI1->SR & SPI_SR_RXNE)) ; 
	return SPI1_DR_8bit;
}

uint16_t SpiSendWord(uint16_t data)
{
	while (!(SPI1->SR & SPI_SR_TXE)) ;     
	SPI1_DR_16bit = data;                       
	while (!(SPI1->SR & SPI_SR_RXNE)) ; 
	return SPI1_DR_16bit;
}


void uPrintf(const char* fmt, ...) {
	


	uint16_t bts;
	va_list args;
	va_start(args, fmt);
	bts = vsnprintf(buff, sizeof(buff), fmt, args);
	va_end(args);	
	//	memcpy(SubBuff, buff, bts);
	//	TxBusy = true;
	
	
	tickstart = HAL_GetTick();
	while (CDC_Transmit_FS((uint8_t*)buff, bts))
	{
		if (HAL_GetTick() - tickstart > 100) break;
	}
	
	
	//HAL_UART_Transmit_DMA(&huart2, (uint8_t*)SubBuff, bts);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == huart->Instance) TxBusy = false;
}


uint8_t FifoGet()
{

	__disable_irq();
	if (RxFifoRecords) {
		uint8_t Data = UsbRxFifo[RxFifoRdPointer++];
		if (RxFifoRdPointer >= USB_FIFO_RX) RxFifoRdPointer = 0;
		RxFifoRecords--;
		__enable_irq();
		return Data;
	}
	__enable_irq();
	return 0;
}


uint16_t FifoSizeGet()
{
	uint16_t Records;
	__disable_irq();
	Records = RxFifoRecords;
	__enable_irq();
	return Records;
}


bool ReadCrc(uint32_t Addr, uint32_t Len, uint32_t * Crc)
{
	static uint32_t Size, Wrd, addr;
	__HAL_CRC_DR_RESET(&hcrc);	
	haltTarget();
	uint32_t tarWrap =  getTarWrap();
	
	if (ErrorFlag != 1) uPrintf("Connect error\r\n");
	else
	{
		writeAP(AP_CSW, AP_CSW_DEFAULT | AP_CSW_AUTO_INCREMENT);
		writeAP(AP_TAR, Addr);
		readAP(AP_DRW, &Wrd);
										
		uint8_t cr = 0;
		for (uint32_t j = 0; j < Len; j++)
		{
			addr = Addr + (j << 2);
			if ((addr & tarWrap) == 0)
			{
				writeAP(AP_TAR, addr);
				readAP(AP_DRW, &Wrd);
			}
			readAP(AP_DRW, &Wrd);
											
			CRC->DR = Wrd;
			if (ErrorFlag != 1) 
			{
				uPrintf("Read error\r\n");
				return false;
			}
		}							

		* Crc = (CRC->DR ^ -1UL);
			
		writeAP(AP_CSW, AP_CSW_DEFAULT);
			
		if (ErrorFlag != 1) return false;
	}
	return true;
}

void StartBoot()
{
	
	MX_USB_DEVICE_Stop();
	
	HAL_RCC_DeInit();
	
	SysTick->CTRL = SysTick->LOAD = SysTick->VAL = 0;
	__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
	__HAL_RCC_SYSCFG_CLK_ENABLE(); 
	
	
	
	
	const uint32_t p = (*((uint32_t *) 0x1FFF0000));
	__set_MSP(p);

	void(*SysMemBootJump)(void);
	SysMemBootJump = (void(*)(void))(*((uint32_t *) 0x1FFF0004));
	SysMemBootJump();

	while (1) {}
	
	
}



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_CRC_Init();
  MX_RNG_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USB_Device_Init();
  /* USER CODE BEGIN 2 */


	uint8_t i;	
	
	HAL_Delay(500);

	//check for HW version
	
	SPI1->CR1 |= SPI_CR1_SPE; //en spi
	SPI1->CR2 = 0x1708; //set 8 bit - tail
	SWDIO_SET_OUTPUT();
	i = SpiSendByte(0xAE);
	OutInv = (i == 0xAE) ? true : false;
	
	SWDIO_SET_INPUT(); 
	

	if (OutInv)
	{
		LEDR(1); HAL_Delay(100);
		LEDB(1); HAL_Delay(100);
		LEDY(1); HAL_Delay(100);
		LEDR(0); HAL_Delay(100);
		LEDB(0); HAL_Delay(200);
		LEDY(0);		
	}
	else
	{
		LEDY(1); HAL_Delay(100);
		LEDR(1); HAL_Delay(100);
		LEDB(1); HAL_Delay(100);
		LEDY(0); HAL_Delay(100);	
		LEDR(0); HAL_Delay(100); 
		LEDB(0);

	}
	
	HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)&AdcRaw, 1);

	usb.CmdSize = 0;
	usb.CMD = true;

	Unique_ID_0 = HAL_CRC_Calculate(&hcrc, (uint32_t *)0x1FFF7590, 12) ^ -1UL;
	
	

	SPI1->CR2 = 0x1F08; //set 16 bit

	uint32_t Rxd;
	uint16_t Rx1, Rx2;
	uint8_t Rxack;
	
	

	while (true)
	{
		if (AdcRaw > 20000 && AdcRaw < 24000) 
		{
			LEDY(1);
		}
		else LEDY(0);
		
		if (FifoSizeGet())
		{

			
			if (usb.CMD)
			{
				usb.CmdIn[usb.CmdSize] = (char)FifoGet();
				if (usb.CmdSize > 31) usb.CmdSize = 0;
				if (usb.CmdIn[usb.CmdSize++] == '\n')
				{
					LEDB(1);
					LEDR(0);
					
					usb.CmdIn[usb.CmdSize] = 0;
					usb.CmdSize = 0;

					//parsing requests					



					if (strstr(usb.CmdIn, "bootloader"))
					{
						uPrintf("Starting Bootloader\r\n***\r\n");
						HAL_Delay(100);
						StartBoot();
					}
					else 
						//no need in rust pilkki software
						//get maximum rx buffer in 32 bit words
					if (strstr(usb.CmdIn, "bufsize"))
					{
						uPrintf("MaxBuf = %lu Words\r\n***\r\n", WRITE_BUFFER);
					}
					else 
						//connect to target
					if (strstr(usb.CmdIn, "con"))
					{
						if (!connectToTarget())
						{
							uPrintf("Connect error\r\n***\r\n");
							LEDR(1);
						}
						else
						{
							PageSize = getPageSize();
								
							if (ErrorFlag != 1)
							{
								uPrintf("Connect error\r\n***\r\n");	
								LEDR(1);
								continue;
							}
							
							FlashSize = getFlashSize();

							if (ErrorFlag != 1)
							{
								uPrintf("Connect error\r\n***\r\n");	
								LEDR(1);
								continue;
							}

							RamSize = getRamSize();

							if (ErrorFlag != 1)
							{
								uPrintf("Connect error\r\n***\r\n");	
								LEDR(1);
								continue;
							}
								
							uPrintf("PageSize %u\r\n", PageSize);
							uPrintf("FlashSize %u\r\n", FlashSize);
							uPrintf("RamSize %u\r\n***\r\n", RamSize);
						}
					}
					else 
						//target reset
					if (strstr(usb.CmdIn, "loader"))
					{
							
						if (LoadLoader()) {
							uPrintf("Loader OK\r\n");
							uint32_t SpiFlashSize = getSpiFlashSize();

							if (SpiFlashSize)
							{
								uPrintf("SPIFlashSize %u\r\n", SpiFlashSize);
							}
							uPrintf("MaxBufSize %lu\r\n", WriteBuffSize);
							uPrintf("***\r\n");
						}
						
						else 
						{
							uPrintf("Loader error\r\n***\r\n");
							LEDR(1);
						}
							
					}
					else 
						//target reset
					if (strstr(usb.CmdIn, "res"))
					{
						//hardware reset
						if (strstr(usb.CmdIn, "-h"))
						{
							uPrintf("Resetting (hard)\r\n");
							hardResetTarget();
							uPrintf("Hard reset OK\r\n***\r\n");
						}
						else
						{
							uPrintf("Resetting (soft)\r\n");
							resetTarget();
							if (ErrorFlag == 1) uPrintf("Soft reset OK\r\n***\r\n");
							else 
							{
								uPrintf("Connect error\r\n***\r\n");
								LEDR(1);
							}
						}
					}
					else 
						
						/********************
						 ********************
						 **** SPI FLASH *****
						 ********************
						 ********************/
						
						//erase SPI flash
					if (strstr(usb.CmdIn, "spierase"))
					{
						char * pEnd = strstr(usb.CmdIn, " ");
						uint32_t StartAddr, Size, Wrd;
						if (strstr(pEnd, "0x")) StartAddr = strtol(pEnd, &pEnd, 16);	
						else StartAddr = strtol(pEnd, &pEnd, 10);

						if (strstr(pEnd, "0x")) Size = strtol(pEnd, &pEnd, 16);
						else Size = strtol(pEnd, &pEnd, 10);	
						
						uint32_t SpiFlashSize = getSpiFlashSize();
						StartAddr = StartAddr - StartAddr % SPIPageSize;
						uint32_t SpiFlashLeftSize = (SpiFlashSize / SPIPageSize) - StartAddr / SPIPageSize;
						if (Size == 0 || Size > SpiFlashLeftSize)
						{
							uPrintf("Wrong request\r\n***\r\n");
							LEDR(1);
						}
						else
						if (Size > 256)
						{
							uPrintf("Wrong request. Will be timed out, no more 256 pages\r\n***\r\n");
							LEDR(1);
						}
						else
						{
							if (!sendSpiFlashErasePageCmd(StartAddr, Size * SPIPageSize))
							{
								uPrintf("SPI Flash Erase error while erasing %u pages at 0x%08lX\r\n***\r\n", Size, StartAddr);
								LEDR(1);
							}
							
							else uPrintf("Erased %u pages at 0x%08lX***\r\n", Size ,  StartAddr);
						}
					}
					else 
					
						//read SPI Flash page
					if (strstr(usb.CmdIn, "spird"))
					{
						char * pEnd = strstr(usb.CmdIn, " ");
						static uint32_t StartAddr, Size, Wrd, addr;
						if (strstr(pEnd, "0x")) StartAddr = strtol(pEnd, &pEnd, 16);	
						else StartAddr = strtol(pEnd, &pEnd, 10);	
						
						if (strstr(pEnd, "0x")) Size = strtol(pEnd, &pEnd, 16);
						else Size = strtol(pEnd, &pEnd, 10);

						uint32_t SpiFlashSize = getSpiFlashSize();
						StartAddr = StartAddr - StartAddr % SPIPageSize;
						uint32_t SpiFlashLeftSize = (SpiFlashSize / SPIPageSize) - StartAddr / SPIPageSize;
						if (Size == 0 || Size > SpiFlashLeftSize)
						{
							uPrintf("Wrong request\r\n***\r\n");
							LEDR(1);
						}
						else
						{
							bool useBuffer1 = true;
							uint32_t bufferPages = getBufferSize() / SPIPageSize;
							int pagesToRead = Size < bufferPages ? Size : bufferPages;
							if (!sendSpiFlashReadCmd(useBuffer1, StartAddr, pagesToRead * SPIPageSize) && ErrorFlag != 1)
							{
								uPrintf("Read error\r\n***\r\n");
								LEDR(1);
							}
							else 
							{

								uint32_t tarWrap =  getTarWrap();
								__HAL_CRC_DR_RESET(&hcrc);

								uint32_t buffer_location;
								uint32_t curPage = 0;

								while (curPage < Size) {
									/* Calculate the number of words to read and send*/
									int pagesToSend = Size - curPage < bufferPages ? Size - curPage : bufferPages;
									int pagesToRead = Size - (curPage + pagesToSend) < bufferPages ? Size - (curPage + pagesToSend) : bufferPages;
									curPage += pagesToSend;
									getSpiFlashReadBufferLocation(useBuffer1, &buffer_location);

									useBuffer1 != useBuffer1;
									if (pagesToRead) {
										if (!sendSpiFlashReadCmd(useBuffer1, StartAddr + curPage * SPIPageSize, pagesToRead * SPIPageSize) && ErrorFlag != 1)
										{
											uPrintf("Read error\r\n***\r\n");
											LEDR(1);
											break;
										}
									}
									UsbPackBufferP = 0;
									writeAP(AP_CSW, AP_CSW_DEFAULT | AP_CSW_AUTO_INCREMENT);
									writeAP(AP_TAR, buffer_location);
									readAP(AP_DRW, &Wrd);
									uint32_t wordsToSend = (pagesToSend * SPIPageSize >> 2);
									for (uint32_t i = 0; i < wordsToSend; i++)
									{
										addr = buffer_location + (i << 2);
										if ((addr & tarWrap) == 0)
										{
											writeAP(AP_TAR, addr);
											readAP(AP_DRW, &Wrd);
										}
										readAP(AP_DRW, &Wrd);
										CRC->DR = Wrd;
										if (ErrorFlag != 1) 
										{
											uPrintf("error\r\n***\r\n");
											LEDR(1);
											break;
										}
										tickstart = HAL_GetTick();

										UsbPackBuffer[UsbPackBufferP++] = Wrd;
											
											
										if (UsbPackBufferP == 128 || i == wordsToSend - 1) 
										{
											while (UsbCheckBusy()) ;
											memcpy(UsbSubBuf, UsbPackBuffer, UsbPackBufferP << 2);
											while (CDC_Transmit_FS((uint8_t*)&UsbSubBuf, UsbPackBufferP << 2)) 
												//while (CDC_Transmit_FS((uint8_t*)&Wrd, 4)) 
											{
												if (HAL_GetTick() - tickstart > 200) break;
											}
											UsbPackBufferP = 0;
										}
										
									}
								}
								writeAP(AP_CSW, AP_CSW_DEFAULT);
								
								Wrd = CRC->DR ^ -1UL;

								tickstart = HAL_GetTick();
								while (CDC_Transmit_FS((uint8_t*)&Wrd, 4)) 
								{
									if (HAL_GetTick() - tickstart > 100) break;
								}
					
								if (ErrorFlag == 1) uPrintf("Done");
							}
						}
					}
					
					else
					//	write buffer to spi flash
					if (strstr(usb.CmdIn, "spiwritebuffer"))
					{
						char * pEnd = strstr(usb.CmdIn, " ");
						static uint32_t StartAddr, Size, Wrd, addr;
						if (strstr(pEnd, "0x")) StartAddr = strtol(pEnd, &pEnd, 16);	
						else StartAddr = strtol(pEnd, &pEnd, 10);	
							
						if (strstr(pEnd, "0x")) Size = strtol(pEnd, &pEnd, 16);	
						else Size = strtol(pEnd, &pEnd, 10);	
							
						uint32_t SpiFlashSize = getSpiFlashSize();
						StartAddr = StartAddr - StartAddr % SPIPageSize;
						uint32_t SpiFlashLeftSize = (SpiFlashSize / SPIPageSize) - StartAddr / SPIPageSize;
						if (Size == 0 || Size > SpiFlashLeftSize)
						{
							uPrintf("Wrong request\r\n***\r\n");
							LEDR(1);
						}

						uploadImageToloaderSpiFlash(StartAddr, WriteBuffer, Size * SPIPageSize);

						if (ErrorFlag != 1) 
						{
							uPrintf("error\r\n***\r\n");
							LEDR(1);
						}
						else 
							uPrintf("Write OK\r\n***\r\n");
						//							}
					}
					else

						/*****************************
						 *****************************
						 ***    END OF SPi FLASH   ***
						 *****************************
						 ******************************/
			
						//erase pages
					if (strstr(usb.CmdIn, "erase"))
					{
						char * pEnd = strstr(usb.CmdIn, " ");
						uint32_t StartAddr, Size, Wrd;
						if (strstr(pEnd, "0x")) StartAddr = strtol(pEnd, &pEnd, 16);	
						else StartAddr = strtol(pEnd, &pEnd, 10);	
						
						if (strstr(pEnd, "0x")) Size = strtol(pEnd, &pEnd, 16);
						else Size = strtol(pEnd, &pEnd, 10);

						FlashSize = getFlashSize();
						StartAddr = StartAddr - StartAddr % PageSize;
						uint32_t FlashLeftSize = (FlashSize / PageSize) - (StartAddr - 0x08000000) / PageSize ;
						if (Size == 0 || Size > FlashLeftSize) 
						{
							uPrintf("Wrong request\r\n***\r\n");
							LEDR(1);
						}
						else
						{
							uPrintf("Erasing Flash with Loader, Start: 0x%08lX, Len: %u Pages\r\n", StartAddr, Size);	

							sendErasePageCmd(StartAddr, Size * PageSize);
							if (ErrorFlag != 1) 
							{
								uPrintf("Erase error\r\n***\r\n");
								LEDR(1);
							}
							else uPrintf("Erase Done\r\n***\r\n");

						}
							
					}
						
					else	
						//read data
					if (strstr(usb.CmdIn, "rd"))
					{
						char * pEnd = strstr(usb.CmdIn, " ");
						static uint32_t StartAddr, Size, Wrd, addr;
						if (strstr(pEnd, "0x")) StartAddr = strtol(pEnd, &pEnd, 16);	
						else StartAddr = strtol(pEnd, &pEnd, 10);	
							
						StartAddr &= 0xFFFFFFFC; //words only
							
						if (strstr(pEnd, "0x")) Size = strtol(pEnd, &pEnd, 16);
						else Size = strtol(pEnd, &pEnd, 10);
						
						FlashSize = getFlashSize();
						uint32_t FlashReqSize = FlashSize - (StartAddr - 0x08000000);
						if (Size == 0 || Size << 2 > FlashReqSize)
						{
							uPrintf("Wrong request\r\n***\r\n");
							LEDR(1);
						}
						else
						{
							__HAL_CRC_DR_RESET(&hcrc);	
								
							haltTarget();
								
							uint32_t tarWrap =  getTarWrap();
							if (ErrorFlag != 1) 
							{
								uPrintf("Connect error\r\n***\r\n");
								LEDR(1);
							}
							else
							{
								if (strstr(usb.CmdIn, "-h"))
								{
										
									uPrintf("Reading memory, Start: 0x%08lX, Len: %u Words\r\n", StartAddr, Size);	
									writeAP(AP_CSW, AP_CSW_DEFAULT | AP_CSW_AUTO_INCREMENT);
									writeAP(AP_TAR, StartAddr);
									readAP(AP_DRW, &Wrd);
										
									uint8_t cr = 0;
									for (uint32_t j = 0; j < Size; j++)
									{
										addr = StartAddr + (j << 2);
										if (!cr) uPrintf("0x%08lX: ", addr);

										if ((addr & tarWrap) == 0)
										{
											writeAP(AP_TAR, addr);
											readAP(AP_DRW, &Wrd);
										}
										readAP(AP_DRW, &Wrd);
											
										CRC->DR = Wrd;
										uPrintf("%08lX ", Wrd);
										if (ErrorFlag != 1) 
										{
											uPrintf("\r\nRead error\r\n***\r\n");
											LEDR(1);
											break;
										}
										cr++;
										if (cr == 8)
										{
											uPrintf("\r\n");
											cr = 0;
										}
									}							
									if (cr) uPrintf("\r\n");
									uPrintf("CRC: 0x%08lX\r\n", CRC->DR ^ -1UL);
									if (ErrorFlag == 1) uPrintf("Read Done\r\n***\r\n");
										
									writeAP(AP_CSW, AP_CSW_DEFAULT);

								}
								else
								{
									//uPrintf("Bin,%lu,%lu:", StartAddr, Size);	
									
									UsbPackBufferP = 0;
					
									writeAP(AP_CSW, AP_CSW_DEFAULT | AP_CSW_AUTO_INCREMENT);
									writeAP(AP_TAR, StartAddr);
									readAP(AP_DRW, &Wrd);
										
									for (uint32_t j = 0; j < Size; j++) 
									{
										addr = StartAddr + (j << 2);
										if ((addr & tarWrap) == 0)
										{
											writeAP(AP_TAR, addr);
											readAP(AP_DRW, &Wrd);
										}
										readAP(AP_DRW, &Wrd);
    
										CRC->DR = Wrd;					
										if (ErrorFlag != 1) 
										{
											uPrintf("error\r\n***\r\n");
											LEDR(1);
											break;
										}
										tickstart = HAL_GetTick();
											
											
										UsbPackBuffer[UsbPackBufferP++] = Wrd;
											
											
										if (UsbPackBufferP == 128 || j == Size - 1) 
										{
											while (UsbCheckBusy()) ;
											memcpy(UsbSubBuf, UsbPackBuffer, UsbPackBufferP << 2);
											while (CDC_Transmit_FS((uint8_t*)&UsbSubBuf, UsbPackBufferP << 2)) 
												//while (CDC_Transmit_FS((uint8_t*)&Wrd, 4)) 
											{
												if (HAL_GetTick() - tickstart > 200) break;
											}
											UsbPackBufferP = 0;
										}
											
											
									}
  
									writeAP(AP_CSW, AP_CSW_DEFAULT);
										
										
									Wrd = CRC->DR ^ -1UL;

									tickstart = HAL_GetTick();
									while (CDC_Transmit_FS((uint8_t*)&Wrd, 4)) 
									{
										if (HAL_GetTick() - tickstart > 100) break;
									}
						
									if (ErrorFlag == 1) uPrintf("Done");
								}
								
							}
									
						}
							
					}
					else 
						//get ID
					if (strstr(usb.CmdIn, "id"))
					{
						uPrintf("EFM32PG2x SWD Programmer r.%u%c 'Kambucha' UID:%08lX\r\n***\r\n", 
							FIRMWARE_REVISION,
							OutInv?'p':'n',
							Unique_ID_0);
					}
					else 
						//halt
					if (strstr(usb.CmdIn, "halt"))
					{
						uPrintf("Halting\r\n");
						haltTarget();
						if (ErrorFlag != 1) 
						{
							uPrintf("error\r\n***\r\n");
							LEDR(1);
						}
						else uPrintf("Halt OK\r\n***\r\n");
					}
					else 
						//run
					if (strstr(usb.CmdIn, "run"))
					{
						uPrintf("Starting\r\n");
						runTarget(); 
						if (ErrorFlag != 1) 
						{
							uPrintf("error\r\n***\r\n");
							LEDR(1);
						}
						else uPrintf("Run OK\r\n***\r\n");
					}
					else
						//CRC
					if (strstr(usb.CmdIn, "crc"))
					{
						uint32_t Crc;
						char * pEnd = strstr(usb.CmdIn, " ");
						static uint32_t StartAddr, Size, Wrd, addr;
						if (strstr(pEnd, "0x")) StartAddr = strtol(pEnd, &pEnd, 16);	
						else StartAddr = strtol(pEnd, &pEnd, 10);	
							
						StartAddr &= 0xFFFFFFFC; //words only
							
				
							
							
						if (strstr(pEnd, "0x")) Size = strtol(pEnd, &pEnd, 16);
						else Size = strtol(pEnd, &pEnd, 10);
							
						//uPrintf("Counting CRC, Start: 0x%08lX, Len: %u Words\r\n", StartAddr, Size);
							
						if (StartAddr == 0 || Size == 0)
						{
							uPrintf("Wrong request\r\n***\r\n");
							LEDR(1);
						}
						else
						{
							if (ReadCrc(StartAddr, Size, &Crc))
							{
								uPrintf("Crc32 = 0x%08lX\r\n***\r\n", Crc);
							}
							else
							{
								uPrintf("error\r\n***\r\n");
								LEDR(1);
							}
						}
					}
					else
						// Write 
					if (strstr(usb.CmdIn, "writebuffer"))
					{
						char * pEnd = strstr(usb.CmdIn, " ");
						static uint32_t StartAddr, Size, Wrd, addr;
						if (strstr(pEnd, "0x")) StartAddr = strtol(pEnd, &pEnd, 16);	
						else StartAddr = strtol(pEnd, &pEnd, 10);	
							
						if (strstr(pEnd, "0x")) Size = strtol(pEnd, &pEnd, 16);	
						else Size = strtol(pEnd, &pEnd, 10);	
							
						StartAddr &= 0xFFFFFFFC; //words only

						uploadImageToFlashloader(StartAddr, WriteBuffer, Size << 2);

						if (ErrorFlag != 1) 
						{
							uPrintf("error\r\n***\r\n");
							LEDR(1);
						}
						else 
							uPrintf("Write OK\r\n***\r\n");
						//							}
					}
					else
						// Load Buffer 
					if (strstr(usb.CmdIn, "loadbuffer"))
					{
						char * pEnd = strstr(usb.CmdIn, " ");
						static uint32_t Size, Wrd, BufAddr;
						if (strstr(pEnd, "0x")) Size = strtol(pEnd, &pEnd, 16);	
						else Size = strtol(pEnd, &pEnd, 10);	
						if (Size > WRITE_BUFFER)
						{
							uPrintf("error buffer overflow\r\n***\r\n");
							LEDR(1);
						}
						else 
						{
							uPrintf("Load ready\r\n***\r\n");
							DirectBuf = true;
							WriteBufP = (uint8_t*)&WriteBuffer;
							BufReceived = 0;
							sBytes = Size << 2;

							while (BufReceived < sBytes) ;
								
							uPrintf("Buffer Loaded with %lu words\r\n***\r\n", Size);
							DirectBuf = false;
						}
					}
					else
					{
						if (strlen(usb.CmdIn) > 1) 
						{
							uPrintf("Invalid request \"%s\"\r\n***\r\n", usb.CmdIn);
							LEDR(1);
						}

					}
				
				}					

			}
			LEDB(0);
			SWDIO_SET_INPUT(); 
		}
						
			
			
	}
	
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	while (1)
	{
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV2;
  RCC_OscInitStruct.PLL.PLLN = 34;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
		/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1)
	{
	}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
		/* User can add his own implementation to report the file name and line number,
		   ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

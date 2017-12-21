#include "stm32f1xx_hal.h"
#include "diskio.h"
                                // 1
#define GPIO_CS                  GPIOA
#define RCC_APB2Periph_GPIO_CS   RCC_APB2Periph_GPIOA
#define GPIO_Pin_CS              GPIO_PIN_4

#define GPIO_PWR                 GPIOA
#define RCC_APB2Periph_GPIO_PWR  RCC_APB2Periph_GPIOA
#define GPIO_Pin_PWR             GPIO_PIN_3
#define GPIO_Mode_PWR            GPIO_MODE_OUTPUT_OD /* pull-up resistor at power FET */

/* Definitions for MMC/SDC command */
#define CMD0	(0x40+0)	/* GO_IDLE_STATE */
#define CMD1	(0x40+1)	/* SEND_OP_COND (MMC) */
#define ACMD41	(0xC0+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(0x40+8)	/* SEND_IF_COND */
#define CMD9	(0x40+9)	/* SEND_CSD */
#define CMD10	(0x40+10)	/* SEND_CID */
#define CMD12	(0x40+12)	/* STOP_TRANSMISSION */
#define ACMD13	(0xC0+13)	/* SD_STATUS (SDC) */
#define CMD16	(0x40+16)	/* SET_BLOCKLEN */
#define CMD17	(0x40+17)	/* READ_SINGLE_BLOCK */
#define CMD18	(0x40+18)	/* READ_MULTIPLE_BLOCK */
#define CMD23	(0x40+23)	/* SET_BLOCK_COUNT (MMC) */
#define ACMD23	(0xC0+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(0x40+24)	/* WRITE_BLOCK */
#define CMD25	(0x40+25)	/* WRITE_MULTIPLE_BLOCK */
#define CMD55	(0x40+55)	/* APP_CMD */
#define CMD58	(0x40+58)	/* READ_OCR */



/* Port Controls  (Platform dependent) */
#define SELECT()        HAL_GPIO_WritePin(GPIO_CS, GPIO_Pin_CS, GPIO_PIN_RESET)    /* MMC CS = L */
#define DESELECT()      HAL_GPIO_WritePin(GPIO_CS, GPIO_Pin_CS, GPIO_PIN_SET)      /* MMC CS = H */
#define PWR_ON()        HAL_GPIO_WritePin(GPIO_PWR, GPIO_Pin_PWR, GPIO_PIN_RESET)
#define PWR_OFF()       HAL_GPIO_WritePin(GPIO_PWR, GPIO_Pin_PWR, GPIO_PIN_SET)
#define PWR_ISON()      ( ( HAL_GPIO_ReadPin(GPIO_PWR, GPIO_Pin_PWR) == 1 ) ? 0 : 1 )

/* Manley EK-STM32F board does not offer socket contacts -> dummy values: */
#define SOCKPORT	1			/* Socket contact port */
#define SOCKWP		0			/* Write protect switch (PB5) */
#define SOCKINS		0			/* Card detect switch (PB4) */

static void FCLK_SLOW(void) /* Set slow clock (100k-400k) */
{
	DWORD tmp;

	tmp = SPI1->CR1;
	tmp = ( tmp | SPI_BAUDRATEPRESCALER_256 );
	SPI1->CR1 = tmp;
}

static void FCLK_FAST(void) /* Set fast clock (depends on the CSD) */
{
	DWORD tmp;

	tmp = SPI1->CR1;
	tmp = ( tmp & ~SPI_BAUDRATEPRESCALER_256 ) | SPI_BAUDRATEPRESCALER_4; // 72MHz/4 here
	SPI1->CR1 = tmp;
}


/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

static volatile
DSTATUS Stat = STA_NOINIT;	/* Disk status */

static volatile
DWORD Timer1, Timer2;	/* 100Hz decrement timers */

static
BYTE CardType;			/* Card type flags */


/*-----------------------------------------------------------------------*/
/* Transmit/Receive a byte to MMC via SPI  (Platform dependent)  1         */
/*-----------------------------------------------------------------------*/
static BYTE stm32_spi_rw( BYTE out )
{
	while (!(SPI1->SR & SPI_SR_TXE));
	SPI1->DR = out;
	while (!(SPI1->SR & SPI_SR_RXNE));
	return (SPI1->DR);

}

/*-----------------------------------------------------------------------*/
/* Transmit a byte to MMC via SPI  (Platform dependent)                  */
/*-----------------------------------------------------------------------*/

#define xmit_spi(dat)  stm32_spi_rw(dat)

/*-----------------------------------------------------------------------*/
/* Receive a byte from MMC via SPI  (Platform dependent)                 */
/*-----------------------------------------------------------------------*/

static
BYTE rcvr_spi (void)
{
	return stm32_spi_rw(0xff);
}

/* Alternative macro to receive data fast */
#define rcvr_spi_m(dst)  *(dst)=stm32_spi_rw(0xff)



/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static
BYTE wait_ready (void)
{
	BYTE res;


	Timer2 = 50;	/* Wait for ready in timeout of 500ms */
	rcvr_spi();
	do
		res = rcvr_spi();
	while ((res != 0xFF) && Timer2);

	return res;
}


/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus                                 */
/*-----------------------------------------------------------------------*/

static
void release_spi (void)
{
	DESELECT();
	rcvr_spi();
}

/*-----------------------------------------------------------------------*/
/* My SPI Init                                                           */
/*-----------------------------------------------------------------------*/
static
void spi_init(void)
{
  RCC->APB2ENR |=  RCC_APB2ENR_AFIOEN;//включить тактирование альтернативных функций
  RCC->APB2ENR |=  RCC_APB2ENR_IOPAEN;//включить тактирование порта А

  //вывод управления SS: выход двухтактный, общего назначения,50MHz
  GPIOA->CRL   |=  GPIO_CRL_MODE4;    //
  GPIOA->CRL   &= ~GPIO_CRL_CNF4;     //
  GPIOA->BSRR   =  GPIO_BSRR_BS4;     //

  //вывод SCK: выход двухтактный, альтернативная функция, 50MHz
  GPIOA->CRL   |=  GPIO_CRL_MODE5;    //
  GPIOA->CRL   &= ~GPIO_CRL_CNF5;     //
  GPIOA->CRL   |=  GPIO_CRL_CNF5_1;   //

  //вывод MISO: вход цифровой с подтягивающим резистором, подтяжка к плюсу
  GPIOA->CRL   &= ~GPIO_CRL_MODE6;    //
  GPIOA->CRL   &= ~GPIO_CRL_CNF6;     //
  GPIOA->CRL   |=  GPIO_CRL_CNF6_1;   //
  GPIOA->BSRR   =  GPIO_BSRR_BS6;     //

  //вывод MOSI: выход двухтактный, альтернативная функция, 50MHz
  GPIOA->CRL   |=  GPIO_CRL_MODE7;    //
  GPIOA->CRL   &= ~GPIO_CRL_CNF7;     //
  GPIOA->CRL   |=  GPIO_CRL_CNF7_1;   //

  //настроить модуль SPI
  RCC->APB2ENR |= RCC_APB2ENR_SPI1EN; //подать тактирование
  SPI1->CR2     = 0x0000;             //
  SPI1->CR1     = SPI_CR1_MSTR;       //контроллер должен быть мастером,конечно
  SPI1->CR1    |= SPI_CR1_BR;         //для начала зададим маленькую скорость
  SPI1->CR1    |= SPI_CR1_SSI;
  SPI1->CR1    |= SPI_CR1_SSM;
  SPI1->CR1    |= SPI_CR1_SPE;        //разрешить работу модуля SPI
}



/*-----------------------------------------------------------------------*/
/* Power Control  (Platform dependent)  1                                 */
/*-----------------------------------------------------------------------*/
/* When the target system does not support socket power control, there   */
/* is nothing to do in these functions and chk_power always returns 1.   */

static
void power_on (void)
{
	SPI_HandleTypeDef  SPI_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure;
	volatile BYTE dummyread;

	/* Enable SPI1 and GPIO clocks */
//	RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI1, ENABLE);
//	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIO_CS | RCC_APB2Periph_GPIO_PWR, ENABLE);
	RCC->APB2ENR |=  RCC_APB2ENR_AFIOEN;//включить тактирование альтернативных функций
	RCC->APB2ENR |=  RCC_APB2ENR_IOPAEN;//включить тактирование порта А

	/* Configure I/O for Power FET */
	GPIO_InitStructure.Pin = GPIO_Pin_PWR;
	GPIO_InitStructure.Mode = GPIO_Mode_PWR;
	GPIO_InitStructure.Speed = GPIO_SPEED_HIGH;
	HAL_GPIO_Init(GPIO_PWR, &GPIO_InitStructure);

	PWR_ON();

	for (Timer1 = 25; Timer1; );	/* Wait for 250ms */

	/* Configure I/O for Flash Chip select */
	GPIO_InitStructure.Pin = GPIO_PIN_4;
	GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStructure.Speed = GPIO_SPEED_HIGH;
	HAL_GPIO_Init(GPIO_CS, &GPIO_InitStructure);

	/* Deselect the Card: Chip Select high */
	DESELECT();

	/* Configure SPI1 pins: SCK and MOSI with default alternate function (not remapped) push-pull */
	GPIO_InitStructure.Pin = GPIO_PIN_5 | GPIO_PIN_7;
	GPIO_InitStructure.Speed = GPIO_SPEED_HIGH;
	GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);
	/* Configure MISO as Input with internal pull-up */
	GPIO_InitStructure.Pin = GPIO_PIN_6;
	GPIO_InitStructure.Speed = GPIO_SPEED_HIGH;
	GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
	GPIO_InitStructure.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

	//настроить модуль SPI
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN; //подать тактирование

	/* SPI1 configuration */
	SPI_InitStructure.Instance = SPI1;
	SPI_InitStructure.Init.Direction = SPI_DIRECTION_2LINES;
	SPI_InitStructure.Init.Mode = SPI_MODE_MASTER;
	SPI_InitStructure.Init.DataSize = SPI_DATASIZE_8BIT;
	SPI_InitStructure.Init.CLKPolarity = SPI_POLARITY_LOW;
	SPI_InitStructure.Init.CLKPhase = SPI_PHASE_1EDGE;
	SPI_InitStructure.Init.NSS = SPI_NSS_SOFT;
	SPI_InitStructure.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256; // 72000kHz/256=281kHz < 400Hz
	SPI_InitStructure.Init.FirstBit = SPI_FIRSTBIT_MSB;
	SPI_InitStructure.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	SPI_InitStructure.Init.CRCPolynomial = 7;
	HAL_SPI_Init(&SPI_InitStructure);

	SPI1->CR1    |= SPI_CR1_SPE;        //разрешить работу модуля SPI

}

static
void power_off (void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	if (!(Stat & STA_NOINIT)) {
		SELECT();
		wait_ready();
		release_spi();
	}

	//SPI_Cmd(SPI1, DISABLE);
	//SPI_I2S_DeInit(SPI1);

	//RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI1, DISABLE);
	RCC->APB2ENR &=  RCC_APB2ENR_AFIOEN;//включить тактирование альтернативных функций
	RCC->APB2ENR &=  RCC_APB2ENR_IOPAEN;//включить тактирование порта А

	/* All SPI-Pins to input with weak internal pull-downs */
	GPIO_InitStructure.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
	GPIO_InitStructure.Speed = GPIO_SPEED_HIGH;
	GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);
	/* Chip select internal pull-down too */
	GPIO_InitStructure.Pin = GPIO_Pin_CS;
	HAL_GPIO_Init(GPIO_CS, &GPIO_InitStructure);

	PWR_OFF();

	Stat |= STA_NOINIT;		/* Set STA_NOINIT */
}

static
int chk_power(void)		/* Socket power state: 0=off, 1=on */
{
	return PWR_ISON() ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from MMC                                        */
/*-----------------------------------------------------------------------*/

static
unsigned char rcvr_datablock (
	BYTE *buff,			/* Data buffer to store received data */
	UINT btr			/* Byte count (must be multiple of 4) */
)
{
	BYTE token;


	Timer1 = 10;
	do {							/* Wait for data packet in timeout of 100ms */
		token = rcvr_spi();
	} while ((token == 0xFF) && Timer1);
	if(token != 0xFE) return 0;	/* If not valid data token, return with error */

	do {							/* Receive the data block into buffer */
		rcvr_spi_m(buff++);
		rcvr_spi_m(buff++);
		rcvr_spi_m(buff++);
		rcvr_spi_m(buff++);
	} while (btr -= 4);

	rcvr_spi();						/* Discard CRC */
	rcvr_spi();

	return 1;					/* Return with success */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to MMC                                             */
/*-----------------------------------------------------------------------*/

#if _READONLY == 0
static
unsigned char xmit_datablock (
	const BYTE *buff,	/* 512 byte data block to be transmitted */
	BYTE token			/* Data/Stop token */
)
{
	BYTE resp;
#ifndef STM32_USE_DMA
	BYTE wc;
#endif

	if (wait_ready() != 0xFF) return 0;

	xmit_spi(token);					/* Xmit data token */
	if (token != 0xFD) {	/* Is data token */

		wc = 0;
		do {							/* Xmit the 512 byte data block to MMC */
			xmit_spi(*buff++);
			xmit_spi(*buff++);
		} while (--wc);


		xmit_spi(0xFF);					/* CRC (Dummy) */
		xmit_spi(0xFF);
		resp = rcvr_spi();				/* Receive data response */
		if ((resp & 0x1F) != 0x05)		/* If not accepted, return with error */
			return 0;
	}

	return 1;
}
#endif /* _READONLY */



/*-----------------------------------------------------------------------*/
/* Send a command packet to MMC                                          */
/*-----------------------------------------------------------------------*/

static
BYTE send_cmd (
	BYTE cmd,		/* Command byte */
	DWORD arg		/* Argument */
)
{
	BYTE n, res;


	if (cmd & 0x80) {	/* ACMD<n> is the command sequence of CMD55-CMD<n> */
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if (res > 1) return res;
	}

	/* Select the card and wait for ready */
	DESELECT();
	SELECT();
	if (wait_ready() != 0xFF) return 0xFF;

	/* Send command packet */
	xmit_spi(cmd);						/* Start + Command index */
	xmit_spi((BYTE)(arg >> 24));		/* Argument[31..24] */
	xmit_spi((BYTE)(arg >> 16));		/* Argument[23..16] */
	xmit_spi((BYTE)(arg >> 8));			/* Argument[15..8] */
	xmit_spi((BYTE)arg);				/* Argument[7..0] */
	n = 0x01;							/* Dummy CRC + Stop */
	if (cmd == CMD0) n = 0x95;			/* Valid CRC for CMD0(0) */
	if (cmd == CMD8) n = 0x87;			/* Valid CRC for CMD8(0x1AA) */
	xmit_spi(n);

	/* Receive command response */
	if (cmd == CMD12) rcvr_spi();		/* Skip a stuff byte when stop reading */
	n = 10;								/* Wait for a valid response in timeout of 10 attempts */
	do
		res = rcvr_spi();
	while ((res & 0x80) && --n);

	return res;			/* Return with the response value */
}











/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

//DSTATUS disk_initialize (
//	BYTE drv		/* Physical drive number (0) */
//)
//{
//
//	if (drv) return STA_NOINIT;			/* Supports only single drive */
//		if (Stat & STA_NODISK) return Stat;	/* No card in the socket */
//
//	if (SD_init() == 0) {
//		Stat &= ~STA_NOINIT;
//	}
//	return Stat;
//}


DSTATUS disk_initialize (
	BYTE drv		/* Physical drive number (0) */
)
{
	BYTE n, cmd, ty, ocr[4];


	if (drv) return STA_NOINIT;			/* Supports only single drive */
	if (Stat & STA_NODISK) return Stat;	/* No card in the socket */

	//power_on();	/* Force socket power on */
	// power on not work (

	spi_init();

	FCLK_SLOW();

	for (n = 10; n; n--) rcvr_spi();	/* 80 dummy clocks */

	ty = 0;
	if (send_cmd(CMD0, 0) == 1) {			/* Enter Idle state */
		Timer1 = 100;						/* Initialization timeout of 1000 msec */
		if (send_cmd(CMD8, 0x1AA) == 1) {	/* SDHC */
			for (n = 0; n < 4; n++) ocr[n] = rcvr_spi();		/* Get trailing return value of R7 resp */
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {				/* The card can work at vdd range of 2.7-3.6V */
				while (Timer1 && send_cmd(ACMD41, 1UL << 30));	/* Wait for leaving idle state (ACMD41 with HCS bit) */
				if (Timer1 && send_cmd(CMD58, 0) == 0) {		/* Check CCS bit in the OCR */
					for (n = 0; n < 4; n++) ocr[n] = rcvr_spi();
					ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;
				}
			}
		} else {							/* SDSC or MMC */
			if (send_cmd(ACMD41, 0) <= 1) 	{
				ty = CT_SD1; cmd = ACMD41;	/* SDSC */
			} else {
				ty = CT_MMC; cmd = CMD1;	/* MMC */
			}
			while (Timer1 && send_cmd(cmd, 0));			/* Wait for leaving idle state */
			if (!Timer1 || send_cmd(CMD16, 512) != 0)	/* Set R/W block length to 512 */
				ty = 0;
		}
	}
	CardType = ty;
	release_spi();

	if (ty) {			/* Initialization succeeded */
		Stat &= ~STA_NOINIT;		/* Clear STA_NOINIT */
		//FCLK_FAST();
	} else {			/* Initialization failed */
		//power_off();
	}

	return Stat;
}




/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE drv		/* Physical drive number (0) */
)
{
	if (drv) return STA_NOINIT;		/* Supports only single drive */
	return Stat;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE drv,			/* Physical drive number (0) */
	BYTE *buff,			/* Pointer to the data buffer to store read data */
	DWORD sector,		/* Start sector number (LBA) */
	BYTE count			/* Sector count (1..255) */
)
{
	if (drv || !count) return RES_PARERR;
	if (Stat & STA_NOINIT) return RES_NOTRDY;

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* Convert to byte address if needed */

	if (count == 1) {	/* Single block read */
		if ((send_cmd(CMD17, sector) == 0)	/* READ_SINGLE_BLOCK */
			&& (rcvr_datablock(buff, 512) == 1))
			count = 0;
	}
	else {				/* Multiple block read */
		if (send_cmd(CMD18, sector) == 0) {	/* READ_MULTIPLE_BLOCK */
			do {
				if (rcvr_datablock(buff, 512) == 0) break;
				buff += 512;
			} while (--count);
			send_cmd(CMD12, 0);				/* STOP_TRANSMISSION */
		}
	}
	release_spi();

	return count ? RES_ERROR : RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if _READONLY == 0
DRESULT disk_write (
	BYTE drv,			/* Physical drive number (0) */
	const BYTE *buff,	/* Pointer to the data to be written */
	DWORD sector,		/* Start sector number (LBA) */
	BYTE count			/* Sector count (1..255) */
)
{
	if (drv || !count) return RES_PARERR;
	if (Stat & STA_NOINIT) return RES_NOTRDY;
	if (Stat & STA_PROTECT) return RES_WRPRT;

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* Convert to byte address if needed */

	if (count == 1) {	/* Single block write */
		if ((send_cmd(CMD24, sector) == 0)	/* WRITE_BLOCK */
			&& (xmit_datablock(buff, 0xFE) == 1))
			count = 0;
	}
	else {				/* Multiple block write */
		if (CardType & CT_SDC) send_cmd(ACMD23, count);
		if (send_cmd(CMD25, sector) == 0) {	/* WRITE_MULTIPLE_BLOCK */
			do {
				if (xmit_datablock(buff, 0xFC) == 0) break;
				buff += 512;
			} while (--count);
			if (xmit_datablock(0, 0xFD) == 0)	/* STOP_TRAN token */
				count = 1;
		}
	}
	release_spi();

	return count ? RES_ERROR : RES_OK;
}
#endif /* _READONLY == 0 */



/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL != 0
DRESULT disk_ioctl (
	BYTE drv,		/* Physical drive number (0) */
	BYTE ctrl,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res;
	BYTE n, csd[16], *ptr = buff;
	WORD csize;


	if (drv) return RES_PARERR;

	res = RES_ERROR;

	if (ctrl == CTRL_POWER) {
		switch (*ptr) {
		case 0:		/* Sub control code == 0 (POWER_OFF) */
			if (chk_power())
				power_off();		/* Power off */
			res = RES_OK;
			break;
		case 1:		/* Sub control code == 1 (POWER_ON) */
			power_on();				/* Power on */
			res = RES_OK;
			break;
		case 2:		/* Sub control code == 2 (POWER_GET) */
			*(ptr+1) = (BYTE)chk_power();
			res = RES_OK;
			break;
		default :
			res = RES_PARERR;
		}
	}
	else {
		if (Stat & STA_NOINIT) return RES_NOTRDY;

		switch (ctrl) {
		case CTRL_SYNC :		/* Make sure that no pending write process */
			SELECT();
			if (wait_ready() == 0xFF)
				res = RES_OK;
			break;

		case GET_SECTOR_COUNT :	/* Get number of sectors on the disk (DWORD) */
			if ((send_cmd(CMD9, 0) == 0) && (rcvr_datablock(csd, 16) == 1)) {
				if ((csd[0] >> 6) == 1) {	/* SDC ver 2.00 */
					csize = csd[9] + ((WORD)csd[8] << 8) + 1;
					*(DWORD*)buff = (DWORD)csize << 10;
				} else {					/* SDC ver 1.XX or MMC*/
					n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
					csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
					*(DWORD*)buff = (DWORD)csize << (n - 9);
				}
				res = RES_OK;
			}
			break;

		case GET_SECTOR_SIZE :	/* Get R/W sector size (WORD) */
			*(WORD*)buff = 512;
			res = RES_OK;
			break;

		case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (DWORD) */
			if (CardType & CT_SD2) {	/* SDC ver 2.00 */
				if (send_cmd(ACMD13, 0) == 0) {	/* Read SD status */
					rcvr_spi();
					if (rcvr_datablock(csd, 16) == 1) {				/* Read partial block */
						for (n = 64 - 16; n; n--) rcvr_spi();	/* Purge trailing data */
						*(DWORD*)buff = 16UL << (csd[10] >> 4);
						res = RES_OK;
					}
				}
			} else {					/* SDC ver 1.XX or MMC */
				if ((send_cmd(CMD9, 0) == 0) && (rcvr_datablock(csd, 16) == 1)) {	/* Read CSD */
					if (CardType & CT_SD1) {	/* SDC ver 1.XX */
						*(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
					} else {					/* MMC */
						*(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
					}
					res = RES_OK;
				}
			}
			break;

		case MMC_GET_TYPE :		/* Get card type flags (1 byte) */
			*ptr = CardType;
			res = RES_OK;
			break;

		case MMC_GET_CSD :		/* Receive CSD as a data block (16 bytes) */
			if (send_cmd(CMD9, 0) == 0		/* READ_CSD */
				&& (rcvr_datablock(ptr, 16) == 1))
				res = RES_OK;
			break;

		case MMC_GET_CID :		/* Receive CID as a data block (16 bytes) */
			if (send_cmd(CMD10, 0) == 0		/* READ_CID */
				&& (rcvr_datablock(ptr, 16) == 1))
				res = RES_OK;
			break;

		case MMC_GET_OCR :		/* Receive OCR as an R3 resp (4 bytes) */
			if (send_cmd(CMD58, 0) == 0) {	/* READ_OCR */
				for (n = 4; n; n--) *ptr++ = rcvr_spi();
				res = RES_OK;
			}
			break;

		case MMC_GET_SDSTAT :	/* Receive SD status as a data block (64 bytes) */
			if (send_cmd(ACMD13, 0) == 0) {	/* SD_STATUS */
				rcvr_spi();
				if (rcvr_datablock(ptr, 64) == 1)
					res = RES_OK;
			}
			break;

		default:
			res = RES_PARERR;
		}

		release_spi();
	}

	return res;
}
#endif /* _USE_IOCTL != 0 */


/*-----------------------------------------------------------------------*/
/* Device Timer Interrupt Procedure  (Platform dependent)                */
/*-----------------------------------------------------------------------*/
/* This function must be called in period of 10ms                        */

RAMFUNC void disk_timerproc (void)
{
	static BYTE pv;
	BYTE n, s;

	n = Timer1;						/* 100Hz decrement timer */
	if (n) Timer1 = --n;
	n = Timer2;
	if (n) Timer2 = --n;

	n = pv;
	pv = SOCKPORT & (SOCKWP | SOCKINS);	/* Sample socket switch */

	if (n == pv) {					/* Have contacts stabled? */
		s = Stat;

		if (pv & SOCKWP)			/* WP is H (write protected) */
			s |= STA_PROTECT;
		else						/* WP is L (write enabled) */
			s &= ~STA_PROTECT;

		if (pv & SOCKINS)			/* INS = H (Socket empty) */
			s |= (STA_NODISK | STA_NOINIT);
		else						/* INS = L (Card inserted) */
			s &= ~STA_NODISK;

		Stat = s;
	}
}


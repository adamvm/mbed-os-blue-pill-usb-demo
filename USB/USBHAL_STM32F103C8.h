/* Copyright (c) 2016 mbed.org, MIT License
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of this software
* and associated documentation files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all copies or
* substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
* BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#ifndef USBHAL_STM32F103C8
#define USBHAL_STM32F103C8

#define USBHAL_IRQn  USB_LP_CAN1_RX0_IRQn


#define NB_ENDPOINT  8
/*  must be multiple of 4 bytes */
#define MAXTRANSFER_SIZE  0x200
#define FIFO_USB_RAM_SIZE (MAXTRANSFER_SIZE+MAX_PACKET_SIZE_EP0+MAX_PACKET_SIZE_EP1+MAX_PACKET_SIZE_EP2+MAX_PACKET_SIZE_EP3)
#if (FIFO_USB_RAM_SIZE > 0x500)
#error "FIFO dimensioning incorrect"
#endif

typedef struct
{
    USBHAL *inst;
    void (USBHAL::*bus_reset)(void);
    void (USBHAL::*sof)(int frame);
    void (USBHAL::*connect_change)(unsigned int  connected);
    void (USBHAL::*suspend_change)(unsigned int suspended);
    void (USBHAL::*ep0_setup)(void);
    void (USBHAL::*ep0_in)(void);
    void (USBHAL::*ep0_out)(void);
    void (USBHAL::*ep0_read)(void);
    bool (USBHAL::*ep_realise)(uint8_t endpoint, uint32_t maxPacket, uint32_t flags);
    bool (USBHAL::*epCallback[2*NB_ENDPOINT-2])(void);
    uint8_t epComplete[8];
    /*  memorize dummy buffer used for reception */
    uint32_t pBufRx[MAXTRANSFER_SIZE>>2];
    uint32_t pBufRx0[MAX_PACKET_SIZE_EP0>>2];
}USBHAL_Private_t;

uint32_t HAL_PCDEx_GetTxFiFo(PCD_HandleTypeDef *hpcd, uint8_t fifo)
{
    return 1024;
}
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
    USBHAL_Private_t *priv=((USBHAL_Private_t *)(hpcd->pData));
    USBHAL *obj= priv->inst;
    uint32_t sofnum = (hpcd->Instance->FNR) & USB_FNR_FN;
    void (USBHAL::*func)(int frame) = priv->sof;
    (obj->*func)(sofnum);
}

USBHAL * USBHAL::instance;

USBHAL::USBHAL(void)
{
    /*  init parameter  */
    USBHAL_Private_t *HALPriv = new(USBHAL_Private_t);
    /*  initialized all field of init including 0 field  */
    /*  constructor does not fill with zero */
    hpcd.Instance = USB;
    /*  initialized all field of init including 0 field  */
    /*  constructor does not fill with zero */
    memset(&hpcd.Init, 0, sizeof(hpcd.Init));
    hpcd.Init.dev_endpoints = NB_ENDPOINT;
    hpcd.Init.ep0_mps =   MAX_PACKET_SIZE_EP0;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd.Init.Sof_enable = 1;
    hpcd.Init.speed = PCD_SPEED_FULL;
    /*  pass instance for usage inside call back */
    HALPriv->inst = this;
    HALPriv->bus_reset = &USBHAL::busReset;
    HALPriv->suspend_change = &USBHAL::suspendStateChanged;
    HALPriv->connect_change = &USBHAL::connectStateChanged;
    HALPriv->sof = &USBHAL::SOF;
    HALPriv->ep0_setup = &USBHAL::EP0setupCallback;
    HALPriv->ep_realise = &USBHAL::realiseEndpoint;
    HALPriv->ep0_in = &USBHAL::EP0in;
    HALPriv->ep0_out = &USBHAL::EP0out;
    HALPriv->ep0_read = &USBHAL::EP0read;
    hpcd.pData = (void*)HALPriv;
    HALPriv->epCallback[0] = &USBHAL::EP1_OUT_callback;
    HALPriv->epCallback[1] = &USBHAL::EP1_IN_callback;
    HALPriv->epCallback[2] = &USBHAL::EP2_OUT_callback;
    HALPriv->epCallback[3] = &USBHAL::EP2_IN_callback;
    HALPriv->epCallback[4] = &USBHAL::EP3_OUT_callback;
    HALPriv->epCallback[5] = &USBHAL::EP3_IN_callback;
    instance = this;

    /* Configure USB Pins:
     * - USB-DP (D+ of the USB connector) <======> PA12 (Blue pill board)
     * - USB-DM (D- of the USB connector) <======> PA11 (Blue pill board)
     *
     * Note: as Blue Pill doesn't reset usb after board reset, we should manuall
     * pull these pind to the ground begore initialization, that will cause usb enumeration
     * on the host side.
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    pin_function(PA_11, STM_PIN_DATA(STM_MODE_OUTPUT_PP, 0, 0));
    pin_function(PA_12, STM_PIN_DATA(STM_MODE_OUTPUT_PP, 0, 0));
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12 | GPIO_PIN_11, GPIO_PIN_RESET);
    wait_ms(50);
    pin_function(PA_11, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_MODE_AF_INPUT));
    pin_function(PA_12, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_MODE_AF_INPUT));

    __HAL_RCC_USB_CLK_ENABLE();

    /* usb clock frequcy validation */
    const uint32_t expectedUsbFreq = 48000000;
    uint32_t usbFreq = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USB);
    if (usbFreq != expectedUsbFreq) {
        error("Wrong usb clock frequncy %d Hz. It must be %d Hz", usbFreq, expectedUsbFreq);
    }

    hpcd.State = HAL_PCD_STATE_RESET;

    HAL_PCD_Init(&hpcd);
    /* hardcoded size of FIFO according definition*/
    HAL_PCDEx_PMAConfig(&hpcd , 0x00 , PCD_SNG_BUF, 0x30);
    HAL_PCDEx_PMAConfig(&hpcd , 0x80 , PCD_SNG_BUF, 0x70);
    HAL_PCDEx_PMAConfig(&hpcd , 0x01 , PCD_SNG_BUF, 0x90);
    HAL_PCDEx_PMAConfig(&hpcd , 0x81 , PCD_SNG_BUF, 0xb0);
    HAL_PCDEx_PMAConfig(&hpcd , 0x2, PCD_SNG_BUF, 0x100);
    HAL_PCDEx_PMAConfig(&hpcd , 0x82, PCD_SNG_BUF, 0x120);

    NVIC_SetVector(USBHAL_IRQn,(uint32_t)&_usbisr);
    NVIC_SetPriority( USBHAL_IRQn, 1);

    HAL_PCD_Start(&hpcd);
}

#endif

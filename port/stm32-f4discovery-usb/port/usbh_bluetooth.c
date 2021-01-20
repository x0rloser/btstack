/*
 * Copyright (C) 2020 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "usbh_bluetooth.c"

#include "usbh_bluetooth.h"

#define log_info puts

typedef struct {
    uint8_t acl_in_ep;
    uint8_t acl_in_pipe;
    uint16_t acl_in_len;
    uint8_t acl_out_ep;
    uint8_t acl_out_pipe;
    uint16_t acl_out_len;
    uint8_t event_in_ep;
    uint8_t event_in_pipe;
    uint16_t event_in_len;
    uint32_t event_in_frame
} USB_Bluetooth_t;

static USB_Bluetooth_t usb_bluetooth;

static const uint8_t hci_reset[] = { 0x03, 0x0c, 0x00};
static uint8_t hci_event[258];

USBH_StatusTypeDef USBH_Bluetooth_InterfaceInit(USBH_HandleTypeDef *phost){
    log_info("USBH_Bluetooth_InterfaceInit");

    // dump everything
    uint8_t interface_index = 0;
    USBH_InterfaceDescTypeDef * interface = &phost->device.CfgDesc.Itf_Desc[interface_index];
    uint8_t num_endpoints = interface->bNumEndpoints;
    uint8_t ep_index;
    int16_t acl_in   = -1;
    int16_t acl_out  = -1;
    int16_t event_in = -1;
    for (ep_index=0;ep_index<num_endpoints;ep_index++){
        USBH_EpDescTypeDef * ep_desc = &interface->Ep_Desc[ep_index];
        printf("Interface %u, endpoint #%u: address 0x%02x, attributes 0x%02x, packet size %u, poll %u\n",
               interface_index, ep_index, ep_desc->bEndpointAddress, ep_desc->bmAttributes, ep_desc->wMaxPacketSize, ep_desc->bInterval);
        // type interrupt, direction incoming
        if  (((ep_desc->bEndpointAddress & USB_EP_DIR_MSK) == USB_EP_DIR_MSK) && (ep_desc->bmAttributes == USB_EP_TYPE_INTR)){
            event_in = ep_index;
            puts("-> HCI Event");
        }
        // type bulk, direction incoming
        if  (((ep_desc->bEndpointAddress & USB_EP_DIR_MSK) == USB_EP_DIR_MSK) && (ep_desc->bmAttributes == USB_EP_TYPE_BULK)){
            acl_in = ep_index;
            puts("-> HCI ACL IN");
        }
        // type bulk, direction incoming
        if  (((ep_desc->bEndpointAddress & USB_EP_DIR_MSK) == 0) && (ep_desc->bmAttributes == USB_EP_TYPE_BULK)){
            acl_out = ep_index;
            puts("-> HCI ACL OUT");
        }
    }

    // all found
    if ((acl_in < 0) && (acl_out < 0) && (event_in < 0)) {
        log_info("Could not find all endpoints");
        return USBH_FAIL;
    }

    // setup
    memset(&usb_bluetooth, 0, sizeof(USB_Bluetooth_t));
    phost->pActiveClass->pData = (void*) &usb_bluetooth;

    // Event In
    USB_Bluetooth_t * usb = &usb_bluetooth;
    usb->event_in_ep =   interface->Ep_Desc[event_in].bEndpointAddress;
    usb->event_in_pipe = USBH_AllocPipe(phost, usb->event_in_ep);

    /* Open pipe for IN endpoint */
    USBH_OpenPipe(phost, usb->event_in_pipe, usb->event_in_ep, phost->device.address,
                  phost->device.speed, USB_EP_TYPE_INTR, interface->Ep_Desc[event_in].wMaxPacketSize);

    USBH_LL_SetToggle(phost, usb->event_in_ep, 0U);

    return USBH_OK;
}

USBH_StatusTypeDef USBH_Bluetooth_InterfaceDeInit(USBH_HandleTypeDef *phost){
    log_info("USBH_Bluetooth_InterfaceDeInit");
    return USBH_OK;
}
USBH_StatusTypeDef USBH_Bluetooth_ClassRequest(USBH_HandleTypeDef *phost){
    log_info("USBH_Bluetooth_ClassRequest");
    return USBH_OK;
}
USBH_StatusTypeDef USBH_Bluetooth_Process(USBH_HandleTypeDef *phost){
    //  log_info("USBH_Bluetooth_ClassRequest");
    static int state = 0;
    USBH_StatusTypeDef status;
    USBH_URBStateTypeDef urb_state;
    USB_Bluetooth_t * usb = (USB_Bluetooth_t *) phost->pActiveClass->pData;
    switch (state){
        case 0:
            // just send HCI Reset naively
            phost->Control.setup.b.bmRequestType = USB_H2D | USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_CLASS;
            phost->Control.setup.b.bRequest = 0;
            phost->Control.setup.b.wValue.w = 0;
            phost->Control.setup.b.wIndex.w = 0U;
            phost->Control.setup.b.wLength.w = sizeof(hci_reset);
            status = USBH_CtlReq(phost, (uint8_t *)  hci_reset, sizeof(hci_reset));
            if (status == USBH_OK) {
                puts("HCI Reset Sent");
                state++;
            }
            return USBH_BUSY;
        case 1:
            // schedule interrupt transfer
            USBH_InterruptReceiveData(phost, hci_event, (uint8_t) sizeof(hci_event), usb->event_in_pipe);
            usb->event_in_frame = phost->Timer;
            state++;
            return USBH_BUSY;
        case 2:
            // poll URB state
            urb_state = USBH_LL_GetURBState(phost, usb->event_in_pipe);
            switch (urb_state){
                case USBH_URB_DONE:
                    state++;
                    puts("Data received");
                    return USBH_OK;
                default:
                    break;
            }
            if ((phost->Timer - usb->event_in_frame) > 2){
                // restart request
                state = 1;
            }
            return USBH_BUSY;
        default:
            return USBH_OK;
    }
    return USBH_OK;
}

USBH_StatusTypeDef USBH_Bluetooth_SOFProcess(USBH_HandleTypeDef *phost){
    // log_info("USBH_Bluetooth_SOFProcess");
    return USBH_OK;
}

USBH_ClassTypeDef  Bluetooth_Class = {
    "Bluetooth",
    USB_BLUETOOTH_CLASS,
    USBH_Bluetooth_InterfaceInit,
    USBH_Bluetooth_InterfaceDeInit,
    USBH_Bluetooth_ClassRequest,
    USBH_Bluetooth_Process,
    USBH_Bluetooth_SOFProcess,
    NULL,
};


/**
 * @file usb_dwc2.c
 * @brief Synopsys DWC2 USB Host Controller Driver for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements USB Host mode for the DesignWare Core USB 2.0 controller
 * on BCM2837. Based on the DWC2 programming guide and USPi/Circle drivers.
 *
 * Features:
 * - USB Host mode initialization
 * - Device enumeration (full-speed, low-speed)
 * - Control and interrupt transfers (polling mode, no DMA)
 * - HID keyboard support (boot protocol)
 */

#include "usb_dwc2.h"
#include "usb_hid.h"
#include "mailbox.h"
#include "timer.h"
#include "uart.h"

#include <string.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* FIFO sizes (in 32-bit words) */
#define USB_RX_FIFO_SIZE        256     /* 1024 bytes */
#define USB_NPTX_FIFO_SIZE      128     /* 512 bytes */
#define USB_PTX_FIFO_SIZE       128     /* 512 bytes */

/* Timeouts (in microseconds) */
#define USB_RESET_TIMEOUT       100000  /* 100ms for reset */
#define USB_XFER_TIMEOUT        1000000 /* 1s for transfers */
#define USB_CONNECT_TIMEOUT     5000000 /* 5s for device connect */

/* Maximum transfer retries */
#define USB_MAX_RETRIES         3

/* ============================================================================
 * DRIVER STATE
 * ============================================================================ */

typedef enum {
    USB_STATE_UNINITIALIZED,
    USB_STATE_POWERED,
    USB_STATE_CONNECTED,
    USB_STATE_RESET,
    USB_STATE_ADDRESSED,
    USB_STATE_CONFIGURED,
    USB_STATE_ERROR
} usb_state_t;

typedef struct {
    usb_state_t state;
    uint8_t     device_address;
    uint8_t     max_packet_size;
    uint8_t     device_speed;       /* 0=HS, 1=FS, 2=LS */
    uint16_t    vendor_id;
    uint16_t    product_id;

    /* HID keyboard info */
    int         has_keyboard;
    uint8_t     kbd_interface;
    uint8_t     kbd_endpoint;
    uint8_t     kbd_interval;

    /* Polling state */
    uint64_t    last_poll;
    int         poll_pending;
} usb_driver_t;

static usb_driver_t g_usb = {0};

/* Data buffer for transfers (must be 32-bit aligned) */
static uint32_t __attribute__((aligned(4))) g_xfer_buffer[256];

/* ============================================================================
 * LOW-LEVEL REGISTER ACCESS
 * ============================================================================ */

static inline uint32_t usb_read(uintptr_t reg)
{
    return mmio_read32(reg);
}

static inline void usb_write(uintptr_t reg, uint32_t val)
{
    mmio_write32(reg, val);
}

static inline void usb_delay_us(uint32_t us)
{
    timer_delay_us(us);
}

static inline uint64_t usb_get_time(void)
{
    return timer_get_us();
}

/* ============================================================================
 * FIFO OPERATIONS
 * ============================================================================ */

/**
 * @brief Write data to TX FIFO (for control/interrupt OUT)
 */
static void usb_fifo_write(int channel, const void *data, int len)
{
    const uint32_t *src = (const uint32_t *)data;
    volatile uint32_t *fifo = (volatile uint32_t *)USB_FIFO(channel);
    int words = (len + 3) / 4;

    for (int i = 0; i < words; i++) {
        *fifo = src[i];
    }
}

/**
 * @brief Read data from RX FIFO
 */
static void usb_fifo_read(int channel, void *data, int len)
{
    uint32_t *dst = (uint32_t *)data;
    volatile uint32_t *fifo = (volatile uint32_t *)USB_FIFO(channel);
    int words = (len + 3) / 4;

    for (int i = 0; i < words; i++) {
        dst[i] = *fifo;
    }
}

/* ============================================================================
 * CHANNEL MANAGEMENT
 * ============================================================================ */

/**
 * @brief Wait for channel to halt (with timeout)
 */
static int usb_wait_channel_halt(int channel, uint32_t timeout_us)
{
    uint64_t start = usb_get_time();

    while ((usb_get_time() - start) < timeout_us) {
        uint32_t hcint = usb_read(USB_HCINT(channel));
        if (hcint & USB_HCINT_CHHLTD) {
            return 0;
        }
        usb_delay_us(10);
    }

    return -1;  /* Timeout */
}

/**
 * @brief Halt a channel
 */
static void usb_halt_channel(int channel)
{
    uint32_t hcchar = usb_read(USB_HCCHAR(channel));

    if (!(hcchar & USB_HCCHAR_CHENA)) {
        return;  /* Already disabled */
    }

    /* Disable channel */
    hcchar |= USB_HCCHAR_CHDIS | USB_HCCHAR_CHENA;
    usb_write(USB_HCCHAR(channel), hcchar);

    /* Wait for halt */
    usb_wait_channel_halt(channel, 10000);

    /* Clear all interrupts */
    usb_write(USB_HCINT(channel), 0xFFFFFFFF);
}

/* ============================================================================
 * CONTROL TRANSFERS
 * ============================================================================ */

/**
 * @brief Send control transfer (setup + optional data + status)
 *
 * @param addr Device address
 * @param setup Setup packet (8 bytes)
 * @param data Data buffer (can be NULL)
 * @param len Data length
 * @return Bytes transferred, or negative error code
 */
static int usb_control_transfer(uint8_t addr, const usb_setup_t *setup,
                                 void *data, uint16_t len)
{
    int channel = 0;  /* Use channel 0 for control transfers */
    uint32_t hcchar, hctsiz, hcint;
    int direction_in = (setup->bmRequestType & USB_RT_DIR_IN) ? 1 : 0;
    int retries;

    /* Determine max packet size */
    uint16_t mps = g_usb.max_packet_size;
    if (mps == 0) mps = 8;  /* Default for initial enumeration */

    /* === SETUP PHASE === */
    retries = USB_MAX_RETRIES;
    while (retries-- > 0) {
        /* Clear any pending interrupts */
        usb_write(USB_HCINT(channel), 0xFFFFFFFF);

        /* Configure channel for SETUP */
        hcchar = USB_HCCHAR_MPS(8) |                /* Setup is always 8 bytes */
                 USB_HCCHAR_EPNUM(0) |              /* Endpoint 0 */
                 USB_HCCHAR_EPTYPE_CTRL |           /* Control endpoint */
                 USB_HCCHAR_DEVADDR(addr);
        if (g_usb.device_speed == 2) {
            hcchar |= USB_HCCHAR_LSDEV;             /* Low-speed device */
        }
        usb_write(USB_HCCHAR(channel), hcchar);

        /* Set transfer size: 8 bytes, 1 packet, SETUP PID */
        hctsiz = USB_HCTSIZ_XFERSIZE(8) |
                 USB_HCTSIZ_PKTCNT(1) |
                 USB_HCTSIZ_PID_SETUP;
        usb_write(USB_HCTSIZ(channel), hctsiz);

        /* Write setup packet to FIFO */
        usb_fifo_write(channel, setup, 8);

        /* Enable channel */
        hcchar |= USB_HCCHAR_CHENA;
        usb_write(USB_HCCHAR(channel), hcchar);

        /* Wait for completion */
        if (usb_wait_channel_halt(channel, USB_XFER_TIMEOUT) < 0) {
            uart_puts("USB: SETUP timeout\n");
            usb_halt_channel(channel);
            continue;
        }

        hcint = usb_read(USB_HCINT(channel));
        usb_write(USB_HCINT(channel), hcint);

        if (hcint & USB_HCINT_XFERCOMPL) {
            break;  /* Success */
        }

        if (hcint & USB_HCINT_STALL) {
            return -2;  /* STALL - endpoint halted */
        }

        if (hcint & USB_HCINT_ERROR_MASK) {
            usb_delay_us(1000);  /* Retry delay */
            continue;
        }
    }

    if (retries < 0) {
        return -1;  /* SETUP failed */
    }

    /* === DATA PHASE (if any) === */
    if (len > 0 && data != NULL) {
        uint8_t *buf = (uint8_t *)data;
        uint16_t remaining = len;
        uint16_t transferred = 0;
        int pid = 1;  /* Start with DATA1 */

        while (remaining > 0) {
            uint16_t xfer_len = (remaining > mps) ? mps : remaining;
            int pkt_count = 1;

            retries = USB_MAX_RETRIES;
            while (retries-- > 0) {
                usb_write(USB_HCINT(channel), 0xFFFFFFFF);

                /* Configure channel for data */
                hcchar = USB_HCCHAR_MPS(mps) |
                         USB_HCCHAR_EPNUM(0) |
                         USB_HCCHAR_EPTYPE_CTRL |
                         USB_HCCHAR_DEVADDR(addr);
                if (direction_in) {
                    hcchar |= USB_HCCHAR_EPDIR;
                }
                if (g_usb.device_speed == 2) {
                    hcchar |= USB_HCCHAR_LSDEV;
                }
                usb_write(USB_HCCHAR(channel), hcchar);

                /* Set transfer size */
                hctsiz = USB_HCTSIZ_XFERSIZE(xfer_len) |
                         USB_HCTSIZ_PKTCNT(pkt_count) |
                         (pid ? USB_HCTSIZ_PID_DATA1 : USB_HCTSIZ_PID_DATA0);
                usb_write(USB_HCTSIZ(channel), hctsiz);

                /* Write OUT data to FIFO if needed */
                if (!direction_in) {
                    usb_fifo_write(channel, buf + transferred, xfer_len);
                }

                /* Enable channel */
                hcchar |= USB_HCCHAR_CHENA;
                usb_write(USB_HCCHAR(channel), hcchar);

                /* Wait for completion */
                if (usb_wait_channel_halt(channel, USB_XFER_TIMEOUT) < 0) {
                    usb_halt_channel(channel);
                    continue;
                }

                hcint = usb_read(USB_HCINT(channel));
                usb_write(USB_HCINT(channel), hcint);

                if (hcint & USB_HCINT_XFERCOMPL) {
                    /* Read IN data from FIFO */
                    if (direction_in) {
                        uint32_t actual = (usb_read(USB_HCTSIZ(channel)) &
                                           USB_HCTSIZ_XFERSIZE_MASK);
                        actual = xfer_len - actual;
                        usb_fifo_read(channel, buf + transferred, actual);
                        xfer_len = actual;
                    }
                    break;  /* Success */
                }

                if (hcint & USB_HCINT_STALL) {
                    return -2;
                }

                if (hcint & USB_HCINT_NAK) {
                    usb_delay_us(100);
                    continue;
                }

                if (hcint & USB_HCINT_ERROR_MASK) {
                    usb_delay_us(1000);
                    continue;
                }
            }

            if (retries < 0) {
                return -1;
            }

            transferred += xfer_len;
            remaining -= xfer_len;
            pid ^= 1;  /* Toggle DATA PID */
        }
    }

    /* === STATUS PHASE === */
    retries = USB_MAX_RETRIES;
    while (retries-- > 0) {
        usb_write(USB_HCINT(channel), 0xFFFFFFFF);

        /* Status direction is opposite of data direction */
        int status_in = !direction_in || (len == 0);

        hcchar = USB_HCCHAR_MPS(mps) |
                 USB_HCCHAR_EPNUM(0) |
                 USB_HCCHAR_EPTYPE_CTRL |
                 USB_HCCHAR_DEVADDR(addr);
        if (status_in) {
            hcchar |= USB_HCCHAR_EPDIR;
        }
        if (g_usb.device_speed == 2) {
            hcchar |= USB_HCCHAR_LSDEV;
        }
        usb_write(USB_HCCHAR(channel), hcchar);

        /* Zero-length packet with DATA1 PID */
        hctsiz = USB_HCTSIZ_XFERSIZE(0) |
                 USB_HCTSIZ_PKTCNT(1) |
                 USB_HCTSIZ_PID_DATA1;
        usb_write(USB_HCTSIZ(channel), hctsiz);

        /* Enable channel */
        hcchar |= USB_HCCHAR_CHENA;
        usb_write(USB_HCCHAR(channel), hcchar);

        if (usb_wait_channel_halt(channel, USB_XFER_TIMEOUT) < 0) {
            usb_halt_channel(channel);
            continue;
        }

        hcint = usb_read(USB_HCINT(channel));
        usb_write(USB_HCINT(channel), hcint);

        if (hcint & USB_HCINT_XFERCOMPL) {
            return len;  /* Success - return requested length */
        }

        if (hcint & USB_HCINT_STALL) {
            return -2;
        }

        if (hcint & USB_HCINT_NAK) {
            usb_delay_us(100);
            continue;
        }

        usb_delay_us(1000);
    }

    return -1;  /* Status phase failed */
}

/* ============================================================================
 * INTERRUPT TRANSFERS
 * ============================================================================ */

/**
 * @brief Perform interrupt IN transfer (for HID keyboards)
 *
 * @param addr Device address
 * @param ep Endpoint number (with direction bit)
 * @param data Buffer to receive data
 * @param len Maximum buffer length
 * @return Bytes received, or negative error code
 */
int usb_interrupt_transfer(uint8_t addr, uint8_t ep, void *data, uint16_t len)
{
    int channel = 1;  /* Use channel 1 for interrupt transfers */
    uint32_t hcchar, hctsiz, hcint;
    int ep_num = ep & 0x7F;
    int direction_in = (ep & 0x80) ? 1 : 0;
    uint16_t mps = 8;  /* HID keyboard max packet size */
    static int data_toggle = 0;  /* Persist across calls */

    /* Clear interrupts */
    usb_write(USB_HCINT(channel), 0xFFFFFFFF);

    /* Configure channel */
    hcchar = USB_HCCHAR_MPS(mps) |
             USB_HCCHAR_EPNUM(ep_num) |
             USB_HCCHAR_EPTYPE_INTR |
             USB_HCCHAR_DEVADDR(addr);
    if (direction_in) {
        hcchar |= USB_HCCHAR_EPDIR;
    }
    if (g_usb.device_speed == 2) {
        hcchar |= USB_HCCHAR_LSDEV;
    }
    usb_write(USB_HCCHAR(channel), hcchar);

    /* Set transfer size */
    hctsiz = USB_HCTSIZ_XFERSIZE(len) |
             USB_HCTSIZ_PKTCNT(1) |
             (data_toggle ? USB_HCTSIZ_PID_DATA1 : USB_HCTSIZ_PID_DATA0);
    usb_write(USB_HCTSIZ(channel), hctsiz);

    /* Enable channel */
    hcchar |= USB_HCCHAR_CHENA;
    usb_write(USB_HCCHAR(channel), hcchar);

    /* Wait for completion (short timeout for polling) */
    if (usb_wait_channel_halt(channel, 50000) < 0) {  /* 50ms */
        usb_halt_channel(channel);
        return -1;
    }

    hcint = usb_read(USB_HCINT(channel));
    usb_write(USB_HCINT(channel), hcint);

    if (hcint & USB_HCINT_XFERCOMPL) {
        /* Calculate actual bytes received */
        uint32_t actual = len - (usb_read(USB_HCTSIZ(channel)) &
                                 USB_HCTSIZ_XFERSIZE_MASK);

        /* Read data from FIFO */
        if (actual > 0) {
            usb_fifo_read(channel, data, actual);
        }

        /* Toggle DATA PID */
        data_toggle ^= 1;

        return actual;
    }

    if (hcint & USB_HCINT_NAK) {
        return 0;  /* No data available */
    }

    if (hcint & USB_HCINT_STALL) {
        data_toggle = 0;  /* Reset toggle on STALL */
        return -2;
    }

    return -1;  /* Error */
}

/* ============================================================================
 * DEVICE ENUMERATION
 * ============================================================================ */

/**
 * @brief Get device descriptor
 */
static int usb_get_device_descriptor(uint8_t addr, usb_device_desc_t *desc)
{
    usb_setup_t setup = {
        .bmRequestType = USB_RT_DIR_IN | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_TYPE_DEVICE << 8),
        .wIndex = 0,
        .wLength = sizeof(usb_device_desc_t)
    };

    return usb_control_transfer(addr, &setup, desc, sizeof(usb_device_desc_t));
}

/**
 * @brief Get configuration descriptor (full)
 */
static int usb_get_config_descriptor(uint8_t addr, uint8_t index,
                                      void *buf, uint16_t len)
{
    usb_setup_t setup = {
        .bmRequestType = USB_RT_DIR_IN | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_TYPE_CONFIG << 8) | index,
        .wIndex = 0,
        .wLength = len
    };

    return usb_control_transfer(addr, &setup, buf, len);
}

/**
 * @brief Set device address
 */
static int usb_set_address(uint8_t addr)
{
    usb_setup_t setup = {
        .bmRequestType = USB_RT_DIR_OUT | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        .bRequest = USB_REQ_SET_ADDRESS,
        .wValue = addr,
        .wIndex = 0,
        .wLength = 0
    };

    return usb_control_transfer(0, &setup, NULL, 0);
}

/**
 * @brief Set device configuration
 */
static int usb_set_configuration(uint8_t addr, uint8_t config)
{
    usb_setup_t setup = {
        .bmRequestType = USB_RT_DIR_OUT | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        .bRequest = USB_REQ_SET_CONFIGURATION,
        .wValue = config,
        .wIndex = 0,
        .wLength = 0
    };

    return usb_control_transfer(addr, &setup, NULL, 0);
}

/**
 * @brief Set HID boot protocol
 */
static int usb_hid_set_protocol(uint8_t addr, uint8_t interface, uint8_t protocol)
{
    usb_setup_t setup = {
        .bmRequestType = USB_RT_DIR_OUT | USB_RT_TYPE_CLASS | USB_RT_RECIP_INTERFACE,
        .bRequest = USB_HID_REQ_SET_PROTOCOL,
        .wValue = protocol,  /* 0 = boot protocol, 1 = report protocol */
        .wIndex = interface,
        .wLength = 0
    };

    return usb_control_transfer(addr, &setup, NULL, 0);
}

/**
 * @brief Set HID idle rate (for keyboards)
 */
static int usb_hid_set_idle(uint8_t addr, uint8_t interface, uint8_t duration)
{
    usb_setup_t setup = {
        .bmRequestType = USB_RT_DIR_OUT | USB_RT_TYPE_CLASS | USB_RT_RECIP_INTERFACE,
        .bRequest = USB_HID_REQ_SET_IDLE,
        .wValue = (duration << 8),  /* Duration in 4ms units, report ID 0 */
        .wIndex = interface,
        .wLength = 0
    };

    return usb_control_transfer(addr, &setup, NULL, 0);
}

/**
 * @brief Parse configuration descriptor for HID keyboard
 */
static int usb_parse_config(const uint8_t *config, uint16_t len)
{
    const uint8_t *p = config;
    const uint8_t *end = config + len;

    /* Skip config descriptor */
    if (p[1] != USB_DESC_TYPE_CONFIG) {
        return -1;
    }
    p += p[0];

    /* Parse interfaces */
    while (p < end) {
        if (p[0] == 0) break;  /* Invalid descriptor */

        if (p[1] == USB_DESC_TYPE_INTERFACE) {
            const usb_interface_desc_t *iface = (const usb_interface_desc_t *)p;

            /* Check for HID keyboard (boot protocol) */
            if (iface->bInterfaceClass == USB_CLASS_HID &&
                iface->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT &&
                iface->bInterfaceProtocol == USB_HID_PROTOCOL_KEYBOARD) {

                uart_printf("USB: Found HID keyboard (interface %d)\n",
                            iface->bInterfaceNumber);

                g_usb.kbd_interface = iface->bInterfaceNumber;

                /* Look for endpoint descriptor */
                p += p[0];
                while (p < end && p[1] != USB_DESC_TYPE_INTERFACE) {
                    if (p[1] == USB_DESC_TYPE_ENDPOINT) {
                        const usb_endpoint_desc_t *ep =
                            (const usb_endpoint_desc_t *)p;

                        if ((ep->bmAttributes & 0x03) == USB_EP_TYPE_INTERRUPT &&
                            (ep->bEndpointAddress & 0x80)) {
                            /* Interrupt IN endpoint */
                            g_usb.kbd_endpoint = ep->bEndpointAddress;
                            g_usb.kbd_interval = ep->bInterval;

                            uart_printf("USB: Keyboard EP 0x%02X, interval %d ms\n",
                                        ep->bEndpointAddress, ep->bInterval);

                            g_usb.has_keyboard = 1;
                            return 0;
                        }
                    }
                    p += p[0];
                }
                continue;
            }
        }

        p += p[0];
    }

    return -1;  /* No keyboard found */
}

/**
 * @brief Enumerate connected USB device
 */
static int usb_enumerate_device(void)
{
    usb_device_desc_t dev_desc;
    uint8_t config_buf[256];
    int result;

    uart_puts("USB: Enumerating device...\n");

    /* Step 1: Get first 8 bytes of device descriptor to learn max packet size */
    g_usb.max_packet_size = 8;  /* Start with minimum */

    result = usb_get_device_descriptor(0, &dev_desc);
    if (result < 8) {
        uart_printf("USB: Failed to get device descriptor (%d)\n", result);
        return -1;
    }

    g_usb.max_packet_size = dev_desc.bMaxPacketSize0;
    uart_printf("USB: Max packet size: %d\n", g_usb.max_packet_size);

    /* Step 2: Set address */
    g_usb.device_address = 1;

    result = usb_set_address(g_usb.device_address);
    if (result < 0) {
        uart_printf("USB: Failed to set address (%d)\n", result);
        return -1;
    }

    usb_delay_us(5000);  /* Device needs time to process SET_ADDRESS */
    g_usb.state = USB_STATE_ADDRESSED;

    uart_printf("USB: Device address set to %d\n", g_usb.device_address);

    /* Step 3: Get full device descriptor */
    result = usb_get_device_descriptor(g_usb.device_address, &dev_desc);
    if (result < (int)sizeof(dev_desc)) {
        uart_printf("USB: Failed to get full device descriptor (%d)\n", result);
        return -1;
    }

    g_usb.vendor_id = dev_desc.idVendor;
    g_usb.product_id = dev_desc.idProduct;

    uart_printf("USB: Device VID=%04X PID=%04X\n",
                g_usb.vendor_id, g_usb.product_id);
    uart_printf("USB: Device class %02X subclass %02X protocol %02X\n",
                dev_desc.bDeviceClass, dev_desc.bDeviceSubClass,
                dev_desc.bDeviceProtocol);

    /* Step 4: Get configuration descriptor (header first) */
    result = usb_get_config_descriptor(g_usb.device_address, 0,
                                        config_buf, sizeof(usb_config_desc_t));
    if (result < (int)sizeof(usb_config_desc_t)) {
        uart_printf("USB: Failed to get config descriptor header (%d)\n", result);
        return -1;
    }

    usb_config_desc_t *cfg = (usb_config_desc_t *)config_buf;
    uint16_t total_len = cfg->wTotalLength;

    if (total_len > sizeof(config_buf)) {
        total_len = sizeof(config_buf);
    }

    /* Get full configuration descriptor */
    result = usb_get_config_descriptor(g_usb.device_address, 0,
                                        config_buf, total_len);
    if (result < total_len) {
        uart_printf("USB: Failed to get full config descriptor (%d)\n", result);
        return -1;
    }

    /* Step 5: Parse configuration for HID keyboard */
    g_usb.has_keyboard = 0;
    usb_parse_config(config_buf, total_len);

    /* Step 6: Set configuration */
    result = usb_set_configuration(g_usb.device_address, cfg->bConfigurationValue);
    if (result < 0) {
        uart_printf("USB: Failed to set configuration (%d)\n", result);
        return -1;
    }

    g_usb.state = USB_STATE_CONFIGURED;
    uart_puts("USB: Device configured\n");

    /* Step 7: If HID keyboard, set boot protocol and idle */
    if (g_usb.has_keyboard) {
        /* Set boot protocol (simpler than report protocol) */
        result = usb_hid_set_protocol(g_usb.device_address,
                                       g_usb.kbd_interface, 0);
        if (result < 0) {
            uart_printf("USB: Warning - failed to set boot protocol (%d)\n", result);
            /* Continue anyway - some devices don't support this */
        }

        /* Set idle rate to 0 (report only on changes) */
        result = usb_hid_set_idle(g_usb.device_address, g_usb.kbd_interface, 0);
        if (result < 0) {
            uart_printf("USB: Warning - failed to set idle rate (%d)\n", result);
            /* Continue anyway */
        }

        /* Initialize HID keyboard driver */
        usb_hid_init(g_usb.device_address, g_usb.kbd_endpoint, g_usb.kbd_interval);

        uart_puts("USB: Keyboard ready!\n");
    }

    return 0;
}

/* ============================================================================
 * CORE INITIALIZATION
 * ============================================================================ */

/**
 * @brief Reset DWC2 core
 */
static int usb_core_reset(void)
{
    uint32_t val;
    uint64_t start;

    /* Wait for AHB idle */
    start = usb_get_time();
    while ((usb_get_time() - start) < USB_RESET_TIMEOUT) {
        val = usb_read(USB_GRSTCTL);
        if (val & USB_GRSTCTL_AHBIDLE) {
            break;
        }
        usb_delay_us(10);
    }

    if (!(val & USB_GRSTCTL_AHBIDLE)) {
        uart_puts("USB: AHB not idle\n");
        return -1;
    }

    /* Core soft reset */
    usb_write(USB_GRSTCTL, USB_GRSTCTL_CSRST);

    /* Wait for reset complete */
    start = usb_get_time();
    while ((usb_get_time() - start) < USB_RESET_TIMEOUT) {
        val = usb_read(USB_GRSTCTL);
        if (!(val & USB_GRSTCTL_CSRST)) {
            break;
        }
        usb_delay_us(10);
    }

    if (val & USB_GRSTCTL_CSRST) {
        uart_puts("USB: Core reset timeout\n");
        return -1;
    }

    /* Wait after reset */
    usb_delay_us(100000);

    return 0;
}

/**
 * @brief Configure core for host mode
 */
static void usb_configure_host(void)
{
    uint32_t val;

    /* Disable global interrupts during configuration */
    usb_write(USB_GAHBCFG, 0);

    /* Force host mode */
    val = usb_read(USB_GUSBCFG);
    val &= ~USB_GUSBCFG_FDMOD;          /* Clear device mode */
    val |= USB_GUSBCFG_FHMOD;           /* Force host mode */
    val |= USB_GUSBCFG_USBTRDTIM(9);    /* USB turnaround time (for internal PHY) */
    usb_write(USB_GUSBCFG, val);

    /* Wait for mode switch */
    usb_delay_us(50000);

    /* Verify we're in host mode */
    val = usb_read(USB_GINTSTS);
    if (!(val & USB_GINTSTS_CURMOD)) {
        uart_puts("USB: Warning - not in host mode\n");
    }

    /* Configure FIFOs */
    /* RX FIFO */
    usb_write(USB_GRXFSIZ, USB_RX_FIFO_SIZE);

    /* Non-periodic TX FIFO (starts after RX FIFO) */
    val = (USB_RX_FIFO_SIZE << 0) |         /* Start address */
          (USB_NPTX_FIFO_SIZE << 16);       /* Depth */
    usb_write(USB_GNPTXFSIZ, val);

    /* Periodic TX FIFO (starts after non-periodic TX FIFO) */
    val = ((USB_RX_FIFO_SIZE + USB_NPTX_FIFO_SIZE) << 0) |
          (USB_PTX_FIFO_SIZE << 16);
    usb_write(USB_HPTXFSIZ, val);

    /* Flush all FIFOs */
    usb_write(USB_GRSTCTL, USB_GRSTCTL_TXFNUM_ALL | USB_GRSTCTL_TXFFLSH);
    usb_delay_us(10);
    while (usb_read(USB_GRSTCTL) & USB_GRSTCTL_TXFFLSH) {
        usb_delay_us(10);
    }

    usb_write(USB_GRSTCTL, USB_GRSTCTL_RXFFLSH);
    usb_delay_us(10);
    while (usb_read(USB_GRSTCTL) & USB_GRSTCTL_RXFFLSH) {
        usb_delay_us(10);
    }

    /* Configure host mode */
    val = USB_HCFG_FSLSPCS_48MHZ;  /* 48 MHz PHY clock for full-speed */
    usb_write(USB_HCFG, val);

    /* Set frame interval */
    usb_write(USB_HFIR, USB_HFIR_FRINT_FS);

    /* Configure AHB for interrupts (no DMA, global interrupt mask) */
    val = USB_GAHBCFG_GLBLINTRMSK;
    usb_write(USB_GAHBCFG, val);

    /* Enable host channel interrupts */
    usb_write(USB_HAINTMSK, 0xFF);  /* All 8 channels */

    /* Enable relevant global interrupts */
    val = USB_GINTSTS_PRTINT |      /* Port interrupt */
          USB_GINTSTS_HCHINT |      /* Channel interrupt */
          USB_GINTSTS_DISCONNINT;   /* Disconnect */
    usb_write(USB_GINTMSK, val);
}

/**
 * @brief Power on USB port and wait for device connection
 */
static int usb_port_power_on(void)
{
    uint32_t val;

    /* Read current port status */
    val = usb_read(USB_HPRT);

    /* Clear write-clear bits */
    val &= ~USB_HPRT_WC_MASK;

    /* Power on port */
    val |= USB_HPRT_PRTPWR;
    usb_write(USB_HPRT, val);

    uart_puts("USB: Port powered\n");

    /* Wait for device connect (with timeout) */
    uint64_t start = usb_get_time();
    while ((usb_get_time() - start) < USB_CONNECT_TIMEOUT) {
        val = usb_read(USB_HPRT);
        if (val & USB_HPRT_PRTCONNSTS) {
            uart_puts("USB: Device connected\n");
            g_usb.state = USB_STATE_CONNECTED;
            return 0;
        }
        usb_delay_us(10000);
    }

    uart_puts("USB: No device connected (timeout)\n");
    return -1;
}

/**
 * @brief Reset USB port
 */
static int usb_port_reset(void)
{
    uint32_t val;
    uint64_t start;

    /* Start port reset */
    val = usb_read(USB_HPRT);
    val &= ~USB_HPRT_WC_MASK;
    val |= USB_HPRT_PRTRST;
    usb_write(USB_HPRT, val);

    /* Keep reset asserted for 60ms (USB spec) */
    usb_delay_us(60000);

    /* Clear reset */
    val = usb_read(USB_HPRT);
    val &= ~USB_HPRT_WC_MASK;
    val &= ~USB_HPRT_PRTRST;
    usb_write(USB_HPRT, val);

    /* Wait for port enabled */
    start = usb_get_time();
    while ((usb_get_time() - start) < USB_RESET_TIMEOUT) {
        val = usb_read(USB_HPRT);
        if (val & USB_HPRT_PRTENA) {
            break;
        }
        usb_delay_us(1000);
    }

    if (!(val & USB_HPRT_PRTENA)) {
        uart_puts("USB: Port not enabled after reset\n");
        return -1;
    }

    /* Determine device speed */
    uint32_t speed = (val & USB_HPRT_PRTSPD_MASK) >> 17;
    g_usb.device_speed = speed;

    const char *speed_str[] = { "high", "full", "low", "?" };
    uart_printf("USB: Port enabled (%s-speed)\n", speed_str[speed & 3]);

    /* If high-speed, BCM2837 DWC2 doesn't support it - must be FS/LS hub */
    if (speed == 0) {
        uart_puts("USB: Warning - high-speed not supported\n");
        g_usb.device_speed = 1;  /* Treat as full-speed */
    }

    g_usb.state = USB_STATE_RESET;

    /* Wait for device to be ready after reset */
    usb_delay_us(10000);

    return 0;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize USB host controller
 */
int usb_init(void)
{
    uint32_t vendor_id;

    uart_puts("\nUSB: Initializing DWC2 controller...\n");

    /* Clear driver state */
    memset(&g_usb, 0, sizeof(g_usb));
    g_usb.state = USB_STATE_UNINITIALIZED;

    /* Power on USB via mailbox */
    uart_puts("USB: Powering on USB controller...\n");
    if (mbox_set_power(MBOX_DEVICE_USB_HCD, 1) < 0) {
        uart_puts("USB: Failed to power on USB\n");
        g_usb.state = USB_STATE_ERROR;
        return -1;
    }

    usb_delay_us(100000);  /* Wait for power stabilization */
    g_usb.state = USB_STATE_POWERED;

    /* Check Synopsys ID */
    vendor_id = usb_read(USB_GSNPSID);
    uart_printf("USB: DWC2 Core ID: 0x%08X\n", vendor_id);

    if ((vendor_id & 0xFFFFF000) != 0x4F542000 &&
        (vendor_id & 0xFFFFF000) != 0x4F543000) {
        uart_puts("USB: Unknown USB controller\n");
        g_usb.state = USB_STATE_ERROR;
        return -1;
    }

    /* Reset core */
    uart_puts("USB: Resetting core...\n");
    if (usb_core_reset() < 0) {
        g_usb.state = USB_STATE_ERROR;
        return -1;
    }

    /* Configure for host mode */
    uart_puts("USB: Configuring host mode...\n");
    usb_configure_host();

    /* Power on port and wait for device */
    if (usb_port_power_on() < 0) {
        /* No device connected - not an error */
        uart_puts("USB: No device - will poll for connection\n");
        return 0;
    }

    /* Reset port and enumerate device */
    if (usb_port_reset() < 0) {
        g_usb.state = USB_STATE_ERROR;
        return -1;
    }

    if (usb_enumerate_device() < 0) {
        g_usb.state = USB_STATE_ERROR;
        return -1;
    }

    return 0;
}

/**
 * @brief Poll USB for events
 */
void usb_poll(void)
{
    uint32_t gintsts, hprt;

    if (g_usb.state == USB_STATE_UNINITIALIZED ||
        g_usb.state == USB_STATE_ERROR) {
        return;
    }

    /* Check for global interrupts */
    gintsts = usb_read(USB_GINTSTS);

    /* Port interrupt */
    if (gintsts & USB_GINTSTS_PRTINT) {
        hprt = usb_read(USB_HPRT);

        /* Device connect detected */
        if (hprt & USB_HPRT_PRTCONNDET) {
            /* Clear interrupt */
            uint32_t clear = hprt & ~USB_HPRT_WC_MASK;
            clear |= USB_HPRT_PRTCONNDET;
            usb_write(USB_HPRT, clear);

            if (hprt & USB_HPRT_PRTCONNSTS) {
                uart_puts("USB: Device connected\n");
                g_usb.state = USB_STATE_CONNECTED;

                /* Reset and enumerate */
                if (usb_port_reset() == 0) {
                    usb_enumerate_device();
                }
            }
        }

        /* Port enable changed */
        if (hprt & USB_HPRT_PRTENCHNG) {
            uint32_t clear = hprt & ~USB_HPRT_WC_MASK;
            clear |= USB_HPRT_PRTENCHNG;
            usb_write(USB_HPRT, clear);
        }
    }

    /* Disconnect */
    if (gintsts & USB_GINTSTS_DISCONNINT) {
        usb_write(USB_GINTSTS, USB_GINTSTS_DISCONNINT);
        uart_puts("USB: Device disconnected\n");

        g_usb.state = USB_STATE_POWERED;
        g_usb.has_keyboard = 0;
        g_usb.device_address = 0;
    }

    /* If keyboard is ready, poll it */
    if (g_usb.has_keyboard && g_usb.state == USB_STATE_CONFIGURED) {
        usb_hid_poll();
    }
}

/**
 * @brief Check if USB device is connected
 */
int usb_device_connected(void)
{
    return (g_usb.state >= USB_STATE_CONNECTED);
}

/**
 * @brief Check if USB keyboard is ready
 */
int usb_keyboard_ready(void)
{
    return (g_usb.has_keyboard && g_usb.state == USB_STATE_CONFIGURED);
}

/**
 * @brief Get USB status string
 */
void usb_get_status(char *status)
{
    static const char *state_names[] = {
        "Uninitialized",
        "Powered",
        "Connected",
        "Reset",
        "Addressed",
        "Configured",
        "Error"
    };

    if (g_usb.has_keyboard) {
        uart_printf(status, "Keyboard (VID=%04X)", g_usb.vendor_id);
    } else if (g_usb.state < 7) {
        strcpy(status, state_names[g_usb.state]);
    } else {
        strcpy(status, "Unknown");
    }
}

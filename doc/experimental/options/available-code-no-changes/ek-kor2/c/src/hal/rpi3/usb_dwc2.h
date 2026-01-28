/**
 * @file usb_dwc2.h
 * @brief Synopsys DWC2 USB Controller Register Definitions and Interface
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * This driver implements USB Host mode for the DesignWare Core USB 2.0
 * controller found on BCM2837 (Raspberry Pi 3B+).
 */

#ifndef USB_DWC2_H
#define USB_DWC2_H

#include <stdint.h>
#include "rpi3_hw.h"

/* ============================================================================
 * USB BASE ADDRESS
 * ============================================================================ */

#define USB_BASE                (PERIPHERAL_BASE + 0x980000UL)

/* ============================================================================
 * CORE GLOBAL REGISTERS (0x000 - 0x3FF)
 * ============================================================================ */

#define USB_GOTGCTL             (USB_BASE + 0x000)  /* OTG Control */
#define USB_GOTGINT             (USB_BASE + 0x004)  /* OTG Interrupt */
#define USB_GAHBCFG             (USB_BASE + 0x008)  /* AHB Configuration */
#define USB_GUSBCFG             (USB_BASE + 0x00C)  /* USB Configuration */
#define USB_GRSTCTL             (USB_BASE + 0x010)  /* Reset Control */
#define USB_GINTSTS             (USB_BASE + 0x014)  /* Interrupt Status */
#define USB_GINTMSK             (USB_BASE + 0x018)  /* Interrupt Mask */
#define USB_GRXSTSR             (USB_BASE + 0x01C)  /* RX Status Read (peek) */
#define USB_GRXSTSP             (USB_BASE + 0x020)  /* RX Status Pop (read) */
#define USB_GRXFSIZ             (USB_BASE + 0x024)  /* RX FIFO Size */
#define USB_GNPTXFSIZ           (USB_BASE + 0x028)  /* Non-periodic TX FIFO Size */
#define USB_GNPTXSTS            (USB_BASE + 0x02C)  /* Non-periodic TX FIFO Status */
#define USB_GI2CCTL             (USB_BASE + 0x030)  /* I2C Control */
#define USB_GPVNDCTL            (USB_BASE + 0x034)  /* PHY Vendor Control */
#define USB_GGPIO               (USB_BASE + 0x038)  /* GPIO */
#define USB_GUID                (USB_BASE + 0x03C)  /* User ID */
#define USB_GSNPSID             (USB_BASE + 0x040)  /* Synopsys ID */
#define USB_GHWCFG1             (USB_BASE + 0x044)  /* Hardware Config 1 */
#define USB_GHWCFG2             (USB_BASE + 0x048)  /* Hardware Config 2 */
#define USB_GHWCFG3             (USB_BASE + 0x04C)  /* Hardware Config 3 */
#define USB_GHWCFG4             (USB_BASE + 0x050)  /* Hardware Config 4 */
#define USB_GLPMCFG             (USB_BASE + 0x054)  /* LPM Config */
#define USB_GPWRDN              (USB_BASE + 0x058)  /* Power Down */
#define USB_GDFIFOCFG           (USB_BASE + 0x05C)  /* DFIFO Config */
#define USB_GADPCTL             (USB_BASE + 0x060)  /* ADP Control */
#define USB_HPTXFSIZ            (USB_BASE + 0x100)  /* Host Periodic TX FIFO Size */
#define USB_DPTXFSIZ(n)         (USB_BASE + 0x104 + ((n) - 1) * 4)  /* Device TX FIFO Size */

/* ============================================================================
 * GAHBCFG - AHB Configuration Register
 * ============================================================================ */

#define USB_GAHBCFG_GLBLINTRMSK     (1 << 0)   /* Global interrupt mask */
#define USB_GAHBCFG_HBSTLEN_MASK    (0xF << 1) /* Burst length mask */
#define USB_GAHBCFG_HBSTLEN_SINGLE  (0 << 1)   /* Single transfers */
#define USB_GAHBCFG_HBSTLEN_INCR    (1 << 1)   /* INCR burst */
#define USB_GAHBCFG_HBSTLEN_INCR4   (3 << 1)   /* INCR4 burst */
#define USB_GAHBCFG_HBSTLEN_INCR8   (5 << 1)   /* INCR8 burst */
#define USB_GAHBCFG_HBSTLEN_INCR16  (7 << 1)   /* INCR16 burst */
#define USB_GAHBCFG_DMAEN           (1 << 5)   /* DMA enable */
#define USB_GAHBCFG_NPTXFEMPLVL     (1 << 7)   /* Non-periodic TxFIFO empty level */
#define USB_GAHBCFG_PTXFEMPLVL      (1 << 8)   /* Periodic TxFIFO empty level */

/* ============================================================================
 * GUSBCFG - USB Configuration Register
 * ============================================================================ */

#define USB_GUSBCFG_TOUTCAL_MASK    (0x7 << 0)   /* Timeout calibration */
#define USB_GUSBCFG_PHYIF16         (1 << 3)     /* PHY interface 16-bit */
#define USB_GUSBCFG_ULPI_UTMI_SEL   (1 << 4)     /* ULPI/UTMI select */
#define USB_GUSBCFG_FSINTF          (1 << 5)     /* Full-speed interface */
#define USB_GUSBCFG_PHYSEL          (1 << 6)     /* USB 1.1 full-speed serial */
#define USB_GUSBCFG_DDRSEL          (1 << 7)     /* DDR select */
#define USB_GUSBCFG_SRPCAP          (1 << 8)     /* SRP capable */
#define USB_GUSBCFG_HNPCAP          (1 << 9)     /* HNP capable */
#define USB_GUSBCFG_USBTRDTIM_MASK  (0xF << 10)  /* USB turnaround time */
#define USB_GUSBCFG_USBTRDTIM(x)    (((x) & 0xF) << 10)
#define USB_GUSBCFG_PHYLPWRCLKSEL   (1 << 15)    /* PHY low-power clock select */
#define USB_GUSBCFG_ULPIFSLS        (1 << 17)    /* ULPI FS/LS select */
#define USB_GUSBCFG_ULPIAUTORES     (1 << 18)    /* ULPI auto resume */
#define USB_GUSBCFG_ULPICLKSUSM     (1 << 19)    /* ULPI clock SuspendM */
#define USB_GUSBCFG_ULPIEVBUSD      (1 << 20)    /* ULPI external VBUS drive */
#define USB_GUSBCFG_ULPIEVBUSI      (1 << 21)    /* ULPI external VBUS indicator */
#define USB_GUSBCFG_TSDPS           (1 << 22)    /* TermSel DLine pulsing */
#define USB_GUSBCFG_PCCI            (1 << 23)    /* Complement output */
#define USB_GUSBCFG_PTCI            (1 << 24)    /* Indicator complement */
#define USB_GUSBCFG_ULPIIPD         (1 << 25)    /* ULPI interface protect disable */
#define USB_GUSBCFG_FHMOD           (1 << 29)    /* Force host mode */
#define USB_GUSBCFG_FDMOD           (1 << 30)    /* Force device mode */
#define USB_GUSBCFG_CTXPKT          (1 << 31)    /* Corrupt TX packet */

/* ============================================================================
 * GRSTCTL - Reset Control Register
 * ============================================================================ */

#define USB_GRSTCTL_CSRST           (1 << 0)     /* Core soft reset */
#define USB_GRSTCTL_HSRST           (1 << 1)     /* HCLK soft reset */
#define USB_GRSTCTL_FRMCNTRRST      (1 << 2)     /* Frame counter reset */
#define USB_GRSTCTL_RXFFLSH         (1 << 4)     /* RX FIFO flush */
#define USB_GRSTCTL_TXFFLSH         (1 << 5)     /* TX FIFO flush */
#define USB_GRSTCTL_TXFNUM_MASK     (0x1F << 6)  /* TX FIFO number */
#define USB_GRSTCTL_TXFNUM(x)       (((x) & 0x1F) << 6)
#define USB_GRSTCTL_TXFNUM_ALL      USB_GRSTCTL_TXFNUM(0x10)  /* Flush all TX FIFOs */
#define USB_GRSTCTL_DMAREQ          (1 << 30)    /* DMA request signal */
#define USB_GRSTCTL_AHBIDLE         (1 << 31)    /* AHB master idle */

/* ============================================================================
 * GINTSTS/GINTMSK - Interrupt Status/Mask Register
 * ============================================================================ */

#define USB_GINTSTS_CURMOD          (1 << 0)     /* Current mode (1=host) */
#define USB_GINTSTS_MODEMIS         (1 << 1)     /* Mode mismatch */
#define USB_GINTSTS_OTGINT          (1 << 2)     /* OTG interrupt */
#define USB_GINTSTS_SOF             (1 << 3)     /* Start of frame */
#define USB_GINTSTS_RXFLVL          (1 << 4)     /* RX FIFO level */
#define USB_GINTSTS_NPTXFEMP        (1 << 5)     /* Non-periodic TX FIFO empty */
#define USB_GINTSTS_GINNAKEFF       (1 << 6)     /* Global IN NAK effective */
#define USB_GINTSTS_GOUTNAKEFF      (1 << 7)     /* Global OUT NAK effective */
#define USB_GINTSTS_ULPICKINT       (1 << 8)     /* ULPI carkit interrupt */
#define USB_GINTSTS_I2CINT          (1 << 9)     /* I2C interrupt */
#define USB_GINTSTS_ERLYSUSP        (1 << 10)    /* Early suspend */
#define USB_GINTSTS_USBSUSP         (1 << 11)    /* USB suspend */
#define USB_GINTSTS_USBRST          (1 << 12)    /* USB reset */
#define USB_GINTSTS_ENUMDONE        (1 << 13)    /* Enumeration done */
#define USB_GINTSTS_ISOUTDROP       (1 << 14)    /* Isochronous OUT drop */
#define USB_GINTSTS_EOPF            (1 << 15)    /* End of periodic frame */
#define USB_GINTSTS_EPMIS           (1 << 17)    /* Endpoint mismatch */
#define USB_GINTSTS_IEPINT          (1 << 18)    /* IN endpoint interrupt */
#define USB_GINTSTS_OEPINT          (1 << 19)    /* OUT endpoint interrupt */
#define USB_GINTSTS_INCOMPISOIN     (1 << 20)    /* Incomplete isochronous IN */
#define USB_GINTSTS_INCOMPIP        (1 << 21)    /* Incomplete periodic transfer */
#define USB_GINTSTS_FETSUSP         (1 << 22)    /* Data fetch suspended */
#define USB_GINTSTS_RESETDET        (1 << 23)    /* Reset detected */
#define USB_GINTSTS_PRTINT          (1 << 24)    /* Host port interrupt */
#define USB_GINTSTS_HCHINT          (1 << 25)    /* Host channel interrupt */
#define USB_GINTSTS_PTXFEMP         (1 << 26)    /* Periodic TX FIFO empty */
#define USB_GINTSTS_LPMINT          (1 << 27)    /* LPM interrupt */
#define USB_GINTSTS_CONIDSTSCHNG    (1 << 28)    /* Connector ID status change */
#define USB_GINTSTS_DISCONNINT      (1 << 29)    /* Disconnect detected */
#define USB_GINTSTS_SESSREQINT      (1 << 30)    /* Session request */
#define USB_GINTSTS_WKUPINT         (1 << 31)    /* Wakeup interrupt */

/* ============================================================================
 * GRXSTSR/GRXSTSP - RX Status Register
 * ============================================================================ */

#define USB_GRXSTS_CHNUM_MASK       (0xF << 0)   /* Channel number */
#define USB_GRXSTS_BCNT_MASK        (0x7FF << 4) /* Byte count */
#define USB_GRXSTS_DPID_MASK        (0x3 << 15)  /* Data PID */
#define USB_GRXSTS_PKTSTS_MASK      (0xF << 17)  /* Packet status */
#define USB_GRXSTS_CHNUM(x)         (((x) >> 0) & 0xF)
#define USB_GRXSTS_BCNT(x)          (((x) >> 4) & 0x7FF)
#define USB_GRXSTS_DPID(x)          (((x) >> 15) & 0x3)
#define USB_GRXSTS_PKTSTS(x)        (((x) >> 17) & 0xF)

/* Packet status values (host mode) */
#define USB_GRXSTS_PKTSTS_IN_DATA   2   /* IN data packet received */
#define USB_GRXSTS_PKTSTS_IN_DONE   3   /* IN transfer completed */
#define USB_GRXSTS_PKTSTS_DT_ERROR  5   /* Data toggle error */
#define USB_GRXSTS_PKTSTS_CH_HALT   7   /* Channel halted */

/* ============================================================================
 * HOST MODE REGISTERS (0x400 - 0x7FF)
 * ============================================================================ */

#define USB_HCFG                (USB_BASE + 0x400)  /* Host Configuration */
#define USB_HFIR                (USB_BASE + 0x404)  /* Host Frame Interval */
#define USB_HFNUM               (USB_BASE + 0x408)  /* Host Frame Number */
#define USB_HPTXSTS             (USB_BASE + 0x410)  /* Host Periodic TX FIFO Status */
#define USB_HAINT               (USB_BASE + 0x414)  /* Host All Channels Interrupt */
#define USB_HAINTMSK            (USB_BASE + 0x418)  /* Host All Channels Interrupt Mask */
#define USB_HFLBADDR            (USB_BASE + 0x41C)  /* Host Frame List Base Address */
#define USB_HPRT                (USB_BASE + 0x440)  /* Host Port Control and Status */

/* ============================================================================
 * HCFG - Host Configuration Register
 * ============================================================================ */

#define USB_HCFG_FSLSPCS_MASK       (0x3 << 0)   /* FS/LS PHY clock select */
#define USB_HCFG_FSLSPCS_48MHZ      (1 << 0)     /* 48 MHz */
#define USB_HCFG_FSLSPCS_6MHZ       (2 << 0)     /* 6 MHz (LS) */
#define USB_HCFG_FSLSS              (1 << 2)     /* FS/LS-only support */
#define USB_HCFG_ENA32KHZS          (1 << 7)     /* Enable 32 kHz suspend */
#define USB_HCFG_RESVALID_MASK      (0xFF << 8)  /* Resume validation period */
#define USB_HCFG_DESCDMA            (1 << 23)    /* Descriptor DMA enable */
#define USB_HCFG_FRLISTEN_MASK      (0x3 << 24)  /* Frame list entries */
#define USB_HCFG_MODECHTIMEN        (1 << 31)    /* Mode change timeout enable */

/* ============================================================================
 * HFIR - Host Frame Interval Register
 * ============================================================================ */

#define USB_HFIR_FRINT_MASK         (0xFFFF << 0)   /* Frame interval */
#define USB_HFIR_FRINT_FS           60000           /* Full-speed (12 Mbps) */
#define USB_HFIR_FRINT_LS           6000            /* Low-speed (1.5 Mbps) */
#define USB_HFIR_RLDCTRL            (1 << 16)       /* Reload control */

/* ============================================================================
 * HPRT - Host Port Control and Status Register
 * ============================================================================ */

#define USB_HPRT_PRTCONNSTS         (1 << 0)     /* Port connect status */
#define USB_HPRT_PRTCONNDET         (1 << 1)     /* Port connect detected */
#define USB_HPRT_PRTENA             (1 << 2)     /* Port enable */
#define USB_HPRT_PRTENCHNG          (1 << 3)     /* Port enable changed */
#define USB_HPRT_PRTOVRCURRACT      (1 << 4)     /* Port overcurrent active */
#define USB_HPRT_PRTOVRCURRCHNG     (1 << 5)     /* Port overcurrent changed */
#define USB_HPRT_PRTRES             (1 << 6)     /* Port resume */
#define USB_HPRT_PRTSUSP            (1 << 7)     /* Port suspend */
#define USB_HPRT_PRTRST             (1 << 8)     /* Port reset */
#define USB_HPRT_PRTLNSTS_MASK      (0x3 << 10)  /* Port line status */
#define USB_HPRT_PRTPWR             (1 << 12)    /* Port power */
#define USB_HPRT_PRTTSTCTL_MASK     (0xF << 13)  /* Port test control */
#define USB_HPRT_PRTSPD_MASK        (0x3 << 17)  /* Port speed */
#define USB_HPRT_PRTSPD_HIGH        (0 << 17)    /* High-speed */
#define USB_HPRT_PRTSPD_FULL        (1 << 17)    /* Full-speed */
#define USB_HPRT_PRTSPD_LOW         (2 << 17)    /* Low-speed */

/* HPRT write-clear bits (must be preserved when writing) */
#define USB_HPRT_WC_MASK            (USB_HPRT_PRTCONNDET | USB_HPRT_PRTENA | \
                                     USB_HPRT_PRTENCHNG | USB_HPRT_PRTOVRCURRCHNG)

/* ============================================================================
 * HOST CHANNEL REGISTERS (8 channels, 0x20 apart, starting at 0x500)
 * ============================================================================ */

#define USB_HCCHAR(n)           (USB_BASE + 0x500 + (n) * 0x20)  /* Characteristics */
#define USB_HCSPLT(n)           (USB_BASE + 0x504 + (n) * 0x20)  /* Split Control */
#define USB_HCINT(n)            (USB_BASE + 0x508 + (n) * 0x20)  /* Interrupt */
#define USB_HCINTMSK(n)         (USB_BASE + 0x50C + (n) * 0x20)  /* Interrupt Mask */
#define USB_HCTSIZ(n)           (USB_BASE + 0x510 + (n) * 0x20)  /* Transfer Size */
#define USB_HCDMA(n)            (USB_BASE + 0x514 + (n) * 0x20)  /* DMA Address */

#define USB_NUM_CHANNELS        8   /* Number of host channels */

/* ============================================================================
 * HCCHAR - Host Channel Characteristics Register
 * ============================================================================ */

#define USB_HCCHAR_MPS_MASK         (0x7FF << 0)    /* Max packet size */
#define USB_HCCHAR_MPS(x)           (((x) & 0x7FF) << 0)
#define USB_HCCHAR_EPNUM_MASK       (0xF << 11)     /* Endpoint number */
#define USB_HCCHAR_EPNUM(x)         (((x) & 0xF) << 11)
#define USB_HCCHAR_EPDIR            (1 << 15)       /* Endpoint direction (1=IN) */
#define USB_HCCHAR_LSDEV            (1 << 17)       /* Low-speed device */
#define USB_HCCHAR_EPTYPE_MASK      (0x3 << 18)     /* Endpoint type */
#define USB_HCCHAR_EPTYPE_CTRL      (0 << 18)       /* Control */
#define USB_HCCHAR_EPTYPE_ISOC      (1 << 18)       /* Isochronous */
#define USB_HCCHAR_EPTYPE_BULK      (2 << 18)       /* Bulk */
#define USB_HCCHAR_EPTYPE_INTR      (3 << 18)       /* Interrupt */
#define USB_HCCHAR_MC_MASK          (0x3 << 20)     /* Multi-count */
#define USB_HCCHAR_MC(x)            (((x) & 0x3) << 20)
#define USB_HCCHAR_DEVADDR_MASK     (0x7F << 22)    /* Device address */
#define USB_HCCHAR_DEVADDR(x)       (((x) & 0x7F) << 22)
#define USB_HCCHAR_ODDFRM           (1 << 29)       /* Odd frame */
#define USB_HCCHAR_CHDIS            (1 << 30)       /* Channel disable */
#define USB_HCCHAR_CHENA            (1 << 31)       /* Channel enable */

/* ============================================================================
 * HCINT - Host Channel Interrupt Register
 * ============================================================================ */

#define USB_HCINT_XFERCOMPL         (1 << 0)     /* Transfer completed */
#define USB_HCINT_CHHLTD            (1 << 1)     /* Channel halted */
#define USB_HCINT_AHBERR            (1 << 2)     /* AHB error */
#define USB_HCINT_STALL             (1 << 3)     /* STALL response */
#define USB_HCINT_NAK               (1 << 4)     /* NAK response */
#define USB_HCINT_ACK               (1 << 5)     /* ACK response */
#define USB_HCINT_NYET              (1 << 6)     /* NYET response */
#define USB_HCINT_XACTERR           (1 << 7)     /* Transaction error */
#define USB_HCINT_BBLERR            (1 << 8)     /* Babble error */
#define USB_HCINT_FRMOVRUN          (1 << 9)     /* Frame overrun */
#define USB_HCINT_DATATGLERR        (1 << 10)    /* Data toggle error */
#define USB_HCINT_BNA               (1 << 11)    /* Buffer not available */
#define USB_HCINT_XCS_XACT_ERR      (1 << 12)    /* Excessive transaction error */
#define USB_HCINT_DESC_LST_ROLLINTR (1 << 13)    /* Descriptor rollover interrupt */

/* Error mask for checking transfer failures */
#define USB_HCINT_ERROR_MASK        (USB_HCINT_AHBERR | USB_HCINT_STALL | \
                                     USB_HCINT_XACTERR | USB_HCINT_BBLERR | \
                                     USB_HCINT_FRMOVRUN | USB_HCINT_DATATGLERR)

/* ============================================================================
 * HCTSIZ - Host Channel Transfer Size Register
 * ============================================================================ */

#define USB_HCTSIZ_XFERSIZE_MASK    (0x7FFFF << 0)  /* Transfer size */
#define USB_HCTSIZ_XFERSIZE(x)      (((x) & 0x7FFFF) << 0)
#define USB_HCTSIZ_PKTCNT_MASK      (0x3FF << 19)   /* Packet count */
#define USB_HCTSIZ_PKTCNT(x)        (((x) & 0x3FF) << 19)
#define USB_HCTSIZ_PID_MASK         (0x3 << 29)     /* PID */
#define USB_HCTSIZ_PID_DATA0        (0 << 29)
#define USB_HCTSIZ_PID_DATA1        (2 << 29)
#define USB_HCTSIZ_PID_DATA2        (1 << 29)
#define USB_HCTSIZ_PID_MDATA        (3 << 29)
#define USB_HCTSIZ_PID_SETUP        (3 << 29)
#define USB_HCTSIZ_DOPING           (1 << 31)       /* Do PING */

/* ============================================================================
 * DATA FIFOs (one per channel)
 * ============================================================================ */

#define USB_FIFO(n)             (USB_BASE + 0x1000 + (n) * 0x1000)
#define USB_FIFO_SIZE           0x1000  /* 4KB per FIFO */

/* ============================================================================
 * USB STANDARD DEFINITIONS
 * ============================================================================ */

/* USB Request Types (bmRequestType) */
#define USB_RT_DIR_OUT              (0 << 7)
#define USB_RT_DIR_IN               (1 << 7)
#define USB_RT_TYPE_STANDARD        (0 << 5)
#define USB_RT_TYPE_CLASS           (1 << 5)
#define USB_RT_TYPE_VENDOR          (2 << 5)
#define USB_RT_RECIP_DEVICE         (0 << 0)
#define USB_RT_RECIP_INTERFACE      (1 << 0)
#define USB_RT_RECIP_ENDPOINT       (2 << 0)
#define USB_RT_RECIP_OTHER          (3 << 0)

/* USB Standard Requests (bRequest) */
#define USB_REQ_GET_STATUS          0
#define USB_REQ_CLEAR_FEATURE       1
#define USB_REQ_SET_FEATURE         3
#define USB_REQ_SET_ADDRESS         5
#define USB_REQ_GET_DESCRIPTOR      6
#define USB_REQ_SET_DESCRIPTOR      7
#define USB_REQ_GET_CONFIGURATION   8
#define USB_REQ_SET_CONFIGURATION   9
#define USB_REQ_GET_INTERFACE       10
#define USB_REQ_SET_INTERFACE       11
#define USB_REQ_SYNCH_FRAME         12

/* USB Descriptor Types */
#define USB_DESC_TYPE_DEVICE        1
#define USB_DESC_TYPE_CONFIG        2
#define USB_DESC_TYPE_STRING        3
#define USB_DESC_TYPE_INTERFACE     4
#define USB_DESC_TYPE_ENDPOINT      5
#define USB_DESC_TYPE_HID           0x21
#define USB_DESC_TYPE_HID_REPORT    0x22

/* USB HID Class Requests */
#define USB_HID_REQ_GET_REPORT      0x01
#define USB_HID_REQ_GET_IDLE        0x02
#define USB_HID_REQ_GET_PROTOCOL    0x03
#define USB_HID_REQ_SET_REPORT      0x09
#define USB_HID_REQ_SET_IDLE        0x0A
#define USB_HID_REQ_SET_PROTOCOL    0x0B

/* USB Endpoint Types */
#define USB_EP_TYPE_CONTROL         0
#define USB_EP_TYPE_ISOCHRONOUS     1
#define USB_EP_TYPE_BULK            2
#define USB_EP_TYPE_INTERRUPT       3

/* USB Device Classes */
#define USB_CLASS_HID               3

/* USB HID Subclasses */
#define USB_HID_SUBCLASS_BOOT       1

/* USB HID Protocols */
#define USB_HID_PROTOCOL_KEYBOARD   1
#define USB_HID_PROTOCOL_MOUSE      2

/* ============================================================================
 * USB DATA STRUCTURES
 * ============================================================================ */

/* USB Setup Packet */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

/* USB Device Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* USB Configuration Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

/* USB Interface Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_desc_t;

/* USB Endpoint Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_desc_t;

/* USB HID Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} __attribute__((packed)) usb_hid_desc_t;

/* ============================================================================
 * USB DRIVER API
 * ============================================================================ */

/**
 * @brief Initialize USB host controller
 * @return 0 on success, negative error code on failure
 */
int usb_init(void);

/**
 * @brief Poll USB for events (call regularly from main loop)
 */
void usb_poll(void);

/**
 * @brief Check if a USB device is connected
 * @return 1 if connected, 0 if not
 */
int usb_device_connected(void);

/**
 * @brief Check if USB keyboard is ready
 * @return 1 if keyboard ready, 0 if not
 */
int usb_keyboard_ready(void);

/**
 * @brief Get keyboard status for display
 * @param status Buffer to receive status string (at least 32 bytes)
 */
void usb_get_status(char *status);

/**
 * @brief Perform interrupt transfer (used by HID driver)
 *
 * @param addr Device address
 * @param ep Endpoint address (with direction bit, e.g., 0x81 for IN EP1)
 * @param data Buffer for data
 * @param len Maximum length
 * @return Bytes transferred, 0 if NAK, negative on error
 */
int usb_interrupt_transfer(uint8_t addr, uint8_t ep, void *data, uint16_t len);

#endif /* USB_DWC2_H */

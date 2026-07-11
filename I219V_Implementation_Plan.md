# Intel I219-V Gigabit LAN Controller Driver — Implementation Plan

## Feasibility Assessment

**Status: Implementable**

ImplusOS provides all necessary infrastructure:

| Requirement | Support | Location |
|---|---|---|
| PCI device discovery | Yes | `PCI_Driver.ELF` — `pci_find_device()` / `pci_bus_driver` probe model |
| MMIO mapping | Yes | `map_mmio_range()` / `g_api->hw.map_mmio_range` |
| DMA allocation | Yes | `dma_alloc()` / `dma_alloc_ex()` — physically contiguous, 32-bit constraint |
| MSI-X interrupts | Yes | `g_api->pci.enable_msix()` — vector alloc + IRQ register |
| NIC driver vtable | Yes | `driver_nic_t` — `init`/`poll`/`send_frame`/`set_rx_callback` etc. |
| Ethernet integration | Yes | `Ethernet.c` → `driver_manager_nic_set_rx_callback()` |
| Build system | Yes | Auto-discovered via `Kernel/Drivers/Server/<name>/Makefile` |

**Intel I219-V Overview:**
- PCI Vendor: `0x8086`, Device IDs: `0x15B7` (LM), `0x15B8` (V), `0x15BD`, `0x15BE`, `0x15BF`, `0x15C0`, plus 25+ more variants
- Intel PRO/1000 (e1000e) register interface
- MMIO BAR0, MSI-X, on-chip PHY
- Descriptor format compatible with legacy Intel Gigabit NICs
- Reference driver: VirtIONet at `Kernel/Drivers/Server/NIC/VirtIONet/VirtIONet.c`

---

## File Layout

```
Kernel/Drivers/Server/NIC/I219V/
├── I219V.h           — Public function declarations
├── I219V.c           — Implementation (~1000-1200 lines)
└── Makefile           — Build integration (3 lines)
```

**Makefile:**
```makefile
DRIVER_NAME := I219V_Driver
include ../../../module.mk
```

Build output: `Build/Modules/x86_64/I219V_Driver/I219V_Driver.ELF`

---

## Driver Structure (I219V.c)

### 1. Include Guards & Module API Wrappers

Follow the exact pattern from VirtIONet.c:

```c
#ifdef IMPLUS_DRIVER_MODULE
#include "Drivers/Module/DriverBinary.h"
#else
#include "Drivers/Client/PCI/PCI_Main.h"
#include "MemoryManagement/DMA_Memory.h"
#include "Core/sync/Spinlock.h"
#include "Debug/serial/Serial.h"
#endif

#ifdef IMPLUS_DRIVER_MODULE
static const driver_binary_t *g_api = NULL;
#define malloc           g_api->malloc
#define free             g_api->free
#define dma_alloc        g_api->dma_alloc
#define dma_free         g_api->dma_free
#define dma_alloc_ex     g_api->mem.dma_alloc_ex
#define memset           g_api->memset
#define memcpy           g_api->memcpy
#define map_mmio_range   g_api->hw.map_mmio_range
#define pci_read_config  g_api->pci_read_config
#define pci_write_config g_api->pci_write_config
#define serial_write_string g_api->serial_write_string
// ... hal ops, spinlock, etc.
#endif
```

### 2. PCI Device ID Table

```c
#define INTEL_VENDOR 0x8086

static const uint16_t i219_device_ids[] = {
    // I219-LM variants
    0x15B7, 0x15BC, 0x15BE, 0x15C0, 0x15C3,
    0x1559, 0x15B5, 0x15B6, 0x15B9, 0x15BA,
    // I219-V variants
    0x15B8, 0x15BD, 0x15BF, 0x15C1, 0x15C2,
    0x155A, 0x15BB, 0x15C4, 0x15C5, 0x15C6,
    // Additional
    0x15C7, 0x15C8, 0x15C9, 0x15CA, 0x15CB,
    0x15CC, 0x15CD, 0x15CE, 0x15CF, 0x15D0,
    0x15D1, 0x15D2, 0x15D3, 0x15D4, 0x15D5,
    0x15D6, 0x15D7, 0x15D8, 0x15D9, 0x15DA,
    0x15DB, 0x15DC, 0x15DD, 0x15DE, 0x15DF,
    0x15E0, 0x15E1, 0x15E2,
};
```

### 3. Key MMIO Register Definitions

```c
// Device Control
#define REG_CTRL         0x0000  // Device Control
#define REG_STATUS       0x0008  // Device Status
#define REG_CTRL_EXT     0x0018  // Extended Device Control

// Interrupt
#define REG_ICR          0x00C0  // Interrupt Cause Read
#define REG_ICS          0x00C8  // Interrupt Cause Set
#define REG_IMS          0x00D0  // Interrupt Mask Set/Read
#define REG_IMC          0x00D8  // Interrupt Mask Clear
#define REG_IAC          0x0100  // Interrupt Auto Clear

// Receive
#define REG_RCTRL        0x0100  // Receive Control
#define REG_RDBAL        0x2800  // RX Descriptor Base Low
#define REG_RDBAH        0x2804  // RX Descriptor Base High
#define REG_RDLEN        0x2808  // RX Descriptor Length
#define REG_RDH          0x2810  // RX Descriptor Head
#define REG_RDT          0x2818  // RX Descriptor Tail
#define REG_RDTR         0x2820  // RX Delay Timer

// Transmit
#define REG_TCTRL        0x0400  // Transmit Control
#define REG_TDBAL        0x3800  // TX Descriptor Base Low
#define REG_TDBAH        0x3804  // TX Descriptor Base High
#define REG_TDLEN        0x3808  // TX Descriptor Length
#define REG_TDH          0x3810  // TX Descriptor Head
#define REG_TDT          0x3818  // TX Descriptor Tail

// MAC Address
#define REG_RAL(n)       (0x5400 + (n) * 8)   // Receive Address Low
#define REG_RAH(n)       (0x5404 + (n) * 8)   // Receive Address High

// Management / NVM
#define REG_MNGC         0x5220  // Manageability Control
#define REG_EEPROM_STATUS 0x4010
#define REG_EEC          0x1008  // EEPROM/Flash Control

// PHY Management (MDIO)
#define REG_MDIC         0x0020  // MDI Control

// CTRL register bits
#define CTRL_RST         (1u << 0)   // Device Reset
#define CTRL_SLU         (1u << 6)   // Set Link Up
#define CTRL_ASDE        (1u << 5)   // Auto-Speed Detection Enable
#define CTRL_FRCSPD      (1u << 11)  // Force Speed
#define CTRL_FRCFDX      (1u << 10)  // Force Full Duplex
#define CTRL_RFCE        (1u << 5)   // Receive Flow Control Enable
#define CTRL_TFCE        (1u << 6)   // Transmit Flow Control Enable

// STATUS register bits
#define STATUS_LU        (1u << 1)   // Link Up
#define STATUS_FD        (1u << 0)   // Full Duplex

// RCTRL register bits
#define RCTRL_EN         (1u << 0)   // Receiver Enable
#define RCTRL_UPE        (1u << 3)   // Unicast Promiscuous Enable
#define RCTRL_MPE        (1u << 4)   // Multicast Promiscuous Enable
#define RCTRL_LPE        (1u << 5)   // Long Packet Reception Enable
#define RCTRL_BSIZE_2048 (0u << 16)  // Buffer Size = 2048
#define RCTRL_SECRC      (1u << 26)  // Strip CRC

// TCTRL register bits
#define TCTRL_EN         (1u << 0)   // Transmitter Enable
#define TCTRL_PSP        (1u << 3)   // Pad Short Packets
#define TCTRL_CT_SHIFT   4u          // Collision Threshold
#define TCTRL_COLD_SHIFT 12u         // Collision Distance
#define TCTRL_COLD_HD    (0x3Fu << TCTRL_COLD_SHIFT)  // Half-duplex slot time

// Interrupt cause bits
#define ICR_TXDW         (1u << 0)   // TX Descriptor Written Back
#define ICR_TXQE         (1u << 1)   // TX Queue Empty
#define ICR_LSC          (1u << 2)   // Link Status Change
#define ICR_RXDMT0       (1u << 4)   // RX Descriptor Minimum Threshold
#define ICR_RXT0         (1u << 7)   // RX Timer Interrupt
#define ICR_OTHER        (1u << 16)  // Other Interrupts

// RAH register bits
#define RAH_AV           (1u << 31)  // Address Valid

// MDIC register bits
#define MDIC_DATA_SHIFT  0u          // Data
#define MDIC_REG_SHIFT   16u         // Register address
#define MDIC_PHY_SHIFT   21u         // PHY address
#define MDIC_OP_WRITE    (1u << 26)  // Opcode = write
#define MDIC_OP_READ     (2u << 26)  // Opcode = read
#define MDIC_READY       (1u << 28)  // MDIC ready
#define MDIC_INT_EN      (1u << 29)  // Interrupt enable
#define MDIC_ERROR       (1u << 30)  // MDIC error

// EEPROM status bits
#define EEC_PRES        (1u << 8)   // EEPROM Present
#define EEC_AUTO_RD     (1u << 9)   // Auto Read Done
```

### 4. Descriptor Format

```c
typedef struct __attribute__((packed)) {
    uint64_t addr;      // DMA buffer physical address
    uint16_t length;    // Data length
    uint16_t cksum;     // Packet checksum
    uint8_t  status;    // Descriptor status
    uint8_t  errors;    // Descriptor errors
    uint16_t special;   // Special field
} e1000_rx_desc_t;

// RX status bits
#define RXD_STAT_DD     (1u << 0)   // Descriptor Done
#define RXD_STAT_EOP    (1u << 1)   // End of Packet
#define RXD_STAT_IXSM   (1u << 2)   // Ignore checksum
#define RXD_STAT_VP     (1u << 3)   // VLAN present

typedef struct __attribute__((packed)) {
    uint64_t addr;      // DMA buffer physical address
    uint16_t length;    // Data length
    uint8_t  cso;       // Checksum offset
    uint8_t  cmd;       // Command field
    uint8_t  status;    // Descriptor status
    uint8_t  css;       // Checksum start
    uint16_t special;   // Special field
} e1000_tx_desc_t;

// TX command bits
#define TXD_CMD_EOP     (1u << 0)   // End of Packet
#define TXD_CMD_IFCS    (1u << 1)   // Insert FCS
#define TXD_CMD_IC      (1u << 2)   // Insert Checksum
#define TXD_CMD_RS      (1u << 3)   // Report Status
#define TXD_CMD_RPS     (1u << 4)   // Report Packet Sent

// TX status bits
#define TXD_STAT_DD     (1u << 0)   // Descriptor Done
#define TXD_STAT_EC     (1u << 1)   // Excess Collisions
#define TXD_STAT_LC     (1u << 2)   // Late Collision
```

### 5. Internal Data Structures

```c
#define I219V_DESC_COUNT    256
#define I219V_BUF_SIZE      2048
#define I219V_DEFAULT_MTU   1500

typedef struct {
    uint8_t  *virt;        // Virtual address of DMA buffer
    uint64_t  phys;        // Physical address of DMA buffer
} i219v_dma_buffer_t;

typedef struct {
    volatile e1000_rx_desc_t *desc;  // RX descriptor ring (DMA)
    i219v_dma_buffer_t *buffers;     // RX buffers (DMA)
    uint16_t  count;                  // Ring size
    uint16_t  next_to_clean;          // Next descriptor to check
    uint16_t  next_to_use;            // Next descriptor to give to HW
} i219v_rx_ring_t;

typedef struct {
    volatile e1000_tx_desc_t *desc;  // TX descriptor ring (DMA)
    i219v_dma_buffer_t *buffers;     // TX buffers (DMA)
    uint16_t  count;                  // Ring size
    uint16_t  next_to_clean;          // Next descriptor to reap
    uint16_t  next_to_use;            // Next descriptor to send
    bool     *in_use;                 // Per-descriptor busy flag
} i219v_tx_ring_t;

// Per-device state
static struct {
    spinlock_t   lock;
    int          ready;
    uint16_t     mtu;
    uint8_t      mac[6];

    volatile uint8_t *mmio;      // Mapped BAR0
    uint8_t           bus, dev, func;

    i219v_rx_ring_t rx_ring;
    i219v_tx_ring_t tx_ring;

    uint32_t msix_vectors[3];    // [link, rx, tx]
    driver_nic_rx_callback_t rx_callback;
} g_i219v;
```

---

## Initialization Sequence (`i219v_init`)

```
1. DMA allocator check
2. PCI scan: loop i219_device_ids[] with pci_read_config
   → match vendor=0x8086, device in table, class=0x02/0x00
3. PCI Command: set Bus Master + MMIO (bits 1,2)
4. BAR0: map_mmio_range(phys_addr, size) → g_i219v.mmio
5. Device Reset:
   - mmio_write32(REG_CTRL, CTRL_RST)
   - delay(1ms)
   - poll for CTRL_RST == 0 (max 10ms)
6. Link Status check:
   - If STATUS & STATUS_LU == 0, set CTRL_SLU to force link up
7. MSI-X setup:
   - g_api->pci.enable_msix(bus, dev, func, 3, g_i219v.msix_vectors)
   - g_api->pci.register_irq(g_i219v.msix_vectors[1], i219v_rx_irq, NULL)
   - Optional: register vector[2] for TX, vector[0] for link
   - Mask all interrupts via IMC, then set desired bits in IMS
8. MAC address:
   - Read mmio_read32(REG_RAL(0)) + mmio_read32(REG_RAH(0))
   - If both are 0xFFFFFFFF → read from EEPROM/Flash via EEC register
   - Store in g_i219v.mac
9. RX ring setup:
   a. dma_alloc_ex(desc_count * 16, 16, 0xFFFFFFFF, &phys) → desc ring
   b. mmio_write32(REG_RDBAL, phys & 0xFFFFFFFF)
   c. mmio_write32(REG_RDBAH, (phys >> 32))
   d. mmio_write32(REG_RDLEN, desc_count * 16)
   e. For each desc: allocate dma_alloc(I219V_BUF_SIZE, &buf_phys)
      Set desc.addr = buf_phys, desc.status = 0
   f. mmio_write32(REG_RDH, 0)
   g. mmio_write32(REG_RDT, desc_count - 1)
10. TX ring setup:
    a. dma_alloc_ex(desc_count * 16, 16, 0xFFFFFFFF, &phys) → desc ring
    b. mmio_write32(REG_TDBAL, phys & 0xFFFFFFFF)
    c. mmio_write32(REG_TDBAH, (phys >> 32))
    d. mmio_write32(REG_TDLEN, desc_count * 16)
    e. For each desc: allocate dma_alloc(I219V_BUF_SIZE, &buf_phys)
       Set desc.addr = buf_phys
    f. mmio_write32(REG_TDH, 0)
    g. mmio_write32(REG_TDT, 0)
11. RCTRL config:
    mmio_write32(REG_RCTRL, RCTRL_EN | RCTRL_UPE | RCTRL_BSIZE_2048 | RCTRL_SECRC)
12. Receive Address:
    mmio_write32(REG_RAL(0), mac_low)
    mmio_write32(REG_RAH(0), mac_high | RAH_AV)
13. TCTRL config:
    mmio_write32(REG_TCTRL, TCTRL_EN | TCTRL_PSP |
                 (0x0F << TCTRL_CT_SHIFT) | TCTRL_COLD_HD)
14. Enable interrupts:
    mmio_write32(REG_IMS, ICR_RXT0 | ICR_RXDMT0 | ICR_LSC | ICR_TXQE)
15. g_i219v.ready = 1
16. Serial log: MAC address
```

---

## Receive Data Path

### Polling (`i219v_poll`)

```c
void i219v_poll(void) {
    // Called from Ethernet layer's network_stack_poll()
    uint16_t cur = mmio_read32(REG_RDH);
    uint16_t cleaned = g_i219v.rx_ring.next_to_clean;

    while (cleaned != cur) {
        volatile e1000_rx_desc_t *desc = &g_i219v.rx_ring.desc[cleaned];
        if (!(desc->status & RXD_STAT_DD))
            break;

        if ((desc->status & RXD_STAT_EOP) && desc->length > 0 &&
            desc->length <= I219V_BUF_SIZE) {
            // Copy frame to temp buffer
            uint8_t frame[I219V_MAX_FRAME];
            memcpy(frame, g_i219v.rx_ring.buffers[cleaned].virt, desc->length);
            if (g_i219v.rx_callback)
                g_i219v.rx_callback(frame, (uint16_t)desc->length);
        }

        // Recycle descriptor
        desc->status = 0;
        cleaned = (cleaned + 1) % g_i219v.rx_ring.count;
    }

    // Replenish descriptors after processing
    if (cleaned != g_i219v.rx_ring.next_to_clean) {
        g_i219v.rx_ring.next_to_clean = cleaned;
        memory_barrier();
        mmio_write32(REG_RDT, (uint16_t)((cleaned - 1 + g_i219v.rx_ring.count) % g_i219v.rx_ring.count));
    }

    // Reap TX completions
    i219v_reap_tx();
}
```

### RX Interrupt Handler

```c
void i219v_rx_irq(void *context) {
    (void)context;
    uint32_t icr = mmio_read32(REG_ICR);
    // Read ICR to clear the cause; RXT0 or RXDMT0 bits indicate new packets
    // Actual packet processing deferred to poll() or inline
    // For simplicity, can just call i219v_poll() here
}
```

### TX Reap (`i219v_reap_tx`)

```c
static void i219v_reap_tx(void) {
    while (g_i219v.tx_ring.in_use[g_i219v.tx_ring.next_to_clean]) {
        volatile e1000_tx_desc_t *desc =
            &g_i219v.tx_ring.desc[g_i219v.tx_ring.next_to_clean];
        if (!(desc->status & TXD_STAT_DD))
            break;
        desc->status = 0;
        g_i219v.tx_ring.in_use[g_i219v.tx_ring.next_to_clean] = false;
        g_i219v.tx_ring.next_to_clean =
            (g_i219v.tx_ring.next_to_clean + 1) % g_i219v.tx_ring.count;
    }
}
```

---

## Transmit Data Path (`i219v_send`)

```c
bool i219v_send(const uint8_t *frame, uint16_t frame_len) {
    if (!g_i219v.ready || frame == NULL || frame_len == 0 ||
        frame_len > g_i219v.mtu + 14) {
        return false;
    }

    irq_flags = irq_save_disable();
    spinlock_lock(&g_i219v.lock);

    // Reap completed TX descriptors
    i219v_reap_tx();

    // Find free descriptor
    uint16_t idx = g_i219v.tx_ring.next_to_use;
    if (g_i219v.tx_ring.in_use[idx]) {
        spinlock_unlock(&g_i219v.lock);
        irq_restore(irq_flags);
        return false;  // TX ring full
    }

    // Copy frame to DMA buffer
    memcpy(g_i219v.tx_ring.buffers[idx].virt, frame, frame_len);

    // Program descriptor
    volatile e1000_tx_desc_t *desc = &g_i219v.tx_ring.desc[idx];
    desc->addr  = g_i219v.tx_ring.buffers[idx].phys;
    desc->length = frame_len;
    desc->cmd    = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    desc->status = 0;

    memory_barrier();

    g_i219v.tx_ring.in_use[idx] = true;
    g_i219v.tx_ring.next_to_use =
        (uint16_t)((idx + 1) % g_i219v.tx_ring.count);

    // Advance Tail to trigger transmission
    memory_barrier();
    mmio_write32(REG_TDT, g_i219v.tx_ring.next_to_use);

    spinlock_unlock(&g_i219v.lock);
    irq_restore(irq_flags);
    return true;
}
```

---

## Module Registration

Follow VirtIONet.c pattern exactly:

```c
#ifdef IMPLUS_DRIVER_MODULE
static const driver_nic_t g_i219v_driver = {
    .init = i219v_init,
    .is_ready = i219v_is_ready,
    .mtu = i219v_mtu,
    .get_mac = i219v_get_mac,
    .send_frame = i219v_send,
    .poll = i219v_poll,
    .set_rx_callback = i219v_set_rx_callback,
};

static void i219v_shutdown(void) {
    // Disable RX/TX, mask interrupts, free DMA buffers
    mmio_write32(REG_IMC, 0xFFFFFFFF);  // Mask all interrupts
    mmio_write32(REG_RCTRL, 0);
    mmio_write32(REG_TCTRL, 0);
    g_i219v.ready = 0;
    // Free DMA buffers...
    g_api = NULL;
}

static const driver_module_descriptor_t g_i219v_module = {
    .magic = DRIVER_DESCRIPTOR_MAGIC,
    .version = DRIVER_DESCRIPTOR_VERSION,
    .kind = DEVICE_TYPE_NIC,
    .load_priority = 50u,               // Same as VirtIONet
    .deps = { "PCI_Driver.ELF", NULL },
    .driver_api = &g_i219v_driver,
    .shutdown = i219v_shutdown,
};

const driver_module_descriptor_t *driver_module_init(const driver_binary_t *api) {
    if (api == NULL ||
        api->malloc == NULL || api->free == NULL ||
        api->dma_alloc == NULL || api->memset == NULL ||
        api->memcpy == NULL || api->hw.map_mmio_range == NULL ||
        api->pci_read_config == NULL || api->pci_write_config == NULL) {
        return NULL;
    }
    g_api = api;
    return &g_i219v_module;
}
#endif
```

---

## Risks & Caveats

1. **32-bit DMA constraint**: I219-V requires DMA addresses below 4GB. Use `dma_alloc_ex()` with `max_address = 0xFFFFFFFF`.
2. **PHY management**: Link status is available from STATUS register, but full PHY configuration may require MDIO (MDIC register) access. The integrated PHY in I219-V typically negotiates automatically.
3. **NVM/EEPROM access**: MAC address might require reading from NVM via the EEC register if the RAL/RAH registers return invalid values.
4. **Device ID coverage**: There are 40+ known I219-V/LM device IDs. The table above covers the most common ones. On real hardware, probe the device first and add IDs iteratively.
5. **QEMU compatibility**: QEMU's `e1000e` device model uses PCI device ID `0x10D3` or `0x10DD` — these are NOT I219 devices. For QEMU testing, you can use a generic Intel Gigabit device model or add these IDs to a secondary match entry with a compatibility code path.
6. **Checksum offloading**: Disabled in initial implementation. The stack handles checksums in software.
7. **Legacy interrupt fallback**: If MSI-X fails (unlikely on real I219-V), consider implementing legacy INTx via IOAPIC as fallback.

---

## Development Roadmap

| Step | Description | Est. Time |
|---|---|---|
| 1 | Create project files (Makefile, I219V.h, I219V.c skeleton) | 1h |
| 2 | PCI device scan + MMIO mapping | 2h |
| 3 | Device init (reset, MSI-X, MAC read) | 4h |
| 4 | RX ring + buffer pool + RX poll path | 4h |
| 5 | TX ring + send path | 3h |
| 6 | Interrupt handlers | 2h |
| 7 | Module registration + build integration | 1h |
| 8 | QEMU testing (e1000e device model) | 3h |
| **Total** | | **~20h** |

## QEMU Test Command

```bash
qemu-system-x86_64 -machine q35 -m 4096 \
  -cdrom Image/ImplusOS.iso \
  -netdev user,id=net0 \
  -device e1000e,netdev=net0
```

Note: QEMU's `e1000e` uses device ID `0x10D3` (not I219), so include it as an additional device ID in the table for testing purposes.

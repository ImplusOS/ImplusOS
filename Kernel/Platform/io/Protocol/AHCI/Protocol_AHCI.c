#include "Protocol_AHCI.h"
#include "../../IO_Main.h"
#include "Drivers/Module/DriverManager.h"
#include "MemoryManagement/DMA_Memory.h"
#include "mmu/Paging_Main.h"
#include <string.h>


#define AHCI_GHC   0x04u   
#define AHCI_PI    0x0Cu   


#define P_CLB   0x00u  
#define P_CLBU  0x04u  
#define P_FB    0x08u  
#define P_FBU   0x0Cu  
#define P_IS    0x10u  
#define P_CMD   0x18u  
#define P_TFD   0x20u  
#define P_SIG   0x24u  
#define P_SSTS  0x28u  
#define P_SERR  0x30u  
#define P_CI    0x38u  

#define SATA_SIG   0x00000101u
#define ATAPI_SIG  0xEB140101u
#define ATA_CMD_READ_DMA_EXT  0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define AHCI_DMA_SECTORS 64u
#define AHCI_SECTOR_SIZE 512u
#define AHCI_MAX_DEVICES 32u
#define AHCI_CLB_BYTES   4096u
#define AHCI_FIS_BYTES   4096u
#define AHCI_CTB_BYTES   4096u
#define AHCI_DMA_BYTES   (AHCI_DMA_SECTORS * AHCI_SECTOR_SIZE)


typedef struct __attribute__((packed)) {
    uint16_t flags;    
    uint16_t prdtl;    
    uint32_t prdbc;    
    uint32_t ctba;     
    uint32_t ctbau;    
    uint32_t rsv[4];
} ahci_cmd_hdr_t;      

typedef struct __attribute__((packed)) {
    uint32_t dba;      
    uint32_t dbau;     
    uint32_t rsv;
    uint32_t dbc;      
} ahci_prdt_t;         

typedef struct __attribute__((packed)) {
    uint8_t    cfis[64];    
    uint8_t    acmd[16];    
    uint8_t    rsv[48];
    ahci_prdt_t prdt[1];   
} ahci_cmd_table_t;         

static ahci_cmd_hdr_t   *s_clb  = NULL;
static uint8_t          *s_fis  = NULL;
static ahci_cmd_table_t *s_ctbl = NULL;
static uint8_t          *s_dma  = NULL;
static uint64_t          s_clb_phys  = 0;
static uint64_t          s_fis_phys  = 0;
static uint64_t          s_ctbl_phys = 0;
static uint64_t          s_dma_phys  = 0;

typedef struct {
    int      port;
    bool     atapi;
    uint64_t total_bytes;
} ahci_device_t;

static uintptr_t     g_abar    = 0;
static int           g_port    = -1;
static bool          g_working = false;
static bool          g_atapi   = false;
static ahci_device_t g_devices[AHCI_MAX_DEVICES];
static uint32_t      g_device_count = 0;
static uint32_t      g_current_device = 0;


static uint32_t g_cached_block = 0xFFFFFFFFu;

static void ahci_free_dma_buffers(void) {
    if (s_clb)  { dma_free(s_clb,  AHCI_CLB_BYTES); s_clb = NULL; }
    if (s_fis)  { dma_free(s_fis,  AHCI_FIS_BYTES); s_fis = NULL; }
    if (s_ctbl) { dma_free(s_ctbl, AHCI_CTB_BYTES); s_ctbl = NULL; }
    if (s_dma)  { dma_free(s_dma,  AHCI_DMA_BYTES); s_dma = NULL; }
    s_clb_phys = 0;
    s_fis_phys = 0;
    s_ctbl_phys = 0;
    s_dma_phys = 0;
}

static bool ahci_ensure_dma_buffers(void) {
    if (s_clb && s_fis && s_ctbl && s_dma) return true;

    if (!dma_init()) return false;

    s_clb = (ahci_cmd_hdr_t *)dma_alloc(AHCI_CLB_BYTES, &s_clb_phys);
    s_fis = (uint8_t *)dma_alloc(AHCI_FIS_BYTES, &s_fis_phys);
    s_ctbl = (ahci_cmd_table_t *)dma_alloc(AHCI_CTB_BYTES, &s_ctbl_phys);
    s_dma = (uint8_t *)dma_alloc(AHCI_DMA_BYTES, &s_dma_phys);

    if (!s_clb || !s_fis || !s_ctbl || !s_dma) {
        ahci_free_dma_buffers();
        return false;
    }

    return true;
}


static inline uint32_t hba_rd(uint32_t off) {
    return *(volatile uint32_t *)((uintptr_t)(g_abar + off));
}
static inline void hba_wr(uint32_t off, uint32_t v) {
    *(volatile uint32_t *)((uintptr_t)(g_abar + off)) = v;
}
static inline uint32_t port_rd(int p, uint32_t r) {
    return hba_rd(0x100u + (uint32_t)p * 0x80u + r);
}
static inline void port_wr(int p, uint32_t r, uint32_t v) {
    hba_wr(0x100u + (uint32_t)p * 0x80u + r, v);
}


static bool port_stop(int p) {
    uint32_t cmd = port_rd(p, P_CMD);
    cmd &= ~((1u << 0) | (1u << 4));   
    port_wr(p, P_CMD, cmd);
    for (uint32_t t = 500000u; t; --t) {
        cmd = port_rd(p, P_CMD);
        if (!(cmd & ((1u << 14) | (1u << 15)))) return true; 
    }
    return false;
}

static void port_start(int p) {
    for (uint32_t t = 500000u; t; --t)
        if (!(port_rd(p, P_CMD) & (1u << 15))) break;

    uint32_t cmd = port_rd(p, P_CMD);
    cmd |= (1u << 4);   
    port_wr(p, P_CMD, cmd);
    cmd |= (1u << 0);   
    port_wr(p, P_CMD, cmd);
}

static void port_setup(int p) {
    port_stop(p);

    memset(s_clb, 0, AHCI_CLB_BYTES);
    memset(s_fis, 0, AHCI_FIS_BYTES);

    port_wr(p, P_CLB,  (uint32_t)s_clb_phys);
    port_wr(p, P_CLBU, (uint32_t)(s_clb_phys >> 32));
    port_wr(p, P_FB,   (uint32_t)s_fis_phys);
    port_wr(p, P_FBU,  (uint32_t)(s_fis_phys >> 32));

    port_wr(p, P_SERR, port_rd(p, P_SERR));
    port_wr(p, P_IS,   port_rd(p, P_IS));

    
    s_clb[0].ctba  = (uint32_t)s_ctbl_phys;
    s_clb[0].ctbau = (uint32_t)(s_ctbl_phys >> 32);

    port_start(p);
}


static bool atapi_read_one(uint32_t lba2048) {
    if (lba2048 == g_cached_block) return true;
    
    for (uint32_t t = 1000000u; t; --t) {
        uint32_t tfd = port_rd(g_port, P_TFD);
        if (!((tfd & 0x80u) || (tfd & 0x08u))) break;
        if (t == 1u) return false;
    }

    for (uint32_t t = 1000000u; t; --t) {
        if (!port_rd(g_port, P_CI)) break;
        if (t == 1u) return false;
    }

    port_wr(g_port, P_SERR, port_rd(g_port, P_SERR));
    port_wr(g_port, P_IS,   port_rd(g_port, P_IS));

    s_clb[0].flags =
    (5u & 0x1Fu) |
    (1u << 5);
    s_clb[0].prdtl = 1u;
    s_clb[0].prdbc = 0u;

    memset(s_ctbl, 0, sizeof(*s_ctbl));

    s_ctbl->cfis[0] = 0x27u;
    s_ctbl->cfis[1] = 0x80u;
    s_ctbl->cfis[2] = 0xA0u;
    s_ctbl->cfis[3] = 0x01u;
    s_ctbl->cfis[7] = 0xA0u;

    s_ctbl->acmd[0] = 0x28u;
    s_ctbl->acmd[2] = (uint8_t)((lba2048 >> 24) & 0xFFu);
    s_ctbl->acmd[3] = (uint8_t)((lba2048 >> 16) & 0xFFu);
    s_ctbl->acmd[4] = (uint8_t)((lba2048 >>  8) & 0xFFu);
    s_ctbl->acmd[5] = (uint8_t)( lba2048        & 0xFFu);
    s_ctbl->acmd[7] = 0x00u;
    s_ctbl->acmd[8] = 0x01u;

    s_ctbl->prdt[0].dba  = (uint32_t)s_dma_phys;
    s_ctbl->prdt[0].dbau = (uint32_t)(s_dma_phys >> 32);
    s_ctbl->prdt[0].dbc  = (2048u - 1u) | (1u << 31);

    port_wr(g_port, P_CI, 1u);

    for (uint32_t t = 10000000u; t; --t) {
        if (!(port_rd(g_port, P_CI) & 1u)) {
            g_cached_block = lba2048;
            return true;
        }
        uint32_t is = port_rd(g_port, P_IS);
        if (is & (1u << 30)) {
            port_wr(g_port, P_IS, is);
            return false;
        }
    }
    return false;  
}

static bool sata_dma_rw(uint32_t lba, uint32_t sectors, bool write) {
    if (sectors == 0 || sectors > AHCI_DMA_SECTORS) return false;
    if (!s_ctbl || !s_dma) return false;

    for (uint32_t t = 1000000u; t; --t) {
        uint32_t tfd = port_rd(g_port, P_TFD);
        if (!((tfd & 0x80u) || (tfd & 0x08u))) break;
        if (t == 1u) return false;
    }

    for (uint32_t t = 1000000u; t; --t) {
        if (!port_rd(g_port, P_CI)) break;
        if (t == 1u) return false;
    }

    port_wr(g_port, P_SERR, port_rd(g_port, P_SERR));
    port_wr(g_port, P_IS,   port_rd(g_port, P_IS));

    memset(s_ctbl, 0, sizeof(*s_ctbl));
    s_clb[0].flags = (5u & 0x1Fu) | (write ? (1u << 6) : 0u);
    s_clb[0].prdtl = 1u;
    s_clb[0].prdbc = 0u;

    s_ctbl->cfis[0] = 0x27u;
    s_ctbl->cfis[1] = 0x80u;
    s_ctbl->cfis[2] = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    s_ctbl->cfis[4] = (uint8_t)(lba & 0xFFu);
    s_ctbl->cfis[5] = (uint8_t)((lba >> 8) & 0xFFu);
    s_ctbl->cfis[6] = (uint8_t)((lba >> 16) & 0xFFu);
    s_ctbl->cfis[7] = 0x40u;
    s_ctbl->cfis[8] = (uint8_t)((lba >> 24) & 0xFFu);
    s_ctbl->cfis[9] = 0u;
    s_ctbl->cfis[10] = 0u;
    s_ctbl->cfis[12] = (uint8_t)(sectors & 0xFFu);
    s_ctbl->cfis[13] = (uint8_t)((sectors >> 8) & 0xFFu);

    s_ctbl->prdt[0].dba  = (uint32_t)s_dma_phys;
    s_ctbl->prdt[0].dbau = (uint32_t)(s_dma_phys >> 32);
    s_ctbl->prdt[0].dbc  = ((sectors * AHCI_SECTOR_SIZE) - 1u) | (1u << 31);

    port_wr(g_port, P_CI, 1u);

    for (uint32_t t = 10000000u; t; --t) {
        uint32_t ci = port_rd(g_port, P_CI);
        uint32_t is = port_rd(g_port, P_IS);
        if (is & (1u << 30)) {
            port_wr(g_port, P_IS, is);
            return false;
        }
        if ((ci & 1u) == 0u) {
            port_wr(g_port, P_IS, is);
            return true;
        }
    }
    return false;
}

bool ahci_read(uint32_t lba_512, uint8_t *buffer, uint32_t sectors_512) {
    if (!buffer || sectors_512 == 0) return false;

    if (!g_atapi) {
        uint32_t done = 0;
        while (done < sectors_512) {
            uint32_t chunk = sectors_512 - done;
            if (chunk > AHCI_DMA_SECTORS) chunk = AHCI_DMA_SECTORS;
            if (!sata_dma_rw(lba_512 + done, chunk, false)) return false;
            memcpy(buffer + done * AHCI_SECTOR_SIZE, s_dma, chunk * AHCI_SECTOR_SIZE);
            done += chunk;
        }
        g_working = true;
        return true;
    }

    for (uint32_t s = 0; s < sectors_512; ++s) {
        uint32_t byte_off     = (lba_512 + s) * 512u;
        uint32_t block_2048   = byte_off / 2048u;
        uint32_t off_in_block = byte_off % 2048u;

        if (!atapi_read_one(block_2048)) return false;

        memcpy(buffer + s * 512u, s_dma + off_in_block, 512u);
    }

    g_working = true;
    return true;
}

bool ahci_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors) {
    if (!buffer || sectors == 0) return false;
    if (g_atapi) return false;

    uint32_t done = 0;
    while (done < sectors) {
        uint32_t chunk = sectors - done;
        if (chunk > AHCI_DMA_SECTORS) chunk = AHCI_DMA_SECTORS;
        memcpy(s_dma, buffer + done * AHCI_SECTOR_SIZE, chunk * AHCI_SECTOR_SIZE);
        if (!sata_dma_rw(lba + done, chunk, true)) return false;
        done += chunk;
    }
    g_working = true;
    return true;
}

bool ahci_is_working(void) { return g_working; }

uint32_t ahci_get_device_count(void) { return g_device_count; }

bool ahci_select_device(uint32_t index) {
    if (index >= g_device_count) return false;
    g_current_device = index;
    g_port = g_devices[index].port;
    g_atapi = g_devices[index].atapi;
    g_cached_block = 0xFFFFFFFFu;
    return true;
}

uint64_t ahci_get_total_bytes(void) {
    if (g_current_device >= g_device_count) return 0;
    return g_devices[g_current_device].total_bytes;
}

static bool ahci_add_device(int port, bool atapi, uint64_t total_bytes) {
    if (g_device_count >= AHCI_MAX_DEVICES) return false;
    g_devices[g_device_count].port = port;
    g_devices[g_device_count].atapi = atapi;
    g_devices[g_device_count].total_bytes = total_bytes;
    g_device_count++;
    return true;
}

bool ahci_init(uint64_t partition_lba) {
    (void)partition_lba;   
    g_working      = false;
    g_port         = -1;
    g_abar         = 0;
    g_atapi        = false;
    g_device_count = 0;
    g_current_device = 0;
    g_cached_block = 0xFFFFFFFFu;
    memset(g_devices, 0, sizeof(g_devices));

    if (!ahci_ensure_dma_buffers()) {
        return false;
    }

    
    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t dev = 0; dev < 32u; ++dev) {
            for (uint8_t func = 0; func < 8u; ++func) {
                uint32_t cr  = pci_read_config((uint8_t)bus, dev, func, 0x08u);
                uint8_t  cls = (uint8_t)((cr >> 24) & 0xFFu);
                uint8_t  sub = (uint8_t)((cr >> 16) & 0xFFu);

                if (cls != 0x01u || sub != 0x06u) {
                    if (func == 0u) {
                        uint32_t hdr = pci_read_config((uint8_t)bus, dev, 0u, 0x0Cu);
                        if (!((hdr >> 16) & 0x80u)) break;
                    }
                    continue;
                }

                
                uint32_t bar5 = pci_read_config((uint8_t)bus, dev, func, 0x24u);
                if (bar5 & 1u) continue;      
                bar5 &= 0xFFFFFFF0u;
                if (bar5 == 0u) continue;

                
                uint32_t cmd = pci_read_config((uint8_t)bus, dev, func, 0x04u);
                pci_write_config((uint8_t)bus, dev, func, 0x04u, cmd | 0x06u);

                void *abar = map_mmio_virt((uint64_t)bar5);
                if (abar == NULL) continue;
                g_abar = (uintptr_t)abar;

                
                hba_wr(AHCI_GHC, hba_rd(AHCI_GHC) | (1u << 31));

                uint32_t pi = hba_rd(AHCI_PI);
                for (int p = 0; p < 32; ++p) {
                    if (!(pi & (1u << p))) continue;

                    uint32_t ssts = port_rd(p, P_SSTS);
                    if ((ssts & 0xFu) != 3u || ((ssts >> 8) & 0xFu) != 1u) continue;

                    g_port = p;
                    port_setup(p);

                    for (int spin = 0; spin < 1000; spin++) {
                        uint32_t sig = port_rd(p, P_SIG);
                        if (sig == ATAPI_SIG || sig == SATA_SIG) break;
                        
                        for (volatile int delay = 0; delay < 10000; delay++);
                    }

                    uint32_t sig = port_rd(p, P_SIG);
                    if (sig == SATA_SIG) {
                        g_atapi = false;
                        if (!sata_dma_rw(0u, 1u, false)) {
                            port_stop(p);
                            g_port = -1;
                            continue;
                        }
                        ahci_add_device(p, false, 0);
                        continue;
                    }

                    if (sig != ATAPI_SIG) {
                        port_stop(p);
                        g_port = -1;
                        continue;
                    }

                    g_atapi = true;
                    if (!atapi_read_one(0u)) {
                        port_stop(p);
                        g_port = -1;
                        g_atapi = false;
                        continue;
                    }
                    ahci_add_device(p, true, 0);
                    continue;
                }

                if (g_device_count > 0) {
                    ahci_select_device(0);
                    g_working = true;
                    return true;
                }

                g_abar = 0u;
            }
        }
    }
    return false;
}

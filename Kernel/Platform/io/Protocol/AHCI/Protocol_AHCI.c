#include "Protocol_AHCI.h"
#include "../../IO_Main.h"
#include "Drivers/Module/DriverManager.h"
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

#define ATAPI_SIG  0xEB140101u   


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

static ahci_cmd_hdr_t   s_clb[32]  __attribute__((aligned(1024)));
static uint8_t          s_fis[256] __attribute__((aligned(256)));
static ahci_cmd_table_t s_ctbl     __attribute__((aligned(128)));
static uint8_t          s_dma[2048] __attribute__((aligned(4)));

static uint32_t g_abar    = 0;
static int      g_port    = -1;
static bool     g_working = false;


static uint32_t g_cached_block = 0xFFFFFFFFu;


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

    memset(s_clb, 0, sizeof(s_clb));
    memset(s_fis, 0, sizeof(s_fis));

    port_wr(p, P_CLB,  (uint32_t)(uintptr_t)s_clb);
    port_wr(p, P_CLBU, 0u);
    port_wr(p, P_FB,   (uint32_t)(uintptr_t)s_fis);
    port_wr(p, P_FBU,  0u);

    port_wr(p, P_SERR, port_rd(p, P_SERR));
    port_wr(p, P_IS,   port_rd(p, P_IS));

    
    s_clb[0].ctba  = (uint32_t)(uintptr_t)&s_ctbl;
    s_clb[0].ctbau = 0u;

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

    
    memset(&s_ctbl, 0, sizeof(s_ctbl));

    
    s_ctbl.cfis[0] = 0x27u;  
    s_ctbl.cfis[1] = 0x80u;  
    s_ctbl.cfis[2] = 0xA0u;  
    s_ctbl.cfis[3] = 0x00u;  
    s_ctbl.cfis[7] = 0xA0u;  

    
    s_ctbl.acmd[0] = 0xA8u;
    s_ctbl.acmd[2] = (uint8_t)((lba2048 >> 24) & 0xFFu);
    s_ctbl.acmd[3] = (uint8_t)((lba2048 >> 16) & 0xFFu);
    s_ctbl.acmd[4] = (uint8_t)((lba2048 >>  8) & 0xFFu);
    s_ctbl.acmd[5] = (uint8_t)( lba2048        & 0xFFu);
    s_ctbl.acmd[9] = 1u;     

    
    s_ctbl.prdt[0].dba  = (uint32_t)(uintptr_t)s_dma;
    s_ctbl.prdt[0].dbau = 0u;
    s_ctbl.prdt[0].dbc  = (2048u - 1u) | (1u << 31);

    
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


bool ahci_read(uint32_t lba_512, uint8_t *buffer, uint32_t sectors_512) {
    if (!buffer || sectors_512 == 0) return false;

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
    (void)lba; (void)buffer; (void)sectors;
    return false;   
}

bool ahci_is_working(void) { return g_working; }

bool ahci_init(uint64_t partition_lba) {
    (void)partition_lba;   
    g_working      = false;
    g_port         = -1;
    g_abar         = 0;
    g_cached_block = 0xFFFFFFFFu;

    
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

                g_abar = bar5;

                
                hba_wr(AHCI_GHC, hba_rd(AHCI_GHC) | (1u << 31));

                
                uint32_t pi = hba_rd(AHCI_PI);
                for (int p = 0; p < 32; ++p) {
                    if (!(pi & (1u << p))) continue;

                    
                    uint32_t ssts = port_rd(p, P_SSTS);
                    if ((ssts & 0xFu) != 3u || ((ssts >> 8) & 0xFu) != 1u) continue;

                    
                    if (port_rd(p, P_SIG) != ATAPI_SIG) continue;

                    g_port = p;
                    port_setup(p);

                    
                    if (!atapi_read_one(0u)) {
                        g_port = -1;
                        continue;
                    }

                    g_working = true;
                    return true;
                }

                g_abar = 0u;
            }
        }
    }
    return false;
}
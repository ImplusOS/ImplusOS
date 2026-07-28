#ifndef _NETINET_TCP_H
#define _NETINET_TCP_H

/* TCP socket options */
#define TCP_NODELAY        0x01  /* don't delay send to coalesce packets */
#define TCP_MAXSEG         0x02  /* set maximum segment size */
#define TCP_CORK           0x03  /* control sending of partial frames */
#define TCP_KEEPIDLE       0x04  /* start keepalives after this period */
#define TCP_KEEPINTVL      0x05  /* interval between keepalives */
#define TCP_KEEPCNT        0x06  /* number of keepalives before death */
#define TCP_SYNCNT         0x07  /* number of SYN retransmits */
#define TCP_LINGER2        0x08  /* lifetime of orphaned FIN-WAIT-2 state */
#define TCP_DEFER_ACCEPT   0x09  /* wake up listener only when data arrive */
#define TCP_WINDOW_CLAMP   0x0a  /* bound advertised window */
#define TCP_INFO           0x0b  /* retrieve tcp_info structure */
#define TCP_QUICKACK       0x0c  /* block/reenable quick acks */

#define TCP_CONGESTION     0x0d  /* congestion control algorithm */
#define TCP_MD5SIG         0x0e  /* TCP MD5 Signature (RFC2385) */
#define TCP_THIN_LINEAR_TIMEOUTS 0x0f
#define TCP_THIN_DUPACK    0x10
#define TCP_USER_TIMEOUT   0x11
#define TCP_NOTSENT_LOWAT  0x12
#define TCP_REPAIR         0x13
#define TCP_REPAIR_QUEUE   0x14
#define TCP_REPAIR_OPTIONS 0x15
#define TCP_FASTOPEN       0x15
#define TCP_SAVE_SYN       0x16
#define TCP_SAVED_SYN      0x17
#define TCP_REPAIR_WINDOW  0x18

/* TCP_INFO socket option */
struct tcp_info {
    uint8_t  tcpi_state;
    uint8_t  tcpi_ca_state;
    uint8_t  tcpi_retransmits;
    uint8_t  tcpi_probes;
    uint8_t  tcpi_backoff;
    uint8_t  tcpi_options;
    uint8_t  tcpi_snd_wscale : 4, tcpi_rcv_wscale : 4;
    uint32_t tcpi_rto;
    uint32_t tcpi_ato;
    uint32_t tcpi_snd_mss;
    uint32_t tcpi_rcv_mss;
    uint32_t tcpi_unacked;
    uint32_t tcpi_sacked;
    uint32_t tcpi_lost;
    uint32_t tcpi_retrans;
    uint32_t tcpi_fackets;
    uint32_t tcpi_last_data_sent;
    uint32_t tcpi_last_ack_sent;
    uint32_t tcpi_last_data_recv;
    uint32_t tcpi_last_ack_recv;
    uint32_t tcpi_pmtu;
    uint32_t tcpi_rcv_ssthresh;
    uint32_t tcpi_rtt;
    uint32_t tcpi_rttvar;
    uint32_t tcpi_snd_ssthresh;
    uint32_t tcpi_snd_cwnd;
    uint32_t tcpi_advmss;
    uint32_t tcpi_reordering;
    uint32_t tcpi_rcv_rtt;
    uint32_t tcpi_rcv_space;
    uint32_t tcpi_total_retrans;
};

#endif /* _NETINET_TCP_H */
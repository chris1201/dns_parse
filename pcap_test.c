#include <stdio.h>
#include <pcap.h>

#include "rtypebase.h"
#include "types.h"

#define VERBOSE

void handler(u_char *, const struct pcap_pkthdr *, const u_char *);

typedef struct ipv4_info {
    u_char srcip[4];
    u_char dstip[4];
    u_short length;
    u_char proto;
} ipv4_info;
    
typedef struct udp_info {
    u_short srcport;
    u_short dstport;
    u_short length;
} upd_info;

typedef struct dns_header {
    u_short id;
    char qr;
    char AA;
    char TC;
    u_char rcode;
    u_char opcode;
    u_short qdcount;
    dns_rr * queries;
    u_short ancount;
    dns_rr * answers;
    u_short nscount;
    dns_rr * name_servers;
    u_short arcount;
    dns_rr * additional;
} dns_header;


int main() {
    pcap_t * pcap_file;
    char * errors;
    int read;
    u_char * empty = "";
   
    pcap_file = pcap_open_offline("current", errors);

    read = pcap_dispatch(pcap_file, 10, (pcap_handler)handler, empty);
    
    printf("done\n");
}

void print_packet(const struct pcap_pkthdr *header, const u_char *packet,
                  bpf_u_int32 start, bpf_u_int32 end, u_int wrap) {
    int i=0;
    while (i < end - start && (i + start) < header->len) {
        printf("%02x ", packet[i+start]);
        i++;
        if ( i % wrap == 0) printf("\n");
    }
    if ( i % wrap != 0) printf("\n");
    return;
}

bpf_u_int32 parse_eth(const struct pcap_pkthdr *header, const u_char *packet) {
    u_char dstmac[6], srcmac[6];
    bpf_u_int32 pos = 0;

    int i;

    if (header->len < 14) {
        printf("Truncated Packet(eth)\n");
        return 0;
    }

    while (pos < 6) {
        dstmac[pos] = packet[pos];
        srcmac[pos] = packet[pos+6];
        pos++;
    }
    pos = pos + 6;

    // Skip VLAN tagging 
    if (packet[pos] == 0x81 && packet[pos+1] == 0) pos = pos + 4;

    if (packet[pos] != 0x08 || packet[pos+1] != 0) {
        printf("Unsupported EtherType: %02x%02x\n", packet[pos], 
                                                    packet[pos+1]);
        for (i=0; i<pos+2; i++) 
            printf("%02x ", packet[i]);
        printf("\n");
        return 0;
    }
    pos = pos + 2;

    #ifdef VERBOSE
    #ifdef SHOW_RAW
    printf("\neth ");
    print_packet(header, packet, 0, pos, 18);
    #endif
    printf("dstmac: %02x:%02x:%02x:%02x:%02x:%02x, "
           "srcmac: %02x:%02x:%02x:%02x:%02x:%02x\n",
           dstmac[0],dstmac[1],dstmac[2],dstmac[3],dstmac[4],dstmac[5],
           srcmac[0],srcmac[1],srcmac[2],srcmac[3],srcmac[4],srcmac[5]);
    #endif
    return pos;

}

bpf_u_int32 parse_ipv4(bpf_u_int32 pos, const struct pcap_pkthdr *header, 
                      const u_char *packet, ipv4_info * ipv4) {

    bpf_u_int32 version, h_len;
    int i;

    if (header-> len - pos < 20) {
        printf("Truncated Packet(ipv4)\n");
        return 0;
    }
    
    version = packet[pos] >> 4;
    h_len = packet[pos] & 0x0f;
    ipv4->length = (packet[pos+2] << 8) + packet[pos+3];
    ipv4->proto = packet[pos+9];

    for (i=0; i<4; i++) {
        ipv4->srcip[i] = packet[pos + 12 + i];
        ipv4->dstip[i] = packet[pos + 16 + i];
    }

    #ifdef VERBOSE
    #ifdef SHOW_RAW
    printf("\nipv4\n");
    print_packet(header, packet, pos, pos + 4*h_len, 4);
    #endif
    printf("version: %d, length: %d, proto: %d\n", 
            version, ipv4->length, ipv4->proto);
    printf("srcip: %d.%d.%d.%d, dstip: %d.%d.%d.%d\n",
           ipv4->srcip[0], ipv4->srcip[1], ipv4->srcip[2], ipv4->srcip[3],
           ipv4->dstip[0], ipv4->dstip[1], ipv4->dstip[2], ipv4->dstip[3]);
    #endif

    // move the position up past the options section.
    pos = pos + 4*h_len;
    return pos;
}

bpf_u_int32 parse_udp(bpf_u_int32 pos, const struct pcap_pkthdr *header, 
                      const u_char *packet, upd_info * udp) {
    u_short test;
    if (header->len - pos < 8) {
        printf("Truncated Packet(udp)\n");
        return 0;
    }

    udp->srcport = (packet[pos] << 8) + packet[pos+1];
    udp->dstport = (packet[pos+2] << 8) + packet[pos+3];
    udp->length = (packet[pos+4] << 8) + packet[pos+5];
    #ifdef VERBOSE
    #ifdef SHOW_RAW
    printf("udp\n");
    print_packet(header, packet, pos, pos + 8, 4);
    #endif
    printf("srcport: %d, dstport: %d, len: %d\n", udp->srcport, udp->dstport, 
                                                  udp->length);
    #endif
    return pos + 8;
}

bpf_u_int32 parse_dns(bpf_u_int32 pos, const struct pcap_pkthdr *header, 
                      const u_char *packet, dns_header * dns) {
    if (header->len - pos < 12) {
        printf("Truncated Packet(dns)\n");
        return 0;
    }

    dns->id = (packet[pos] << 8) + packet[pos+1];
    dns->qr = packet[pos+2] >> 7;
    dns->AA = (packet[pos+2] & 0x04) >> 2;
    dns->TC = (packet[pos+2] & 0x02) >> 1;
    dns->rcode = packet[pos + 3] & 0x0f;
    dns->qdcount = (packet[pos+4] << 8) + packet[pos+5];
    dns->ancount = (packet[pos+6] << 8) + packet[pos+7];
    dns->nscount = (packet[pos+8] << 8) + packet[pos+9];
    dns->arcount = (packet[pos+10] << 8) + packet[pos+11];
    #ifdef VERBOSE
    #ifdef SHOW_RAW
    printf("dns\n");
    print_packet(header, packet, pos, pos + 12, 2);
    #endif
    printf("DNS id:%d, qr:%d, AA:%d, TC:%d, rcode:%d\n", 
           dns->id, dns->qr, dns->AA, dns->TC, dns->rcode);
    printf("DNS qdcount:%d, ancount:%d, nscount:%d, arcount:%d\n",
           dns->qdcount, dns->ancount, dns->nscount, dns->arcount);
    #endif
    return pos + 12;
}

void handler(u_char * args, const struct pcap_pkthdr *header, 
             const u_char *packet) {
    int pos;
    u_char proto;
    struct ipv4_info ipv4;
    struct udp_info udp;
    struct dns_header dns;

    #ifdef VERBOSE
    printf("\nPacket %d.%d\n", header->ts.tv_sec, header->ts.tv_usec);
    #endif

    pos = parse_eth(header, packet);
    if (pos == 0) return;
    pos = parse_ipv4(pos, header, packet, &ipv4);
    if ( pos == 0) return;
    if (ipv4.proto != 17) {
        printf("Unsupported Protocol(%d)", ipv4.proto);
        return;
    }
    
    pos = parse_udp(pos, header, packet, &udp);
    if ( pos == 0 ) return;

    pos = parse_dns(pos, header, packet, &dns);
    
}

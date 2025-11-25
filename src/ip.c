#include "ip.h"

#include "arp.h"
#include "ethernet.h"
#include "icmp.h"
#include "net.h"

static uint16_t ip_id = 0;

void ip_set_head(ip_hdr_t *hdr, int total_len, uint16_t id, uint16_t offset, int mf, net_protocol_t protocol, uint8_t *ip);

/**
 * @brief 处理一个收到的数据包
 *
 * @param buf 要处理的数据包
 * @param src_mac 源mac地址
 */
void ip_in(buf_t *buf, uint8_t *src_mac) {
    // TO-DO
    if(buf->len < sizeof(ip_hdr_t)) {
        return;
    }

    ip_hdr_t *hdr = (ip_hdr_t *)buf->data;

    uint16_t total_len = swap16(hdr->total_len16);
    if(hdr->version != IP_VERSION_4 || 
       total_len > buf->len || 
       total_len < sizeof(ip_hdr_t) ||
       hdr->ttl == 0) {
        return;
    }

    uint16_t hdr_checksum16_origin = hdr->hdr_checksum16;
    hdr->hdr_checksum16 = 0;
    if(hdr_checksum16_origin != checksum16((uint16_t *)hdr, (IP_HDR_LEN_PER_BYTE * hdr->hdr_len) / 2)) {
        return;
    }
    hdr->hdr_checksum16 = hdr_checksum16_origin;

    if(memcmp(hdr->dst_ip, net_if_ip, NET_IP_LEN) != 0) {
        return;
    }

    if(buf->len > total_len) {
        if(buf_remove_padding(buf, buf->len - total_len) < 0)
            return;
    }

    net_protocol_t protocol = hdr->protocol;
    uint8_t src_ip[NET_IP_LEN];
    memcpy(src_ip, hdr->src_ip, NET_IP_LEN);
    uint8_t hdr_len = hdr->hdr_len;

    if(buf_remove_header(buf, IP_HDR_LEN_PER_BYTE * hdr_len) < 0) {
        return;
    }

    if(net_in(buf, protocol, src_ip) < 0) {
        // 协议不可达，恢复IP报头后发送ICMP
        if(buf_add_header(buf, IP_HDR_LEN_PER_BYTE * hdr_len) >= 0) {
            icmp_unreachable(buf, src_ip, ICMP_CODE_PROTOCOL_UNREACH);
        }
    }
}
/**
 * @brief 处理一个要发送的ip分片
 *
 * @param buf 要发送的分片
 * @param ip 目标ip地址
 * @param protocol 上层协议
 * @param id 数据包id
 * @param offset 分片offset，必须被8整除
 * @param mf 分片mf标志，是否有下一个分片
 */
void ip_fragment_out(buf_t *buf, uint8_t *ip, net_protocol_t protocol, uint16_t id, uint16_t offset) {
    // TO-DO
    if(buf->len > ETHERNET_MAX_TRANSPORT_UNIT - sizeof(ip_hdr_t)) {
        
        buf_t fragment_buf;
        buf_init(&fragment_buf, ETHERNET_MAX_TRANSPORT_UNIT - sizeof(ip_hdr_t));
        
        memcpy(fragment_buf.data, buf->data, ETHERNET_MAX_TRANSPORT_UNIT - sizeof(ip_hdr_t));
        
        buf_add_header(&fragment_buf, sizeof(ip_hdr_t));
        
        ip_hdr_t *hdr = (ip_hdr_t *)fragment_buf.data;
        ip_set_head(hdr, ETHERNET_MAX_TRANSPORT_UNIT, id, offset, 1, protocol, ip);
        
        arp_out(&fragment_buf, ip);
        
        buf_remove_header(buf, ETHERNET_MAX_TRANSPORT_UNIT - sizeof(ip_hdr_t));
        
        uint16_t next_offset = offset + (ETHERNET_MAX_TRANSPORT_UNIT - sizeof(ip_hdr_t)) / 8;
        
        ip_fragment_out(buf, ip, protocol, id, next_offset);
    } else {
        buf_add_header(buf, sizeof(ip_hdr_t));
        
        ip_hdr_t *hdr = (ip_hdr_t *)buf->data;
        ip_set_head(hdr, buf->len, id, offset, 0, protocol, ip);
        
        arp_out(buf, ip);
    }
}

/**
 * @brief 处理一个要发送的ip数据包
 *
 * @param buf 要处理的包
 * @param ip 目标ip地址
 * @param protocol 上层协议
 */
void ip_out(buf_t *buf, uint8_t *ip, net_protocol_t protocol) {
    // TO-DO
    ip_fragment_out(buf, ip, protocol, ip_id, 0);
    ip_id++;
}

/**
 * @brief 为IP分片设置头部
 * @param hdr 要设置的IP头部字段
 * @param total_len 分片总长度
 * @param id 分片所属数据包id
 * @param offset 分片offset(已整除8)
 * @param mf mf标志
 * @param protocol 上层协议
 * @param ip 目的ip地址
 */
void ip_set_head(ip_hdr_t *hdr, int total_len, uint16_t id, uint16_t offset, int mf, net_protocol_t protocol, uint8_t *ip) {
    hdr->hdr_len = 5;
    hdr->version = 4;
    hdr->tos = 0;

    hdr->total_len16 = swap16(total_len);

    hdr->id16 = swap16(id);

    uint16_t flags_fragment16 = offset | (mf << 13);
    hdr->flags_fragment16 = swap16(flags_fragment16);
    
    hdr->ttl = 64;

    hdr->protocol = protocol;
    hdr->hdr_checksum16 = 0;

    memcpy(hdr->src_ip, net_if_ip, NET_IP_LEN);

    memcpy(hdr->dst_ip, ip, NET_IP_LEN);

    hdr->hdr_checksum16 = checksum16((uint16_t *)hdr, (IP_HDR_LEN_PER_BYTE * hdr->hdr_len) / 2);
}

/**
 * @brief 初始化ip协议
 *
 */
void ip_init() {
    net_add_protocol(NET_PROTOCOL_IP, ip_in);
}
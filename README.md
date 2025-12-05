# 计算机网络2025实验 协议栈
- lab 1 虚拟网络配置 
- lab 2 以太网协议
- lab 3 ARP协议
- lab 4 路由器配置
- lab 5 IPv4协议
- lab 6 ICMP协议
- lab 7 UDP协议
- lab 8 IPv6物理过渡配置
- lab 9 TCP协议和Web服务器设计

## 协议栈整体流程图

```mermaid
%%{init: {'theme':'base', 'themeVariables': { 'primaryColor':'#e3f2fd','primaryTextColor':'#01579b','primaryBorderColor':'#1976d2','lineColor':'#424242','secondaryColor':'#fff3e0','tertiaryColor':'#f3e5f5'}}}%%
graph TB
    subgraph Main["主循环 - net_poll()"]
        direction TB
        POLL[net_poll<br/>主循环入口] --> EPOLL[ethernet_poll<br/>轮询网卡]
        EPOLL --> TIMER[定时任务<br/>ARP超时检查<br/>ICMP统计]
        EPOLL --> RX_START
    end
    
    subgraph RX["接收路径 (RX Path)"]
        direction TB
        RX_START[driver_recv<br/>网卡接收] --> ETH_IN[ethernet_in<br/>解析以太网帧]
        
        ETH_IN -->|ARP 0x0806| ARP_PROC[arp_in<br/>ARP处理]
        ETH_IN -->|IP 0x0800| IP_PROC[ip_in<br/>IP层处理]
        
        ARP_PROC -->|Reply| ARP_UPDATE[更新ARP表<br/>发送缓存包]
        ARP_PROC -->|Request| ARP_RESP[arp_resp<br/>ARP应答]
        
        IP_PROC --> FRAG_CHECK{是否分片?}
        FRAG_CHECK -->|是| REASM[ip_reassemble<br/>IP重组]
        FRAG_CHECK -->|否| NET_DISP[net_in<br/>协议分发]
        REASM --> NET_DISP
        
        NET_DISP -->|ICMP 1| ICMP_PROC[icmp_in<br/>ICMP处理]
        NET_DISP -->|UDP 17| UDP_PROC[udp_in<br/>UDP处理]
        NET_DISP -->|TCP 6| TCP_PROC[tcp_in<br/>TCP处理]
        
        ICMP_PROC -->|Echo Req| ICMP_RESP[icmp_resp<br/>发送回显]
        ICMP_PROC -->|Echo Reply| ICMP_STAT[更新RTT统计]
        
        UDP_PROC --> UDP_APP[udp_handler<br/>UDP应用层]
        
        TCP_PROC --> TCP_SM[TCP状态机<br/>SYN/ACK处理]
        TCP_SM --> TCP_APP[tcp_handler<br/>TCP应用层]
        TCP_APP -->|HTTP| WEB[web_server<br/>HTTP处理]
    end
    
    subgraph TX["发送路径 (TX Path)"]
        direction TB
        APP_START[应用层发送] --> PROTO_SEL{选择协议}
        
        PROTO_SEL -->|UDP| UDP_OUT[udp_out<br/>UDP封装]
        PROTO_SEL -->|TCP| TCP_OUT[tcp_out<br/>TCP封装]
        PROTO_SEL -->|ICMP| ICMP_OUT[icmp_echo_request<br/>ICMP请求]
        
        UDP_OUT --> IP_OUT[ip_out<br/>IP封装]
        TCP_OUT --> IP_OUT
        ICMP_OUT --> IP_OUT
        
        IP_OUT --> FRAG_CHK{MTU检查<br/>需要分片?}
        FRAG_CHK -->|是 >1500| IP_FRAG[ip_fragment_out<br/>IP分片]
        FRAG_CHK -->|否| ARP_RESOLVE[arp_out<br/>ARP解析]
        IP_FRAG --> ARP_RESOLVE
        
        ARP_RESOLVE --> ARP_CHK{ARP表<br/>有MAC?}
        ARP_CHK -->|有| ETH_OUT[ethernet_out<br/>以太网封装]
        ARP_CHK -->|无| ARP_REQ[arp_req<br/>发送ARP请求]
        ARP_REQ --> ARP_BUF[缓存到arp_buf<br/>等待ARP应答]
        
        ETH_OUT --> TX_END[driver_send<br/>网卡发送]
    end
    
    %% 连接主循环与接收发送
    EPOLL -.->|触发| RX_START
    ARP_UPDATE -.->|触发缓存包| TX
    
    %% 样式定义
    classDef startNode fill:#4caf50,stroke:#2e7d32,stroke-width:3px,color:#fff
    classDef endNode fill:#f44336,stroke:#c62828,stroke-width:3px,color:#fff
    classDef processNode fill:#2196f3,stroke:#1565c0,stroke-width:2px,color:#fff
    classDef decisionNode fill:#ff9800,stroke:#e65100,stroke-width:2px,color:#fff
    classDef appNode fill:#9c27b0,stroke:#6a1b9a,stroke-width:2px,color:#fff
    
    class RX_START,APP_START startNode
    class TX_END endNode
    class ETH_IN,IP_PROC,ARP_PROC,ICMP_PROC,UDP_PROC,TCP_PROC,IP_OUT,ETH_OUT processNode
    class FRAG_CHECK,FRAG_CHK,ARP_CHK,PROTO_SEL decisionNode
    class UDP_APP,TCP_APP,WEB appNode
```

### 主要函数调用关系说明

**接收路径 (RX)：**
- `driver_recv()` → `ethernet_in()` → 根据协议类型分发
- ARP: `arp_in()` → 更新ARP表 / `arp_resp()` 发送响应
- IP: `ip_in()` → 分片重组（如需）→ `net_in()` → 协议分发
- ICMP: `icmp_in()` → `icmp_resp()` 回显应答 / 统计RTT
- UDP: `udp_in()` → 调用注册的应用层处理函数
- TCP: `tcp_in()` → TCP状态机 → 应用层处理 → Web服务器

**发送路径 (TX)：**
- 应用层 → `udp_out()` / `tcp_out()` / `icmp_echo_request()`
- → `ip_out()` → 分片（如需）→ `arp_out()` 
- → 查ARP表 → `ethernet_out()` → `driver_send()`
- 若无MAC则 `arp_req()` 并缓存包到 `arp_buf`

**主循环：**
- `net_poll()` → `ethernet_poll()` → 轮询接收 + 定时任务处理
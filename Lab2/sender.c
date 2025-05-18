#include "rtp.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>   
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>

#define MAX_RETRIES 50
#define TIMEOUT_MS 100
#define HANDSHAKE_TIMEOUT_SEC 5
#define MAX_DATA_SUM 100000

rtp_packet_t Data[MAX_DATA_SUM];             // 将文件数据按照MTU分片存储到packet类型的Data数组里

// 作为全局变量，函数共用
int Seq_Num_Global=1; 

// 创建一个socket，并判断是否判断成功
int create_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation ERROR");
        exit(EXIT_FAILURE);
    }
    return sock;
}

// 函数：发送一个packet
int send_packet(int sock, struct sockaddr_in *Addr, rtp_packet_t *packet) {
    // 先置0，避免影响checksum计算
    packet->rtp.checksum = 0;
    packet->rtp.checksum = compute_checksum(packet, sizeof(rtp_header_t) + packet->rtp.length); // 计算校验和
    
    ssize_t sent_bytes = sendto(sock, packet, sizeof(rtp_header_t) + packet->rtp.length, 0,
                                (struct sockaddr *)Addr, sizeof(*Addr));
    if (sent_bytes < 0) {
        perror("send_packet ERROR");
        return -1;
    }
    return 0;
}

// 函数：接收一个packet 
int receive_packet(int sock, struct sockaddr_in *Addr, rtp_packet_t *packet) {
    socklen_t addr_len = sizeof(*Addr);
    ssize_t received_bytes = recvfrom(sock, &packet->rtp, sizeof(rtp_header_t) + PAYLOAD_MAX, 0,
                                      (struct sockaddr *)Addr, &addr_len);
    if (received_bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1; // Timeout
        } else {
            perror("receive_packet ERROR");
            return -1;
        }
    }

    // 如果length出现错误，直接和校验和错误一样，丢弃
    if(packet->rtp.length > PAYLOAD_MAX)
        return 2;
    if(packet->rtp.length > 0 && packet->rtp.flags != 0)
        return 2;

    //if(packet->rtp.length > 0)
    //    recvfrom(sock, packet->payload, packet->rtp.length, 0,(struct sockaddr *)Addr, &addr_len);
        
    // 校验校验和
    uint32_t received_checksum = packet->rtp.checksum;
    packet->rtp.checksum = 0; // 清空校验和字段用于校验
    if (received_checksum != compute_checksum(packet,sizeof(rtp_header_t)+ packet->rtp.length)){
        fprintf(stderr, "receive_packet: checksum mismatch, discarding packet\n");
        return 2; // 校验和错误 
    }
    return 0;
}


// 函数：三次握手建立连接
int Connection_Init(int sock, struct sockaddr_in *dstAddr) 
{
    //struct sockaddr_in lstAddr;
    rtp_packet_t packet;
    rtp_packet_t response;
    int retries = 0;

    // 设置套接字超时时间
    struct timeval timeout = {0, TIMEOUT_MS * 1000}; // 100ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // 第一次握手：Sender -> Receiver (SYN)
    //srand(time(NULL));
    //uint32_t seq_num = rand() % 1000000;  // 获取任意的seq_num
    uint32_t seq_num=0;            // 直接设置初始seq_num为0
    packet.rtp.seq_num = seq_num;
    packet.rtp.length = 0;
    packet.rtp.flags = RTP_SYN;

    Seq_Num_Global=seq_num;

    struct timeval start_time, current_time;
    
    // 等待回复，100ms内未收到回复则重新发送，尝试MAX_RETRIES次
    while (retries < MAX_RETRIES) 
    {
        if (send_packet(sock, dstAddr, &packet) < 0) 
            return -1;
        LOG_DEBUG("Successfully send ACK\n");

        // 等待接收 SYNACK 报文
        gettimeofday(&start_time, NULL);
        current_time=start_time;
        long elapsed_time = (current_time.tv_sec - start_time.tv_sec) * 1000000 + (current_time.tv_usec - start_time.tv_usec);

        while (elapsed_time < TIMEOUT_MS*1000) 
        {
            int recv_status = receive_packet(sock, dstAddr, &response);
            if (recv_status == 0 && (response.rtp.flags & (RTP_SYN | RTP_ACK)) == (RTP_SYN | RTP_ACK) && response.rtp.seq_num == seq_num + 1) 
            {
                LOG_DEBUG("Connection_Init: Received SYNACK, seq_num=%u\n", response.rtp.seq_num);
                goto second_handshake;
            } 
            else if (recv_status == -1) 
                return -1; // 接收错误
            
            // 其他情况：丢弃报文
            gettimeofday(&current_time, NULL);
            elapsed_time = (current_time.tv_sec - start_time.tv_sec) * 1000000 + (current_time.tv_usec - start_time.tv_usec);
        }

        // 超时，重新发送
        LOG_DEBUG("Connection_Init: SYN timeout, retrying...\n");
        retries++;
    }

    fprintf(stderr, "Connection_Init: Failed to establish connection, no response to SYN\n");
    return -1;

// 第二次握手成功收到Receiver回复
second_handshake:
    retries = 0;

    // 第三次握手: Sender -> Receiver (ACK)
    packet.rtp.seq_num = seq_num + 1;
    packet.rtp.length = 0;
    packet.rtp.flags = RTP_ACK;
    LOG_DEBUG("Connection_Init: Sending ACK, seq_num=%u\n", packet.rtp.seq_num);

    while (retries < MAX_RETRIES) 
    {
        if (send_packet(sock, dstAddr, &packet) < 0) 
            return -1;
        LOG_DEBUG("Successfully send second-ACK\n");

        // 等待 2s 查看是否需要重发
        sleep(2);
        int recv_status = receive_packet(sock, dstAddr, &response);
        if (recv_status == 0 && (response.rtp.flags & (RTP_SYN | RTP_ACK)) == (RTP_SYN | RTP_ACK) && response.rtp.seq_num == seq_num + 1) 
            LOG_DEBUG("Connection_Init: Receiver did not receive ACK, resending...\n");
        else if (recv_status == 1) 
        {
            LOG_DEBUG("Connection_Init: Connection established successfully\n");
            return 0; // 没有收到回复，成功建立连接
        } 
        else if (recv_status == -1) 
            return -1; // 接收错误

        retries++;
    }

    fprintf(stderr, "Connection_Init: Failed to establish connection, receiver did not confirm ACK\n");
    return -1;
}

// 函数：发送数据
int Send_Message(int sock, struct sockaddr_in *dstAddr, const char *file_path, int mode, int window_size)
{
    // 打开要读取数据的文件
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Failed to open file");
        return -1;
    }

    // 变量初始化
    int window_base = Seq_Num_Global+1;          // 滑动窗口的起始位置
    int next_seq_num = 0;                        // 下一个待发送的数据包序列号
    int Data_Status[MAX_DATA_SUM];               //存储数据包状态，0表示未发送，1表示已发送未确认，2表示已发送已确认
    memset(Data_Status, 0, sizeof(Data_Status));
    int Data_num=0;                              //存储有效需要发送数据包个数

    int start=0, end=window_size -1 ;            // 维护滑动窗口的起始

    int eof_reached=0; //标记是否已读取到文件末尾

    // 设置套接字超时时间
    struct timeval timeout = {0, TIMEOUT_MS * 1000}; // 100ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct timeval start_time, current_time;

    //读取文件数据，得到所有要发送的数据包
    for (int i = 0; i < MAX_DATA_SUM; i++) {
        rtp_packet_t *packet = &Data[i];
        memset(packet, 0, sizeof(rtp_packet_t));
        packet->rtp.seq_num = window_base + i;
        packet->rtp.flags = 0; // 普通数据包

        // 从文件读取数据
        size_t bytes_read =0;
        if(eof_reached)
            break;
        bytes_read = fread(packet->payload, 1, PAYLOAD_MAX, file);


        if (bytes_read > 0) 
        {
            Data_num++;
            packet->rtp.length = bytes_read;
            packet->rtp.checksum = compute_checksum(packet, sizeof(rtp_header_t) + packet->rtp.length);
            Seq_Num_Global = packet->rtp.seq_num; // 维护全局seq_num变量
            //LOG_DEBUG("Send_Message: Load packet seq_num=%u into window\n", packet->rtp.seq_num);
        } 
        else
        {
            LOG_DEBUG("%d\n",Data_num);
            eof_reached = 1; // 已到达文件末尾
            LOG_DEBUG("Send_Message: Reached end of file\n");
            break; // 文件读取结束
        }
    }

    // 回退N算法
    if(mode==0)
    {
        while(start < Data_num){
            end = start + window_size - 1;

            // 发送窗口内的所有数据包
            for (int i = start; i <= end && i < Data_num; i++) {
                if(Data_Status[i]==0){
                    if (send_packet(sock, dstAddr, &Data[i]) < 0) {
                        fclose(file);
                        return -1;
                    }
                    Data_Status[i]=1; //标记为已发送未确认
                    next_seq_num++; // 更新下一个待发送的数据包序列号
                    //LOG_DEBUG("Send_Message: Sent packet seq_num=%u\n", Data[i].rtp.seq_num);
                }
            }

            // 等待ACK
            while(1){
                uint32_t ack_seq_num;

                gettimeofday(&start_time, NULL);
                current_time=start_time;
                long elapsed_time = (current_time.tv_sec - start_time.tv_sec) * 1000000 + (current_time.tv_usec - start_time.tv_usec);

                while(elapsed_time < TIMEOUT_MS*1000)
                {
                    rtp_packet_t ack_packet;

                    int ack_status = receive_packet(sock, dstAddr, &ack_packet);
                    ack_seq_num=ack_packet.rtp.seq_num;

                    // 接收到ACK
                    if (ack_status == 0)
                    {
                        //LOG_DEBUG("Send_Message: Received ACK seq_num=%u\n", ack_seq_num);

                        // 更新滑动窗口状态
                        if (ack_seq_num >= Data[start].rtp.seq_num) 
                        {
                            for (uint32_t i = start; i < start + ack_seq_num - Data[start].rtp.seq_num; i++) 
                            {
                                Data_Status[i]=2; //更新为已确认
                            }
                            start += ack_seq_num - Data[start].rtp.seq_num ; // 滑动窗口右移
                            next_seq_num = start;
                        }
                        if(start > end || start >= Data_num)
                        {
                            //LOG_DEBUG("%d\n", next_seq_num);
                            //LOG_DEBUG("over\n");
                            break;
                        }
                        gettimeofday(&start_time, NULL);//重新时间清零
                    }
                    else if(ack_status == -1)
                    {
                        fclose(file);
                        return -1; // 出现问题，退出
                    }

                    gettimeofday(&current_time, NULL);
                    elapsed_time = (current_time.tv_sec - start_time.tv_sec) * 1000000 + (current_time.tv_usec - start_time.tv_usec);
                }

                //说明是超时，则重传所有未确认的数据
                if(start <= end && start < Data_num)
                {
                    for(int i=start; i<=end && i< Data_num; i++)
                    {
                        if(Data_Status[i]==1)
                        {
                            if (send_packet(sock, dstAddr, &Data[i]) < 0) {
                                fclose(file);
                                return -1;
                            }
                            LOG_DEBUG("Send_Message: Retransmitted packet seq_num=%u\n", Data[i].rtp.seq_num);
                        }
                    }
                }
                else
                    break;
            }
        }
    }

    // 选择重传算法
    else if(mode==1)
    {
        while(start < Data_num){
            end = start + window_size - 1;

            // 发送窗口内的所有数据包
            for (int i = start; i <= end && i < Data_num; i++) {
                if(Data_Status[i]==0){
                    if (send_packet(sock, dstAddr, &Data[i]) < 0) {
                        fclose(file);
                        return -1;
                    }
                    Data_Status[i]=1; //标记为已发送未确认
                    next_seq_num++; // 更新下一个待发送的数据包序列号
                    //LOG_DEBUG("Send_Message: Sent packet seq_num=%u\n", Data[i].rtp.seq_num);
                }
            }

            // 等待ACK
            while(1){
                uint32_t ack_seq_num;

                gettimeofday(&start_time, NULL);
                current_time=start_time;
                long elapsed_time = (current_time.tv_sec - start_time.tv_sec) * 1000000 + (current_time.tv_usec - start_time.tv_usec);

                while(elapsed_time < TIMEOUT_MS*1000)
                {
                    rtp_packet_t ack_packet;

                    int ack_status = receive_packet(sock, dstAddr, &ack_packet);
                    ack_seq_num=ack_packet.rtp.seq_num;

                    // 接收到ACK
                    if (ack_status == 0)
                    {
                        //LOG_DEBUG("Send_Message: Received ACK seq_num=%u\n", ack_seq_num);

                        // 滑动窗口移动
                        Data_Status[ack_seq_num - window_base] = 2;
                        while(Data_Status[start] == 2)
                            start++;

                        if(start > end || start >= Data_num)
                        {
                            //LOG_DEBUG("%d\n", next_seq_num);
                            //LOG_DEBUG("over\n");
                            break;
                        }
                        gettimeofday(&start_time, NULL);//重新时间清零
                    }
                    else if(ack_status == -1)
                    {
                        fclose(file);
                        return -1; // 出现问题，退出
                    }

                    gettimeofday(&current_time, NULL);
                    elapsed_time = (current_time.tv_sec - start_time.tv_sec) * 1000000 + (current_time.tv_usec - start_time.tv_usec);
                }

                //说明是超时，则重传所有未确认的数据
                if(start <= end && start < Data_num)
                {
                    for(int i=start; i<=end && i< Data_num; i++)
                    {
                        if(Data_Status[i]==1)
                        {
                            if (send_packet(sock, dstAddr, &Data[i]) < 0) {
                                fclose(file);
                                return -1;
                            }
                            LOG_DEBUG("Send_Message: Retransmitted packet seq_num=%u\n", Data[i].rtp.seq_num);
                        }
                    }
                }
                else
                    break;
            }
        }
    }
    fclose(file);
    LOG_DEBUG("Send_Message Complete\n");
    return 0;
}

// 函数：关闭连接
int Connection_Terminate(int sock, struct sockaddr_in *dstAddr)
{
    //struct sockaddr_in lstAddr;
    rtp_packet_t packet;
    rtp_packet_t response;
    int retries = 0;

    // 设置套接字超时时间
    struct timeval timeout = {0, TIMEOUT_MS * 1000}; // 100ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // 第一次握手：Sender -> Receiver (SYN)
    packet.rtp.seq_num = Seq_Num_Global + 1;
    packet.rtp.length = 0;
    packet.rtp.flags = RTP_FIN;

    struct timeval start_time, current_time;

    while (retries < MAX_RETRIES){
        if (send_packet(sock, dstAddr, &packet) < 0) 
            return -1;  // 发送失败

        // 等待接收 FINACK 报文
        gettimeofday(&start_time, NULL);
        current_time=start_time;
        long elapsed_time = (current_time.tv_sec - start_time.tv_sec) * 1000000 + (current_time.tv_usec - start_time.tv_usec);

        while (elapsed_time < TIMEOUT_MS*1000) {
            int recv_status = receive_packet(sock, dstAddr, &response);
            if (recv_status == 0 && (response.rtp.flags & (RTP_FIN | RTP_ACK)) == (RTP_FIN | RTP_ACK) && response.rtp.seq_num == packet.rtp.seq_num) {
                LOG_DEBUG("Connection_Terminate: Received FINACK, seq_num=%u\n", response.rtp.seq_num);
                goto end;  // 收到 FINACK，认为连接终止
            } 
            else if (recv_status == -1) 
                return -1; // 接收错误

            // 其他：继续等待FIN报文
            gettimeofday(&current_time, NULL);
            elapsed_time = (current_time.tv_sec - start_time.tv_sec) * 1000000 + (current_time.tv_usec - start_time.tv_usec);
        }

        // 超时，重新发送
        LOG_DEBUG("Connection_Terminate: FIN timeout, retrying...\n");
        retries++;
    }

    fprintf(stderr, "Connection_Terminate: Failed to establish connection, no response to FIN\n");
    return -1;

// 终止：当接收到Receiver的回复报文/长时间未收到报文，认为对方已退出，终止运行
end:
    printf("Connection_Terminate succeed.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        LOG_FATAL("Usage: ./sender [receiver ip] [receiver port] [file path] "
                  "[window size] [mode]\n");
    }
 
    const char *receiver_ip = argv[1];
    int receiver_port = atoi(argv[2]);
    const char *file_path = argv[3];
    int window_size = atoi(argv[4]);
    int mode = atoi(argv[5]);
    struct sockaddr_in dstAddr;

    int sock = create_socket();
    memset(&dstAddr, 0, sizeof(dstAddr));  // dstAddr is the receiver's location in the network
    dstAddr.sin_family = AF_INET;
    inet_pton(AF_INET, receiver_ip, &dstAddr.sin_addr);
    dstAddr.sin_port = htons(receiver_port);

    // 建立连接
    if (Connection_Init(sock, &dstAddr) != 0) {
        printf("ERROR establish connection\n");
        return -1;
    }

    // 发送数据
    if (Send_Message(sock, &dstAddr, file_path, mode, window_size) != 0) {
        printf("ERROR send message\n");
        return -1;
    }

    // 关闭连接
    if (Connection_Terminate(sock, &dstAddr) != 0) {
        printf("ERROR terminate connection\n");
        return -1;
    }

    close(sock);

    LOG_DEBUG("Sender: exiting...\n");
    return 0;
}

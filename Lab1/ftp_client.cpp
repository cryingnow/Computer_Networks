/* Debug记录
*  0.最开始我没有设置Data_Message的payload部分，于是我只能够将payload分第二次发送.
*  1.open：问题出在我没有设置char IP[16],没有提前分配空间，导致了segmentation fault.
*  2.ls：问题出在is_Connected的设置-代码为while(true)循环，注意open之后就是连接状态，就不需要再重新connect了.
*  3.get：Server可以过，client怎么也过不了..
*       (1)问题出在UNUSED，Lab文档没有提过UNUSED为0，我自己安插了这个define，在判断Reply.m_status!=UNUSED时就会出现问题，于是直接把这个删除。
*       (2)以及htonl和ntohl总是容易忘，注意细节观察.
*  4.put:类似get
*  5.sha256:类似ls的操作，使用popen获取命令结果
*  6.cd：client不需要考虑并发，只需要当成命令处理即可
*/
#include <iostream>
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <string>
#include <unistd.h>
#include <netinet/in.h>  
#include <arpa/inet.h>
#include <sys/types.h> 
#include <sys/socket.h>
using namespace std;

#define MAGIC_NUMBER_LENGTH 6
#define UNUSED              0
#define HEADER_LENGTH       12 

const char Message_protocol[]={'\xc1','\xa1','\x10','f','t','p'}; 
bool is_Connected=0; //判断是否已经连接

//Lab要求的数据报文结构
struct Data_Message
{
    u_int8_t m_protocol[MAGIC_NUMBER_LENGTH];   /* protocol magic number (6 bytes) */
    u_int8_t m_type;                            /* type (1 byte) */
    u_int8_t m_status;                          /* status (1 byte) */
    uint32_t m_length;                          /* length (4 bytes) in Big endian*/
}__attribute__ ((packed));

//按照Lab文档编写成功将数据放入缓冲区的send函数，(可以发送Data_Message或其他字符串)
void Send(int sock, void *buffer, int len)
{
    size_t ret = 0;
    while (ret < len) {
        ssize_t b = send(sock, (char *) buffer + ret, len - ret, 0);
        if (b == 0) printf("socket Closed"); // 当连接断开
        if (b < 0) printf("Error ?"); // 这里可能发生了一些意料之外的情况
        ret += b; // 成功将b个byte塞进了缓冲区
    }
}

//按照Lab文档编写成功将数据放入缓冲区的recv函数，(可以接收Data_Message或其他字符串)
void Recv(int sock, void *buffer, int len)
{
    size_t ret = 0;
    while (ret < len) {
        ssize_t b = recv(sock, (char *) buffer + ret, len - ret, 0);
        if (b == 0) printf("socket Closed"); // 当连接断开
        if (b < 0) printf("Error ?"); // 这里可能发生了一些意料之外的情况
        ret += b; // 成功将b个byte塞进了缓冲区
    }
}

//判断是否为正确的的协议版本
bool isMYFTP(Data_Message Header)
{
    return memcmp(Header.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH) == 0;
}



// 命令open：open <IP> <port>  -建立一个到 <IP>:<port> 的连接
int Open(int &sock, const char *ip, int port){
    struct sockaddr_in server_address;

    //服务器结构初始化，port设置
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_port = htons(port);
    server_address.sin_family = AF_INET;

    //监听地址，将字符串表示转化为二进制表示。将IP地址转换为网络字节序(ip)
    if (inet_pton(AF_INET, ip, &server_address.sin_addr) <= 0) {
        cerr << "Address Error" << endl;
        close(sock);
        //exit(EXIT_FAILURE);
        return 0;
    }

    //尝试连接
    if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        cerr << "Connection Error" << endl;
        close(sock);
        //exit(EXIT_FAILURE);
        return 0;
    }

    is_Connected=1;

    Data_Message Open_Request;
    Data_Message Open_Reply;
    memset(&Open_Request, 0, sizeof(Open_Request));
    memset(&Open_Reply, 0, sizeof(Open_Reply));

    //设置OPEN_CONN_REQUEST报文
    //Open_Request.m_protocol=Message_protocol;
    memcpy(Open_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Open_Request.m_type=0xA1;
    Open_Request.m_status=UNUSED;
    Open_Request.m_length=htonl(HEADER_LENGTH);              //报文的长度 m_length 为大端法表示，后续不再赘述

    //发送，接收
    Send(sock, &Open_Request, HEADER_LENGTH);
    Recv(sock, &Open_Reply, HEADER_LENGTH);

    //对接收进行检验
    if(!isMYFTP(Open_Reply))
        return 0;
    if(Open_Reply.m_type!=0xA2 || Open_Reply.m_status!=1 || Open_Reply.m_length!=htonl(HEADER_LENGTH))
        return 0;

    printf("Connected to %s:%d\n", ip, port);
    return 1;
}

//命令quit:如有连接则断开连接，回到 open 前的状态；如果已经是 open 前的状态，则关闭 Client
int Quit(int &sock)
{
    //如果已经断开连接那么直接退出关闭sock即可
    if(is_Connected==0)
        //return 1;
        exit(0);
    
    Data_Message Quit_Request;
    Data_Message Quit_Reply;
    memset(&Quit_Request, 0, sizeof(Quit_Request));
    memset(&Quit_Reply, 0, sizeof(Quit_Reply));

    //设置QUIT_REQUEST报文
    memcpy(Quit_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Quit_Request.m_type=0xAD;
    Quit_Request.m_status=UNUSED;
    Quit_Request.m_length=htonl(HEADER_LENGTH);

    //发送，接收
    Send(sock, &Quit_Request, HEADER_LENGTH);
    Recv(sock, &Quit_Reply, HEADER_LENGTH);

    //对接收进行检验
    if(!isMYFTP(Quit_Reply))
        return 0;
    if(Quit_Reply.m_type!=0xAE  || Quit_Reply.m_length!=12)
        return 0;

    if(is_Connected)
        close(sock);
    is_Connected=0;
    return 1;
}

//命令ls: 列出存储在 Server 当前工作目录中的所有文件
int Ls(int &sock)
{
    cout<<"I am lsing"<<endl;
    Data_Message LS_Request;
    Data_Message LS_Reply;
    char file_list[2048] = {0};
    memset(&LS_Request, 0, sizeof(LS_Request));
    memset(&LS_Reply, 0, sizeof(LS_Reply));

    //设置LS_REQUEST报文
    memcpy(LS_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    LS_Request.m_type=0xA3;
    LS_Request.m_status=UNUSED;
    LS_Request.m_length=htonl(HEADER_LENGTH);

    //发送，接收
    Send(sock, &LS_Request, HEADER_LENGTH);
    Recv(sock, &LS_Reply, HEADER_LENGTH);

    //对接收进行检验
    if(!isMYFTP(LS_Reply))
        return 0;
    if(LS_Reply.m_type!=0xA4)
        return 0;

    //读取file_list的长度，并接收
    int payload_length= ntohl(LS_Reply.m_length)-HEADER_LENGTH;
    cout<<payload_length<<endl;
    Recv(sock, file_list, payload_length);
    
    //输出ls的结果
    for(int i=0;i<payload_length;i++)
        cout<<file_list[i];

    return 1;
}

//命令：get FILE，其中FILE是要下载的文件的名称
int Get(int &sock, char *Filename)
{
    Data_Message Get_Request;
    Data_Message Get_Reply;
    memset(&Get_Request, 0, sizeof(Get_Request));
    memset(&Get_Reply, 0, sizeof(Get_Reply));

    FILE *fp=fopen(Filename,"w");
    if(fp==NULL)
    {
        cerr << "fp Error" << endl;
        return 0;
    }

    //获取payload长度
    int filename_length=strlen(Filename);
    //Filename[filename_length]='\0';

    //设置Get_REQUEST报文
    memcpy(Get_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Get_Request.m_type=0xA7;
    Get_Request.m_status=UNUSED;
    Get_Request.m_length=htonl(HEADER_LENGTH + filename_length + 1);

    //发送报文，发送payload，接收
    Send(sock, &Get_Request, HEADER_LENGTH);
    Send(sock, Filename, filename_length + 1);
    Recv(sock, &Get_Reply, HEADER_LENGTH);

    
    if(!isMYFTP(Get_Reply))
        return 0;
    if(Get_Reply.m_type!=0xA8)
        return 0;
    
    //判断是否存在文件，如果存在的话，接收文件内容
    if(Get_Reply.m_status==1)
    {
        char File[4194304];
        Data_Message Get_Data;
        memset(&Get_Data, 0, sizeof(Get_Data));
        Recv(sock, &Get_Data, HEADER_LENGTH);
        if(!isMYFTP(Get_Data))
            return 0;
        if(Get_Data.m_type!=0xFF)
            return 0;
        
        //获取payload的长度，接收
        int file_size=ntohl(Get_Data.m_length)-HEADER_LENGTH;
        Recv(sock, File, file_size);

        //将payload的内容写入到文件中，达成下载文件的效果
        fwrite(File,1,file_size,fp);
    }
    fclose(fp);

    return 1;
}

//命令：put FILE，其中FILE是要传送的文件的名称
int Put(int &sock, char *Filename)
{
    FILE *fp;
    Data_Message Put_Request;
    Data_Message Put_Reply;
    memset(&Put_Request, 0, sizeof(Put_Request));
    memset(&Put_Reply, 0, sizeof(Put_Reply));

    //先判断文件是否存在于本地
    if((fp=fopen(Filename,"r"))==NULL)
    {
        cerr << "File not exist." << endl;
        return 0;
    }

    //获取payload长度
    int filename_length=strlen(Filename);
    Filename[filename_length]='\0';

    //设置Put_REQUEST报文
    memcpy(Put_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Put_Request.m_type=0xA9;
    Put_Request.m_status=UNUSED;
    Put_Request.m_length=htonl(HEADER_LENGTH + filename_length + 1);

    //发送报文，发送payload，接收
    Send(sock, &Put_Request, HEADER_LENGTH);
    Send(sock, Filename, filename_length + 1);
    Recv(sock, &Put_Reply, HEADER_LENGTH);

    
    if(!isMYFTP(Put_Reply))
        return 0;
    if(Put_Reply.m_type!=0xAA)
        return 0;
    
    //读取文件内容，并发送给Server.
    char File[4194304];
    Data_Message File_Data_Header;
    memset(&File_Data_Header, 0, sizeof(File_Data_Header));

    //fread读取文件，并获得长度
    int file_length=fread(File,1,4194304,fp);

    //先发送HEADER_LENGTH的报文，再发送payload
    File_Data_Header.m_type=0xFF;
    File_Data_Header.m_status=UNUSED;
    File_Data_Header.m_length=htonl(HEADER_LENGTH + file_length);
    Send(sock, &File_Data_Header, HEADER_LENGTH);
    Send(sock, File, file_length);

    fclose(fp);
    return 1;
}

//命令：sha256 FILE，其中FILE是要查询校验码的文件的名称
int Sha(int &sock, char *Filename)
{
    Data_Message SHA_Request;
    Data_Message SHA_Reply;
    //char file_list[2048] = {0};
    memset(&SHA_Request, 0, sizeof(SHA_Request));
    memset(&SHA_Reply, 0, sizeof(SHA_Reply));

    //获取payload长度
    int filename_length=strlen(Filename);
    //Filename[filename_length]='\0';

    //设置SHA_REQUEST报文
    memcpy(SHA_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    SHA_Request.m_type=0xAB;
    SHA_Request.m_status=UNUSED;
    SHA_Request.m_length=htonl(HEADER_LENGTH + filename_length + 1);

    //发送报文，发送payload，接收
    Send(sock, &SHA_Request, HEADER_LENGTH);
    Send(sock, Filename, filename_length + 1);
    Recv(sock, &SHA_Reply, HEADER_LENGTH);


    //对接收进行检验
    if(!isMYFTP(SHA_Reply))
        return 0;
    if(SHA_Reply.m_type!=0xAC)
        return 0;


    //判断是否存在文件，如果存在的话，接收关于查询消息的FILE_DATA
    if(SHA_Reply.m_status==1)
    {
        char SHA[4096];
        Data_Message SHA_Data;
        memset(&SHA_Data, 0, sizeof(SHA_Data));
        Recv(sock, &SHA_Data, HEADER_LENGTH);
        if(!isMYFTP(SHA_Data))
            return 0;
        if(SHA_Data.m_type!=0xFF)
            return 0;
        
        //获取payload的长度，接收
        int payload_length=ntohl(SHA_Data.m_length)-HEADER_LENGTH;
        Recv(sock, SHA, payload_length);

        //输出sha256的结果
        for(int i=0;i<payload_length;i++)
            cout<<SHA[i];
    }

    return 1;
}

//命令：cd 更改当前client对应的server的工作目录
int Cd(int &sock, char *Directory)
{
    Data_Message CD_Request;
    Data_Message CD_Reply;
    memset(&CD_Request, 0, sizeof(CD_Request));
    memset(&CD_Reply, 0, sizeof(CD_Reply));

    //获取payload长度
    int Directory_length=strlen(Directory);

    //设置LS_REQUEST报文
    memcpy(CD_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    CD_Request.m_type=0xA5;
    CD_Request.m_status=UNUSED;
    CD_Request.m_length=htonl(HEADER_LENGTH + Directory_length + 1);

    //发送，接收
    Send(sock, &CD_Request, HEADER_LENGTH);
    Send(sock, Directory, Directory_length + 1);
    Recv(sock, &CD_Reply, HEADER_LENGTH);

    //对接收进行检验
    if(!isMYFTP(CD_Reply))
        return 0;
    if(CD_Reply.m_type!=0xA6)
        return 0;

    //如果m_status返回0，说明这个目录不存在
    if(CD_Reply.m_status==0)
        cerr << "Directory Not Exist." << endl;

    return 1;
}

int main() {
    cerr << "hello from ftp client" << endl;
    string command;
    int sock;
    is_Connected=0;

    //创建 Socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr<< "Socket creation Error" << endl;
        //exit(EXIT_FAILURE);
        return 0;
    }

    while(true){
        //分类处理command命令，进行分类区分
        cin>>command;
        if(command=="open")
        {
            char IP[16];
            int port;
            
            cin>>IP>>port;
            if(!Open(sock,IP,port))
                cerr << "Connection Error." << endl;
        }
        else if(command=="ls")
        { 
            if(!Ls(sock))
                cerr << "ls Error." << endl;
        }
        else if(command=="cd")
        {
            char Directory[2048];
            cin>>Directory;
            if(!Cd(sock,Directory))
                cerr << "Get Error." << endl;
        }
        else if(command=="get")
        {
            char Filename[2048]; 
            cin>>Filename;
            if(!Get(sock,Filename))
                cerr << "Get Error." << endl;
        }
        else if(command=="put")
        {
            char Filename[2048];
            cin>>Filename;
            if(!Put(sock,Filename))
                cerr << "Put Error." << endl;
        }
        else if(command=="sha256")
        {
            char Filename[2048];
            cin>>Filename;
            if(!Sha(sock,Filename))
                cerr << "Sha256 Error." << endl;
        }
        else if(command=="quit")
        {
            if(!Quit(sock))
                cerr << "Quit Error." << endl;
        }
        else
        {
            cerr << "Command Error from ftp client" << endl;
        }
    }

    // 关闭 Socket
    close(sock);
    printf("Connection closed.\n");
    return 0;
}
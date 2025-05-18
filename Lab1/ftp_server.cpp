/*
* 1.修改并发，在原来代码的基础上，使用pthread，每一个客户请求都连接创建新线程进行服务。is_Connected的修改或许有点问题，
* (和我设想的不太一样，本来以为要继续debug)，但是在修改线程代码之后就可以过multiput和multiget了，遂作罢。
* 2.在实现并发(多个客户端)的基础上，实现cd.维护一个不同客户的工作目录数组，在这里我设置的是Directory[2048]，
* 不同client值对应不同Directory[client].(假设同时连接的客户不会超过2048个)
* 3.GetBig,PutBig:1MB=1*1024*1024=1048576 B.需要至少这么大的数组存储，我选取的是4194304.
* 4.MultiCdList:为应对此项目，修改ls命令的处理函数，对每个客户，先chdir到工作目录再调用ls命令。
* 同时为了保证并发互不影响，引入mutex作为目录的锁，处理chdir(Working_Directory)和chdir(".")的过程上锁。
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
#include <pthread.h>
#include <dirent.h>
#include <mutex>
using namespace std;
 
#define MAGIC_NUMBER_LENGTH 6
#define UNUSED              0 
#define HEADER_LENGTH       12

const char Message_protocol[]={'\xc1','\xa1','\x10','f','t','p'}; 
bool is_Connected=0; //判断是否已经连接

string Working_Directory[2048];
std::mutex dir_mutex; // 创建一个mutex对象，ls命令时用来保护chdir


//Lab要求的数据报文结构
struct Data_Message
{
    u_int8_t m_protocol[MAGIC_NUMBER_LENGTH];   /* protocol magic number (6 bytes) */
    u_int8_t m_type;                            /* type (1 byte) */
    u_int8_t m_status;                          /* status (1 byte) */
    uint32_t m_length;                          /* length (4 bytes) in Big endian*/
}__attribute__ ((packed));

//按照Lab文档编写成功将数据放入缓冲区的send函数(可以发送Data_Message或其他字符串)
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

//按照Lab文档编写成功将数据放入缓冲区的recv函数(可以接收Data_Message或其他字符串) 
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

/*对open命令处理*/
void Open(int client)
{
    //初始化回复报文内容，并回复
    Data_Message Open_Reply;
    memset(&Open_Reply, 0, sizeof(Open_Reply));

    memcpy(Open_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Open_Reply.m_type=0xA2;
    Open_Reply.m_status=1;
    Open_Reply.m_length=htonl(HEADER_LENGTH);              //报文的长度 m_length 为大端法表示，后续不再赘述
    Send(client, &Open_Reply, HEADER_LENGTH);

    is_Connected=1;

}

/*对quit命令处理*/
void Quit(int client)
{
    Data_Message Quit_Reply;
    memset(&Quit_Reply, 0, sizeof(Quit_Reply));

    //设置QUIT_REQUEST报文
    memcpy(Quit_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Quit_Reply.m_type=0xAE;
    Quit_Reply.m_status=UNUSED;
    Quit_Reply.m_length=htonl(HEADER_LENGTH);
    Send(client, &Quit_Reply, HEADER_LENGTH);

    is_Connected=0;
}

/*对ls命令处理*/
void Ls(int client)
{
    Data_Message LS_Reply;
    memset(&LS_Reply, 0, sizeof(LS_Reply));

    char file_list[2048];
    char original_dir[2048];
    FILE *fp;

    //上锁保证服务当前客户ls命令时其他客户不会干涉
    dir_mutex.lock();                  
    //获取原工作目录    
    getcwd(original_dir, sizeof(original_dir));
    //更改到当前客户的工作目录
    chdir(Working_Directory[client].c_str());

    //利用popen获取ls命令执行后的内容
    fp = popen("ls", "r");
    if (fp == NULL) {
        cerr << "popen failed" << endl;
        dir_mutex.unlock();
        return;
    }

    //回到最初目录
    chdir(original_dir);
    dir_mutex.unlock();

    //读取文件
    //while (fgets(buffer, sizeof(buffer), fp) != NULL) {
    //        strcat(file_list, buffer);
    //}
    int payload_length=fread(file_list,1,2048,fp);
    pclose(fp);
    

    //加入'\0'结尾
    //int payload_length=strlen(file_list); 
    file_list[payload_length]='\0';

    //设置LS_REPLY报文
    memcpy(LS_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    LS_Reply.m_type=0xA4;
    LS_Reply.m_status=UNUSED;
    LS_Reply.m_length=htonl(HEADER_LENGTH + payload_length + 1);
    Send(client, &LS_Reply, HEADER_LENGTH);

    //把payload的内容传送给client
    Send(client, file_list, payload_length + 1);
}

/*对get命令处理*/
void Get(int client, Data_Message Get_Request)
{
    Data_Message Get_Reply;
    char Filename[2048];
    FILE *fp;
    memset(&Get_Reply, 0, sizeof(Get_Reply));

    int filename_length=ntohl(Get_Request.m_length)-HEADER_LENGTH;
    Recv(client, Filename, filename_length);
    //根据当前工作目录，获取绝对路径
    strcpy(Filename, string(Working_Directory[client] + "/" + string(Filename)).c_str());

    //设置Get_REPLY报文 
    memcpy(Get_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Get_Reply.m_type=0xA8;
    Get_Reply.m_length=htonl(HEADER_LENGTH);

    //判断是否存在该文件
    if((fp=fopen(Filename,"r"))==NULL){
        Get_Reply.m_status=0;
        Send(client, &Get_Reply, HEADER_LENGTH);
    }
    else
    {
        //先发送HEADER_LENGTH的回复报文
        Get_Reply.m_status=1;
        Send(client, &Get_Reply, HEADER_LENGTH);

        //接下来读取文件内容，并发送给client.
        char File[4194304];
        Data_Message File_Data_Header;
        memset(&File_Data_Header, 0, sizeof(File_Data_Header));

        //fread读取文件，并获得长度
        int file_length=fread(File,1,4194304,fp);

        //先发送HEADER_LENGTH的报文，再发送payload
        File_Data_Header.m_type=0xFF;
        File_Data_Header.m_status=UNUSED;
        File_Data_Header.m_length=htonl(HEADER_LENGTH + file_length);
        Send(client, &File_Data_Header, HEADER_LENGTH);
        Send(client, File, file_length);

    }
    fclose(fp);
}

/*对put命令处理*/
void Put(int client, Data_Message Put_Request)
{
    Data_Message Put_Reply;
    char Filename[2048];
    memset(&Put_Reply, 0, sizeof(Put_Reply));

    int filename_length=ntohl(Put_Request.m_length)-HEADER_LENGTH;
    Recv(client, Filename, filename_length);

    //根据当前工作目录，获取绝对路径
    strcpy(Filename, string(Working_Directory[client] + "/" + string(Filename)).c_str());

    //设置Put_REPLY报文,并回复
    memcpy(Put_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Put_Reply.m_type=0xAA;
    Put_Reply.m_length=htonl(HEADER_LENGTH);
    Put_Reply.m_status=UNUSED;
    Send(client, &Put_Reply, HEADER_LENGTH);

    //准备接收文件内容，并写入自己的文件中，有可能需要覆盖已有的文件
    char File[4194304];
    Data_Message Put_Data;
    memset(&Put_Data, 0, sizeof(Put_Data));
    Recv(client, &Put_Data, HEADER_LENGTH);
    
    //获取payload的长度，接收
    int file_size=ntohl(Put_Data.m_length)-HEADER_LENGTH;
    Recv(client, File, file_size);

    //将payload的内容写入到文件中，达成Put的效果
    FILE *fp=fopen(Filename,"w");
    fwrite(File,1,file_size,fp);
    fclose(fp);
}

/*对sha256命令处理*/
void Sha(int client, Data_Message SHA_Request)
{
    Data_Message SHA_Reply;
    char Filename[2048];
    FILE *fp;
    char SHA[2048];
    memset(&SHA_Reply, 0, sizeof(SHA_Reply));

    int filename_length=ntohl(SHA_Request.m_length)-HEADER_LENGTH;
    Recv(client, Filename, filename_length);

    //根据当前工作目录，获取绝对路径
    strcpy(Filename, string(Working_Directory[client] + "/" + string(Filename)).c_str());

    //设置SHA_REPLY报文 
    memcpy(SHA_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    SHA_Reply.m_type=0xAC;
    SHA_Reply.m_length=htonl(HEADER_LENGTH);

    //根据是否存在确定status类型，如果存在，则把sha256的消息返回
    //string sha_command="sha256sum "+ string(Filename);
    if((fp=popen(string("sha256sum "+string(Filename)).c_str(), "r"))==NULL){
        SHA_Reply.m_status=0;
        Send(client, &SHA_Reply, HEADER_LENGTH);
    }
    else
    {
        SHA_Reply.m_status=1;
        Send(client, &SHA_Reply ,HEADER_LENGTH);

        Data_Message File_Data_Header;
        memset(&File_Data_Header, 0, sizeof(File_Data_Header));

        //读取sha256得到的内容
        int payload_length=fread(SHA,1,2048,fp);
        pclose(fp);

        //加入'\0'结尾
        //int payload_length=strlen(file_list);
        SHA[payload_length]='\0';

        //设置要发送的FILE_DATA报文
        memcpy(File_Data_Header.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
        File_Data_Header.m_type=0xFF;
        File_Data_Header.m_status=UNUSED;
        File_Data_Header.m_length=htonl(HEADER_LENGTH + payload_length + 1);
        Send(client, &File_Data_Header, HEADER_LENGTH);

        //把payload的内容传送给client
        Send(client, SHA, payload_length + 1);

    }
}

/*对cd命令处理*/
void Cd(int client, Data_Message CD_Request)
{
    Data_Message CD_Reply;
    char Directory[2048];
    memset(&CD_Reply, 0, sizeof(CD_Reply));

    int Directory_length=ntohl(CD_Request.m_length)-HEADER_LENGTH;
    Recv(client, Directory, Directory_length);

    //设置CD_REPLY报文
    memcpy(CD_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    CD_Reply.m_type=0xA6;
    CD_Reply.m_length=htonl(HEADER_LENGTH);

    DIR *dir = opendir(Directory);
    if(dir) 
    {
        //目录存在，关闭目录并更新工作目录
        closedir(dir);
        Working_Directory[client]= Directory;
        
        //标记成功
        CD_Reply.m_status=1;
    } 
    else 
        CD_Reply.m_status=0;

    //发送回复
    Send(client, &CD_Reply, HEADER_LENGTH);
}

void* handleClient(void* arg) {
    int client = *(int*)arg;
    delete (int*)arg; 

    //可能有误，设置连接成功
    is_Connected = true;

    while(true) 
    {
        //接收命令报文内容
        Data_Message Open_Request;
        Recv(client, &Open_Request, sizeof(Open_Request));
        cerr << "I have received." << endl;

        if(!isMYFTP(Open_Request))  
            return 0;

        //针对不同m_type确认请求的命令类型，分别处理
        if(Open_Request.m_type==0xA1)
            Open(client);
        else if(Open_Request.m_type==0xAD)
            Quit(client);
        else if(Open_Request.m_type==0xA3)
            Ls(client);
        else if(Open_Request.m_type==0xA7)
            Get(client,Open_Request);
        else if(Open_Request.m_type==0xA9)
            Put(client,Open_Request);
        else if(Open_Request.m_type==0xAB)
            Sha(client,Open_Request);
        else if(Open_Request.m_type==0xA5)
            Cd(client,Open_Request);
        else
            cerr << "Command Error from ftp server" << endl;
    }
}

int main(int argc, char *argv[]) {
    cerr << "hello from ftp server" << endl;

    int sock;                          //listen套接字
    struct sockaddr_in server_addr;
    //int client;

    //创建socket
    sock = socket(AF_INET, SOCK_STREAM, 0); 
    cout << "Creating sock." << endl;
    if (sock < 0) {
        cerr << "Socket creation Error" << endl;
        exit(EXIT_FAILURE);
    }

    //服务器结构初始化,argv[1]表示IP，argv[2]表示port
    server_addr.sin_port = htons(atoi(argv[2])); 
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); //监听地址，将字符串表示转化为二进制表示。

    //服务器进行监听
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Bind Error" << endl;
        close(sock);
        exit(EXIT_FAILURE);
    }
    cout << "I am binding." << endl;

    if(listen(sock, 128) < 0 )
    {
        cerr << "Listen Error" << endl;
        close(sock);
        exit(EXIT_FAILURE);
    }
    cout << "I am listening." << endl;

    while(true){
        cout<<sock<<endl;

        /*
        //先判断是否已经连接，如果未连接则accept,否则不需要
        if(!is_Connected)
            client=accept(sock, nullptr, nullptr);

        if(client<0)
        {
            cerr<< "Accept Error" << endl;
            close(sock);
            exit(EXIT_FAILURE);
        }
        cout << "I am accepting." << endl;
        */

       int* client_socket = new int; //为客户socket分配空间(后面会清理)
        *client_socket = accept(sock, nullptr, nullptr);
        Working_Directory[*client_socket]=".";
        
        if (*client_socket < 0) {
            cerr << "Accept error" << endl;
            close(sock);
            delete client_socket; 
            continue;    //尝试连接下一个client
        }
        
        cout << "I am accepting." << endl;

        //为客户准备新线程，服务
        pthread_t thread_id;
        if (pthread_create(&thread_id, nullptr, handleClient, client_socket) != 0) {
            cerr << "Failed to create thread" << endl;
            close(*client_socket);
            delete client_socket; 
            continue;  //尝试连接下一个client
        }

        //detach线程，保证后面结束后清除
        pthread_detach(thread_id);

        /*
        Data_Message Open_Request;
        Recv(client, &Open_Request, sizeof(Open_Request));
        cerr << "I have received." << endl;

        if(!isMYFTP(Open_Request))  
            return 0;

        if(Open_Request.m_type==0xA1)
            Open(client);
        else if(Open_Request.m_type==0xAD)
            Quit(client);
        else if(Open_Request.m_type==0xA3)
            Ls(client);
        else if(Open_Request.m_type==0xA7)
            Get(client,Open_Request);
        else if(Open_Request.m_type==0xA9)
            Put(client,Open_Request);
        else if(Open_Request.m_type==0xAB)
            Sha(client,Open_Request);
        else if(Open_Request.m_type==0xA5)
            Cd(client,Open_Request);
        */

    }
    // 关闭socket
    close(sock);
    return 0;
}
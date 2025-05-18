/*
* 1.�޸Ĳ�������ԭ������Ļ����ϣ�ʹ��pthread��ÿһ���ͻ��������Ӵ������߳̽��з���is_Connected���޸Ļ����е����⣬
* (��������Ĳ�̫һ����������ΪҪ����debug)���������޸��̴߳���֮��Ϳ��Թ�multiput��multiget�ˣ������ա�
* 2.��ʵ�ֲ���(����ͻ���)�Ļ����ϣ�ʵ��cd.ά��һ����ͬ�ͻ��Ĺ���Ŀ¼���飬�����������õ���Directory[2048]��
* ��ͬclientֵ��Ӧ��ͬDirectory[client].(����ͬʱ���ӵĿͻ����ᳬ��2048��)
* 3.GetBig,PutBig:1MB=1*1024*1024=1048576 B.��Ҫ������ô�������洢����ѡȡ����4194304.
* 4.MultiCdList:ΪӦ�Դ���Ŀ���޸�ls����Ĵ���������ÿ���ͻ�����chdir������Ŀ¼�ٵ���ls���
* ͬʱΪ�˱�֤��������Ӱ�죬����mutex��ΪĿ¼����������chdir(Working_Directory)��chdir(".")�Ĺ���������
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
bool is_Connected=0; //�ж��Ƿ��Ѿ�����

string Working_Directory[2048];
std::mutex dir_mutex; // ����һ��mutex����ls����ʱ��������chdir


//LabҪ������ݱ��Ľṹ
struct Data_Message
{
    u_int8_t m_protocol[MAGIC_NUMBER_LENGTH];   /* protocol magic number (6 bytes) */
    u_int8_t m_type;                            /* type (1 byte) */
    u_int8_t m_status;                          /* status (1 byte) */
    uint32_t m_length;                          /* length (4 bytes) in Big endian*/
}__attribute__ ((packed));

//����Lab�ĵ���д�ɹ������ݷ��뻺������send����(���Է���Data_Message�������ַ���)
void Send(int sock, void *buffer, int len)
{
    size_t ret = 0;
    while (ret < len) {
        ssize_t b = send(sock, (char *) buffer + ret, len - ret, 0);
        if (b == 0) printf("socket Closed"); // �����ӶϿ�
        if (b < 0) printf("Error ?"); // ������ܷ�����һЩ����֮������
        ret += b; // �ɹ���b��byte�����˻�����
    }
}

//����Lab�ĵ���д�ɹ������ݷ��뻺������recv����(���Խ���Data_Message�������ַ���) 
void Recv(int sock, void *buffer, int len)
{
    size_t ret = 0;
    while (ret < len) {
        ssize_t b = recv(sock, (char *) buffer + ret, len - ret, 0);
        if (b == 0) printf("socket Closed"); // �����ӶϿ�
        if (b < 0) printf("Error ?"); // ������ܷ�����һЩ����֮������
        ret += b; // �ɹ���b��byte�����˻�����
    }
}

//�ж��Ƿ�Ϊ��ȷ�ĵ�Э��汾
bool isMYFTP(Data_Message Header)
{
    return memcmp(Header.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH) == 0;
}

/*��open�����*/
void Open(int client)
{
    //��ʼ���ظ��������ݣ����ظ�
    Data_Message Open_Reply;
    memset(&Open_Reply, 0, sizeof(Open_Reply));

    memcpy(Open_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Open_Reply.m_type=0xA2;
    Open_Reply.m_status=1;
    Open_Reply.m_length=htonl(HEADER_LENGTH);              //���ĵĳ��� m_length Ϊ��˷���ʾ����������׸��
    Send(client, &Open_Reply, HEADER_LENGTH);

    is_Connected=1;

}

/*��quit�����*/
void Quit(int client)
{
    Data_Message Quit_Reply;
    memset(&Quit_Reply, 0, sizeof(Quit_Reply));

    //����QUIT_REQUEST����
    memcpy(Quit_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Quit_Reply.m_type=0xAE;
    Quit_Reply.m_status=UNUSED;
    Quit_Reply.m_length=htonl(HEADER_LENGTH);
    Send(client, &Quit_Reply, HEADER_LENGTH);

    is_Connected=0;
}

/*��ls�����*/
void Ls(int client)
{
    Data_Message LS_Reply;
    memset(&LS_Reply, 0, sizeof(LS_Reply));

    char file_list[2048];
    char original_dir[2048];
    FILE *fp;

    //������֤����ǰ�ͻ�ls����ʱ�����ͻ��������
    dir_mutex.lock();                  
    //��ȡԭ����Ŀ¼    
    getcwd(original_dir, sizeof(original_dir));
    //���ĵ���ǰ�ͻ��Ĺ���Ŀ¼
    chdir(Working_Directory[client].c_str());

    //����popen��ȡls����ִ�к������
    fp = popen("ls", "r");
    if (fp == NULL) {
        cerr << "popen failed" << endl;
        dir_mutex.unlock();
        return;
    }

    //�ص����Ŀ¼
    chdir(original_dir);
    dir_mutex.unlock();

    //��ȡ�ļ�
    //while (fgets(buffer, sizeof(buffer), fp) != NULL) {
    //        strcat(file_list, buffer);
    //}
    int payload_length=fread(file_list,1,2048,fp);
    pclose(fp);
    

    //����'\0'��β
    //int payload_length=strlen(file_list); 
    file_list[payload_length]='\0';

    //����LS_REPLY����
    memcpy(LS_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    LS_Reply.m_type=0xA4;
    LS_Reply.m_status=UNUSED;
    LS_Reply.m_length=htonl(HEADER_LENGTH + payload_length + 1);
    Send(client, &LS_Reply, HEADER_LENGTH);

    //��payload�����ݴ��͸�client
    Send(client, file_list, payload_length + 1);
}

/*��get�����*/
void Get(int client, Data_Message Get_Request)
{
    Data_Message Get_Reply;
    char Filename[2048];
    FILE *fp;
    memset(&Get_Reply, 0, sizeof(Get_Reply));

    int filename_length=ntohl(Get_Request.m_length)-HEADER_LENGTH;
    Recv(client, Filename, filename_length);
    //���ݵ�ǰ����Ŀ¼����ȡ����·��
    strcpy(Filename, string(Working_Directory[client] + "/" + string(Filename)).c_str());

    //����Get_REPLY���� 
    memcpy(Get_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Get_Reply.m_type=0xA8;
    Get_Reply.m_length=htonl(HEADER_LENGTH);

    //�ж��Ƿ���ڸ��ļ�
    if((fp=fopen(Filename,"r"))==NULL){
        Get_Reply.m_status=0;
        Send(client, &Get_Reply, HEADER_LENGTH);
    }
    else
    {
        //�ȷ���HEADER_LENGTH�Ļظ�����
        Get_Reply.m_status=1;
        Send(client, &Get_Reply, HEADER_LENGTH);

        //��������ȡ�ļ����ݣ������͸�client.
        char File[4194304];
        Data_Message File_Data_Header;
        memset(&File_Data_Header, 0, sizeof(File_Data_Header));

        //fread��ȡ�ļ�������ó���
        int file_length=fread(File,1,4194304,fp);

        //�ȷ���HEADER_LENGTH�ı��ģ��ٷ���payload
        File_Data_Header.m_type=0xFF;
        File_Data_Header.m_status=UNUSED;
        File_Data_Header.m_length=htonl(HEADER_LENGTH + file_length);
        Send(client, &File_Data_Header, HEADER_LENGTH);
        Send(client, File, file_length);

    }
    fclose(fp);
}

/*��put�����*/
void Put(int client, Data_Message Put_Request)
{
    Data_Message Put_Reply;
    char Filename[2048];
    memset(&Put_Reply, 0, sizeof(Put_Reply));

    int filename_length=ntohl(Put_Request.m_length)-HEADER_LENGTH;
    Recv(client, Filename, filename_length);

    //���ݵ�ǰ����Ŀ¼����ȡ����·��
    strcpy(Filename, string(Working_Directory[client] + "/" + string(Filename)).c_str());

    //����Put_REPLY����,���ظ�
    memcpy(Put_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Put_Reply.m_type=0xAA;
    Put_Reply.m_length=htonl(HEADER_LENGTH);
    Put_Reply.m_status=UNUSED;
    Send(client, &Put_Reply, HEADER_LENGTH);

    //׼�������ļ����ݣ���д���Լ����ļ��У��п�����Ҫ�������е��ļ�
    char File[4194304];
    Data_Message Put_Data;
    memset(&Put_Data, 0, sizeof(Put_Data));
    Recv(client, &Put_Data, HEADER_LENGTH);
    
    //��ȡpayload�ĳ��ȣ�����
    int file_size=ntohl(Put_Data.m_length)-HEADER_LENGTH;
    Recv(client, File, file_size);

    //��payload������д�뵽�ļ��У����Put��Ч��
    FILE *fp=fopen(Filename,"w");
    fwrite(File,1,file_size,fp);
    fclose(fp);
}

/*��sha256�����*/
void Sha(int client, Data_Message SHA_Request)
{
    Data_Message SHA_Reply;
    char Filename[2048];
    FILE *fp;
    char SHA[2048];
    memset(&SHA_Reply, 0, sizeof(SHA_Reply));

    int filename_length=ntohl(SHA_Request.m_length)-HEADER_LENGTH;
    Recv(client, Filename, filename_length);

    //���ݵ�ǰ����Ŀ¼����ȡ����·��
    strcpy(Filename, string(Working_Directory[client] + "/" + string(Filename)).c_str());

    //����SHA_REPLY���� 
    memcpy(SHA_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    SHA_Reply.m_type=0xAC;
    SHA_Reply.m_length=htonl(HEADER_LENGTH);

    //�����Ƿ����ȷ��status���ͣ�������ڣ����sha256����Ϣ����
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

        //��ȡsha256�õ�������
        int payload_length=fread(SHA,1,2048,fp);
        pclose(fp);

        //����'\0'��β
        //int payload_length=strlen(file_list);
        SHA[payload_length]='\0';

        //����Ҫ���͵�FILE_DATA����
        memcpy(File_Data_Header.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
        File_Data_Header.m_type=0xFF;
        File_Data_Header.m_status=UNUSED;
        File_Data_Header.m_length=htonl(HEADER_LENGTH + payload_length + 1);
        Send(client, &File_Data_Header, HEADER_LENGTH);

        //��payload�����ݴ��͸�client
        Send(client, SHA, payload_length + 1);

    }
}

/*��cd�����*/
void Cd(int client, Data_Message CD_Request)
{
    Data_Message CD_Reply;
    char Directory[2048];
    memset(&CD_Reply, 0, sizeof(CD_Reply));

    int Directory_length=ntohl(CD_Request.m_length)-HEADER_LENGTH;
    Recv(client, Directory, Directory_length);

    //����CD_REPLY����
    memcpy(CD_Reply.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    CD_Reply.m_type=0xA6;
    CD_Reply.m_length=htonl(HEADER_LENGTH);

    DIR *dir = opendir(Directory);
    if(dir) 
    {
        //Ŀ¼���ڣ��ر�Ŀ¼�����¹���Ŀ¼
        closedir(dir);
        Working_Directory[client]= Directory;
        
        //��ǳɹ�
        CD_Reply.m_status=1;
    } 
    else 
        CD_Reply.m_status=0;

    //���ͻظ�
    Send(client, &CD_Reply, HEADER_LENGTH);
}

void* handleClient(void* arg) {
    int client = *(int*)arg;
    delete (int*)arg; 

    //���������������ӳɹ�
    is_Connected = true;

    while(true) 
    {
        //�������������
        Data_Message Open_Request;
        Recv(client, &Open_Request, sizeof(Open_Request));
        cerr << "I have received." << endl;

        if(!isMYFTP(Open_Request))  
            return 0;

        //��Բ�ͬm_typeȷ��������������ͣ��ֱ���
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

    int sock;                          //listen�׽���
    struct sockaddr_in server_addr;
    //int client;

    //����socket
    sock = socket(AF_INET, SOCK_STREAM, 0); 
    cout << "Creating sock." << endl;
    if (sock < 0) {
        cerr << "Socket creation Error" << endl;
        exit(EXIT_FAILURE);
    }

    //�������ṹ��ʼ��,argv[1]��ʾIP��argv[2]��ʾport
    server_addr.sin_port = htons(atoi(argv[2])); 
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); //������ַ�����ַ�����ʾת��Ϊ�����Ʊ�ʾ��

    //���������м���
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
        //���ж��Ƿ��Ѿ����ӣ����δ������accept,������Ҫ
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

       int* client_socket = new int; //Ϊ�ͻ�socket����ռ�(���������)
        *client_socket = accept(sock, nullptr, nullptr);
        Working_Directory[*client_socket]=".";
        
        if (*client_socket < 0) {
            cerr << "Accept error" << endl;
            close(sock);
            delete client_socket; 
            continue;    //����������һ��client
        }
        
        cout << "I am accepting." << endl;

        //Ϊ�ͻ�׼�����̣߳�����
        pthread_t thread_id;
        if (pthread_create(&thread_id, nullptr, handleClient, client_socket) != 0) {
            cerr << "Failed to create thread" << endl;
            close(*client_socket);
            delete client_socket; 
            continue;  //����������һ��client
        }

        //detach�̣߳���֤������������
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
    // �ر�socket
    close(sock);
    return 0;
}
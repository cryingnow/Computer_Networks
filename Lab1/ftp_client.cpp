/* Debug��¼
*  0.�ʼ��û������Data_Message��payload���֣�������ֻ�ܹ���payload�ֵڶ��η���.
*  1.open�����������û������char IP[16],û����ǰ����ռ䣬������segmentation fault.
*  2.ls���������is_Connected������-����Ϊwhile(true)ѭ����ע��open֮���������״̬���Ͳ���Ҫ������connect��.
*  3.get��Server���Թ���client��ôҲ������..
*       (1)�������UNUSED��Lab�ĵ�û�����UNUSEDΪ0�����Լ����������define�����ж�Reply.m_status!=UNUSEDʱ�ͻ�������⣬����ֱ�Ӱ����ɾ����
*       (2)�Լ�htonl��ntohl������������ע��ϸ�ڹ۲�.
*  4.put:����get
*  5.sha256:����ls�Ĳ�����ʹ��popen��ȡ������
*  6.cd��client����Ҫ���ǲ�����ֻ��Ҫ�����������
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
bool is_Connected=0; //�ж��Ƿ��Ѿ�����

//LabҪ������ݱ��Ľṹ
struct Data_Message
{
    u_int8_t m_protocol[MAGIC_NUMBER_LENGTH];   /* protocol magic number (6 bytes) */
    u_int8_t m_type;                            /* type (1 byte) */
    u_int8_t m_status;                          /* status (1 byte) */
    uint32_t m_length;                          /* length (4 bytes) in Big endian*/
}__attribute__ ((packed));

//����Lab�ĵ���д�ɹ������ݷ��뻺������send������(���Է���Data_Message�������ַ���)
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

//����Lab�ĵ���д�ɹ������ݷ��뻺������recv������(���Խ���Data_Message�������ַ���)
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



// ����open��open <IP> <port>  -����һ���� <IP>:<port> ������
int Open(int &sock, const char *ip, int port){
    struct sockaddr_in server_address;

    //�������ṹ��ʼ����port����
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_port = htons(port);
    server_address.sin_family = AF_INET;

    //������ַ�����ַ�����ʾת��Ϊ�����Ʊ�ʾ����IP��ַת��Ϊ�����ֽ���(ip)
    if (inet_pton(AF_INET, ip, &server_address.sin_addr) <= 0) {
        cerr << "Address Error" << endl;
        close(sock);
        //exit(EXIT_FAILURE);
        return 0;
    }

    //��������
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

    //����OPEN_CONN_REQUEST����
    //Open_Request.m_protocol=Message_protocol;
    memcpy(Open_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Open_Request.m_type=0xA1;
    Open_Request.m_status=UNUSED;
    Open_Request.m_length=htonl(HEADER_LENGTH);              //���ĵĳ��� m_length Ϊ��˷���ʾ����������׸��

    //���ͣ�����
    Send(sock, &Open_Request, HEADER_LENGTH);
    Recv(sock, &Open_Reply, HEADER_LENGTH);

    //�Խ��ս��м���
    if(!isMYFTP(Open_Reply))
        return 0;
    if(Open_Reply.m_type!=0xA2 || Open_Reply.m_status!=1 || Open_Reply.m_length!=htonl(HEADER_LENGTH))
        return 0;

    printf("Connected to %s:%d\n", ip, port);
    return 1;
}

//����quit:����������Ͽ����ӣ��ص� open ǰ��״̬������Ѿ��� open ǰ��״̬����ر� Client
int Quit(int &sock)
{
    //����Ѿ��Ͽ�������ôֱ���˳��ر�sock����
    if(is_Connected==0)
        //return 1;
        exit(0);
    
    Data_Message Quit_Request;
    Data_Message Quit_Reply;
    memset(&Quit_Request, 0, sizeof(Quit_Request));
    memset(&Quit_Reply, 0, sizeof(Quit_Reply));

    //����QUIT_REQUEST����
    memcpy(Quit_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Quit_Request.m_type=0xAD;
    Quit_Request.m_status=UNUSED;
    Quit_Request.m_length=htonl(HEADER_LENGTH);

    //���ͣ�����
    Send(sock, &Quit_Request, HEADER_LENGTH);
    Recv(sock, &Quit_Reply, HEADER_LENGTH);

    //�Խ��ս��м���
    if(!isMYFTP(Quit_Reply))
        return 0;
    if(Quit_Reply.m_type!=0xAE  || Quit_Reply.m_length!=12)
        return 0;

    if(is_Connected)
        close(sock);
    is_Connected=0;
    return 1;
}

//����ls: �г��洢�� Server ��ǰ����Ŀ¼�е������ļ�
int Ls(int &sock)
{
    cout<<"I am lsing"<<endl;
    Data_Message LS_Request;
    Data_Message LS_Reply;
    char file_list[2048] = {0};
    memset(&LS_Request, 0, sizeof(LS_Request));
    memset(&LS_Reply, 0, sizeof(LS_Reply));

    //����LS_REQUEST����
    memcpy(LS_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    LS_Request.m_type=0xA3;
    LS_Request.m_status=UNUSED;
    LS_Request.m_length=htonl(HEADER_LENGTH);

    //���ͣ�����
    Send(sock, &LS_Request, HEADER_LENGTH);
    Recv(sock, &LS_Reply, HEADER_LENGTH);

    //�Խ��ս��м���
    if(!isMYFTP(LS_Reply))
        return 0;
    if(LS_Reply.m_type!=0xA4)
        return 0;

    //��ȡfile_list�ĳ��ȣ�������
    int payload_length= ntohl(LS_Reply.m_length)-HEADER_LENGTH;
    cout<<payload_length<<endl;
    Recv(sock, file_list, payload_length);
    
    //���ls�Ľ��
    for(int i=0;i<payload_length;i++)
        cout<<file_list[i];

    return 1;
}

//���get FILE������FILE��Ҫ���ص��ļ�������
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

    //��ȡpayload����
    int filename_length=strlen(Filename);
    //Filename[filename_length]='\0';

    //����Get_REQUEST����
    memcpy(Get_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Get_Request.m_type=0xA7;
    Get_Request.m_status=UNUSED;
    Get_Request.m_length=htonl(HEADER_LENGTH + filename_length + 1);

    //���ͱ��ģ�����payload������
    Send(sock, &Get_Request, HEADER_LENGTH);
    Send(sock, Filename, filename_length + 1);
    Recv(sock, &Get_Reply, HEADER_LENGTH);

    
    if(!isMYFTP(Get_Reply))
        return 0;
    if(Get_Reply.m_type!=0xA8)
        return 0;
    
    //�ж��Ƿ�����ļ���������ڵĻ��������ļ�����
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
        
        //��ȡpayload�ĳ��ȣ�����
        int file_size=ntohl(Get_Data.m_length)-HEADER_LENGTH;
        Recv(sock, File, file_size);

        //��payload������д�뵽�ļ��У���������ļ���Ч��
        fwrite(File,1,file_size,fp);
    }
    fclose(fp);

    return 1;
}

//���put FILE������FILE��Ҫ���͵��ļ�������
int Put(int &sock, char *Filename)
{
    FILE *fp;
    Data_Message Put_Request;
    Data_Message Put_Reply;
    memset(&Put_Request, 0, sizeof(Put_Request));
    memset(&Put_Reply, 0, sizeof(Put_Reply));

    //���ж��ļ��Ƿ�����ڱ���
    if((fp=fopen(Filename,"r"))==NULL)
    {
        cerr << "File not exist." << endl;
        return 0;
    }

    //��ȡpayload����
    int filename_length=strlen(Filename);
    Filename[filename_length]='\0';

    //����Put_REQUEST����
    memcpy(Put_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    Put_Request.m_type=0xA9;
    Put_Request.m_status=UNUSED;
    Put_Request.m_length=htonl(HEADER_LENGTH + filename_length + 1);

    //���ͱ��ģ�����payload������
    Send(sock, &Put_Request, HEADER_LENGTH);
    Send(sock, Filename, filename_length + 1);
    Recv(sock, &Put_Reply, HEADER_LENGTH);

    
    if(!isMYFTP(Put_Reply))
        return 0;
    if(Put_Reply.m_type!=0xAA)
        return 0;
    
    //��ȡ�ļ����ݣ������͸�Server.
    char File[4194304];
    Data_Message File_Data_Header;
    memset(&File_Data_Header, 0, sizeof(File_Data_Header));

    //fread��ȡ�ļ�������ó���
    int file_length=fread(File,1,4194304,fp);

    //�ȷ���HEADER_LENGTH�ı��ģ��ٷ���payload
    File_Data_Header.m_type=0xFF;
    File_Data_Header.m_status=UNUSED;
    File_Data_Header.m_length=htonl(HEADER_LENGTH + file_length);
    Send(sock, &File_Data_Header, HEADER_LENGTH);
    Send(sock, File, file_length);

    fclose(fp);
    return 1;
}

//���sha256 FILE������FILE��Ҫ��ѯУ������ļ�������
int Sha(int &sock, char *Filename)
{
    Data_Message SHA_Request;
    Data_Message SHA_Reply;
    //char file_list[2048] = {0};
    memset(&SHA_Request, 0, sizeof(SHA_Request));
    memset(&SHA_Reply, 0, sizeof(SHA_Reply));

    //��ȡpayload����
    int filename_length=strlen(Filename);
    //Filename[filename_length]='\0';

    //����SHA_REQUEST����
    memcpy(SHA_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    SHA_Request.m_type=0xAB;
    SHA_Request.m_status=UNUSED;
    SHA_Request.m_length=htonl(HEADER_LENGTH + filename_length + 1);

    //���ͱ��ģ�����payload������
    Send(sock, &SHA_Request, HEADER_LENGTH);
    Send(sock, Filename, filename_length + 1);
    Recv(sock, &SHA_Reply, HEADER_LENGTH);


    //�Խ��ս��м���
    if(!isMYFTP(SHA_Reply))
        return 0;
    if(SHA_Reply.m_type!=0xAC)
        return 0;


    //�ж��Ƿ�����ļ���������ڵĻ������չ��ڲ�ѯ��Ϣ��FILE_DATA
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
        
        //��ȡpayload�ĳ��ȣ�����
        int payload_length=ntohl(SHA_Data.m_length)-HEADER_LENGTH;
        Recv(sock, SHA, payload_length);

        //���sha256�Ľ��
        for(int i=0;i<payload_length;i++)
            cout<<SHA[i];
    }

    return 1;
}

//���cd ���ĵ�ǰclient��Ӧ��server�Ĺ���Ŀ¼
int Cd(int &sock, char *Directory)
{
    Data_Message CD_Request;
    Data_Message CD_Reply;
    memset(&CD_Request, 0, sizeof(CD_Request));
    memset(&CD_Reply, 0, sizeof(CD_Reply));

    //��ȡpayload����
    int Directory_length=strlen(Directory);

    //����LS_REQUEST����
    memcpy(CD_Request.m_protocol, Message_protocol, MAGIC_NUMBER_LENGTH);
    CD_Request.m_type=0xA5;
    CD_Request.m_status=UNUSED;
    CD_Request.m_length=htonl(HEADER_LENGTH + Directory_length + 1);

    //���ͣ�����
    Send(sock, &CD_Request, HEADER_LENGTH);
    Send(sock, Directory, Directory_length + 1);
    Recv(sock, &CD_Reply, HEADER_LENGTH);

    //�Խ��ս��м���
    if(!isMYFTP(CD_Reply))
        return 0;
    if(CD_Reply.m_type!=0xA6)
        return 0;

    //���m_status����0��˵�����Ŀ¼������
    if(CD_Reply.m_status==0)
        cerr << "Directory Not Exist." << endl;

    return 1;
}

int main() {
    cerr << "hello from ftp client" << endl;
    string command;
    int sock;
    is_Connected=0;

    //���� Socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr<< "Socket creation Error" << endl;
        //exit(EXIT_FAILURE);
        return 0;
    }

    while(true){
        //���ദ��command������з�������
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

    // �ر� Socket
    close(sock);
    printf("Connection closed.\n");
    return 0;
}
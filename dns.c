/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h> /*  struct  sockaddr_in  */
#include <arpa/inet.h> /*  inet_pton ,  htons  */
#include <unistd.h> /*  read  */
#include <netdb.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h> //fcntl

#define BUFSIZE 1024
#define T_A 1 //Ipv4 address
#define T_NS 2 //Nameserver
#define T_CNAME 5 // canonical name
#define T_SOA 6 /* start of authority zone */
#define T_PTR 12 /* domain name pointer */
#define T_MX 15 //Mail server
#define T_AAAA 28

int flag = 0;

//DNS header structure
struct DNS_HEADER
{
    unsigned short id; // identification number
 
    unsigned char rd :1; // recursion desired
    unsigned char tc :1; // truncated message
    unsigned char aa :1; // authoritive answer
    unsigned char opcode :4; // purpose of message
    unsigned char qr :1; // query/response flag
 
    unsigned char rcode :4; // response code
    unsigned char cd :1; // checking disabled
    unsigned char ad :1; // authenticated data
    unsigned char z :1; // its z! reserved
    unsigned char ra :1; // recursion available
 
    unsigned short q_count; // number of question entries
    unsigned short ans_count; // number of answer entries
    unsigned short auth_count; // number of authority entries
    unsigned short add_count; // number of resource entries
};
 
//Constant sized fields of query structure
struct QUESTION
{
    unsigned short qtype;
    unsigned short qclass;
};
 
//Constant sized fields of the resource record structure
#pragma pack(push, 1)
struct R_DATA
{
    unsigned short type;
    unsigned short _class;
    unsigned int ttl;
    unsigned short data_len;
};

//Structure of a Query
typedef struct
{
    unsigned char *name;
    struct QUESTION *ques;
} QUERY;
/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

void  ALARMhandler(int sig)
{
    signal(SIGALRM, SIG_IGN);          /* ignore this signal       */
    printf("AAAA <none>\n\n");
    printf("Deu alarme\n");
    flag = 1;
    signal(SIGALRM, ALARMhandler);     /* reinstall the handler    */
    exit(1);
}

void ChangetoDnsNameFormat(unsigned char* dns,unsigned char* host) 
{
    int lock = 0 , i;
    strcat((char*)host,".");
     
    for(i = 0 ; i < strlen((char*)host) ; i++) 
    {
        if(host[i]=='.') 
        {
            *dns++ = i-lock;
            for(;lock<i;lock++) 
            {
                *dns++=host[lock];
            }
            lock++; //or lock=i+1;
        }
    }
    *dns++='\0';
}

static void print_buffer(char *buffer, int nBytes){

    int count = 0;

    //nBytes - 4 para desconsiderar o fim do vertor "0x00"
    while(count <= nBytes - 4){
        printf("%x ", (unsigned char)buffer[count]);
        count++;
    }
    printf("\n");

}

static void usage (char *program)
{
    printf ("usage: %s www.terra.com.br\n", program);
    printf("ou\n");
    printf ("usage: %s www.terra.com.br 127.0.0.1\n", program);
    exit (1);
}

static void *filtroDominio(char *manipular, char *dominio){

    int tam = strlen(manipular);
    strcpy(dominio, "" );

    if(manipular[tam-1] == '.'){
        strncpy(dominio, manipular, tam-1);
    }

    else{
        strncpy(dominio, manipular, tam);
    }

}


int main(int argc, char **argv) {
    int sockfd, port = 53, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *dominio_ip;
    char *hostname;
    char msg[BUFSIZE] = "Mensagem Teste";
    char *server = "127.0.0.1"; /*servidor padrao se nao for especificado outro*/
    size_t num_to_send;
    size_t num_sent;
    struct DNS_HEADER *dns = NULL;
    struct QUESTION *qinfo = NULL;
    char bufServerReply[BUFSIZE];
    int size_recv , total_size= 0;
    char addr[INET_ADDRSTRLEN];

    int query_type = T_AAAA;

    unsigned char buf[65536],*qname,*reader;
    int i , j , stop , s;

    //se programa foi executado com menos de duas linhas de comando
    if((argc != 2) && (argc != 3))
        usage (argv [0]);

    //se ip do servidor for passado por linha de comando atribui a variavel server
    if(argc == 3)
       server = argv [2];


    int tam = strlen(argv[1]);
    char dominio[BUFSIZE] = "";
    filtroDominio(argv[1], dominio);

    // Fill query structure
    //Set the DNS structure to standard queries
    dns = (struct DNS_HEADER *)&buf;
 
    dns->id = (unsigned short) htons(getpid());
    dns->qr = 0; //This is a query
    dns->opcode = 0; //This is a standard query
    dns->aa = 0; //Not Authoritative
    dns->tc = 0; //This message is not truncated
    dns->rd = 1; //Recursion Desired
    dns->ra = 0; //Recursion not available! hey we dont have it (lol)
    dns->z = 0;
    dns->ad = 0;
    dns->cd = 0;
    dns->rcode = 0;
    dns->q_count = htons(1); //we have only 1 question
    dns->ans_count = 0;
    dns->auth_count = 0;
    dns->add_count = 0;
 
    //point to the query portion
    qname =(unsigned char*)&buf[sizeof(struct DNS_HEADER)];
 
    ChangetoDnsNameFormat(qname , dominio);
    int tamDominio = strlen(dominio);
    dominio[tamDominio-1] = '\0';
    qinfo =(struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname) + 1)]; //fill it
 
    qinfo->qtype = htons( query_type); //type of the query , A , MX , CNAME , NS etc
    qinfo->qclass = htons(1); //its internet (lol)


    /*gethostbyname: get the server's DNS entry */
    if (!(dominio_ip = gethostbyname(dominio))) {
        fprintf(stderr,"ERROR, no such host as %s\n", dominio);
        exit(0);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    //sockfd = socket(AF_INET, SOCK_STREAM,0);
    //sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    
    //memset(&serveraddr, 0, sizeof(serveraddr));
    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    //serveraddr.sin_len = sizeof(serveraddr);
    //serverAddr.sin_addr.s_addr = inet_addr()
    serveraddr.sin_addr.s_addr=htonl(INADDR_ANY);
    serveraddr.sin_port = htons(port);
    memcpy(&serveraddr.sin_addr.s_addr,
            dominio_ip->h_addr_list[0],
            sizeof(struct in_addr));
    //serveraddr.sin_addr.s_addr=htonl(INADDR_ANY);

    /* Fazendo a conexao com o sender*/
    if(connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr))<0)
    {
        printf("\n Erro : Conexao com o sender falhou \n");
        return 1;
    }

    //faz o socket nao blockear
    //fcntl(sockfd, F_SETFL, O_NONBLOCK);
        
    printf("I am about to send request to IP address %s and port %d with Type AAAA",
        inet_ntoa(serveraddr.sin_addr), port);

    printf("\nSending Packet...\n");
    if( sendto(sockfd,(char*)buf,sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION),0,(struct sockaddr*)&serveraddr,sizeof(serveraddr)) < 0)
    {
        perror("sendto failed");
        exit(1);
    }

    sleep(2);

    /*
    if(!inet_ntop(dominio_ip->h_addrtype, &serveraddr, addr, INET_ADDRSTRLEN)){
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }*/

    printf("Done\n");

    signal(SIGALRM, ALARMhandler);
    alarm(10);

    //Receive the answer
    //int tam_sender = sizeof(struct sockaddr_in);
    i = sizeof serveraddr;
    printf("\nTrying Receiving answer...\n");
    printf("%s:\n", dominio);
    if(recvfrom (sockfd, (char*)buf, 65536 , 0 , (struct sockaddr*)&serveraddr , (socklen_t*)&i) < 0)
    {
        perror("recvfrom failed");
        exit(1);
    }
    printf("AAAA");
    //print_buffer(bufServerReply, BUFSIZE);

    printf("\nDone");

    return 0;
}

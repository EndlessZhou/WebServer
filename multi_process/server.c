#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/stat.h>

#define MAX_REQ 1024
struct Response
{
    char *protocol;
    char *status;
    char *content_type;
    int content_length;
    char *body;
};

struct Response *newResponse();
void handle_client(char *, int);
char *makeResponse(struct Response *);
int file_size(char *filename);
char *get_file(char *file_path, int fileSize);
char *get_file_path(char *filename, char *root_path);
char *get_mime(char *uri);
void handle_client(char *root_path, int fd);
void kill_zombie(int signal);
int main(int argc, char *argv[])
{
    int port = 8080;
    char *root_path = "./webcontent";
    if (argc >= 2)
    {
        port = atoi(argv[1]); //第二个参数是监听端口号
    }
    if (argc == 3)
    {
        root_path = argv[2]; //第二个参数是虚拟根目录
    }

    /*
    struct sockaddr_in
    {
		sa_family_t sin_family;//地址族
		uint16_t sin_port;//16位端口号
		struct in_addr sin_addr;//32位IP地址
		char sin_zero[8];//不使用
    };
    */
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr)); //全部置空
    addr.sin_family = AF_INET;  //地址族设置为IPv4
    // 理解一下 htonl 和 htons 是干什么的
    // INADDR_ANY 表示什么？
    //TCP/IP规定的网络字节序是大端
    //一般计算机内存是小端
    //INADDR_ANY相当于inet_addr("0.0.0.0")，即本机所有网卡
    //inet_addr()的功能是将一个点分十进制的IP转换成一个无符号32位整数型数
    //htonl()返回以网络字节序表示的32位整数
    //htons()返回以网络字节序表示的16位整数
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    /*
    int socket(int domin,int type,int protocol)
	domin:
    指定使用的地址族
	type:
    指定使用的套接字的类型:SOCK_STREAM（TCP套接字） SOCK_DGRAM（UDP套接字）
	protocol:
    如果套接字类型不是原始套接字，那么这个参数就为0，表示默认协议，SOCK_STREAM和SOCK_DGRAM对应的默认协议分别为TCP和UDP
    套接字创建成功返回套接字描述符，失败返回-1
    */
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    //检查是否创建套接字失败
    if (listenfd < 0)
    {
        perror("socket");
        exit(-1);
    }
    //将套接字绑定到addr（IP/端口号）
    //三个参数分别为套接字、通用套接字地址、地址所占空间
    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(-1);
    }
    //listen把一个未连接的套接字转换成一个被动套接字
    //第一个参数是套接字，第二个参数是连接队列总大小
    if (listen(listenfd, 5) < 0)
    {
        perror("listen");
        exit(-1);
    }

    int clientfd = -1;
    int ret = 0;
    //int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
    //从已完成队列队头返回下一个已完成连接
    //若成功则为非负描述符，若出错则为-1
    //如果accept成功，那么其返回值是由内核自动生成的一个全新描述符，代表与客户端的TCP连接。
    while ((clientfd = accept(listenfd, NULL, NULL)) >= 0)
    {
        // 如何获取客户端 IP:PORT
        //printf("Client connected: %d\n", clientfd);
        // 理解一下fork
        // 从当前位置创建子进程处理连接
        ret = fork();
        //printf("ret=%d\n",ret);
        if (ret < 0)
        {
            perror("fork");
            exit(-1);
        }
        //ret==0说明当前进程为子进程
        if (ret == 0)
        {
            close(listenfd);
            //在子进程中处理请求
            handle_client(root_path, clientfd);
            // 如果缺少这个exit，会不会有问题？
            exit(0);
        }
        signal(SIGCHLD,  &kill_zombie);
        close(clientfd);
    }

    return 0;
}
void handle_client(char *root_path, int fd)
{
    char req[MAX_REQ];
    int req_len = 0;
    req[req_len] = '\0';
    int n = 0;
    // 为什么需要一个循环来读取
    while (strstr(req, "\r\n\r\n") == NULL)
    {
        n = read(fd, req + req_len, MAX_REQ - req_len);
        if (n < 0)
        {
            perror("read");
            exit(-1);
        }
        if (n == 0)
        {
            fprintf(stderr, "client closed");
            return;
        }
        req_len += n;
        req[req_len] = '\0';
    }

    // 获取 URI
    strtok(req, " ");
    char *uri = strtok(NULL, " ");

    // printf("[%d] request_uri=%s\n", fd, uri);
    // printf("uri:%s\n",uri );
    // 这里只是给个Demo，直接响应"Hello World";
    // 按要求替换成相应的文件内容，文件路径为<root_path>/<uri>
    // 考察一下怎么防攻击：比如读到其它目录下的文件
    // 如果文件没有找到，应该怎么办？
    char *file_path = get_file_path(uri, root_path);
    int fileSize = file_size(file_path);
    struct Response *res = newResponse();
    res->content_length = fileSize;
    res->body = get_file(file_path, fileSize);
    res->body[fileSize] = 0;
    res->content_type = get_mime(uri);
    printf("%s\n", res->body);
    char *resp = makeResponse(res);
    int MAX_RESP = strlen(resp);
    printf("%s\n", resp);
    printf("%s\n", res->body);
    write(fd, resp, MAX_RESP);
    write(fd, res->body, fileSize);
    // do{
    //     int n=write(fd, resp+len, MAX_RESP-len);
    //     //printf("%d\n",n );
    //     len+=n;
    // }while(n!=0);
    // len=0;
    // do{
    //     int n=write(fd, res->body+len, fileSize-len);
    //     //printf("%d\n",n );
    //     len+=n;
    // }while(n!=0);
    close(fd);
}
int file_size(char *filename)
{
    struct stat statbuf;
    stat(filename, &statbuf);
    int size = statbuf.st_size;
    return size;
}

char *get_file(char *file_path, int fileSize)
{
    char *buf = NULL;
    FILE *fp = fopen(file_path, "r");
    if (fp == NULL)
    {
        perror("FILE error");
        return NULL;
    }
    else
    {
        printf("%s FILE is exist,size=%d\n", file_path, fileSize);
        buf = malloc(fileSize);
        int fer = fread(buf, fileSize, 1, fp);
        printf("fread:%d\n", fer);
    }
    fclose(fp);
    return buf;
}
char *get_file_path(char *filename, char *root_path)
{
    char *file_path = malloc(100);
    strcpy(file_path, root_path);
    strcat(file_path, filename);
    // printf("filename:%s\n", filename);
    // printf("file_path:%s\n",file_path);
    return file_path;
}
char *get_mime(char *uri)
{
    int len = strlen(uri);
    for (int i = len - 1; i >= 0; i--)
    {
        if (uri[i] == '.')
        {
            if (strstr(uri + i, "jpg") && len - 1 - i == 3)
            {
                return "image/jpeg";
            }
            if (strstr(uri + i, "css") && len - 1 - i == 3)
            {
                return "text/css";
            }
            if (strstr(uri + i, "js") && len - 1 - i == 2)
            {
                return "application/x-javascript";
            }
        }
    }
    return "text/html";
}

char *makeResponse(struct Response *res)
{
    char *resp = malloc(1024 + res->content_length);
    sprintf(resp, "%s %s\r\n", res->protocol, res->status);
    sprintf(resp + strlen(resp), "Content-Length: %d\r\n", res->content_length);
    sprintf(resp + strlen(resp), "Content-Type: %s\r\n", res->content_type);
    sprintf(resp + strlen(resp), "Connection: Close\r\n");
    sprintf(resp + strlen(resp), "\r\n");
    return resp;
}
struct Response *newResponse()
{
    struct Response *res = malloc(sizeof(struct Response));
    res->protocol = "HTTP/1.1";
    res->status = "200 OK";
    res->content_type = "text/html";
    res->content_length = 0;
    res->body = NULL;
    return res;
}
void kill_zombie(int signal)
{
    pid_t pid;
    int stat;
    while(((pid=waitpid(-1,&stat,WNOHANG)))>0)
        printf("child %d terminated.\n", pid);
}
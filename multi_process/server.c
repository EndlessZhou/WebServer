#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdbool.h>

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
void write_s(int,const void *,size_t);
bool checkPath(char *file_path, char *root_path);

int main(int argc, char *argv[])
{
    int port = 8080;
    char *root_path = "./resource";
    if (argc >= 2)
    {
        port = atoi(argv[1]); //第二个参数是监听端口号
    }
    if (argc == 3)
    {
        root_path = argv[2]; //第二个参数是虚拟根目录
    }

    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr)); //全部置空
    addr.sin_family = AF_INET;  //地址族设置为IPv4
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
            exit(0);
        }
        signal(SIGCHLD, &kill_zombie);
        close(clientfd);
    }
    return 0;
}

void handle_client(char *root_path, int fd)
{
    struct Response *res = newResponse();
    char *resp = NULL;
    char req[MAX_REQ];
    int req_len = 0;
    req[req_len] = '\0';
    int n = 0;
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
    char *file_path = get_file_path(uri, root_path);
    char *real_root_path = malloc(100);
    realpath(root_path, real_root_path);
    if (!checkPath(file_path, real_root_path))
    {
        res->status = "403 Forbidden";
        res->body = "<p>403 Forbidden</p>";
        res->content_length = strlen(res->body);
        printf("403 Forbidden\n");
        resp = makeResponse(res);
        write_s(fd, resp, strlen(resp));
        write_s(fd, res->body, res->content_length);
        close(fd);
        return;
    }
    int fileSize = file_size(file_path);
    if (fileSize < 0)
    {
        res->status = "404 Not Found";
        res->body = "<p>404 Not Found</p>";
        res->content_length = strlen(res->body);
        printf("404 Not Found\n");
        resp = makeResponse(res);
        write_s(fd, resp, strlen(resp));
        write_s(fd, res->body, res->content_length);
        close(fd);
        return;
    }
    res->content_length = fileSize;
    res->body = get_file(file_path, fileSize);
    res->body[fileSize] = 0;
    res->content_type = get_mime(uri);
    //printf("%s\n", res->body);
    resp = makeResponse(res);
    int MAX_RESP = strlen(resp);
    //printf("%s\n", resp);
    //printf("%s\n", res->body);
    write_s(fd, resp, MAX_RESP);
    write_s(fd, res->body, fileSize);
    close(fd);
}

void write_s(int fd,const void *str,size_t len)
{
    int st=0,n=0;
    while(true){
        n=write(fd,str+st,len-st);
        if(st<0){
            printf("write error");
            return;
        }else if(st==0){
            return;
        }
        st+=n;
    }
}

bool checkPath(char *file_path, char *root_path)
{
    //printf("file_path:%s\n", file_path);
    //printf("root_path:%s\n", root_path);
    int len1 = strlen(file_path);
    int len2 = strlen(root_path);
    if (len1 < len2)
        return 0;
    return !strncmp(file_path, root_path, len2);
}

int file_size(char *filename)
{
    struct stat statbuf;
    if (stat(filename, &statbuf) < 0)
        return -1;
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
        //printf("%s FILE is exist,size=%d\n", file_path, fileSize);
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
    char *real_path = malloc(100);
    realpath(file_path, real_path);
    //printf("filename:%s\n", filename);
    //printf("file_path:%s\n",real_path);
    return real_path;
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
    char *resp = malloc(MAX_REQ + res->content_length);
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
    while (((pid = waitpid(-1, &stat, WNOHANG))) > 0)
        printf("child %d terminated.\n", pid);
}
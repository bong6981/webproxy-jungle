#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *requestline_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *eof = "\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

void doit(int connfd);

void *thread(int connfd); /*concurrent porxy에서 추가된 부분*/

void parse_uri(char *uri, char *hostname, char *path, int *port, char *query);
void build_req_msg(char *req_msg, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid; /* pthread의 자료형 */

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port> \n", argv[0]);
    exit(1); 
  }

  listenfd = Open_listenfd(argv[1]); /*듣기 소켓을 생성하고 listenfd를 받는다 */
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); /* 연결 되면 connfd 반환 */

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); 
    printf("Accepted connection from (%s %s).\n", hostname, port);

    /* 호출된 함수에서 쓰레드를 생성한다 */
    /* thread 식별자, 쓰레드 특성 지정 사용시 부여하는 값(기본NULL), 분기시켜서 실행할 함수, 실행할 함수에 매개변수로 넘겨지는 값*/
    /* doit(connfd), Close(connfd); 를 thread 함수 내에서 (다른 쓰레드에서) 실행할 것 */
    /* connection 맺어질 떄마다 새로운 쓰레드에서 작업한다 */
    Pthread_create(&tid, NULL, thread, (void *)connfd); 
  }
  return 0;
}

void *thread(int connfd) { 
  Pthread_detach(pthread_self()); /* pthread_create()로 쓰레드 생성시, thread 종료하더라도 자원 반환되지 않는다.
                                     detach하면 thread 종료시 다른 쓰레드랑 join할 필요 없이 자동으로 자원이 반환된다
                                     join을 호출하는 쓰레드에서 인자로 넣어준 쓰레드가 끝날 때까지 기다리다가 해당 쓰레드 종료되면 자원을 받아온다*/ 
  doit(connfd);
  Close(connfd); 
}

void doit(int connfd)
{
  /*store the request line arguments*/
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], query[MAXLINE];
  int port;
  
  char buf[MAXLINE], req_msg[MAXLINE];

  rio_t client_rio, server_rio; 
  int end_serverfd; /*the end server file descriptor*/

  Rio_readinitb(&client_rio, connfd);
  Rio_readlineb(&client_rio, buf, MAXLINE);
  /*read the client request line*/
  sscanf(buf, "%s %s %s", method, uri, version); 

  if (strcasecmp(method, "GET")) /* GET 메소드 아니면 에러 */
  {
    clienterror(connfd, method, 501, "Not implemented",
                "Proxy does not implement this method");
    return;
  }

  /*parse the uri to get hostname, file path, port, query*/
  /*query를 받았으나 메시지 바디에 넣지는 못했다*/
  parse_uri(uri, hostname, path, &port, query);

  /*build the http header which will send to the end server*/
  build_req_msg(req_msg, hostname, path, port, &client_rio);

  /*connect to the end server*/
  end_serverfd = connect_endServer(hostname, port);
  if (end_serverfd < 0)
  {
    printf("connection failed\n");
    return;
  }

  Rio_readinitb(&server_rio, end_serverfd);
  /*write the http header to endserver*/
  Rio_writen(end_serverfd, req_msg, strlen(req_msg));

  /*receive message from end server and send to the client*/
  size_t n;
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)))
  {
    printf("proxy received %d bytes,then send\n", n);
    Rio_writen(connfd, buf, n);
  }
  Close(end_serverfd);
}


void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXLINE];

  /*Build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s : %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s \r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void build_req_msg(char *req_msg, char *hostname, char *path, int port, rio_t *client_rio)
{
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  /* request line */
  sprintf(request_hdr, requestline_hdr_format, path);

  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
  { /*라인별로 읽기*/
    if (!strcmp(buf, eof))
      break;

    if (!strncasecmp(buf, host_key, strlen(host_key)))
    {
      strcpy(host_hdr, buf);
      continue;
    }

    /* connection, proxy_connection, user_gent에 해당하는 헤더는 고정값으로 넣어줄 것이기 때문에 그 외 헤더만 넣는다 */
    if (strncasecmp(buf, connection_key, strlen(connection_key)) && strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) && strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
    {
      strcat(other_hdr, buf);
    }
  }

  if (strlen(host_hdr) == 0)
  {
    sprintf(host_hdr, host_hdr_format, hostname);
  }

  sprintf(req_msg, "%s%s%s%s%s%s%s",
          request_hdr,
          host_hdr,
          conn_hdr,
          prox_hdr,
          user_agent_hdr,
          other_hdr,
          eof);
  return;
}

inline int connect_endServer(char *hostname, int port)
{
  char port_str[100];
  sprintf(port_str, "%d", port);
  return Open_clientfd(hostname, port_str);
}

void parse_uri(char *uri, char *hostname, char *path, int *port, char *query)
{
  *port = 80; /*포트번호 입력되지 않으면 기본 80 */

  char *hostname_start_p = strstr(uri, "//");
  hostname_start_p = hostname_start_p + 2;

  char *query_start_p = strstr(hostname_start_p, "?");
  if (query_start_p != NULL)
  {
    *query_start_p = '\0';
    sscanf(query_start_p + 1, "%s", query);
  }

  char *path_start_p = strstr(hostname_start_p, "/");
  if (path_start_p != NULL)
  {
    sscanf(path_start_p, "%s", path);
    *path_start_p = '\0';
  }

  char *port_start_p = strstr(hostname_start_p, ":");
  if (port_start_p != NULL)
  {
    *port_start_p = '\0';
    sscanf(port_start_p + 1, "%d[^\n]", port);
  }
  sscanf(hostname_start_p, "%s", hostname);

  return;
}

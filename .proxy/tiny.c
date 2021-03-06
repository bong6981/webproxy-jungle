/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); /* getaddrinfo로 구조체 얻어서, sock() 소켓생성, bind() 소켓에 주소 바인딩 까지의 결과로 listenfd를 얻는다 */
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  /* line:netp:tiny:accept, accpet호출해서 block되어 클라이언트의 요청을 기다리고 클라이언트 요청이 오면 addr구조체제 client주소, 포트를 저장한다.(연결 수립) */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);  /* clientaddr에 있는 호스트,서비스 이름 스트링으로 변환해서 hostname, port에 저장한다. */
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  
  /* Read request line and headers */
  /* 버퍼를 통한 읽기를 위해 Rio_readinitb(), Rio_readlineb()를 사용한다 */
  Rio_readinitb(&rio, fd); /* 식별자 fd를 주소 rio_t 타입의 읽기 버퍼와 연결한다. */
  Rio_readlineb(&rio, buf, MAXLINE); /*MAXLINE-1 개의 바이트를 읽어(+NULL) (한줄), buf에 복사 */
  printf("Request headers:\n");
  printf("%s", buf); 
  sscanf(buf, "%s %s %s", method, uri, version); /* 첫 줄은 request line에 해당한다 */
  
  if(strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) { /* method가 GET, HEAD만 받는 것으로 설정 */
    clienterror(fd, method, 501, "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); /* 다른 헤더들을 읽는 부분인데 아무 처리를 해주지 않아 사실상 아무 기능하지 않는다 */

  /*Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs); /* 동적 로딩이면 0, 정적 로딩이면 1 값을 반환한다. uri를 filename과 cgiars(동적 로딩 cgi 를 위한 args 아규먼트)로 분리한다.  */
  if (stat(filename, &sbuf) < 0) { /* stat는 파일 상태를 알아오는 함수, file상태를 얻어와서 sbuf에 채워 넣는다, 성공할 경우 0, 실패 -1 */
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't fid this file");
    return;            
  }

  if (is_static) { /* Serve static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { /* 일반 파일 여부 검사 || 접근 권한 값이 has read permission */
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method); /* fd에 응답아 작성되어 클라이언트한테 전달된다 */
  } 
  else { /* Serve dynamic content */
    if (!S_ISREG(sbuf.st_mode) || !(S_IXUSR & sbuf.st_mode)) { /* 일반 파일 여부 검사 || 접근 권한 값이 has execute permission */
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method); /* 자식 프로세스 fork하고 그 후에 CGI 프로그램을 자식 프로세스에서 실행, 동적 컨텐츠 제공, 표준출력을 -> fd로 변환하여 결과를 클라이언트한테 전달 */
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXLINE];

  /*Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
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

void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) { /* request 헤더들 뒤에는 eof가 있으므로 이를 만날 떄까지 한 줄씩 읽는다 */
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char* cgiargs) {
  char *ptr;

  if(!strstr(uri, "cgi-bin")) { /* Static content */
    strcpy(cgiargs, ""); /* cgi 인자 스트링을 지운다 (strcpy는 덮어쓰기)*/
    strcpy(filename, "."); 
    strcat(filename, uri); /* ./index.html 같은 상대경로로 변경 (strcat는 이어쓰기) */
    if(uri[strlen(uri)-1] == '/')  /* /로 끝난다면 */
      strcat(filename, "home.html"); /* 기본 파일 이름을 추가한다 */ 
    return 1;
  }
  else { /* Dynamic content */
    ptr = index(uri, '?'); /* ?부분의 포인터 찾아서 */
    if (ptr) {            
      strcpy(cgiargs, ptr+1); /*cgi 인자들을 추출*/
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, ""); 
    strcpy(filename, "."); 
    strcat(filename, uri); /* 나머지 uri 부분을 상대 경로로 변경한다. ./index.html */
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize, char *method) {

  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXLINE];
  
  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); /* response line */
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf); /* 여기서부터 응답 headers */
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); /* 헤더 끝에는 빈 줄 추가 */
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* HEAD 메소드일 때는 헤드까지만 출력한다 */
  if (!strcasecmp(method, "HEAD"))
    return;

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0); /* 파일을 성공 적으로 열었다면 파일 지정 번호 return */
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); ./* 11-9 풀기 이전의 코드로 주석 처리, 파일 사이즈만큼 가상 메모리 영역으로 매핑, PROT_READ : 읽을 수 있다(접근권한설정), 0 매핑한 객체의 유형 설명 비트*/
  srcp = (char *)Malloc(filesize); /* 파일 사이즈 만큼 메모리 할당 */
  Rio_readn(srcfd, srcp, filesize); 
  Close(srcfd);  /* 새롭게 메모리에 매핑했으니 기존에 연 파일은 닫아준다 */
  Rio_writen(fd, srcp, filesize); /* 매핑 한 것을 fd에 write */
  free(srcp);
  // Munmap(srcp, filesize); /* 11-9 풀기 이전의 코드로 주석처리, fd에 적었으니 srcp 메모리 해제 */ 
}

/*
 * get_filetype - Derive file type from filename
 * else if 로 허용할 파일 확장자를 추가할 수 있다 
*/
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) 
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, ".gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, ".png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {

  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1,0 200 OK\r\n"); /* response line */
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { /* 결과 값이 0이면 Child 프로세스라는 것, 부모 프로세스라면 이를 통과한다. */
    /* Real server sould set all CGI vars here 여기서는 간단하게 하기 위해 QUERY_STRING 만 가져옴 */
    setenv("QUERY_STRING", cgiargs, 1); /* 1은 overwrite 할지 말지. 0은 overwirte X */
    setenv("REQUEST_METHOD", method, 1);
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to clinet */
    Execve(filename, emptylist, environ); /* CGI프로그램 로드, 실행, emptylist는 argv가 들어가는 부분, environ은 환경변수 들어가는 부분 */
  }
  Wait(NULL); /* Parent waits for and reaps child */
}



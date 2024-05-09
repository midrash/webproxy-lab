/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void sigchldHandler(int sig);
void echo(int connfd);
void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize,char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs,char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,char *longmsg);
void sigchild_handler(int sig);

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

  // if (Signal(SIGCHLD, sigchild_handler) == SIG_ERR)
  //   unix_error("signal child handler error");
    
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    printf("리슨중이에요(수락대기중)\n");
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    printf("수락했어요\n");
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    /* 트랜젝션을 수행 */
    //echo(connfd);
    doit(connfd);   // line:netp:tiny:doit
    /* 트랜잭션이 수행된 후 자신 쪽의 연결 끝 (소켓) 을 닫는다. */
    Close(connfd);   // 자신 쪽의 연결 끝을 닫는다.
  }
}

void echo(int connfd)
{
    size_t n;
    char buf[MAXLINE];
    rio_t rio;
    Rio_readinitb(&rio, connfd);
    while((n= Rio_readlineb(&rio,buf,MAXLINE))!=0){
        printf("server received %d bytes\n",(int)n);
        Rio_writen(connfd,buf,n);
    }
}

// 클라이언트의 요청 라인을 확인해 정적, 동적컨텐츠인지 구분하고 각각의 서버에 보냄
void doit(int fd) // -> connfd가 인자로 들어옴
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];  // 클라이언트에게서 받은 요청(rio)으로 채워진다.
  char filename[MAXLINE], cgiargs[MAXLINE];  // parse_uri를 통해 채워진다.
  rio_t rio;
  /* Read request line and headers */
  /* 클라이언트가 rio로 보낸 request 라인과 헤더를 읽고 분석한다. */
  /* Rio = Robust I/O */
  // rio_t 구조체를 초기화 해준다.
  Rio_readinitb(&rio, fd); // &rio 주소를 가지는 읽기 버퍼와 식별자 connfd를 연결한다.
  Rio_readlineb(&rio, buf, MAXLINE); // 그리고 rio(==connfd)에 있는 string 한 줄(응답 라인)을 모두 buf로 옮긴다.
  printf("Request headers:\n");
  printf("%s", buf);  // 요청 라인 buf = "GET /godzilla.gif HTTP/1.1\0"을 표준 출력만 해줌.
  sscanf(buf, "%s %s %s", method, uri, version); // buf에서 문자열 3개를 읽어와 method, uri, version이라는 문자열에 저장.

  printf("method :%s\n",method);
  printf("strcasecmp(method, HEAD) :%d\n",strcasecmp(method, "HEAD"));
  // 요청 method가 GET이 아니면 종료. main으로 가서 연결 닫고 다음 요청 기다림.
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {  // method 스트링이 GET이 아니면 0이 아닌 값이 나옴
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // 요청 라인을 뺀 나머지 요청 헤더들을 무시한다.
  read_requesthdrs(&rio);

  /* Parse URI from GET request */
  /* URI 를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고, 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정한다.  */
  is_static = parse_uri(uri, filename, cgiargs); // 정적이면 1 동적이면 0

  /* stat(file, *buffer) : file의 상태를 buffer에 넘긴다. */
  // 여기서 filename은 parse_uri로 만들어준 filename
  if (stat(filename, &sbuf) < 0) {  // 못 넘기면 fail. 파일이 없다. 404.
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  /* 컨텐츠의 유형(정적, 동적)을 파악한 후 각각의 서버에 보낸다. */
  if (is_static) { /* Serve static content */
    // !(일반 파일이다) or !(읽기 권한이 있다)
    printf("정적 커텐츠임:\n");
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    // 정적 컨텐츠면 사이즈를 같이 서버에 보낸다. -> Response header에 Content-length 위해
    serve_static(fd, filename, sbuf.st_size,method);
  } else { /* Serve dynamic content */
    // !(일반 파일이다) or !(실행 권한이 있다)
    printf("동적 커텐츠임:\n");
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    // 동적 컨텐츠면 인자를 같이 서버에 보낸다.
    serve_dynamic(fd, filename, cgiargs,method);
  }
}
// 클라이언트 오류 보고
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF]; // 에러메세지, 응답 본체
  
  // build HTTP response
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server></em>\r\n", body);
  
  // print HTTP response
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  
  // Rio_writen으로 buf와 body를 서버 소켓을 통해 클라이언트에게 보냄
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// tiny는 요청 헤더 내의 어떤 정보도 사용하지 않고 이들을 읽고 무시
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) { // EOF(한 줄 전체가 개행문자인 곳) 만날 때 까지 계속 읽기
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
}
// uri를 받아 요청받은 filename(파일이름), cgiarg(인자)를 채워줌.
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;
  printf("parse_uri: %s\n", uri);
  /* strstr 으로 cgi-bin이 들어있는지 확인하고 양수값을 리턴하면 dynamic content를 요구하는 것이기에 조건문을 탈출 */
  if (!strstr(uri, "cgi-bin")) { /* Static content*/
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);

    //결과 cgiargs = "" 공백 문자열, filename = "./~~ or ./home.html
	  // uri 문자열 끝이 / 일 경우 home.html을 filename에 붙혀준다.
    /*
      uri : /home.html
      cgiargs : 
      filename : ./home.html
    */
    if (uri[strlen(uri) - 1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;

  } else { // 동적 컨텐츠 요청
    /*
      uri : /cgi-bin/adder?1234&1234
      cgiargs : 1234&1234
      filename : ./cgi-bin/adder
    */
    ptr = index(uri, '?');
    // '?'가 있으면 cgiargs를 '?' 뒤 인자들과 값으로 채워주고 ?를 NULL로
    if (ptr) {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    } else { // '?' 없으면 cgiargs에 아무것도 안 넣어줌
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);

    return 0;
  }
}
// 클라이언트가 원하는 정적 컨텐츠를 받아와서 응답 라인과 헤더를 작성하고 서버에게 보냄, 그 후 정적 컨텐츠 파일을 읽어 그 응답 바디를 클라이언트에게 보냄
void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // Send response headers to client 클라이언트에게 응답 헤더 보내기
  // 응답 라인과 헤더 작성
  get_filetype(filename, filetype);  // 파일 타입 찾아오기 
  sprintf(buf, "HTTP/1.0 200 OK\r\n");  // 응답 라인 작성
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);  // 응답 헤더 작성
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  
  /* 응답 라인과 헤더를 클라이언트에게 보냄 */
  Rio_writen(fd, buf, strlen(buf));  // connfd를 통해 clientfd에게 보냄
  printf("Response headers:\n");
  printf("%s", buf);  // 서버 측에서도 출력한다.

  if (strcasecmp(method, "HEAD")==0){
    printf("정적 헤드 메서드에요\n");
    return; // void 타입이라 바로 리턴해도 됨(끝내라)
  } // 같으면 0(false). 다를 때 if문 안으로 들어감

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0); // filename의 이름을 갖는 파일을 읽기 권한으로 불러온다.
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //-> Mmap방법 : 파일의 메모리를 그대로 가상 메모리에 매핑함.
  // Close(srcfd); // 파일을 닫는다.
  // Rio_writen(fd, srcp, filesize);  // 해당 메모리에 있는 파일 내용들을 fd에 보낸다.
  // Munmap(srcp, filesize); //-> Mmap() 방법 : free해주는 느낌
  srcp = (char *)Malloc(filesize); // 11.9 문제 : mmap()과 달리, 먼저 파일의 크기만큼 메모리를 동적할당 해줌.
  Rio_readn(srcfd, srcp, filesize); // rio_readn을 사용하여 파일의 데이터를 메모리로 읽어옴. ->  srcp에 srcfd의 내용을 매핑해줌
  Close(srcfd); // 파일을 닫는다.
  Rio_writen(fd, srcp, filesize);  // 해당 메모리에 있는 파일 내용들을 fd에 보낸다.
  free(srcp); // malloc 썼으니까 free
  /*
  malloc(): segregated free list로 힙 영역 내에서 메모리 할당해주는 함수
  mmap(): 커널에 새 가상 메모리 영역을 생성해줄 것을 요청하는 함수
  mmap은 “가상 메모리”에 매핑하는 함수. 할당해야 할 메모리 크기가 크면 mmap()으로 할당. 
  mmap()에서는 페이지 단위(최소 4KB - 4096 Bytes)로 영역을 할당해주는 반면, 그 이하는 malloc()으로 heap에서 할당.
  malloc()은 free()를 호출한다고 해서 해당 메모리 자원이 시스템에 바로 반환되지 X(초기화 안됨.) 반면 munmap()의 경우에는 즉시 시스템에 반환됨.
  malloc()은 큰 메모리 블록 요청이 들어오면 내부적으로 mmap()을 써서 메모리를 할당한다. 이때 큰 메모리 기준은 mallopt() 함수로 제어. 
  따라서 특별한 제어가 필요한 게 아니면 개발자가 직접 mmap()을 쓸 필요는 없음.
  */

}

// filename을 조사해서 filetype을 입력해줌.
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html"))  // filename 스트링에 ".html" 
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4"); // 11.7 문제
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = { NULL };
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  printf("method :%s\n",method);
  printf("strcasecmp(method, HEAD) :%d\n",strcasecmp(method, "HEAD"));
  if (strcasecmp(method, "HEAD")==0){
    printf("동적 헤드 메서드에요\n");
    return; // void 타입이라 바로 리턴해도 됨(끝내라)
  } // 같으면 0(false). 다를 때 if문 안으로 들어감

  signal(SIGCHLD, sigchldHandler);
  int rc = Fork();
  if (rc == 0) { /* Child */
    printf("자식(%d)이 일처리하러 들어왔어요\n",(int)getpid());
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1);  // 

    // 클라이언트의 표준 출력을 CGI 프로그램의 표준 출력과 연결한다.
    // 이제 CGI 프로그램에서 printf하면 클라이언트에서 출력됨
    printf("자식(%d)이 일처리를 setenv 끝냈어요!\n",(int)getpid());
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */
    printf("자식(%d)이 일처리를 Dup2 끝냈어요!\n",(int)getpid());
    Execve(filename, emptylist, environ); /* Run CGI program */
    printf("자식(%d)이 일처리를 Execve 끝냈어요!\n",(int)getpid());
    exit(EXIT_SUCCESS);
  }
  //Wait(NULL); /* Parent waits for and reaps child */
  printf("부모(%d)가 일하고 있어요!\n",(int)getpid());
}
// void sigchldHandler(int sig)
// {
//     int old_error_num = errno ;
//     while(waitpid(-1,NULL,0)>0){
//         continue ;
//     }
//     if (errno != ECHILD) Sio_error("waitpid error");
//     errno = old_error_num ;
// }
void sigchldHandler(int signum)
{
    pid_t pid;
    int status;

    // 모든 종료된 자식 프로세스의 상태 확인
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            printf("자식 프로세스(pid=%d)가 정상적으로 종료되었습니다.\n", pid);
        } else {
            printf("자식 프로세스(pid=%d)가 비정상적으로 종료되었습니다.\n", pid);
        }
    }
}
void sigchild_handler(int sig) {
  int old_errno = errno;
  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
  }
  errno = old_errno;
}
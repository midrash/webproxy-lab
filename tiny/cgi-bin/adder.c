/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
  char *buf, *p, *method;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&');
    *p = '\0';
    // 기본 adder.c
    strcpy(arg1, buf);
    strcpy(arg2, p + 1);
    n1 = atoi(arg1);
    n2 = atoi(arg2);
    

    // 11. 10 문제
    ///cgi-bin/adder?first=13&second=5
    sscanf(buf, "first=%d", &n1); // buf에서 %d를 읽어서 n1에 저장
    sscanf(p + 1, "second=%d", &n2); // p + 1은 second를 가리키게 됨.
  }

  method = getenv("REQUEST_METHOD");

  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sThe Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  
  // method가 GET일 경우에만 body를 보냄
  if(strcasecmp(method, "HEAD") != 0)
    printf("%s", content);
    
  fflush(stdout);
  
  exit(0);
}
/* $end adder */
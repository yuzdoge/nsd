#include <stdio.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#define SERVER_PORT 0x1234
#define SERVER_ADDR INADDR_ANY

#define STDIN 0

#define MAXN 64
typedef struct socket_list {
  int mainsock;
  int num;
  int sock_arr[MAXN];
}SockList;

static void init_list(SockList *list) {
  memset(list, 0, sizeof(SockList));
}

static void ins_list(int s, SockList *list) {
  //printf("%ld\n", sizeof(list->sock_arr) / sizeof(list->sock_arr[0]));
  for (int i = 0; i < MAXN; i++) {
    if (list->sock_arr[i] == 0) {
      list->sock_arr[i] = s;
	  list->num++;
	  break;
	}
  }
}

static void del_list(int s, SockList *list) {
  for (int i = 0; i < MAXN; i++) { 
    if (list->sock_arr[i] == s) {
      list->sock_arr[i]	= 0;
	  list->num--;
	  break;
	}
  }
}

static void mk_fdlist(SockList *list, fd_set *fd_list) {
//put the fd watched into the fd_list
  FD_SET(list->mainsock, fd_list);
  FD_SET(STDIN, fd_list);
  for (int i = 0; i < MAXN; i++) {
    if (list->sock_arr[i] > 0) {
      FD_SET(list->sock_arr[i], fd_list);
	}
  }
}

#define setaddr(addr, fm, port, ipaddr) \
addr.sin_family = fm; \
addr.sin_port = htons(port); \
addr.sin_addr.s_addr = htonl(ipaddr)

int main() {
  sockaddr_in svraddr, cltaddr;
  SockList slist; 
  fd_set readfds, writefds, exceptfds; 
  timeval timeout __attribute__((unused));
  
  int s = socket(AF_INET, SOCK_STREAM, 0);
  assert(s != -1);
  setaddr(svraddr, AF_INET, SERVER_PORT, SERVER_ADDR);

  bind(s, (sockaddr *)&svraddr, sizeof(svraddr));

  listen(s, 5);
  printf("listening at 0.0.0.0:%u, mainsock = %d\n", SERVER_PORT, s);

  init_list(&slist);
  slist.mainsock = s;

  //int arg = 1; ioctl(slist.mainsock, FIONBIO, &arg); 
  //we can use `ioctl` to set the state of the fd, but I select the way below.

  fcntl(slist.mainsock, F_SETFD, fcntl(slist.mainsock, F_GETFD, 0) | O_NONBLOCK);
  /*`fcntl` is an system call to manipulate file descriptor, F_SETFD means set the value of the state to the fd,
   * the value is indicated by the third parameter; F_GETFD means get the state of the fd;
   * we get the original state and append the O_NONBLOCK to it through `or` operation.*/ 


#define is_ready(flag) (flag & 1) 
#define has_sent(flag) ((flag >> 1) & 1)
#define set_ready(flag) (flag | 1)
#define set_sent(flag) (flag | 2)
  char buf[128];
  char sendbuf[128];
  int flag = 0;
  int cur = 0;

  for (; ;) {
	FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    mk_fdlist(&slist, &readfds);
	mk_fdlist(&slist, &writefds);
	mk_fdlist(&slist, &exceptfds);

	int fdn = select(10, &readfds, &writefds, &exceptfds, &timeout); //the number of the fds of three fd_set should not greater than `nfds - 1`.
	assert(fdn != -1);
	if (fdn == 0) continue;
	//if (fdn > 0) printf("select %d\n", fdn);
    
	if (FD_ISSET(slist.mainsock, &readfds)) {
      socklen_t len = sizeof(cltaddr);	
	  int newsock = accept(slist.mainsock, (sockaddr *)&cltaddr, &len);
	  if (newsock == -1) continue; //Normally, if mainsock is in the readfds, it will not excute this statement.
	  
	  printf("accept a connection, newsock = %d\n", newsock);
	  ins_list(newsock, &slist);
	}

	if (FD_ISSET(STDIN, &readfds)) {
	  ssize_t rdn = read(STDIN, &sendbuf[cur], sizeof(sendbuf) - 1 - cur);
	  cur += rdn;
	  if (sendbuf[cur - 1] == '\n') {
		sendbuf[cur - 1] = 0;
		cur = 0;	
		flag = set_ready(flag);
	  }
	}

	for (int i = 0; i < MAXN; i++) {
	  int sfd = slist.sock_arr[i];
	  if (sfd != 0) {

	    if (FD_ISSET(sfd, &readfds)) {
	      ssize_t rcvn = recv(sfd, buf, sizeof(buf), 0);
          if (rcvn == 0) {
		    close(sfd);
			printf("close a socket\n");
			del_list(sfd, &slist);
		  }
		  else if (rcvn == -1) {
			close(sfd);
		    perror("Error");
			del_list(sfd, &slist);
		  }
		  else {
		    buf[rcvn] = 0;
		    printf("-->%s\n", buf);
		  }
		}

		if (FD_ISSET(sfd, &writefds)) {
		  if (is_ready(flag)) { 
		    send(sfd, sendbuf, strlen(sendbuf), 0);
			flag = set_sent(flag);
		  }
		}

	    if (FD_ISSET(sfd, &exceptfds)) {
		  printf("socket %d exception", sfd); 
		  perror("");
		  close(sfd);
		}
      }
	}
	if (has_sent(flag)) flag = 0; 
  }
  close(slist.mainsock);
  return 0;
}

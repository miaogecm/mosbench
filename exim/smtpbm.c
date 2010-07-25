/*
 * smtpbm ip-address user-name
 *
 * generate lots of mail to somebody's port 25.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <netinet/in.h>
#include <assert.h>
#include <sys/time.h>
#include <netinet/tcp.h>

static int total = 0;

static void usage(void)
{
	fprintf(stderr, "Usage: smtpbm ip-address port username from-address\n");
	exit(1);
}

static void oops(const char *s)
{
	extern int errno;
	fprintf(stderr, "smtpbm: %s: %s\n", s, strerror(errno));
	exit(1);
}

static double now(void)
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void xwrite(int s, const char *l)
{
	unsigned int n;
	char buf[512];
	
	n = strlen(l);
	assert(n < sizeof(buf) - 10);
	strcpy(buf, l);
	strcat(buf, "\r\n");
	if(write(s, buf, n+2) != n+2)
		oops("write");
}

static void xread(int s, char *buf)
{
	int n = 0, cc, i;
	
	while(n+1 < 512){
		cc = read(s, buf + n, 512 - n);
		if(cc <= 0)
			oops("xread EOF");
		n += cc;
		buf[n] = '\0';
		for(i = 0; i+1 < n; i++){
			if(buf[i] == '\r' && buf[i+1] == '\n'){
#if 0
				printf("xread: %s\n", buf);
#endif
				return;
			}
		}
	}
	oops("xread line too long");
}

static void do1(struct sockaddr_in *sin, const char *user, const char *from)
{
	char mail_from[128], head_from[128];
	char rcpt_to[128], head_to[128];
	char buf[512];
	int s, i;

	int yes = 1;

	snprintf(mail_from, sizeof(mail_from), "MAIL FROM:<%s>", from);
	snprintf(head_from, sizeof(mail_from), "From: %s", from);
	snprintf(rcpt_to, sizeof(rcpt_to), "RCPT TO:<%s>", user);
	snprintf(head_to, sizeof(head_to), "To: %s", user);
	
	s = socket(AF_INET, SOCK_STREAM, 0);
	if(s < 0)
		oops("socket");
	
	if(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0)
		oops("TCP_NODELAY");
	
	yes = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
		oops("SO_REUSEADDR");
	
	if(connect(s, (struct sockaddr *) sin, sizeof(*sin)) < 0)
		oops("connect");
	
	xread(s, buf);
	xwrite(s, "HELO smtpbm.foo.edu");
	xread(s, buf);
	for(i = 0; i < 10; i++){
		xwrite(s, mail_from);
		xread(s, buf);
		xwrite(s, rcpt_to);
		xread(s, buf);
		xwrite(s, "DATA");
		xread(s, buf);
		xwrite(s, head_from);
		xwrite(s, head_to);
		xwrite(s, "");
		xwrite(s, "the body");
		xwrite(s, ".");
		xread(s, buf);
		if(buf[0] != '2')
			oops("did not get final 2xx");
		total++;
	}
	xwrite(s, "QUIT");
	xread(s, buf);
	close(s);
}

int main(int argc, char ** argv)
{
	struct sockaddr_in sin;
	double t0, t1;
	
	if(argc != 5)
		usage();
	
	memset(&sin, '\0', sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(argv[2]));
	sin.sin_addr.s_addr = inet_addr(argv[1]);
	if(sin.sin_addr.s_addr == INADDR_NONE)
		usage();
	
	t0 = now();
	while(now() - t0 < 60)
		do1(&sin, argv[3], argv[4]);
	t1 = now();
	printf("%.2f / sec\n", total / (t1 - t0));
	return 0;
}

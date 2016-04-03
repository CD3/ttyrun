/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	  This product includes software developed by the University of
 *  	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* 1999-02-22 Arkadiusz Mi�kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 */

/* 2000-12-27 Satoru Takabayashi <satoru@namazu.org>
 * - modify `script' to create `ttyrec'.
 */

/*
 * script
 */

/* 2016-04-01 CD Clark III <clifton.clark@gmail.com>
 * - modify `ttyrec' to create `ttyrun'.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#if defined(SVR4)
#include <fcntl.h>
#include <stropts.h>
#endif /* SVR4 */

#include <sys/time.h>
#include "ttyrun.h"

#define HAVE_inet_aton
#define HAVE_scsi_h
#define HAVE_kd_h

#define _(FOO) FOO

#ifdef HAVE_openpty
#include <libutil.h>
#endif

#if defined(SVR4) && !defined(CDEL)
#if defined(_POSIX_VDISABLE)
#define CDEL _POSIX_VDISABLE
#elif defined(CDISABLE)
#define CDEL CDISABLE
#else /* not _POSIX_VISIBLE && not CDISABLE */
#define CDEL 255
#endif /* not _POSIX_VISIBLE && not CDISABLE */
#endif /* SVR4 && ! CDEL */

void done(void);
void fail(void);
void fixtty(void);
void getmaster(void);
void getslave(void);
void doinput(void);
void dooutput(void);
void doshell(const char*);

int parsectl(const char*,char*);
int passthrough(const char*);
void delay(const char*);

char	*shell;
FILE	*ifile;
int	master;
int	slave;
int	child;
int	subchild;

struct	termios tt;
struct	winsize win;
int	lb;
int	l;
#if !defined(SVR4)
#ifndef HAVE_openpty
char	line[] = "/dev/ptyXX";
#endif
#endif /* !SVR4 */
int	dflg;
int	nflg;

int
main(argc, argv)
	int argc;
	char *argv[];
{
	extern int optind;
	int ch;
	void finish();
	char *getenv();
	char *command = NULL;
	char *session = NULL;

	while ((ch = getopt(argc, argv, "e:dnh?")) != EOF)
		switch((char)ch) {
		case 'd': // add delays
		  dflg++;
			break;
		case 'n': // non-interactive mode
      nflg++;
			break;
		case 'e':
			command = strdup(optarg);
			break;
		case 'h':
		case '?':
		default:
			fprintf(stderr, _("usage: ttyrun [-e command] [-d] [-n] [file]\n"));
			exit(1);
		}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		session = argv[0];
	else
		session = "session.sh";

	if ((ifile = fopen(session, "r")) == NULL) {
		perror(session);
		fail();
	}

	shell = getenv("SHELL");
	if (shell == NULL)
		shell = "/bin/sh";

	getmaster();
	fixtty();

	(void) signal(SIGCHLD, finish);
	child = fork();
	if (child < 0) {
		perror("fork");
		fail();
	}
	if (child == 0) {
		subchild = child = fork();
		if (child < 0) {
			perror("fork");
			fail();
		}
		if (child)
			dooutput();
		else
			doshell(command);
	}
	doinput();

	return 0;
}

void
doinput()
{
	register int cc;
	char ibuf[BUFSIZ];
	char cbuf[BUFSIZ];   // command buffer
  char *sbuf;          // session file buffer
  int sbufi, sbufN;
  int i;
#ifdef HAVE_openpty
	(void) close(slave);
#endif

  // read the session file into memory
  sbufN = 0;
  sbuf = malloc( 0 );
  sbufi = 0;
  while(fgets(ibuf, BUFSIZ, ifile) != NULL)
  {
    if( sbufi + 1 >= sbufN )
    {
      sbufN += 2;
      sbuf = realloc(sbuf, sbufN*BUFSIZ*sizeof(char));
    }
    strcpy( sbuf+sbufi*BUFSIZ, ibuf );
    sbufi++;
  }
  strcpy( sbuf+sbufi*BUFSIZ, "EOF" );



  // loop through session commands
  for( sbufi = 0; sbufi < sbufN; sbufi++ )
  {
    if( strcmp( sbuf+sbufi*BUFSIZ, "EOF" ) == 0 )
      break; // last line in the file

    // parse the line to see if it is a 
    // control command
    parsectl( sbuf+sbufi*BUFSIZ, cbuf );
    // handl control commands
    if( strstr( cbuf, "pass" ) != NULL )
    {
      passthrough("\n\r");
      cbuf[0] = '\0';
      continue;
    }

    if( strstr( cbuf, "delay" ) != NULL )
    {
      delay( strchr(cbuf,' ') );
      cbuf[0] = '\0';
      continue;
    }


    // get ready
    if(nflg) // non-interactive mode
      delay("5"); // half second dalay
    else // user has to hit enter to start new command
      cc = read(0, cbuf, BUFSIZ);

    // start sending commands to the shell
    strcpy( ibuf, sbuf+sbufi*BUFSIZ );
    for( i = 0; i < strlen(ibuf); i++)
    {
      if( strchr( "\n\r\0", ibuf[i] ) )
      {
        if(nflg) // non-interactive mode
          delay("5"); // half second dalay
        else // user has to hit enter when newline/return is incountered
          cc = read(0, cbuf, BUFSIZ);
      }

      // send character
      (void) write(master, ibuf+i, 1);

      delay("2");
    }


  }

  free(sbuf);

  // read user input for remainder of the session.
  passthrough(NULL);
	done();
}

int
parsectl( const char* ibuf, char* cbuf )
{
  // control commands are given in comments.
  // any line that starts with a # could be a control command
  if( strchr( ibuf, '#' ) == NULL )
    return 0;

  // find first '#' and first non-space. if these are the same,
  // then we have a line that starts with (possibly) white space and
  // a '#'
  if( strspn( ibuf, "#" ) == strcspn( ibuf, " ")+1 )
    return 0;

  strcpy( cbuf, ibuf + strspn(ibuf," #") );

  return 1;
}


int
passthrough(const char *terms)
{
  // pass the user input to the tty until one of
  // the terminating chars is read.
	register int cc;
	char buf[BUFSIZ];
	while ((cc = read(0, buf, 1)) > 0)
  {
		(void) write(master, buf, 1);
    if( terms != NULL && strchr( terms, buf[0] ) )
        break;
  }
}

void
delay(const char *_dt)
{
  // delay, by calling nanosleep, for a specified
  // number of tenths of a second.
  // we accept a string, as opposed to an int, so that we
  // can support extracting the delay argument from a text file.
  long long dt;
  if( !dflg )
    return;

  dt = strtoll( _dt == NULL ? "5" : _dt, NULL, 10 )*1e8;
  struct timespec t;
  t.tv_sec  = dt/1000000000;
  t.tv_nsec = dt%1000000000;
  nanosleep(&t, NULL);

  return;
}

#include <sys/wait.h>

void
finish()
{
#if defined(SVR4)
	int status;
#else /* !SVR4 */
	union wait status;
#endif /* !SVR4 */
	register int pid;
	register int die = 0;

	while ((pid = wait3((int *)&status, WNOHANG, 0)) > 0)
		if (pid == child)
			die = 1;

	if (die)
		done();
}

struct linebuf {
    char str[BUFSIZ + 1]; /* + 1 for an additional NULL character.*/
    int len;
};

void
dooutput()
{
	int cc;
	char obuf[BUFSIZ];

	setbuf(stdout, NULL);
	(void) close(0);
#ifdef HAVE_openpty
	(void) close(slave);
#endif
	for (;;) {
		Header h;

		cc = read(master, obuf, BUFSIZ);
		if (cc <= 0)
			break;

		h.len = cc;
		gettimeofday(&h.tv, NULL);
		(void) write(1, obuf, cc);
	}
	done();
}

void
doshell(const char* command)
{
	/***
	int t;

	t = open(_PATH_TTY, O_RDWR);
	if (t >= 0) {
		(void) ioctl(t, TIOCNOTTY, (char *)0);
		(void) close(t);
	}
	***/
	getslave();
	(void) close(master);
	(void) dup2(slave, 0);
	(void) dup2(slave, 1);
	(void) dup2(slave, 2);
	(void) close(slave);

	if (!command) {
		execl(shell, strrchr(shell, '/') + 1, "-i", 0);
	} else {
		execl(shell, strrchr(shell, '/') + 1, "-c", command, 0);	
	}
	perror(shell);
	fail();
}

void
fixtty()
{
	struct termios rtt;

	rtt = tt;
#if defined(SVR4)
	rtt.c_iflag = 0;
	rtt.c_lflag &= ~(ISIG|ICANON|XCASE|ECHO|ECHOE|ECHOK|ECHONL);
	rtt.c_oflag = OPOST;
	rtt.c_cc[VINTR] = CDEL;
	rtt.c_cc[VQUIT] = CDEL;
	rtt.c_cc[VERASE] = CDEL;
	rtt.c_cc[VKILL] = CDEL;
	rtt.c_cc[VEOF] = 1;
	rtt.c_cc[VEOL] = 0;
#else /* !SVR4 */
	cfmakeraw(&rtt);
	rtt.c_lflag &= ~ECHO;
#endif /* !SVR4 */
	(void) tcsetattr(0, TCSAFLUSH, &rtt);
}

void
fail()
{

	(void) kill(0, SIGTERM);
	done();
}

void
done()
{
	if (subchild) {
		(void) fclose(ifile);
		(void) close(master);
	} else {
		(void) tcsetattr(0, TCSAFLUSH, &tt);
	}
	exit(0);
}

void
getmaster()
{
#if defined(SVR4)
	(void) tcgetattr(0, &tt);
	(void) ioctl(0, TIOCGWINSZ, (char *)&win);
	if ((master = open("/dev/ptmx", O_RDWR)) < 0) {
		perror("open(\"/dev/ptmx\", O_RDWR)");
		fail();
	}
#else /* !SVR4 */
#ifdef HAVE_openpty
	(void) tcgetattr(0, &tt);
	(void) ioctl(0, TIOCGWINSZ, (char *)&win);
	if (openpty(&master, &slave, NULL, &tt, &win) < 0) {
		fprintf(stderr, _("openpty failed\n"));
		fail();
	}
#else
#ifdef HAVE_getpt
	if ((master = getpt()) < 0) {
		perror("getpt()");
		fail();
	}
#else
	char *pty, *bank, *cp;
	struct stat stb;

	pty = &line[strlen("/dev/ptyp")];
	for (bank = "pqrs"; *bank; bank++) {
		line[strlen("/dev/pty")] = *bank;
		*pty = '0';
		if (stat(line, &stb) < 0)
			break;
		for (cp = "0123456789abcdef"; *cp; cp++) {
			*pty = *cp;
			master = open(line, O_RDWR);
			if (master >= 0) {
				char *tp = &line[strlen("/dev/")];
				int ok;

				/* verify slave side is usable */
				*tp = 't';
				ok = access(line, R_OK|W_OK) == 0;
				*tp = 'p';
				if (ok) {
					(void) tcgetattr(0, &tt);
				    	(void) ioctl(0, TIOCGWINSZ, 
						(char *)&win);
					return;
				}
				(void) close(master);
			}
		}
	}
	fprintf(stderr, _("Out of pty's\n"));
	fail();
#endif /* not HAVE_getpt */
#endif /* not HAVE_openpty */
#endif /* !SVR4 */
}

void
getslave()
{
#if defined(SVR4)
	(void) setsid();
	grantpt( master);
	unlockpt(master);
	if ((slave = open((const char *)ptsname(master), O_RDWR)) < 0) {
		perror("open(fd, O_RDWR)");
		fail();
	}
	if (isastream(slave)) {
		if (ioctl(slave, I_PUSH, "ptem") < 0) {
			perror("ioctl(fd, I_PUSH, ptem)");
			fail();
		}
		if (ioctl(slave, I_PUSH, "ldterm") < 0) {
			perror("ioctl(fd, I_PUSH, ldterm)");
			fail();
		}
#ifndef _HPUX_SOURCE
		if (ioctl(slave, I_PUSH, "ttcompat") < 0) {
			perror("ioctl(fd, I_PUSH, ttcompat)");
			fail();
		}
#endif
		(void) ioctl(0, TIOCGWINSZ, (char *)&win);
	}
#else /* !SVR4 */
#ifndef HAVE_openpty
	line[strlen("/dev/")] = 't';
	slave = open(line, O_RDWR);
	if (slave < 0) {
		perror(line);
		fail();
	}
	(void) tcsetattr(slave, TCSAFLUSH, &tt);
	(void) ioctl(slave, TIOCSWINSZ, (char *)&win);
#endif
	(void) setsid();
	(void) ioctl(slave, TIOCSCTTY, 0);
#endif /* SVR4 */
}

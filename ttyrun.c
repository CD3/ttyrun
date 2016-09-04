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

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
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
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <termios.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#if defined(SVR4)
#include <fcntl.h>
#include <stropts.h>
#endif /* SVR4 */

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


typedef struct header {
    struct timeval tv;
    int len;
} Header;


// FUNCTIONS
void done(void);
void fail(void);
void fixtty(void);
void getmaster(void);
void getslave(void);
void doinput(void);
void dooutput(void);
void doshell(const char*);

int parsectl(const char*,char*);
int passthrough();
void delay(long);

void print_usage(FILE*);
void print_help(FILE*);

// GLOBAL VARS
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

int main(int argc, char *argv[])
{
	extern int optind;
	int ch;
	void finish();
	char *getenv();
	char *command = NULL;
	char *session = NULL;

	char cbuf[BUFSIZ];   // command buffer

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
      print_help(stdout);
			exit(1);
      break;
		case '?':
		default:
      print_usage(stderr);
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

  // create processes to do the work
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
			dooutput(); // sub-child
		else
			doshell(command); // child
	}
	doinput(); // everybody

	return 0;
}

void print_usage(FILE* fp)
{
  fprintf(fp, _("usage: ttyrun [-e command] [-d] [-n] [file]\n"));
}

void print_help(FILE* fp)
{
  print_usage(fp);
  fprintf(fp, _("\n"));
  fprintf(fp, _("ttyrun reads a text file an executes each line as if it were typed into a shell.     \n"));
  fprintf(fp, _("By default, lines are loaded but not executed until the user hits return. This       \n"));
  fprintf(fp, _("is useful for giving demonstrations or tutorials from the command line.              \n"));
  fprintf(fp, _("                                                                                     \n"));
  fprintf(fp, _("options                                                                              \n"));
  fprintf(fp, _("   -d : add delays when sending input characters to shell. simulates typing.         \n"));
  fprintf(fp, _("   -n : non-interactive mode. don't wait for user comments, just run the script.     \n"));
  fprintf(fp, _("                                                                                     \n"));
  fprintf(fp, _("control commands                                                                     \n"));
  fprintf(fp, _("  commands can be given in the session file, or input by the user. all commands      \n"));
  fprintf(fp, _("  in the session file are given in comment lines.                                    \n"));
  fprintf(fp, _("   # passthrough : pass user input directly to the shell. allows user to run some    \n"));
  fprintf(fp, _("                   commands interactivly.                                            \n"));
  fprintf(fp, _("   # delay DELAY : add a DELAY (tenths of second) delay.                             \n"));
  fprintf(fp, _("   Press 'Return' : send line to stdin of shell.                                     \n"));
  fprintf(fp, _("   Press 'x' : exit                                                                  \n"));
  fprintf(fp, _("   Press 'p' : passthrough user input.                                               \n"));

}

void doinput()
{
	register int cc;
	char ibuf[BUFSIZ];   // input buffer
	char cbuf[BUFSIZ];   // command buffer
  char *sbuf;          // session file buffer
  int sbufi, sbufN;
  int i, j;
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
    // copy line to the input buffer
    strcpy( ibuf, sbuf+sbufi*BUFSIZ );

    if( strcmp( ibuf, "EOF" ) == 0 )
      break; // last line in the file

    // check line for a control command
    if( parsectl( ibuf, cbuf ) )
    {
      // handle control commands
      if( strstr( cbuf, "pass" ) != NULL )
      {
        passthrough(); // passthrough input
        cbuf[0] = '\0'; // clear the command buffer
        continue;
      }

      if( strstr( cbuf, "delay" ) != NULL )
      {
        delay( strtoll( strchr(cbuf,' ') == NULL ? "5" : strchr(cbuf,' '), NULL, 10 ) ); // pause for some time
        cbuf[0] = '\0'; // clear command buffer
        continue;
      }

      continue;

    }

    // ok, we have an input line. we wait for the user before sending
    // it to the shell unless non-interactive mode is on, and then we
    // pause for a half second.
    if(nflg) // non-interactive mode
      delay(5); // half second dalay
    else // read user command
    {
      j = 0;
      while( read(0,cbuf+j,BUFSIZ-j) > 0 ) // read characters until buffer is full or end of file or error
      {
        if( cbuf[j] == '\n' || cbuf[j] == '\r' )
        {
          cbuf[j] = '\0';
          break;
        }
        j++;
      }
    }

    if( strcmp( cbuf, "exit" ) == 0 || strcmp( cbuf, "x" ) == 0 )
      done();

    if( strcmp( cbuf, "passthrough" ) == 0 || strcmp( cbuf, "pass" ) == 0 || strcmp( cbuf, "p" ) == 0 )
      passthrough();

    // send input to the shell, one character at a time.
    // when we get to the end of the line, we wait for the user.
    for( i = 0; i < strlen(ibuf); i++)
    {
      if( ibuf[i] == '\n' || ibuf[i] == '\r' )
      { // newline or end of string

        if(nflg) // non-interactive mode
          delay(5); // half second dalay
        else // read user command
        {
          j = 0;
          while( read(0,cbuf+j,BUFSIZ-j) > 0 ) // read characters until buffer is full or end of file or error
          {
            if( cbuf[j] == '\n' || cbuf[j] == '\r' )
            {
              cbuf[j] = '\0';
              break;
            }
            j++;
          }
        }

        if( strcmp( cbuf, "exit" ) == 0 || strcmp( cbuf, "x" ) == 0 )
          done();

        if( strcmp( cbuf, "passthrough" ) == 0 || strcmp( cbuf, "p" ) == 0 )
          passthrough();


      }

      // send character
      (void) write(master, ibuf+i, 1);

      if(dflg) // delays flag
        delay(2); // half second dalay
    }


  }

  free(sbuf);

  // read user input for remainder of the session.
  /*passthrough();*/
	done();
}

void finish()
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

	setbuf(stdout, NULL); // set stdout to unbuffered
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
		execl(shell, strrchr(shell, '/') + 1, "-i", (char *)0);
	} else {
		execl(shell, strrchr(shell, '/') + 1, "-c", command, (char *)0);	
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





// parse a line to see if it contains a control command
int parsectl( const char* ibuf, char* cbuf )
{
  // control commands are given in comments.
  // any line that starts with a # could be a control command
  if( strchr( ibuf, '#' ) == NULL )
  {
    cbuf[0] = '\0';
    return 0;
  }

  // find first '#' and first non-space. if these are the same,
  // then we have a line that starts with (possibly) white space and
  // a '#'
  if( strspn( ibuf, "#" ) == strcspn( ibuf, " ")+1 )
    return 0;

  // copy the command into cbuf
  strcpy( cbuf, ibuf + strspn(ibuf," #") );

  return 1;
}

int passthrough()
{
  // pass the user input to the tty until one of
  // the terminating chars is read.
	register int cc;
	char buf[BUFSIZ];
	while ((cc = read(0, buf, 1)) > 0)
  {
    if( buf[0] == 3 ) // end of transmission
        break;
		(void) write(master, buf, 1);
  }
}

void delay(long counts)
{
  // delay, by calling nanosleep, for a specified number of counts.
  // one count is a tenth of a second.
  long long dt = counts*1e8;
  struct timespec t;
  t.tv_sec  = dt/1000000000;
  t.tv_nsec = dt%1000000000;
  nanosleep(&t, NULL);

  return;
}


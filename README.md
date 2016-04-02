# `ttyrun`

`ttyrun` is a tty session runner. It is based on `ttyrec`,
and is based on a stripped down version of the `ttyrec`
source.
It reads commands from a text file and runs them, but pauses
before executing each and waits for user input.

This is useful if you need to setup a demo or lecture on a
command line utility, but are not good at "live" demos. It
basically allows you to script your demo so that you don't
make mistakes or forget something.

## Building:

    % make

or if your system is SVR4 system (Solaris etc.),

    % make CFLAGS=-DSVR4

or if your system supports getpt(3),

    % make CFLAGS=-DHAVE_getpt

HAVE_getpt is required if your linux system uses devfs.


## Usage:

    % ttyrun session.sh

`ttyrun` will read commands from `session.sh` and run them in
a tty session. It will load each command and then wait for
you to press `Enter` before executing.

### Options:

```
  -d   Insert delays to simulate keystrokes
  -n   Non-interactive mode. Don't wait for the user to press `Enter`.
  -e command Run command for the session. Defaults to SHELL.
```

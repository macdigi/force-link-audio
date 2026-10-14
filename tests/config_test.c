#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
static void fs_log(const char *fmt, ...) {(void)fmt;}
#include "../src/addin_runtime.h"
#include <assert.h>
static void check(const char *text, int valid, int enabled, int port) {
 char path[]="/tmp/fla-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 assert(write(fd,text,strlen(text))==(ssize_t)strlen(text));close(fd);
 struct fl_settings c;assert(fl_read_settings(path,&c)==valid);
 if(valid){assert(c.enabled==enabled);assert(c.port==port);}unlink(path);
}
int main(void) {
 check("enabled=0\n",1,0,9000);
 check("enabled=1\nport=19000\npeer=Test Force\nchannel=Main\n",1,1,19000);
 check(" # comment\n enabled = 1 \n port = 65535\n",1,1,65535);
 check("enabled=2\n",0,0,0);check("enabled=1oops\n",0,0,0);
 check("port=0\n",0,0,0);check("port=-1\n",0,0,0);check("port=65536\n",0,0,0);
 check("unknown=1\n",0,0,0);check("enabled=$(touch /tmp/nope)\n",0,0,0);
 check("peer=\n",0,0,0);check("enabled\n",0,0,0);
 puts("12 configuration cases passed");return 0;
}

/* OpenMPC adapter. GPL-2.0-or-later; Mac Digi / Rare Data.
 * Setup only, never called from the audio callback. No shell evaluates settings.
 */
#include <limits.h>
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
struct fl_settings { int enabled, port; char peer[96], channel[96]; };
static char fl_dir[PATH_MAX];
static struct fl_settings fl_cfg;
static char *fl_trim(char *s) {
    while (isspace((unsigned char)*s)) ++s;
    size_t n = strlen(s); while(n && isspace((unsigned char)s[n-1])) s[--n]=0;
    return s;
}
static int fl_read_settings(const char *path, struct fl_settings *c) {
    *c=(struct fl_settings){0,9000,"Akai Force","Force Main"};
    FILE *f=fopen(path,"r"); if(!f) return 0;
    char line[512]; int valid=1;
    while(fgets(line,sizeof line,f)) {
        if(!strchr(line,'\n') && !feof(f)){valid=0;break;}
        char *s=fl_trim(line); if(!*s||*s=='#')continue;
        char *eq=strchr(s,'=');if(!eq){valid=0;break;}*eq=0;
        char *k=fl_trim(s),*v=fl_trim(eq+1);
        if(!strcmp(k,"enabled")||!strcmp(k,"port")) {
            char *end;errno=0;long n=strtol(v,&end,10);
            if(errno||!*v||*end){valid=0;break;}
            if(!strcmp(k,"enabled")){if(n<0||n>1){valid=0;break;}c->enabled=(int)n;}
            else {if(n<1024||n>65535){valid=0;break;}c->port=(int)n;}
        } else if(!strcmp(k,"peer")||!strcmp(k,"channel")) {
            if(!*v||strlen(v)>=96){valid=0;break;}
            strcpy(!strcmp(k,"peer")?c->peer:c->channel,v);
        } else {valid=0;break;}
    }
    if(ferror(f))valid=0;fclose(f);return valid;
}
static int fl_prepare(void) {
    char exe[PATH_MAX];ssize_t n=readlink("/proc/self/exe",exe,sizeof(exe)-1);
    if(n<0)return 0;exe[n]=0;char *base=strrchr(exe,'/');
    if(strcmp(base?base+1:exe,"MPC"))return 0;
    Dl_info info;if(!dladdr((void*)&fl_prepare,&info)||!info.dli_fname)return 0;
    if(!realpath(info.dli_fname,fl_dir))return 0;
    char *slash=strrchr(fl_dir,'/');if(!slash)return 0;*slash=0;
    char path[PATH_MAX];if(snprintf(path,sizeof path,"%s/force-link-audio.conf",fl_dir)>=(int)sizeof path)return 0;
    if(!fl_read_settings(path,&fl_cfg)||!fl_cfg.enabled)return 0;
    /* Two taps on one ALSA stream corrupt capture; migrate the old addon first. */
    const char *preload=getenv("LD_PRELOAD");
    if(preload && strstr(preload,"/ForceLinkAudio/forceStream.so")) {
        fs_log("[Force Link Audio] legacy addon also loaded; new addin stays disabled");return 0;
    }
    return 1;
}
static void *fl_reap(void *arg) {
    pid_t child=(pid_t)(intptr_t)arg;
    while(waitpid(child,NULL,0)<0 && errno==EINTR){}
    return NULL;
}
static int fl_spawn(void) {
    char helper[PATH_MAX],port[16],parent[24];
    if(snprintf(helper,sizeof helper,"%s/forceLinkAudio",fl_dir)>=(int)sizeof helper)return 0;
    snprintf(port,sizeof port,"%d",fl_cfg.port);snprintf(parent,sizeof parent,"%ld",(long)getpid());
    /* Shared installer stores payloads as 0644; use the device's ELF interpreter.
       This is the same armhf loader selected by the helper's ELF header. */
    char *argv[]={"/lib/ld-linux-armhf.so.3",helper,port,fl_cfg.peer,fl_cfg.channel,parent,NULL};
    char *env[]={"PATH=/usr/bin:/bin","LANG=C",NULL};
    posix_spawn_file_actions_t a;posix_spawn_file_actions_init(&a);
    posix_spawn_file_actions_addopen(&a,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_addopen(&a,STDOUT_FILENO,"/tmp/forceLinkAudio.log",O_WRONLY|O_CREAT|O_APPEND,0600);
    posix_spawn_file_actions_adddup2(&a,STDOUT_FILENO,STDERR_FILENO);
    pid_t pid;int rc=posix_spawn(&pid,argv[0],&a,NULL,argv,env);
    posix_spawn_file_actions_destroy(&a);
    if(rc){fs_log("[Force Link Audio] helper launch failed: %d",rc);return 0;}
    pthread_t waiter;
    if(pthread_create(&waiter,NULL,fl_reap,(void*)(intptr_t)pid)==0)pthread_detach(waiter);
    return 1;
}

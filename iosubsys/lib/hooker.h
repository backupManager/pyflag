//We call hard exit when hooking exit()
extern void _exit(int);

#define DEBUG_LEVEL 0

/* Used for Debugging messages*/
void debug(int level, const char *message, ...)
{
	va_list ap;
	
	if(DEBUG_LEVEL>level) {
	  va_start(ap, message);
	  vfprintf(stderr,message, ap);
	  fflush(stderr);
	  va_end(ap);
	};
};

struct dispatcher_t {
  //Handle for libc
  void *handle;
  //original hooked functions:
  int (*open)(const char *pathname, int flags,int mode);
  int (*open64)(const char *pathname, int flags,int mode);
  off_t (*lseek)(int fildes, off_t offset, int whence);
  off_t (*lseek64)(int fildes, off_t offset, int whence);
  ssize_t (*read)(int fd, void *buf, size_t count);
  void (*exit)(int status);
  int (*dup2)(int oldfd, int newfd);
  int (*close)(int fd);
  FILE * (*fopen)(const char *path, const char *mode);
  size_t (*fread)(void *ptr, size_t size, size_t nmemb, FILE *stream);
  int (*fclose)(FILE *stream);
  ssize_t (*write)(int fd, const void *buf, size_t count);
} *dispatch=NULL;

#define HOOK(x)   dispatch->x = dlsym(dispatch->handle,#x); check_errors();
#define CHECK_INIT  if(!dispatch) { init_hooker(); };

// This static variable is used to decide when we should hook calls
// through the library. The library itself will be using the same
// calls we are trying to hook (i.e. open,read seek etc). When running
// within the context of the library, we do not want to hook those
// calls, just use the original so functions. Hence we set the context
// to UNHOOKED just before we service the call, and return it to
// HOOKED just after. Note that this is _not_ thread safe - so we will
// have problems running programs with threads!!!!
enum context_t {
  HOOKED, UNHOOKED
};

void init_hooker(void);
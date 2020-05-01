#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>

// only for std::quoted()
#include <string>
#include <iomanip>
#include <sstream>


/* config file containing paths to (bind)mount */
#ifdef TEST
char volfile[] = "ucontainer.conf";
#else
char volfile[] = "/etc/containers/ucontainer.conf";
#endif

char tfile[] = "/tmp/CSTARTXXXXXX";

#define PATHLEN 256
char imgname[PATHLEN];
char homepath[PATHLEN];
int interactive = 0;

char *opt_interactive = (char *)"-it";
char *opt_start = (char *)"/START.sh";

char delim[] = " \t\r\n\v\f"; /* possible delimiters in volfile */

/* leave space for 32 bind mounts */
#define CMDMAX (10+64)
char *cmd[CMDMAX] = {
                     /*
                     "/usr/bin/gdb",
                     "--args",
                     */
                     (char *)"/usr/bin/docker-current",
                     (char *)"run",
                     (char *)"--rm",
                     (char *)"--rm", /* redundant, replaced by -it if interactive */
                     (char *)"-w",   /* workdir -> home */
                     NULL,   /* placeholder */
                     (char *)"-v",   /* placeholder for bindmount of /START.sh */
                     NULL
};
/* change the offset if you insert anything before the docker command */
#define CMDOFFS 0

void usage(char *prog)
{
  printf("Run a command in a container on behalf of a normal user.\n");
  printf("The container bind-mounts a set of external directories.\n");
  printf("\nUsage:\n");
  printf("\t%s [-i|--interactive] <image_name> [cmd]\n", prog);
  exit(0);
}

int mk_start_file(uid_t uid, int argc, char *argv[])
{
  gid_t gid = getgid();
  int fd = mkstemp(tfile);
  if (fd < 0) {
    perror("Failed to create temporary file!");
    return -1;
  }
  struct passwd *pwd = getpwuid(uid);
  if (!pwd) {
    perror("Could not locate password entry!");
    return -1;
  }
  struct group *grp = getgrgid(gid);
  if (!grp) {
    perror("Could not locate group entry!");
    return -1;
  }
  std::stringstream ssg, ssn, ssp, ss;
  ssg << std::quoted(std::string(grp->gr_name));
  dprintf(fd, "#!/bin/bash\n");
  dprintf(fd, "/usr/sbin/groupadd -g %d %s\n", gid, ssg.str().c_str());
  ss.str(std::string());
  ssp << std::quoted(std::string(pwd->pw_dir));
  ssn << std::quoted(std::string(pwd->pw_name));
  dprintf(fd, "/usr/sbin/useradd -g %d -d %s -M -u %d %s\n",
          gid, ssp.str().c_str(), uid, ssn.str().c_str());
  if (argc == 0) {
    dprintf(fd, "exec /sbin/runuser -u %s -- /bin/bash\n", ssn.str().c_str());
  } else {
    ss << std::quoted(std::string(argv[0]));
    dprintf(fd, "exec /sbin/runuser -u %s -- /bin/bash -c %s\n",
            ssn.str().c_str(), ss.str().c_str());
  }
  fchmod(fd, 0755);
  close(fd);
  strncpy(homepath, pwd->pw_dir, PATHLEN);
  return 0;
}

void mk_docker_cmd()
{
  /* add interactive option */
  if (interactive)
    cmd[CMDOFFS+3] = opt_interactive;

  /* add workdir -> homepath */
  cmd[CMDOFFS+5] = homepath;

  /* add /START.sh mount */
  int slen = strlen(tfile) + 1 + 9 + 1;
  cmd[CMDOFFS+7] = (char *)malloc(slen);
  snprintf(cmd[CMDOFFS+7], slen, "%s:/START.sh", tfile);

  /* now add the bind mounts in the config file */
  int cmdp = CMDOFFS+8; /* next free slot in cmd */
  char *buffer = NULL;
  size_t len;
  FILE *fp;
  if ((fp = fopen(volfile, "rb")) != NULL) {
    ssize_t bytes_read = getdelim( &buffer, &len, '\0', fp);
    if (bytes_read == -1) {
      perror("Failed while reading volume config file!");
      exit(1);
    }
    fclose(fp);

    char *vol;
    vol = strtok(buffer, delim);
    while (vol != NULL && cmdp < CMDMAX - 4) {
      if (vol[0] == '#') {
        vol = strtok(NULL, delim);
        continue;
      }
      cmd[cmdp++] = cmd[CMDOFFS+6]; /* pointer to -v */
      cmd[cmdp] = (char *)malloc(2 * strlen(vol) + 2);
      sprintf(cmd[cmdp], "%s:%s", vol, vol);
      ++cmdp;
      vol = strtok(NULL, delim);
    }
  } else {
    perror("Failed to open volume config file!");
  }

  /* add imagename and /START.sh */
  cmd[cmdp++] = imgname;
  cmd[cmdp++] = opt_start;
  cmd[cmdp] = NULL;
}

void run_cmd()
{
  pid_t pid = fork();
  if (pid == 0) {
    // this is the child
    uid_t uid = getuid();
    uid_t euid = geteuid();
    //printf("child uid=%d euid=%d\n", uid, euid);

    int err = execv(cmd[0], cmd);
    if (err) {
      perror("ERROR: execv");
      _exit(errno);
    }
  }  else if (pid > 0) {
    // this is the parent
    int wstatus;
    do {
      pid_t w = waitpid(pid, &wstatus, WUNTRACED | WCONTINUED);
      if (w == -1) {
        perror("waitpid");
        exit(EXIT_FAILURE);
      }
      
      if (WIFEXITED(wstatus)) {
        printf("exited, status=%d\n", WEXITSTATUS(wstatus));
      } else if (WIFSIGNALED(wstatus)) {
        printf("killed by signal %d\n", WTERMSIG(wstatus));
      } else if (WIFSTOPPED(wstatus)) {
        printf("stopped by signal %d\n", WSTOPSIG(wstatus));
      } else if (WIFCONTINUED(wstatus)) {
        printf("continued\n");
      }
    } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));

    /* remove temporary file. Could be done earlier, but we don't
       know when docker actually bind-mounted it.
       Should we have a signal handler to remove the file? When?
    */
    unlink(tfile);
    exit(EXIT_SUCCESS);
  } else {
    perror("ERROR: fork failed");
    exit(-errno);
  }
}


int main(int argc, char *argv[])
{
  int argp = 0;

  if (argc < 2 ||
      strncmp(argv[1], "-h", 3) == 0 ||
      strncmp(argv[1], "--help", 7) == 0)
    usage(argv[0]);

  uid_t uid = getuid();
  uid_t euid = geteuid();

#ifndef TEST
  if (euid != 0) {
    printf("*** This program must be run with suid root! ***\n\n");
    usage(argv[0]);
  }
#endif
  if (uid < 1000) {
    printf("*** This program cannot be called by system users with uid < 1000! ***\n\n");
    usage(argv[0]);
  }

  if (++argp >= argc)
    usage(argv[0]);

  if (strncmp(argv[argp], "-i", 3) == 0 ||
      strncmp(argv[argp], "--interactive", 14) == 0) {
    interactive = 1;
    if (++argp >= argc)
      usage(argv[0]);
  }
  //printf("here argp=%d argc=%d\n", argp, argc);
  strncpy(imgname, argv[argp], PATHLEN);
#ifdef TEST
  printf("uid=%d euid=%d imgname=%s\n", uid, euid, imgname);
#endif
  ++argp;

  if (mk_start_file(uid, argc - argp, &argv[argp]) != 0) {
    perror("failed to create tempfile");
    exit(1);
  }
#ifdef TEST
  else {
    printf("tmpfile = %s\n", tfile);
  }
#endif

  mk_docker_cmd();

#ifdef TEST
  for (int i = 0; cmd[i] != NULL; i++)
    printf(" %s", cmd[i]);
  printf("\n");
#else
  run_cmd();
#endif
  
  return 0;
}

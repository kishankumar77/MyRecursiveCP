/*************************************************************************

  plink -- hardlink a list of files quickly

  The target directory is specified with -d, and source file names are
  read from stdin.  Create hard links in the target directory at the
  same relative path.

  Does *not* do mkdir; the target directory structure is assumed to
  already exist, e.g. with 
      pfind -d . | (cd target; xargs -P10 mkdir -p)

  Algorithm: worklist 'work' holds the filenames read from stdin.  A
  thread gets a batch of files and hardlinks them.  The worklist is
  processed in the order that threads become available.

*************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <string>
#include <list>
using namespace std;

/* 128 is the number of outstanding RPCs allowed by the 2.6 Linux
   kernel (c.f. sunrpc.tcp_slot_table_entries).  Maybe we can get that
   many filer requests in flight; certainly no more.
*/
#define MAXPARALLEL 128
pthread_t thread_id[MAXPARALLEL];
pthread_cond_t work_cond;
pthread_mutex_t work_lock;

list<string> work;
int wip_cnt, rc, work_eof;
int opt_p, opt_n;
string opt_d;
char *progname;

/*
   Top level thread routine.  Read a batch of source files from the
   worklist and link them to the target directory.
*/
void *
walk(void *p)
{
    while (1) {
	/* Enter critical section and pop the first work item.  Exit
	   thread if and only if (1) the list is empty, and (2) no
	   workers are active (who might add to the list).
	*/
	pthread_mutex_lock (&work_lock);
	while (work.empty()) {
	    if (!wip_cnt && work_eof) {
		pthread_mutex_unlock (&work_lock);
		pthread_exit(NULL);
	    }
	    // drop the lock, wait to be signaled, get the lock again
	    pthread_cond_wait(&work_cond, &work_lock);
	}

	// get some work items.  link() is fast, so to amortize the
	// overhead of locking the worklist we get a bunch at a time.
	list<string> mywork;
	for (int i=0; i<opt_n && !work.empty(); i++) {
	    mywork.push_back(work.front());
	    work.pop_front();
	}
	// show work in progress, so other threads stick around
	wip_cnt++;  
	pthread_mutex_unlock (&work_lock);
	// end of critical section

	// process the items we got
	while (!mywork.empty()) {
	    // relative path from source tree (cwd)
	    string sfrom = mywork.front();
	    mywork.pop_front();

	    // form path to destination tree
	    string sto = opt_d+"/"+sfrom;
	    const char *from, *to;
	    from = sfrom.c_str();
	    to = sto.c_str();

	    // try to hard link
	    if (link(from,to)) {
		fprintf(stderr, "%s: %s: from %s to %s\n", progname,
			strerror(errno), from, to);
		rc=1;
	    }
	}

	// show we're looking for work
	pthread_mutex_lock (&work_lock);
	wip_cnt--;
	// all done?  everyone wake up and exit
	if (!wip_cnt && work.empty() && work_eof) {
	    pthread_cond_broadcast(&work_cond);
	}
	pthread_mutex_unlock (&work_lock);
    }
}

void
chomp(char *s)
{
    int l = strlen(s);
    if (l>0 && s[l-1]=='\n') {
	s[l-1] = '\0';
    }
}

void
usage()
{
    fprintf(stderr, "usage: %s -d dir\n", progname);
    exit(1);
}

int
main(int argc, char **argv)
{
    int c;

    progname = argv[0];
    opt_p = 15;   		// default parallelism
    opt_n = 50;			// num items to read from worklist
    while ((c = getopt(argc, argv, "d:v")) != EOF) {
	switch (c) {
	case 'd': opt_d = optarg; break;
	case 'p': {		// specify number of threads
	    int conv = sscanf(optarg, "%d", &opt_p);
	    if (conv != 1) {
		fprintf(stderr,"%s: -p option must be an integer\n", progname);
		exit(1);
	    }
	    if (opt_p > MAXPARALLEL) {
		opt_p = MAXPARALLEL;
	    }
	    if (opt_p < 1) {
		opt_p = 1;
	    }
	    break;
	}
	case 'n': {		// specify items to read from worklist
	    int conv = sscanf(optarg, "%d", &opt_n);
	    if (conv != 1) {
		fprintf(stderr,"%s: -n option must be an integer\n", progname);
		exit(1);
	    }
	    if (opt_n < 1) {
		opt_n = 1;
	    }
	    break;
	}
	default:
	    usage();
	}
    }
    if (argc - optind != 0) {
	usage();
    }

    // -d is required
    if (strlen(opt_d.c_str()) == 0) {
	usage();
    }

    // work_lock protects the worklist data; work_cond lets threads
    // sleep without spinning during times there is not enough work
    // for all threads
    pthread_mutex_init(&work_lock,NULL);
    pthread_cond_init(&work_cond,NULL);

    // init worklist.  input is assumed to come from a fast source, so
    // skip the extra coordination required to distinguish temporary
    // out-of-work from final.
    char buf[2000];
    while (fgets(buf, sizeof(buf), stdin)) {
	if (strlen(buf) == sizeof(buf)-1) {
	    fprintf(stderr, "input too long: %s\n", buf);
	    exit(1);
	}
	chomp(buf);
	work.push_back(string(buf));
    }
    work_eof = 1;

    // start threads
    int nthr = 0;
    int trc;
    for (int i=0; i<opt_p; i++) {
	trc = pthread_create(&thread_id[nthr], NULL, &walk, NULL);
	// if some threads can't be started, I guess we can
	// still hobble along.
	if (trc) {
	    fprintf(stderr, "%s: %s\n", progname, strerror(errno));
	}
	else {
	    nthr++;
	}
    }
    if (nthr==0) {
	fprintf(stderr, "%s: can't start any threads; exiting\n", progname);
	exit(1);
    }

    // wait for completion
    for (int i=0; i<nthr; i++) {
	trc = pthread_join(thread_id[i], NULL);
	if (trc) {
	    // Now we can't tell when the workers are done.  Punt.
	    fprintf(stderr, "%s: %s in pthread_join; exiting\n", progname, strerror(errno));
	    exit(1);
	}
    }

    return rc;
}


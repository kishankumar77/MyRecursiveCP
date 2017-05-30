/*************************************************************************

  pfind -- fast recursive directory lister

  Prints the equivalent of "find <path> ! -type d".  The -d option
  inverts the '!' and prints directories instead of files.  The -s
  option calls stat() on each file.

  The main feature is using multiple threads to raise throughput on
  filesystems where readdir and stat latency is nontrivial.  One use
  is to pipe the output to "xargs -P nn <cmd>" to quickly generate a
  work list for <cmd>, and then run parallel copies of <cmd>.  Another
  use is to load file metadata into caches (pfind -s) before a program
  like 'make' is run.

  Algorithm: worklist 'work' holds directories.  A thread gets the
  next directory, lists its contents, adding subdirectories to the
  worklist.

  The worklist is processed in the order that threads become
  available.  For the output to match the output of find we don't
  print anything during the directory traversal but just build a tree
  of nodes that corresponds to the directory tree.  The abstract tree
  is then walked and printed in the desired order.

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
#include <dirent.h>
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

/* A generic node for recording the shape of the directory tree.
 */
class Node {
public:
    list<Node*> l;
    string name;
    Node(string _name) { name = _name; dir_p = false; }
    bool dir_p;
};

/* Worklist has the name of a directory and the node struct
   that corresponds to the directory (to be filled in)
*/
typedef pair<string,Node *> workitem;
list<workitem> work;	// invariant: every elt of work is a dir

int opt_d, opt_v, opt_s, opt_p, opt_q, opt_0, opt_t;
int wip_cnt, stat_spins, stat_stats, rc;
char *progname;

/*
   Top level thread routine.  Read an item from the worklist, which
   gives us ownership of the item.  List the contents with readdir()
   and add them as children of the node.  If an item is a directory it
   needs to be pushed on the worklist along with its node for further
   elaboration.
*/
void *
walk(void *p)
{
    // workarea for this thread to do readdir_r
    dirent *dentry = (dirent*)calloc(1,sizeof(struct dirent)+MAXNAMLEN);
    assert(dentry);

    while (1) {
	/* Enter critical section and pop the first work item.  Exit
	   thread if and only if (1) the list is empty, and (2) no
	   workers are active (who might add to the list).
	*/
	pthread_mutex_lock (&work_lock);
	while (work.empty()) {
	    if (!wip_cnt) {
		pthread_mutex_unlock (&work_lock);
		pthread_exit(NULL);
	    }
	    stat_spins++;
	    // drop the lock, wait to be signaled, get the lock again
	    pthread_cond_wait(&work_cond, &work_lock);
	}
	// get a work item
	string dir_name = work.front().first;
	Node *dir_node = work.front().second;
	work.pop_front();
	// show work in progress, so other threads stick around
	wip_cnt++;  
	pthread_mutex_unlock (&work_lock);
	// end of critical section

	DIR *dir = opendir(dir_name.c_str());
	if (!dir) {
	    // suppress "permission denied" if -q was specified.
	    // these may be expected, 
	    // root-owned subdirectories.
	    if (!opt_q || errno != EACCES) {
		fprintf(stderr, "%s: %s:%s\n",
			progname, dir_name.c_str(), strerror(errno));
	    }
	}

	// read the directory contents
	while(dir) {
	    struct dirent *dirent;
	    int n = readdir_r(dir, dentry, &dirent);
	    if (n) {
		fprintf(stderr, "%s: %s reading directory %s\n",
			progname, strerror(errno), dir_name.c_str());
		rc=1;
		break;
	    }
	    else if (dirent==NULL) { // end of dir entries
		break;
	    }

	    // skip . and ..
	    if (!strcmp(dirent->d_name, ".")
		|| !strcmp(dirent->d_name, "..")) {
		continue;
	    }

	    // skip .snapshot unless requested with -t
	    if (!opt_t && !strcmp(dirent->d_name, ".snapshot")) {
		continue;
	    }

	    // append this name to the dir contents
	    Node *sub_node = new Node(dirent->d_name);
	    assert(sub_node);
	    dir_node->l.push_back(sub_node);

	    string sub_path = dir_name + "/"+ (dirent->d_name);

	    // readdir() doesn't always define the file type even on
	    // linux, and never on solaris; then you have to use
	    // a costly stat().  hmph.
	    // http://lists.debian.org/debian-glibc/2003/09/msg00251.html
	    bool type_known_p = false, dir_p = false, did_stat = false;
	    struct stat statbuf;
#ifdef _DIRENT_HAVE_D_TYPE
	    // d_type is a linux extension to dirent that gives you
	    // the file type for free, sometimes, without having to
	    // stat (maybe based on NFSv3 READDIRPLUS.)
	    if (dirent->d_type != DT_UNKNOWN) {
		type_known_p = true;
	    }
	    if (dirent->d_type == DT_DIR) {
		dir_p = true;
	    }
#endif
	    // stat if we don't know whether it's a dir, or if requested
	    // by -s to warm up the client cache.
	    if (! type_known_p || opt_s) {
	        stat_stats++;
		n = lstat(sub_path.c_str(), &statbuf);
		if (n==-1) {
		    fprintf(stderr, "%s: %s trying to stat %s\n", progname,
			    strerror(errno), sub_path.c_str());
		    rc=1;
		    continue;
		}
		if (S_ISDIR(statbuf.st_mode)) {
		    dir_p = true;
		}
            }

	    // new directories go on the work list
	    if (dir_p) {
		sub_node->dir_p = true;
		pthread_mutex_lock(&work_lock);
		work.push_back(workitem(sub_path,sub_node));
		// Wake up one other thread waiting for work, if any
		pthread_cond_signal(&work_cond);
		pthread_mutex_unlock(&work_lock);
	    }
	}

	if (dir) {
	    closedir(dir);
	}

	pthread_mutex_lock (&work_lock);
	wip_cnt--;
	// all done?  everyone wake up and exit
	if (!wip_cnt && work.empty()) {
	    pthread_cond_broadcast(&work_cond);
	}
	pthread_mutex_unlock (&work_lock);
    }
}

void
files_depth_first(Node *n, string path)
{
    path += n->name + "/";
    list<Node *>::iterator li;
    for(li=n->l.begin(); li != n->l.end(); li++) {
	// if file, print the path; if dir, recurse on it
	if ((*li)->dir_p) {
	    files_depth_first(*li, path);
	}
	else {
	    printf("%s%s%c", path.c_str(), (*li)->name.c_str(), opt_0);
	}
    }
}

void
dirs_depth_first(Node *n, string path)
{
    path += n->name;
    printf("%s%c", path.c_str(), opt_0);
    list<Node *>::iterator li;
    for(li=n->l.begin(); li != n->l.end(); li++) {
	// recurse on dirs
	if ((*li)->dir_p) {
	    dirs_depth_first(*li, path+"/");
	}
    }
}

void
usage()
{
    fprintf(stderr, "usage: %s [-d] [-f] [-p threads] [-q] [-s] [-t] [-v] [-0] dir\n",
	    progname);
    exit(1);
}

int
main(int argc, char **argv)
{
    int c;

    // options processing
    progname = argv[0];
    opt_p = 30;   		// default parallelism
    opt_0 = '\n';		// default line termination
    while ((c = getopt(argc, argv, "0dfp:qstv")) != EOF) {
	switch (c) {
	case 'd': opt_d = 1; break; // print directories (not files)
	case 'f': opt_d = 0; break; // print files (not directories)
	  // oops, there's no way to do both -d and -f currently.
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
	case 'q': opt_q = 1; break; // suppress EACCES warnings
	case 's': opt_s = 1; break; // force stat() on each file
	case 't': opt_t = 1; break; // descend into .snapshot
	case 'v': opt_v = 1; break; // print some debugging on stderr
	case '0': opt_0 = 0; break; // use null instead of newline
	default:
	    usage();
	}
    }
    if (argc - optind != 1) {	// need exactly one dir arg
	usage();
    }
    if (opt_v) {
	fprintf(stderr,"using %d threads\n", opt_p);
    }

    // work_lock protects the worklist data; work_cond lets threads
    // sleep without spinning during times there is not enough work
    // for all threads
    pthread_mutex_init(&work_lock,NULL);
    pthread_cond_init(&work_cond,NULL);

    // init worklist
    string root=argv[optind];
    Node *dirtree = new Node(root);
    assert(dirtree);
    work.push_back(workitem(root,dirtree));

    // start threads
    int nthr = 0;
    int trc;
    for (int ii=0; ii<opt_p; ii++) {
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

    // print either files or directories as requested
    if (opt_d) {
	dirs_depth_first(dirtree,"");
    }
    else {
	files_depth_first(dirtree,"");
    }

    if (opt_v) {
	fprintf(stderr, "%d spins, %d stats\n", stat_spins, stat_stats);
    }

    return rc;
}


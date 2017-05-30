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
#include <iostream>
using namespace std;

#define MAXPARALLEL 100
pthread_t thread_id[MAXPARALLEL];
pthread_cond_t work_cond;
pthread_mutex_t work_lock;
string root;
list<string> work, files;	// invariant: every elt of work is a dir
int wip_cnt;
int rc;

void *
walk(void *p)
{
    while (1) {
	string e;

	/* Pop the first item from work list.  If the list is empty
	   but someone else is processing, don't exit yet; this may be
	   a dry spell.  Unlocking a mutex causes the scheduler to
	   run, so spinning in this way shouldn't block work from
	   happening, although it will chew up cycles.
	*/
	pthread_mutex_lock (&work_lock);
	while (work.empty()) {
	    if (!wip_cnt) {
		pthread_mutex_unlock (&work_lock);
		pthread_exit(NULL);
	    }
	    // drop the lock, wait to be signaled, get the lock again
	    pthread_cond_wait(&work_cond, &work_lock);
	}
	// get a work item
	e = work.front();
	work.pop_front();
	wip_cnt++;  // show work in progress, so other threads stick around
	pthread_mutex_unlock (&work_lock);
	
	DIR *dir;
	struct dirent *dirent, entry;
	string fpath;

	dir = opendir(e.c_str());
	if (!dir) {
	    fprintf(stderr, "opendir %s:%s\n", e.c_str(), strerror(errno));
	    rc=1;
	    goto bomb;
	}

	while(1) {
	    int n = readdir_r(dir,&entry,&dirent);
	    if (n != 0) {
		fprintf(stderr, "readdir:%s\n", strerror(errno));
		rc=1;
		break;
	    }
	    else if (dirent==NULL) // end of dir entries
		break;

	    if (!strcmp(dirent->d_name, ".")
		|| !strcmp(dirent->d_name, ".."))
		continue;

	    fpath = e+"/"+ (dirent->d_name);

	    // readdir() doesn't always define the file type, and you
	    // have to use stat().  costly.  if there's a better way,
	    // use it.
	    // http://lists.debian.org/debian-glibc/2003/09/msg00251.html
	    if (dirent->d_type == DT_UNKNOWN) {
		struct stat statbuf;
		n = lstat(fpath.c_str(), &statbuf);
		if (n == -1) {
		    fprintf(stderr, "stat %s:%s\n", fpath.c_str(), strerror(errno));
		    // treat this un-stat-able thing as a file
		}
		else if (S_ISDIR(statbuf.st_mode)) {
		    dirent->d_type = DT_DIR;
		}
		// treat anything else as a file
	    }

	    if (dirent->d_type == DT_DIR) {
		pthread_mutex_lock(&work_lock);
		work.push_back(fpath);
		// Wake up one other thread waiting for work, if any
		pthread_cond_signal(&work_cond);
		pthread_mutex_unlock(&work_lock);
	    }
	    else {
		int rc = unlink(fpath.c_str());
		if (rc) {
		    rc=1;
		    fprintf(stderr, "unlink %s: %s\n",
			    fpath.c_str(),
			    strerror(errno));
		}
	    }
	}
	closedir(dir);

    bomb:
	pthread_mutex_lock (&work_lock);
	wip_cnt--;
	// might be done for good, everyone wake up and check
	if (!wip_cnt && work.empty())
	    pthread_cond_broadcast(&work_cond);
	pthread_mutex_unlock (&work_lock);
    }
}

void
deletetree(string e)
{
	DIR *dir;
	struct dirent *dirent;
	string fpath;

	dir = opendir(e.c_str());
	if (!dir) {
	    fprintf(stderr, "opendir %s: %s\n", e.c_str(), strerror(errno));
	    return;
	}

	while(dirent = readdir(dir)) {
	    if (!strcmp(dirent->d_name, ".")
		|| !strcmp(dirent->d_name, ".."))
		continue;

	    fpath = e+"/"+ (dirent->d_name);
	    if (dirent->d_type == DT_DIR) {
		deletetree(fpath);
	    }
	}
	closedir(dir);
	int rc = rmdir(e.c_str());
	if (rc) {
	    fprintf(stderr, "rmdir %s: %s\n", e.c_str(), strerror(errno));
	}
}

int
main(int argc, char **argv)
{
    int i;

    // be paranoid, this command can cause a lot of destruction...quickly
    if (argc<3 || strcmp(argv[1],"-rfp")!=0) {
	fprintf(stderr, "usage: %s -rfp dir\n", argv[0]);
	exit(1);
    }

    root=argv[2];
    work.push_back(root);
    
    pthread_mutex_init(&work_lock,NULL);
    pthread_cond_init(&work_cond,NULL);

    for (i=0; i<MAXPARALLEL; i++) {
	pthread_create(&thread_id[i], NULL, &walk, NULL);
    }
    for (i=0; i<MAXPARALLEL; i++) {
	pthread_join(thread_id[i], NULL);
    }

    list<string>::iterator si;
    for (si=files.begin(); si!=files.end(); si++) {
	printf("%s\n", si->c_str());
    }

    if (rc==0)
	deletetree(root);

    return rc;
}


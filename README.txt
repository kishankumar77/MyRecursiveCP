Description:
    This script will create hard links to source dirs/files at destination location

	srcdir - source directory, mandatory
	destndir - destination directory, mandatory and it should be non-existing
	threads - number of threads to be spawn, if not defined default value is set

Syntax:
   RecursCP.pl --srcdir="/u/testdir/mydir" --destndir="/u/testdir/mydir2"
   RecursCP.pl --srcdir="/tmp/d" --destndir="/tmp/b" --threads="40"
   

Default value of threads is 40, it is recommended that threads value option to  be set to maximum cores count, this will create  more parallelization.

Example  to get cores details  in RHEL machine :
-bash-4.2$ lscpu  |grep -e '^CPU(s)'
CPU(s):                16


Testing :
Testing performed on RHEL 6.4 (Santiago ), containing 40 cores 
perl version - 5.8.8
script will not copy broken symlinks or links to dirs, to do that please use RecursCP_Retry.pl 


Instructions to download and execute the tool on Linux  RHEL machine
--------------------------------------------------------------------
Download the zip file 'MyRecursCP.zip'
Extract the zip file 'tar  -xvf MyRecursCP.zip'
cd MyRecursCP
vi RecursCP.pl and change the line "my $pfind = '/u/build/kishank/MyRecursCp/pfind';" to  local pfind tool path.
	for example if the download path is /u/userA/MyRecursCP/ then the pfind tool path will be "/u/userA/MyRecursCP/pfind"
	ie, my $pfind = '/u/build/kishank/MyRecursCp/pfind';
		modify to my $pfind = '/u/userA/MyRecursCP/pfind';

Execute: 	
change the  srcdir and destndir to your local paths  for the below command and execute
srcdir - path of source dir for which you need hard links 
destndir - path of destination dir for which the link will be created
./RecursCP.pl --srcdir="/u/testdir/mydir" --destndir="/u/testdir/mydir2"


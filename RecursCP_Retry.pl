#!/usr/software/bin/perl5.8.8

use strict;
use warnings;

use File::Temp;
use Getopt::Long;
use File::Basename;

my $pfind = '/u/build/kishank/MyRecursCp/pfind';
my $tempdirs= File::Temp::tempnam( "/tmp", "dir" );
my $tempFiles= File::Temp::tempnam( "/tmp", "files" );


my ( $sourcedir, $destndir, $help );

my $threads = 40;

if (
        !&GetOptions(
            "help|h"            => \$help,
            "srcdir=s"       => \$sourcedir,
            "destndir=s"        => \$destndir,
			"threads=i" 		=> \$threads
            )   
   ) { 
        &Usage;
}   
if ( $help ) {
        &Usage;
}

if( !$sourcedir || !$destndir ) {
	#print "sourcedir and destndir needs to provided\n";
	&Usage;
}

if( -d $destndir ) {
	print "Input  non existing destination directory\n";
	&Usage;
}
	#chop( $destndir ) if( $destndir =~/\/$/ );
	#chop( $sourcedir ) if( $sourcedir =~/\/$/ );
my $starttime = time();
print "Start time: $starttime\n";
print "Source directory - $sourcedir\nDestination Directory - $destndir\nThreads - $threads\n";

&generateSubDirs();
&makeDestnDirs();
&generateFileList();
&CreateHardLinks();


my $endtime = time();
print "End time: $endtime\n";

my $totaltime = $endtime - $starttime ;
print "Duration in Seconds: <$totaltime>\n";


sub ExecCommand {
	
    my ( $cmd ) = @_;
	my $sstime = time();
    #chomp $cmd;
    print("Running Command:<$cmd>\n");
	print "Command start time: $sstime\n";
    my $output = `$cmd`;
    my $rc = $?;
	if ( $rc ) {
		print "Command Failed - $cmd\nERROR STATUS - $rc\n"; 
		print "ERROR - $output\n" if ( $output );
	} else {
			print("Command Success - $cmd\n" );
			print("Command Output - $output\n" ) if ( $output );
	}
	my $sendtime = time();
	my $difftime = $sendtime - $sstime;
	print "command execution end time: <$sendtime>\n";
	print "Time taken for command Execution in Seconds : <$difftime>\n\n";
	return ( $output, $rc );
}

sub CreateHardLinks {
	my $create_links_cmd = "/bin/cat $tempFiles | /usr/bin/tr '\\n' '\\0' | /usr/software/bin/xargs -I\{\} -0 -P $threads cp -l $sourcedir\{\} $destndir\{\} 2>&1";
	my( $cmd_out, $exit_status ) = &ExecCommand( $create_links_cmd );
	if( $exit_status ) {
		if( $cmd_out =~ /(omitting directory)|(cannot stat)/ ) {
			print "Trying creating links for broken symlinks and links to dirs\n";
			my @linkstodir = ( $cmd_out =~ /omitting directory \`(.*)'/g);
		 	my @brokenlinks =( $cmd_out =~ m|cannot stat \`(.*)'|g );
			my @brokenlinks1 = grep( !/(cannot stat)/, @brokenlinks );		
			my @couldparse = grep( /(cannot stat)/, @brokenlinks );
			my @parsedlinks;
			foreach my $line ( @couldparse ){
				my @temp = split(/\s/, $line);
				foreach my $newline ( @temp ){
					if ( $newline =~ /\`(.*)\'/)
					{
						my $tval = $1;
						push( @parsedlinks, $tval );
					}
				}
			}
			foreach my $path ( @linkstodir, @brokenlinks1 ) {
				my $readlink = readlink( $path );
				my $newpath;  
				$newpath = $path;
				$newpath =~ s|$sourcedir|$destndir|g;
				my $newdir = dirname ($newpath);
				my $newfile = basename($newpath);
				#print "path: <$path>,link: <$readlink>, newpath: $newpath , newdir: <$newdir>, newfile: <$newfile>\n";
				system( "cd $newdir && ln -s $readlink $newfile" );
			} 
		}
    } else {
        #print "Links created successfully at destination directory $destndir'\n";
		print " Unlinking $tempdirs and $tempFiles\n";
		#unlink $tempdirs;
		#unlink $tempFiles;
		print "Unlinking of $tempdirs and $tempFiles is done \n";
		print " Destination is created: $destndir \n";
    }
}

sub generateFileList {
    chdir($sourcedir);
    my $command_findfiles= "$pfind -f -p $threads . > $tempFiles";
	my( $cmd_out, $exit_status ) = &ExecCommand( $command_findfiles );

    if( $exit_status ) {
		exit;
    } else {
       # print "file list is generated successfully, $tempFiles\n";
       print "substituting ^. in $tempFiles file \n";
		`sed -i 's/^\.//g' $tempFiles`;
		#`sed -i '\/^$\/d' $tempFiles`;
    }
}

sub makeDestnDirs {
	print "Creating subdirectories at destination location\n";
	#my $create_subdirs_cmd = "/bin/cat $tempdirs| /usr/bin/tr '\\n' '\\0' | /usr/software/bin/xargs -I\{\} -0 -P $threads $mymkdir $destndir  \{\}";
	system( "mkdir -p $destndir" );
	my $create_subdirs_cmd = "/bin/cat $tempdirs| /usr/bin/tr '\\n' '\\0' | /usr/software/bin/xargs -I\{\} -0 -P $threads mkdir -p \{\}";
	my( $cmd_out, $exit_status ) = &ExecCommand( $create_subdirs_cmd );
	
	exit if( $exit_status ) ;
	
}

sub generateSubDirs {
	chdir($sourcedir);
	my $command_finddirs = "$pfind -d -p $threads . > $tempdirs";
	my( $cmd_out, $exit_status ) = &ExecCommand( $command_finddirs );

	if( $exit_status ) {
		#print "failed to get subdirectories list";
		exit;
	} else {
		#print "subdirectories list is generated , '$tempdirs'\n";
		my $cmd1 = "sed -i '/^\.\$/d' $tempdirs";
		my( $cmd_out1, $exit_status1 ) = &ExecCommand( $cmd1 );
		my $cmd2 = "sed -i 's|^\\./|$destndir/|g' $tempdirs";
		my( $cmd_out2, $exit_status2 ) = &ExecCommand( $cmd2 );
		exit if( $exit_status2 );
	}
}


sub Usage {
#########################################################################
    # This just prints script usage via a "here" document, and exits.
#########################################################################

    print <<EOM;

Description:
    This script will create links to source dirs/files at destination location

	srcdir - source directory, mandatory
	destndir - destination directory, mandatory and it should be non-existing
	threads - number of threads to be spawn, if not defined default value is set

Syntax:
   RecursCP.pl --srcdir="/u/testdir/mydir" --destndir="/u/testdir/mydir2"
   RecursCP.pl --srcdir="/tmp/d" --destndir="/tmp/b" --threads="40"


EOM
    exit(0);
}    #Usage





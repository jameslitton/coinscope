#!/usr/bin/perl

use strict;
use warnings;

use File::Path qw(make_path);
use File::Copy; 
use Data::Dumper;

if ($> != 0) {
	print "Installer must be ran as root euid\n";
	exit(1);
}

my ($login, $pass, $uid, $gid);
unless (($login, $pass, $uid, $gid) = getpwnam("coinscope")) {
	print "Creating coinscope user and group. You should add yourself to the group\n";
	if (system('/usr/sbin/useradd', '-M', '-s', '/bin/false', 'coinscope') == 0) {
		($login, $pass, $uid, $gid) = getpwnam("coinscope");
	} else {
		die "useradd failed: $?";
	}
}

sub install {
	print "Copying $_[0] to $_[1]\n";
	copy($_[0], $_[1]) or die "Copy of $_[0] to $_[1] failed: $!\n";
	if ($#_ > 1) {
		chmod($_[2], $_[1]);
	}
	chown($uid, $gid, $_[1]);
}

my @paths = qw(/etc/coinscope /opt/coinscope);
@paths = (@paths, map { '/opt/coinscope/' . $_ } qw(connector logserver logclient clients tools));
make_path(@paths, {verbose => 1, mode => 0755});

foreach my $p (@paths) {
	chown($uid, $gid, $p);
}

my %tocopy = (
           'connector' => [ [ qw(groundctrl groundctrl)], [ qw(main connector) ]],
           'logserver' => [ [ qw(main logserver) ]],
           'logclient' => [ [ qw(verbatim verbatim)], [ qw(console console) ]],
           'clients' => [ [ qw(getaddr getaddr)], [ qw(get_nodes get_nodes) ]],
           'tools' => [ [ qw(logchecker logchecker)], [ qw(logtruncate logtruncate) ]]
             );
while(my ($directory, $files) = each %tocopy) {
	my $from = "./${directory}/";
	my $to = "/opt/coinscope/${directory}/";
	foreach my $f (@$files) {
		install($from . $f->[0], $to . $f->[1], 0755);
	}
}

install('netmine.cfg', '/etc/coinscope/netmine.template.cfg', 0664);


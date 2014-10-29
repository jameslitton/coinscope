#!/usr/bin/env perl

use strict;
use warnings;

use DBI qw(:sql_types);
use DBD::Pg qw(:pg_types);

use Test::utf8;
use Term::ProgressBar;
use Data::Dumper;
use Set::Scalar;
use Memoize;
use DateTime;
use POSIX ":sys_wait_h";


my %g_pids;
my $g_dbh;




my @g_pass_data; # any global data needed for passes, which can be zeroed out as you move along



sub inet_ntoa {
	return sprintf("%d.%d.%d.%d", $_[0] & 0xff, ($_[0] >> 8) & 0xff, ($_[0] >> 16) & 0xff, ($_[0] >> 24) & 0xff);
}


sub iso8601 {
	my $date = DateTime->from_epoch(epoch => $_[0], time_zone => 'UTC');
	return $date->ymd().' '.$date->hms().'z';
}


sub prolog {
	my ($source_id, $type_id, $uts) = @_;
	if ($type_id != 2 && $type_id != 4 && $type_id != 8 && $type_id != 16 && $type_id != 32 && $type_id != 64) {
		print "Corrupt log? Got $type_id of $type_id\n";
		my $timestamp = iso8601($uts);
		print "$source_id\t$type_id\t$timestamp\n";
	}
}


sub get_text_id {
	my $text = shift;
	$text =~ s/\x00*$//;			 # Log file includes terminating null...
	return 0;
}


sub as_text {
	my ($source_id, $type, $timestamp, $rest) = @_;
	if (length($rest)) {
		get_text_id($rest);
	}
}


sub bitcoin_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $addr_pack = "vnLa[8]";
	my ($handle_id, $update_type, 
	    $remote_family, $remote_port, $remote_addr, $rzeros,
	    $local_family, $local_port, $local_addr, $lzeros,
	    $text_len, $text) = unpack("NN${addr_pack}${addr_pack}NZ*", $rest);

	if ($remote_family != 2) {
		print "Corrupt log? Got remote family of $remote_family\n";
	}

	if ($local_family != 2) {
		print "Corrupt log? Got local family of $local_family\n";
	}

	if (length($text)) {
		my $text_id = get_text_id($text);
	}

}



sub get_command_id {
	my $str = $_[0];
	is_within_ascii($str);
	print "$str\n";
	return 0;
	Encode::_utf8_on($str);
	unless (is_valid_string($str)) {
		print "Bad command, got ".$_[0]."\n";
	}
	return 0;
}

memoize('get_command_id');


sub bitcoin_msg_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my ($handle_id, $is_sender,
	    $magic, $command, $length,
	    $checksum, $payload) = unpack("NCVZ[12]VVa*", $rest);
	my $command_id = get_command_id($command);
}

sub pass1_bitcoin_msg_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my ($handle_id, $is_sender,
	    $magic, $command, $length,
	    $checksum, $payload) = unpack("NCVZ[12]VVa*", $rest);
	my $command_id = get_command_id($command);
	$g_pass_data[1]{bid_rows}++;
}


sub pass2_bitcoin_msg_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $mid = pass2_prolog($source_id, $type, $timestamp);
	my ($handle_id, $is_sender,
	    $magic, $command, $length,
	    $checksum, $payload) = unpack("NCVZ[12]VVH*", $rest);

	my $command_id = get_command_id($command);
	my $bid = $g_pass_data[2]{next_bid}++;
	print { $g_pass_data[2]{bitcoin_messages} } "$bid\t$mid\t$handle_id\t$is_sender\t$command_id\n";

	if (length($payload)) {
		print { $g_pass_data[2]{bitcoin_message_payloads} } "$bid\t\\\\x$payload\n";
	}
}



sub handle_message {
	my ($source_id, $type, $timestamp, $rest) = unpack("NCQ>a*", $_[0]);
	my $handlers = $_[1];
	if (!defined $handlers->{$type}) {
		print { my_err() } "Unhandled type : type\n";
	} else {
		$handlers->{$type}->($source_id, $type, $timestamp, $rest);
	}
}

BEGIN {
	my $dbh = 0;
	sub get_handle { # init db (if necessary and return a database
		# handle). Try to not be a total knob and use db
		# specific things in using it, because it probably
		# won't always be sqlite

		unless ($dbh) {
			$dbh = DBI->connect("dbi:Pg:dbname=connector", "litton", "",
			                       { RaiseError => 1,
			                         AutoCommit => 0})
			  or die $DBI::errstr;
		}

		return $dbh;
	}
}

sub do_pass {
	(my $IN, my $pass, my $handler) = @_;
	binmode($IN) || die "Cannot binmode IN";

	my $filesize = (stat($IN))[7];
	my $progress = 0;
	$progress = Term::ProgressBar->new({count => $filesize, ETA => 'linear', name => $pass});
	$progress->max_update_rate(1);
	my $next_update = 0;

	my $reading_len = 1;
	my $to_read = 4;
	my $has_read = 0;
	my $cur;
	my $total_read = 0;
	while (1) {
		my $rv = read($IN, $cur, $to_read, $has_read);
		if ($rv > 0) {
			$to_read -= $rv;
			$has_read += $rv;
			$total_read += $rv;
		} elsif ($rv == 0) {
			last;
		} elsif (! defined $rv) {
			print { my_err() } "Received error on read: $!\n";
			last;
		}

		if ($to_read == 0) {
			if ($reading_len) {
				$to_read = unpack("N", $cur);
				$reading_len = 0;
			} else {
				handle_message($cur, $handler);
				$reading_len = 1;
				$to_read = 4;
				if ($progress && $total_read > $next_update) {
					$next_update = $progress->update($total_read);
					#$g_dbh->commit;
				}
			}
			$has_read = 0;
			$cur = "";
		}
	}
	$progress->update($total_read) if $progress;
}

my %handlers = (
                      2 => \&as_text,
                      4 => \&as_text,
                      8 => \&as_text,
                      16 => \&bitcoin_handler,
                      32 => \&bitcoin_msg_handler,
                      64 => \&as_text,
                     );


sub child_main {
	my $filename = $_[0];
	open(my $IN, '<', $filename) or die "Could not open file";
	do_pass($IN, "verification pass on $filename", \%handlers);
	close($IN);
}

foreach my $filename(@ARGV) {
	child_main($filename);
}

if (0) {

foreach my $filename (@ARGV) {
	my $pid = fork();
	if ($pid > 0) {
		print "Made child $pid\n";
		$g_pids{$pid} = $filename;
	} elsif ($pid == 0) {
		child_main($filename);
		exit(0);
	} else {
		die "Bad pid $pid: $!\n";
	}
}

while(scalar(keys %g_pids)) {
	my $cpid = wait;
	if ($cpid > 0) {
		delete $g_pids{$cpid};
		if ($? != 0) {
			print STDERR "Wait gave non-zero status for $cpid: $?\n";
		}
	} elsif ($cpid == -1) {
		print STDERR "No children to wait on\n";
	}
}

}


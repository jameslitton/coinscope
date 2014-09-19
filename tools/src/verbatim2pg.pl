#!/usr/bin/env perl

use strict;
use warnings;

use DBI qw(:sql_types);
use DBD::Pg qw(:pg_types);
use Term::ProgressBar;
use Data::Dumper;
use Memoize;

# This reads a verbatim file and writes it to a database.

# This is just a modification of the previous one for postgres
# insertion. Presumably the sqlite will get dropped.

# Also, this does NOT try to prevent duplicates. If you run this on
# the same verbatim record twice, you'll get duplicates. This is
# harder to solve than it sounds, since duplicate records in a log are
# not necessarily incorrect. Probably just adapt it to only insert
# stuff from a range of timestamps

my $g_dbh;

sub inet_ntoa {
	return sprintf("%d.%d.%d.%d", $_[0] & 0xff, ($_[0] >> 8) & 0xff, ($_[0] >> 16) & 0xff, ($_[0] >> 24) & 0xff);
}


sub insert_prolog {
	my $sth = $g_dbh->prepare_cached('insert into messages (source_id, type_id, timestamp) values (?, ?, to_timestamp(?))') or die $g_dbh->errstr;
	$sth->execute(@_);
	return $g_dbh->last_insert_id(undef, undef, undef, undef, {sequence=>'messages_id_seq'});
}

sub get_text_id {
	my $text_sth = $g_dbh->prepare_cached(q{
insert into text_strings (txt) values (?)});
	$text_sth->execute($_[0]);
	my $text_ret = $g_dbh->prepare_cached(q{
select id from text_strings where txt = ?
});
	$text_ret->execute($_[0]);
	my $rv = $text_ret->fetch()->[0];
	$text_ret->finish;
	return $rv;
}

memoize('get_text_id');

sub as_text {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $mid = insert_prolog($source_id, $type, $timestamp);
	if (length($rest)) {
		my $text_id = get_text_id($rest);
		my $sth = $g_dbh->prepare_cached('insert into text_messages (message_id, text_id) values (?,?)');
		$sth->execute($mid, $text_id);
	}
}

sub get_addr_id {
	my $addr_sth = $g_dbh->prepare_cached(q{
INSERT into addresses (family, address, port) values (?,?,?)});
	$addr_sth->bind_param(1, $_[0]);
	$addr_sth->bind_param(2, $_[1], { pg_type => PG_INET });
	$addr_sth->bind_param(3, $_[2]);
	$addr_sth->execute;

	my $getaddr_sth = $g_dbh->prepare_cached(q{
select id from addresses where family = ? and address = ? and port = ?});
	$getaddr_sth->bind_param(1, $_[0]);
	$getaddr_sth->bind_param(2, $_[1], { pg_type => PG_INET });
	$getaddr_sth->bind_param(3, $_[2]);
	$getaddr_sth->execute;

	my $rv =  $getaddr_sth->fetch()->[0];
	$getaddr_sth->finish;
	return $rv;
}

memoize('get_addr_id');

sub bitcoin_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $mid = insert_prolog($source_id, $type, $timestamp);
	my $addr_pack = "vnLa[8]";
	my ($handle_id, $update_type, 
	    $remote_family, $remote_port, $remote_addr, $rzeros,
	    $local_family, $local_port, $local_addr, $lzeros,
	    $text_len, $text) = unpack("NN${addr_pack}${addr_pack}NZ*", $rest);

	my $remote_id = get_addr_id($remote_family, inet_ntoa($remote_addr), $remote_port);
	my $local_id = get_addr_id($local_family, inet_ntoa($local_addr), $local_port);

	my $sth = $g_dbh->prepare_cached(q{
insert into bitcoin_cxn_messages 
(message_id, cxn_type_id, hid, remote_id, local_id)
values
(?,?,?,?,?)
});
	$sth->execute($mid, $update_type, get_hid($source_id, $handle_id), $remote_id, $local_id);
	if (length($text)) {
		my $b_id = $g_dbh->last_insert_id(undef, undef, undef, undef, {sequence=>'bitcoin_cxn_messages_id_seq'});
		my $text_id = get_text_id($text);
		my $sth = $g_dbh->prepare_cached(q{
insert into cxn_text_map (bitcoin_cxn_msg_id, txt_id) values (?,?)
});
		$sth->execute($b_id, $text_id);
	}
}


sub get_command_id {
	my $cmd_sth = $g_dbh->prepare_cached(q{
INSERT into commands (command) values (?)});
	$cmd_sth->execute($_[0]);

	my $getcmd_sth = $g_dbh->prepare_cached(q{
select id from commands where command = ?});
	$getcmd_sth->execute($_[0]);

	my $rv = $getcmd_sth->fetch()->[0];
	$getcmd_sth->finish;
	return $rv;
}

memoize('get_command_id');

sub get_hid {
	my $hid_sth = $g_dbh->prepare_cached(q{
INSERT into unique_hid (source_id, handle_id) values (?, ?)});
	$hid_sth->execute($_[0], $_[1]);

	my $gethid_sth = $g_dbh->prepare_cached(q{
select hid from unique_hid where source_id = ? and handle_id = ?});
	$gethid_sth->execute($_[0], $_[1]);

	my $rv = $gethid_sth->fetch()->[0];
	$gethid_sth->finish;
	return $rv;
}

memoize('get_hid');

sub bitcoin_msg_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $mid = insert_prolog($source_id, $type, $timestamp);
	my ($handle_id, $is_sender,
	    $magic, $command, $length,
	    $checksum, $payload) = unpack("NCVZ[12]VVa*", $rest);

	my $command_id = get_command_id($command);
	my $sth = $g_dbh->prepare_cached(q{
insert into bitcoin_messages (message_id, hid, is_sender, command_id)
values
(?, ?, ?, ?)});
	$sth->execute($mid, get_hid($source_id, $handle_id), $is_sender, $command_id);
	if (length($payload)) {
		my $bt_mid = $g_dbh->last_insert_id(undef, undef, undef, undef, {sequence=>'bitcoin_messages_id_seq'});
		my $sth = $g_dbh->prepare_cached('insert into bitcoin_message_payloads (bitcoin_msg_id, payload) values (?,?)');
		$sth->bind_param(1, $bt_mid);
		$sth->bind_param(2, $payload, {pg_type => PG_BYTEA});
		$sth->execute;
	}
}

my %handlers = (
                2 => \&as_text,
                4 => \&as_text,
                8 => \&as_text,
                16 => \&bitcoin_handler,
                32 => \&bitcoin_msg_handler,
);


sub handle_message {
	my ($source_id, $type, $timestamp, $rest) = unpack("NCQ>a*", $_[0]);
	# my ($type, $timestamp, $rest) = unpack("CQ>a*", $_[0]);
	# my $source_id = PICK_A_SOURCE_FOR_CONNECTOR;
	if (!defined $handlers{$type}) {
		print STDERR "Unhandled type : type\n";
	} else {
		$handlers{$type}->($source_id, $type, $timestamp, $rest);
	}
}

sub get_handle { # init db (if necessary and return a database
                 # handle). Try to not be a total knob and use db
                 # specific things in using it, because it probably
                 # won't always be sqlite


	my $dbh = DBI->connect("dbi:Pg:dbname=connector", "litton", "",
	                       { RaiseError => 1,
	                         AutoCommit => 0})
	  or die $DBI::errstr;

	return $dbh;
};

sub main {
	my $IN = shift;
	binmode($IN) || die "Cannot binmode IN";

	my $filesize = (stat($IN))[7];
	my $progress = Term::ProgressBar->new({count => $filesize, ETA => 'linear', name => 'data insertion'});
	$progress->max_update_rate(2);
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
			print STDERR "Done reading file\n";
			last;
		} elsif (! defined $rv) {
			print STDERR "Received error on read: $!\n";
			last;
		}

		if ($to_read == 0) {
			if ($reading_len) {
				$to_read = unpack("N", $cur);
				$reading_len = 0;
			} else {
				handle_message($cur);
				$reading_len = 1;
				$to_read = 4;
				if ($total_read > $next_update) {
					$next_update = $progress->update($total_read);
					$g_dbh->commit;
				}
			}
			$has_read = 0;
			$cur = "";
		}
	}
	$progress->update($total_read);
}

die "Please supply a filename" if ($#ARGV == -1);
my $filename = $ARGV[0];
open(my $IN, '<', $filename) or die "Could not open file";

$g_dbh = get_handle();
main($IN);

close($IN);

$g_dbh->commit;




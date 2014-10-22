#!/usr/bin/env perl

use strict;
use warnings;

use DBI qw(:sql_types);
use DBD::Pg qw(:pg_types);
use Fcntl qw(:seek);
use Term::ProgressBar;
use Data::Dumper;
use Set::Scalar;
use Memoize;
use DateTime;

# This reads a verbatim file and writes it to a database.

my $g_dbh;
my $g_postfix = '';

my @g_pass_data; # any global data needed for passes, which can be zeroed out as you move along

foreach (qw/messages bitcoin_cxn_messages cxn_text_map bitcoin_messages bitcoin_message_payloads text_messages/) {
	open(my $fp, '+>', "$_.$$.copy") or die "Could not open file: $!\n";
	$g_pass_data[2]{$_} = $fp;
}


sub inet_ntoa {
	return sprintf("%d.%d.%d.%d", $_[0] & 0xff, ($_[0] >> 8) & 0xff, ($_[0] >> 16) & 0xff, ($_[0] >> 24) & 0xff);
}


sub iso8601 {
	my $date = DateTime->from_epoch(epoch => $_[0], time_zone => 'UTC');
	return $date->ymd().' '.$date->hms().'z';
}


sub pass2_prolog {
	my ($source_id, $type_id, $uts) = @_;
	my $mid = $g_pass_data[2]{next_mid}++;
	my $timestamp = iso8601($uts);
	print { $g_pass_data[2]{messages} } "$mid\t$source_id\t$type_id\t$timestamp\n";
	return $mid;
}

my %g_text_id;
sub prepopulate_text_id {
	my $sth = $g_dbh->prepare(q{select id, txt from text_strings});
	$sth->execute;
	my $count = 0;
	while (my $aref = $sth->fetchrow_arrayref) {
		$g_text_id{$aref->[1]} = $aref->[0];
		++$count;
	}
	$sth->finish;
}

sub get_text_id {
	my $text = shift;
	$text =~ s/\x00*$//; # Log file includes terminating null...
	unless (defined $g_text_id{$text}) {

		my $text_sth = $g_dbh->prepare_cached(q{
insert into text_strings (txt) values (?)});
		$text_sth->execute($text);
		my $text_ret = $g_dbh->prepare_cached(q{
select id from text_strings where txt = ?
});
		$text_ret->execute($text);
		my $rv = $text_ret->fetch()->[0];
		$text_ret->finish;
		$g_text_id{$text} = $rv;
	}
	return $g_text_id{$text}
}



sub void_handler { }

sub pass0_get_text_id {
	$g_pass_data[0]{text_messages}++;
}

sub copy_escape {
	my $val = shift;
	$val =~ s/(\\|\n|\r|\t)/\\$1/g;
	$val;
}

sub pass0_as_text {
	my ($source_id, $type, $timestamp, $rest) = @_;
	if (length($rest)) {
		pass0_get_text_id($rest);
	}
	$g_pass_data[0]{rows}++;
}

sub pass1_get_text_id {
	my $original_text = shift;
	$original_text =~ s/\x00*$//; # Log file includes terminating null...
	unless (defined $g_text_id{$original_text}) {
		my $id = $g_pass_data[1]{text_next}++;
		my $text = copy_escape($original_text);
		push(@{ $g_pass_data[1]{text_copy} }, "$id\t$text\n");
		$g_text_id{$original_text} = $id;
	}
	$g_text_id{$original_text};
}

sub pass1_as_text {
	my ($source_id, $type, $timestamp, $rest) = @_;
	if (length($rest)) {
		pass1_get_text_id($rest);
	}
}

sub pass2_as_text {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $mid = pass2_prolog($source_id, $type, $timestamp);
	if (length($rest)) {
		my $text_id = get_text_id($rest);
		print { $g_pass_data[2]{text_messages} } "$mid\t$text_id\n";
	}
}

sub fetch_all_addr_id {
	my $rv = Set::Scalar->new;
	my $sth = $g_dbh->prepare(q{select id from addresses});
	$sth->execute;
	while ((my $aref = $sth->fetch)) {
		$rv->insert($aref->[0]);
	}
	$sth->finish;
	return $rv;
}

sub get_addr_key {
	my $family = int(shift);
	my $address = int(shift); # 32 bit number
	my $port = int(shift);
	my $id = (($family & 0xffff) << 48) | (($address & 0xffffffff) << 16) | ($port & 0xffff);
	return $id;
}

sub pass0_bitcoin_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $addr_pack = "vnLa[8]";
	my ($handle_id, $update_type, 
	    $remote_family, $remote_port, $remote_addr, $rzeros,
	    $local_family, $local_port, $local_addr, $lzeros,
	    $text_len, $text) = unpack("NN${addr_pack}${addr_pack}NZ*", $rest);

	my $remote_id = get_addr_key($remote_family, $remote_addr, $remote_port);
	unless ($g_pass_data[0]{address_set}->contains($remote_id)) {
		my $addr = inet_ntoa($remote_addr);
		push($g_pass_data[0]{address_copy}, "$remote_id\t$remote_family\t$addr\t$remote_port\n");
	}
	$g_pass_data[0]{address_set}->insert($remote_id);

	my $local_id = get_addr_key($local_family, $local_addr, $local_port);
	unless ($g_pass_data[0]{address_set}->contains($local_id)) {
		my $addr = inet_ntoa($local_addr);
		push($g_pass_data[0]{address_copy}, "$local_id\t$local_family\t$addr\t$local_port\n");
	}
	$g_pass_data[0]{address_set}->insert($local_id);

	if (length($text)) {
		my $text_id = pass0_get_text_id($text);
	}
	$g_pass_data[0]{rows}++;
}
;

sub pass1_bitcoin_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $addr_pack = "vnLa[8]";
	my ($handle_id, $update_type, 
	    $remote_family, $remote_port, $remote_addr, $rzeros,
	    $local_family, $local_port, $local_addr, $lzeros,
	    $text_len, $text) = unpack("NN${addr_pack}${addr_pack}NZ*", $rest);
	$g_pass_data[1]{cid_rows}++;

	if (length($text)) {
		pass1_get_text_id($text);
	}
}

sub pass2_bitcoin_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my $mid = pass2_prolog($source_id, $type, $timestamp);
	my $addr_pack = "vnLa[8]";
	my ($handle_id, $update_type, 
	    $remote_family, $remote_port, $remote_addr, $rzeros,
	    $local_family, $local_port, $local_addr, $lzeros,
	    $text_len, $text) = unpack("NN${addr_pack}${addr_pack}NZ*", $rest);

	my $remote_id = get_addr_key($remote_family, $remote_addr, $remote_port);
	my $local_id = get_addr_key($local_family, $local_addr, $local_port);

	my $cid = $g_pass_data[2]{next_cid}++;
	print { $g_pass_data[2]{bitcoin_cxn_messages} } "$cid\t$mid\t$handle_id\t$update_type\t$remote_id\t$local_id\n";
	if (length($text)) {
		my $text_id = get_text_id($text);
		print { $g_pass_data[2]{cxn_text_map} } "$cid\t$text_id\n";
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


sub pass0_bitcoin_msg_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my ($handle_id, $is_sender,
	    $magic, $command, $length,
	    $checksum, $payload) = unpack("NCVZ[12]VVa*", $rest);
	my $command_id = get_command_id($command);
	$g_pass_data[0]{rows}++;
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
		print STDERR "Unhandled type : type\n";
	} else {
		$handlers->{$type}->($source_id, $type, $timestamp, $rest);
	}
}

sub get_handle {	  # init db (if necessary and return a database
						  # handle). Try to not be a total knob and use db
						  # specific things in using it, because it probably
						  # won't always be sqlite


	my $dbh = DBI->connect("dbi:Pg:dbname=connector", "litton", "",
	                       { RaiseError => 1,
	                         AutoCommit => 0})
	  or die $DBI::errstr;

	return $dbh;
}
;

sub do_pass {
	(my $IN, my $pass, my $handler) = @_;
	binmode($IN) || die "Cannot binmode IN";

	my $filesize = (stat($IN))[7];
	my $progress = Term::ProgressBar->new({count => $filesize, ETA => 'linear', name => $pass});
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
			print STDERR "Received error on read: $!\n";
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
				if ($total_read > $next_update) {
					$next_update = $progress->update($total_read);
					#$g_dbh->commit;
				}
			}
			$has_read = 0;
			$cur = "";
		}
	}
	$progress->update($total_read);
}


sub jump_sequence {
	my $stmt = $g_dbh->prepare("select nextval(?)");
	$stmt->execute($_[0]);
	my $aref = $stmt->fetch;
	my $next_id = $aref->[0];
	$stmt = $g_dbh->prepare("select setval(?, ?)");
	$stmt->execute($_[0], $next_id + $_[1]);
	return $next_id;
}


my %pass0_handlers = (
                      2 => \&pass0_as_text,
                      4 => \&pass0_as_text,
                      8 => \&pass0_as_text,
                      16 => \&pass0_bitcoin_handler,
                      32 => \&pass0_bitcoin_msg_handler,
                     );

my %pass1_handlers = (
                      2 => \&pass1_as_text,
                      4 => \&pass1_as_text,
                      8 => \&pass1_as_text,
                      16 => \&pass1_bitcoin_handler,
                      32 => \&pass1_bitcoin_msg_handler,
                     );

my %pass2_handlers = (
                      2 => \&pass2_as_text,
                      4 => \&pass2_as_text,
                      8 => \&pass2_as_text,
                      16 => \&pass2_bitcoin_handler,
                      32 => \&pass2_bitcoin_msg_handler,
                      );


die "Please supply a filename" if ($#ARGV == -1);
my $filename = $ARGV[0];
open(my $IN, '<', $filename) or die "Could not open file";
$g_dbh = get_handle();

$g_dbh->do("LOCK TABLE addresses IN SHARE MODE");
$g_pass_data[0]{address_set} = fetch_all_addr_id();
$g_pass_data[0]{address_copy} = [];
do_pass($IN, "Counting and address pass", \%pass0_handlers);


$g_dbh->do("COPY addresses FROM STDIN");
foreach my $a (@{ $g_pass_data[0]{address_copy} }) {
	$g_dbh->pg_putcopydata($a);
}
$g_dbh->pg_putcopyend();
$g_dbh->commit; #release lock


$g_pass_data[1]{rows} = $g_pass_data[0]{rows};
$g_pass_data[1]{text_messages} = $g_pass_data[0]{text_messages};
$g_pass_data[0] = {};


$g_dbh->do("LOCK TABLE text_strings IN SHARE MODE");
$g_pass_data[1]{text_next} = jump_sequence('text_strings_id_seq', $g_pass_data[1]{text_messages}+1);

prepopulate_text_id;
seek($IN, 0, SEEK_SET);
$g_pass_data[1]{text_copy} = [];
do_pass($IN, "text string pass", \%pass1_handlers);
$g_dbh->do("COPY text_strings FROM STDIN");
foreach my $a (@{ $g_pass_data[1]{text_copy} }) {
	$g_dbh->pg_putcopydata($a);
}
$g_dbh->pg_putcopyend();
$g_dbh->commit; #release lock

$g_pass_data[2]{rows} = $g_pass_data[1]{rows};
$g_pass_data[2]{cid_rows} = $g_pass_data[1]{cid_rows};
$g_pass_data[2]{bid_rows} = $g_pass_data[1]{bid_rows};
$g_pass_data[1] = {};

$g_dbh->do("LOCK TABLE messages IN SHARE MODE");
$g_pass_data[2]{next_mid} = jump_sequence('messages_id_seq', $g_pass_data[2]{rows});
$g_dbh->commit;

$g_dbh->do("LOCK TABLE bitcoin_cxn_messages IN SHARE MODE");
$g_pass_data[2]{next_cid} = jump_sequence('bitcoin_cxn_messages_id_seq', $g_pass_data[2]{cid_rows});
$g_dbh->commit;

$g_dbh->do("LOCK TABLE bitcoin_messages IN SHARE MODE");
$g_pass_data[2]{next_bid} = jump_sequence('bitcoin_messages_id_seq', $g_pass_data[2]{bid_rows});
$g_dbh->commit;


seek($IN, 0, SEEK_SET);
do_pass($IN, "Main pass", \%pass2_handlers);

{
	my $stmt = $g_dbh->prepare('insert into imported (filename) values (?)');
	$stmt->execute($filename);
	$g_postfix = '' . $g_dbh->last_insert_id(undef, undef, undef, undef, {sequence=>'imported_id_seq'});
	my @statements = (
	                  "CREATE TABLE messages${g_postfix} () INHERITS (messages)",
	                  "CREATE TABLE text_messages${g_postfix} () INHERITS (text_messages)",
	                  "CREATE TABLE cxn_text_map${g_postfix} () INHERITS (cxn_text_map)",
	                  "CREATE TABLE bitcoin_cxn_messages${g_postfix} () INHERITS (bitcoin_cxn_messages)",
	                  "CREATE TABLE bitcoin_messages${g_postfix} () INHERITS (bitcoin_messages)",
	                  "CREATE TABLE bitcoin_message_payloads${g_postfix} () INHERITS (bitcoin_message_payloads)",
	                 );
	foreach my $s (@statements) {
		$g_dbh->do($s);
	}
}



foreach my $t (qw/messages bitcoin_cxn_messages bitcoin_messages text_messages cxn_text_map bitcoin_message_payloads/) {
	$g_dbh->do("COPY ${t}${g_postfix} FROM STDIN");
	seek($g_pass_data[2]{$t}, 0, SEEK_SET);
	my $fh = $g_pass_data[2]{$t};
	while (<$fh>) {
		$g_dbh->pg_putcopydata($_);
	}
	$g_dbh->pg_putcopyend();
	close($g_pass_data[2]{$t});
}

{
	my @statements =
	  (
	   "ALTER TABLE bitcoin_cxn_messages${g_postfix} ADD CONSTRAINT bitcoin_cxn_messages${g_postfix}_pkey PRIMARY KEY (id)",
	   "ALTER TABLE messages${g_postfix} ADD CONSTRAINT messages${g_postfix}_pkey PRIMARY KEY (id)",
	   "ALTER TABLE cxn_text_map${g_postfix} ADD CONSTRAINT cxn_text_map${g_postfix}_pkey PRIMARY KEY (bitcoin_cxn_msg_id, txt_id)",
	   "ALTER TABLE bitcoin_messages${g_postfix} ADD CONSTRAINT bitcoin_messages${g_postfix}_pkey PRIMARY KEY(id)",
	   "ALTER TABLE messages${g_postfix} ADD CONSTRAINT messages${g_postfix}_type_id_fkey FOREIGN KEY (type_id) REFERENCES msg_types(id)",

	   "CREATE INDEX message_tid${g_postfix} ON messages${g_postfix}(type_id)",
	   "ALTER TABLE text_messages${g_postfix} ADD CONSTRAINT text_messages${g_postfix}_message_id_fkey FOREIGN KEY (message_id) REFERENCES messages${g_postfix}(id)",
	   "ALTER TABLE text_messages${g_postfix} ADD CONSTRAINT text_messages${g_postfix}_text_id_fkey FOREIGN KEY (text_id) REFERENCES text_strings(id)",
	   "ALTER TABLE text_messages${g_postfix} ADD CONSTRAINT text_messages${g_postfix}_message_id_key UNIQUE (message_id)",

	   "ALTER TABLE cxn_text_map${g_postfix} ADD CONSTRAINT cxn_text_map${g_postfix}_msg_id_key UNIQUE (bitcoin_cxn_msg_id)",
	   "ALTER TABLE cxn_text_map${g_postfix} ADD CONSTRAINT cxn_text_map${g_postfix}_msg_id_fkey FOREIGN KEY (bitcoin_cxn_msg_id) REFERENCES bitcoin_cxn_messages${g_postfix}(id)",
	   "ALTER TABLE cxn_text_map${g_postfix} ADD CONSTRAINT cxn_text_map${g_postfix}_txt_id_fkey FOREIGN KEY (txt_id) REFERENCES text_strings(id)",

	   "ALTER TABLE bitcoin_cxn_messages${g_postfix} ADD CONSTRAINT bitcoin_cxn_messages${g_postfix}_message_id_key UNIQUE (message_id)",
	   "CREATE INDEX bc_tid${g_postfix} ON bitcoin_cxn_messages${g_postfix}(cxn_type_id)",
	   "ALTER TABLE bitcoin_cxn_messages${g_postfix} ADD CONSTRAINT bitcoin_cxn_messages${g_postfix}_cxn_type_id_fkey FOREIGN KEY (cxn_type_id) REFERENCES bitcoin_cxn_types(id)",
	   "ALTER TABLE bitcoin_cxn_messages${g_postfix} ADD CONSTRAINT bitcoin_cxn_messages${g_postfix}_local_id_fkey FOREIGN KEY (local_id) REFERENCES addresses(id)",
	   "ALTER TABLE bitcoin_cxn_messages${g_postfix} ADD CONSTRAINT bitcoin_cxn_messages${g_postfix}_message_id_fkey FOREIGN KEY (message_id) REFERENCES messages${g_postfix}(id)",
	   "ALTER TABLE bitcoin_cxn_messages${g_postfix} ADD CONSTRAINT bitcoin_cxn_messages${g_postfix}_remote_id_fkey FOREIGN KEY (remote_id) REFERENCES addresses(id)",

	   "ALTER TABLE bitcoin_messages${g_postfix} ADD CONSTRAINT bitcoin_messages${g_postfix}_message_id_key UNIQUE (message_id)",
	   "CREATE INDEX bm_command${g_postfix} ON bitcoin_messages${g_postfix}(command_id)",
	   "ALTER TABLE bitcoin_messages${g_postfix} ADD CONSTRAINT bitcoin_messages${g_postfix}_command_id_fkey FOREIGN KEY (command_id) REFERENCES commands(id)",
	   "ALTER TABLE bitcoin_messages${g_postfix} ADD CONSTRAINT bitcoin_messages${g_postfix}_message_id_fkey FOREIGN KEY (message_id) REFERENCES messages${g_postfix}(id)",
	   "ALTER TABLE bitcoin_message_payloads${g_postfix} ADD CONSTRAINT bitcoin_message_payloads${g_postfix}_bitcoin_msg_id_key UNIQUE (bitcoin_msg_id)",
	   "ALTER TABLE bitcoin_message_payloads${g_postfix} ADD CONSTRAINT bitcoin_message_payloads${g_postfix}_bitcoin_msg_id_fkey FOREIGN KEY (bitcoin_msg_id) REFERENCES bitcoin_messages${g_postfix}(id)",
	   "CREATE INDEX timestamp_idx${g_postfix} ON messages${g_postfix}(timestamp)",
	  );

	foreach my $s (@statements) {
		print STDERR ".";
		$g_dbh->do($s);
	}
	print "\n";
	$g_dbh->commit;
	# get min and max and set a constraint. when they don't overlap this will partition the tables. When they do....fix that?
	my $stmt = $g_dbh->prepare("select min(timestamp), max(timestamp) from messages${g_postfix}");
	$stmt->execute;
	my $aref = $stmt->fetch;
	$stmt->finish;
	$stmt = $g_dbh->prepare("alter table messages${g_postfix} add constraint timechk${g_postfix} CHECK (timestamp >= ? and timestamp <= ?)");
	$stmt->execute($aref->[0], $aref->[1]);
}


close($IN);

$g_dbh->commit;

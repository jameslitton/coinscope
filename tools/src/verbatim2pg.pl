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
use POSIX ":sys_wait_h";
use IPC::SysV qw/SEM_UNDO S_IRUSR S_IWUSR IPC_CREAT IPC_EXCL ftok/;
use IPC::Semaphore;
use Sys::CPU;

# This reads a verbatim file and writes it to a database.

my $g_dbh;
my $g_suffix = '';
my %g_pids;

my $id = ftok($0, 0);
die unless $id;
my $g_sem = IPC::Semaphore->new($id, 5, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
if ($g_sem) {
	my $cpus = Sys::CPU::cpu_count();
	if ($cpus > 1) {
		$cpus--;						  # Leave one core free 
	}
	$g_sem->setval(0, 1);
	$g_sem->setval(1, 1);
	$g_sem->setval(2, 1);
	$g_sem->setval(3, 1);
	$g_sem->setval(4, $cpus);
} else {
	print "Failed to create semaphore: $!\n";
	print "Need to clean up, perhaps\n";
	exit(1);
}

sub lock {
	$g_sem->op($_[0], -1, 0);
}


sub unlock {
	$g_sem->op($_[0], 1, 0);
}



my @g_pass_data; # any global data needed for passes, which can be zeroed out as you move along

sub is_interactive {
	return -t STDIN && -t STDOUT;
}

BEGIN {
	open(my $dev_null, '>>', "/dev/null") or die "Could not open dev/null: $!\n";
	sub my_out {				 # return a file descriptor to print out to
		return *STDOUT if is_interactive;
		return $dev_null;
	}

	sub my_err {				 # return a file descriptor to print out to
		return *STDERR if is_interactive;
		return $dev_null;
	}
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
	my $sth = $g_dbh->prepare(q{select id, txt from text_strings where id in (select distinct txt_id from cxn_text_map)});
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
	$text =~ s/\x00*$//;	       # Log file includes terminating null...
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
	$rest =~ s/\x00*$//;	       # Log file includes terminating null...
	if ($type == 2 && $rest eq 'launching getaddr program...') {
		$timestamp = $g_pass_data[0]{last_ts} if ($timestamp + 120 < $g_pass_data[0]{last_ts});
		my $stmt = $g_dbh->prepare('insert into experiments (experiment, start_time) values (?, ?)');
		$stmt->execute('getaddr', iso8601($timestamp));
	}
}


sub pass1_get_text_id {
	my $original_text = shift;
	$original_text =~ s/\x00*$//; # Log file includes terminating null...
	my $id = get_text_id($original_text);
	return $id;
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
	my $address = int(shift);	  # 32 bit number
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

	$g_pass_data[0]{last_ts} = $timestamp;
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


my $g_consecutive_invalids = 0;
BEGIN {
	my %command_hash;
	sub get_command_id {
		unless (exists $command_hash{$_[0]}) {
			my $invalid = 0;
			my $str = $_[0];
			if ($str =~ /[[:^ascii:]]/) {
				(my $p) = unpack("H*", $str);
				$str = "\\x$p";
				$str = substr($str, 0, 12);
				$invalid = 1;
			}
			my $cmd_sth = $g_dbh->prepare_cached(q{
INSERT into commands (command) values (?)});
			$cmd_sth->execute($str);

			my $getcmd_sth = $g_dbh->prepare_cached(q{
select id from commands where command = ?});
			$getcmd_sth->execute($str);

			my $rv = $getcmd_sth->fetch()->[0];
			$getcmd_sth->finish;
			$command_hash{$_[0]} = [ $rv, $invalid ];
		}
		$g_consecutive_invalids += $command_hash{$_[0]}[1];
		return $command_hash{$_[0]}[0];
	}
}



sub pass0_bitcoin_msg_handler {
	my ($source_id, $type, $timestamp, $rest) = @_;
	my ($handle_id, $is_sender,
	    $magic, $command, $length,
	    $checksum, $payload) = unpack("NCVZ[12]VVa*", $rest);
	my $command_id = get_command_id($command);

	$g_pass_data[0]{last_ts} = $timestamp;

	if ($command eq 'getaddr') {
		my $bucket = $timestamp - ($timestamp % 120);
		$g_pass_data[0]{getaddr}{$bucket}++;
	}
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
		print { my_err() } "Unhandled type : $type\n";
	} else {
		my $lasterr = $g_consecutive_invalids;
		$handlers->{$type}->($source_id, $type, $timestamp, $rest);
		$g_consecutive_invalids = 0 if $g_consecutive_invalids  == $lasterr;
		die "three malformed messages in a row. Check for log corruption" if $g_consecutive_invalids > 3;
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

	my $progress = 0;
	my $filesize = (stat($IN))[7];

	if (is_interactive) {
		$progress = Term::ProgressBar->new({count => $filesize, ETA => 'linear', name => $pass});
		$progress->max_update_rate(1);
	}

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


sub jump_sequence {
	my ($seq, $offset) = @_;
	my $stmt = $g_dbh->prepare("select nextval(?)");
	$stmt->execute($seq);
	my $aref = $stmt->fetch;
	my $next_id = $aref->[0];
	if ($offset == 0) {
		return $next_id;
	}
	$stmt = $g_dbh->prepare("select setval(?, ?)");
	$stmt->execute($seq, $next_id + $offset);
	return $next_id;
}

sub process_buckets {
	my @buckets = sort { $a <=> $b } keys %{$g_pass_data[0]{getaddr}};
	return if ($#buckets == -1);
	
	my @v = sort { $a <=> $b } values %{$g_pass_data[0]{getaddr}};
	my $median;
	if (($#v + 1) % 2 == 0) {
		$median = $v[($#v + 1) / 2];
	} else {
		$median = ($v[$#v / 2] + $v[($#v / 2) + 1]) / 2;
	}
	
	@v = ();
	my $sum_absdev = 0;
	foreach my $b (@buckets) {
		push(@v, abs($g_pass_data[0]{getaddr}{$b} - $median));
		$sum_absdev += abs($g_pass_data[0]{getaddr}{$b} - $median);
	}

	@v = sort { $a <=> $b } @v;
	my $med_absdev;
	if (($#v + 1) % 2 == 0) {
		$med_absdev = $v[($#v + 1) / 2];
	} else {
		$med_absdev = ($v[$#v / 2] + $v[($#v / 2) + 1]) / 2;
	}

	my $avg_absdev = $sum_absdev / ($#buckets+1);

	# we collect statistics here to do several measures, but twice the average seems to be a good way to detect the start
	my @getaddrs;
	foreach my $b (@buckets) {
		if ($g_pass_data[0]{getaddr}{$b} - $median > 2*$avg_absdev) {
			my $newaddr = 1;
			if ($#getaddrs >= 0) {
				if ($b - $getaddrs[ $#getaddrs ]{last} < 6*60) { # detected a getaddr within six minutes ( 3 buckets )
					$getaddrs[ $#getaddrs]{last} = $b;
					$newaddr = 0;
				}
			}
			if ($newaddr) {
				push(@getaddrs, {first => $b, last => $b});
			}
		}
	}
	my $stmt = $g_dbh->prepare('insert into experiments (experiment, start_time) values (?, ?)');
	foreach (@getaddrs) {
		$stmt->execute('getaddr', iso8601($_->{first}));
	}
}



my %pass0_handlers = (
                      2 => \&pass0_as_text,
                      4 => \&void_handler,
                      8 => \&void_handler,
                      16 => \&pass0_bitcoin_handler,
                      32 => \&pass0_bitcoin_msg_handler,
                      64 => \&pass0_as_text,
                     );

my %pass1_handlers = (
                      2 => \&void_handler,
                      4 => \&void_handler,
                      8 => \&void_handler,
                      16 => \&pass1_bitcoin_handler,
                      32 => \&pass1_bitcoin_msg_handler,
                      64 => \&pass1_as_text,
                     );

my %pass2_handlers = (
                      2 => \&void_handler,
                      4 => \&void_handler,
                      8 => \&void_handler,
                      16 => \&pass2_bitcoin_handler,
                      32 => \&pass2_bitcoin_msg_handler,
                      64 => \&pass2_as_text,
                     );

sub child_main {

	my $filename = $_[0];
	my $rootfn = "$0 $filename";
	$0 = "$rootfn waiting\0"; # Make it easier to see who is doing what

	lock(4); # to be unlocked by the parent when this guy dies. lock is misnomer, just a semaphore to throttle #kids
	#print { my_out() } "Child $$ proceeding\n";


	$g_dbh = get_handle();

	{
		my $stmt = $g_dbh->prepare('select succeeded from imported where filename = ?');
		$stmt->execute($filename);
		my $aref = $stmt->fetch;
		if ($aref) {
			if (! $aref->[0]) {
				#print { my_err() } "Not importing $filename. A Failed import exists in the imported table.\n";
				exit(0);
			} else {
				#print { my_out() } "Skipping $filename, already imported\n";
				exit(0);				  #already imported, yay
			}
		}
	}

	my $IN;

	if (index($filename, 'gz') != -1) {
		$0 = "$rootfn gunzip\0";
		my $rv = system("gunzip -c $filename > $filename.unzipped");
		if ($rv != 0) {
			print "Could not gunzip file: $!\n";
			exit(1);
		}
		open($IN, '<', "$filename.unzipped") or die "Could not open file: $!\n";
		unlink("$filename.unzipped");
	} else {
		open($IN, '<', $filename) or die "Could not open file: $!\n";
	}

	{
		my $stmt = $g_dbh->prepare('insert into imported (filename, size) values (?, ?)');
		$stmt->execute($filename, (stat($IN))[7]);
		$g_dbh->commit;
	}


	foreach (qw/messages bitcoin_cxn_messages cxn_text_map bitcoin_messages bitcoin_message_payloads text_messages/) {
		open(my $fp, '+>', "$_.$$.copy") or die "Could not open file: $!\n";
		$g_pass_data[2]{$_} = $fp;
	}



	$0 = "$rootfn waiting 0\0";
	lock(0);
	$0 = "$rootfn doing pass 0\0";
	eval {
		$g_dbh->do("LOCK TABLE addresses IN SHARE MODE");
		$g_pass_data[0]{address_set} = fetch_all_addr_id();
		$g_pass_data[0]{address_copy} = [];
		$g_pass_data[0]{text_messages} = 0;
		do_pass($IN, "Counting and address pass", \%pass0_handlers);

		$g_dbh->do("COPY addresses FROM STDIN");
		foreach my $a (@{ $g_pass_data[0]{address_copy} }) {
			$g_dbh->pg_putcopydata($a);
		}
		$g_dbh->pg_putcopyend();
		$g_dbh->commit;			  #release lock
	};
	if ($@) {
		unlock(0);
		die $@;
	}
	unlock(0);

	$0 = "$rootfn bucket processing\0";
	#process_buckets();

	$g_pass_data[1]{rows} = $g_pass_data[0]{rows};
	$g_pass_data[1]{text_messages} = $g_pass_data[0]{text_messages};
	$g_pass_data[0] = {};


	$0 = "$rootfn waiting 1\0";
	lock(1);
	$0 = "$rootfn doing pass 1\0";
	eval {
		$g_dbh->do("LOCK TABLE text_strings IN SHARE MODE");
		$g_pass_data[1]{text_next} = jump_sequence('text_strings_id_seq', 0);
		my $original_next = $g_pass_data[1]{text_next};

		prepopulate_text_id;
		seek($IN, 0, SEEK_SET);
		$g_pass_data[1]{text_copy} = [];
		do_pass($IN, "text string pass", \%pass1_handlers);
		jump_sequence('text_strings_id_seq', $g_pass_data[1]{text_next}+1 - $original_next);
		$g_dbh->do("COPY text_strings FROM STDIN");
		foreach my $a (@{ $g_pass_data[1]{text_copy} }) {
			$g_dbh->pg_putcopydata($a);
		}
		$g_dbh->pg_putcopyend();
		$g_dbh->commit;			  #release lock
	};
	if ($@) {
		unlock(1);
		die $@;
	}
	unlock(1);
	
	$g_pass_data[2]{rows} = $g_pass_data[1]{rows};
	$g_pass_data[2]{cid_rows} = $g_pass_data[1]{cid_rows};
	$g_pass_data[2]{bid_rows} = $g_pass_data[1]{bid_rows};
	$g_pass_data[1] = {};

	$0 = "$rootfn doing table jumps\0";
	lock(2);
	eval {
		$g_dbh->do("LOCK TABLE messages IN SHARE MODE");
		$g_pass_data[2]{next_mid} = jump_sequence('messages_id_seq', $g_pass_data[2]{rows});
		$g_dbh->commit;
	};
	if ($@) {
		unlock(2);
		die $@;
	}
	unlock(2);

	lock(3);
	eval {
		$g_dbh->do("LOCK TABLE bitcoin_cxn_messages IN SHARE MODE");
		$g_pass_data[2]{next_cid} = jump_sequence('bitcoin_cxn_messages_id_seq', $g_pass_data[2]{cid_rows});
		$g_dbh->commit;
	}; 
	if ($@) {
		unlock(3);
		die $@;
	}
	unlock(3);

	lock(3);
	eval {
		$g_dbh->do("LOCK TABLE bitcoin_messages IN SHARE MODE");
		$g_pass_data[2]{next_bid} = jump_sequence('bitcoin_messages_id_seq', $g_pass_data[2]{bid_rows});
		$g_dbh->commit;
	};
	if ($@) {
		unlock(3);
		die $@;
	}
	unlock(3);


	$0 = "$rootfn doing pass 2\0";
	seek($IN, 0, SEEK_SET);
	do_pass($IN, "Main pass", \%pass2_handlers);

	$0 = "$rootfn creating tables\0";
	{
		$g_suffix = '' . $g_dbh->last_insert_id(undef, undef, undef, undef, {sequence=>'imported_id_seq'});
		my @statements = (
		                  "CREATE TABLE messages${g_suffix} () INHERITS (messages)",
		                  "CREATE TABLE text_messages${g_suffix} () INHERITS (text_messages)",
		                  "CREATE TABLE cxn_text_map${g_suffix} () INHERITS (cxn_text_map)",
		                  "CREATE TABLE bitcoin_cxn_messages${g_suffix} () INHERITS (bitcoin_cxn_messages)",
		                  "CREATE TABLE bitcoin_messages${g_suffix} () INHERITS (bitcoin_messages)",
		                  "CREATE TABLE bitcoin_message_payloads${g_suffix} () INHERITS (bitcoin_message_payloads)",
		                  "CREATE TABLE address_mapping${g_suffix} () INHERITS (address_mapping)",
		                 );
		foreach my $s (@statements) {
			$g_dbh->do($s);
		}
	}


	foreach my $t (qw/messages bitcoin_cxn_messages bitcoin_messages text_messages cxn_text_map bitcoin_message_payloads/) {
		$0 = "$rootfn copy ${t}${g_suffix}\0";
		$g_dbh->do("COPY ${t}${g_suffix} FROM STDIN");
		seek($g_pass_data[2]{$t}, 0, SEEK_SET);
		my $fh = $g_pass_data[2]{$t};
		while (<$fh>) {
			$g_dbh->pg_putcopydata($_);
		}
		$g_dbh->pg_putcopyend();
		close($g_pass_data[2]{$t});
	}

	$0 = "$rootfn final statements\0";
	{
		my @statements =
		  (
		   "ALTER TABLE bitcoin_cxn_messages${g_suffix} ADD CONSTRAINT bitcoin_cxn_messages${g_suffix}_pkey PRIMARY KEY (id)",
		   "ALTER TABLE messages${g_suffix} ADD CONSTRAINT messages${g_suffix}_pkey PRIMARY KEY (id)",
		   "ALTER TABLE cxn_text_map${g_suffix} ADD CONSTRAINT cxn_text_map${g_suffix}_pkey PRIMARY KEY (bitcoin_cxn_msg_id, txt_id)",
		   "ALTER TABLE bitcoin_messages${g_suffix} ADD CONSTRAINT bitcoin_messages${g_suffix}_pkey PRIMARY KEY(id)",
		   "ALTER TABLE messages${g_suffix} ADD CONSTRAINT messages${g_suffix}_type_id_fkey FOREIGN KEY (type_id) REFERENCES msg_types(id)",

		   "CREATE INDEX message_tid${g_suffix} ON messages${g_suffix}(type_id)",
		   "ALTER TABLE text_messages${g_suffix} ADD CONSTRAINT text_messages${g_suffix}_message_id_fkey FOREIGN KEY (message_id) REFERENCES messages${g_suffix}(id)",
		   "ALTER TABLE text_messages${g_suffix} ADD CONSTRAINT text_messages${g_suffix}_text_id_fkey FOREIGN KEY (text_id) REFERENCES text_strings(id)",
		   "ALTER TABLE text_messages${g_suffix} ADD CONSTRAINT text_messages${g_suffix}_message_id_key UNIQUE (message_id)",

		   "ALTER TABLE cxn_text_map${g_suffix} ADD CONSTRAINT cxn_text_map${g_suffix}_msg_id_key UNIQUE (bitcoin_cxn_msg_id)",
		   "ALTER TABLE cxn_text_map${g_suffix} ADD CONSTRAINT cxn_text_map${g_suffix}_msg_id_fkey FOREIGN KEY (bitcoin_cxn_msg_id) REFERENCES bitcoin_cxn_messages${g_suffix}(id)",
		   "ALTER TABLE cxn_text_map${g_suffix} ADD CONSTRAINT cxn_text_map${g_suffix}_txt_id_fkey FOREIGN KEY (txt_id) REFERENCES text_strings(id)",

		   "ALTER TABLE bitcoin_cxn_messages${g_suffix} ADD CONSTRAINT bitcoin_cxn_messages${g_suffix}_message_id_key UNIQUE (message_id)",
		   "CREATE INDEX bc_tid${g_suffix} ON bitcoin_cxn_messages${g_suffix}(cxn_type_id)",
		   "ALTER TABLE bitcoin_cxn_messages${g_suffix} ADD CONSTRAINT bitcoin_cxn_messages${g_suffix}_cxn_type_id_fkey FOREIGN KEY (cxn_type_id) REFERENCES bitcoin_cxn_types(id)",
		   "ALTER TABLE bitcoin_cxn_messages${g_suffix} ADD CONSTRAINT bitcoin_cxn_messages${g_suffix}_local_id_fkey FOREIGN KEY (local_id) REFERENCES addresses(id)",
		   "ALTER TABLE bitcoin_cxn_messages${g_suffix} ADD CONSTRAINT bitcoin_cxn_messages${g_suffix}_message_id_fkey FOREIGN KEY (message_id) REFERENCES messages${g_suffix}(id)",
		   "ALTER TABLE bitcoin_cxn_messages${g_suffix} ADD CONSTRAINT bitcoin_cxn_messages${g_suffix}_remote_id_fkey FOREIGN KEY (remote_id) REFERENCES addresses(id)",

		   "ALTER TABLE bitcoin_messages${g_suffix} ADD CONSTRAINT bitcoin_messages${g_suffix}_message_id_key UNIQUE (message_id)",
		   "CREATE INDEX bm_command${g_suffix} ON bitcoin_messages${g_suffix}(command_id)",
		   "ALTER TABLE bitcoin_messages${g_suffix} ADD CONSTRAINT bitcoin_messages${g_suffix}_command_id_fkey FOREIGN KEY (command_id) REFERENCES commands(id)",
		   "ALTER TABLE bitcoin_messages${g_suffix} ADD CONSTRAINT bitcoin_messages${g_suffix}_message_id_fkey FOREIGN KEY (message_id) REFERENCES messages${g_suffix}(id)",
		   "ALTER TABLE bitcoin_message_payloads${g_suffix} ADD CONSTRAINT bitcoin_message_payloads${g_suffix}_bitcoin_msg_id_key UNIQUE (bitcoin_msg_id)",
		   "ALTER TABLE bitcoin_message_payloads${g_suffix} ADD CONSTRAINT bitcoin_message_payloads${g_suffix}_bitcoin_msg_id_fkey FOREIGN KEY (bitcoin_msg_id) REFERENCES bitcoin_messages${g_suffix}(id)",
		   "CREATE INDEX timestamp_idx${g_suffix} ON messages${g_suffix}(timestamp)",
		   "CREATE INDEX bcm_handle_id${g_suffix} ON bitcoin_cxn_messages${g_suffix}(handle_id)",
		   "CREATE INDEX m_source_id${g_suffix} ON messages${g_suffix}(source_id)",
		  );

		foreach my $s (@statements) {
			print { my_err() } ".";
			$g_dbh->do($s);
		}
		print { my_err() } "\n";
		$g_dbh->commit;
		# get min and max and set a constraint. when they don't overlap this will partition the tables. When they do....fix that?
		my $stmt = $g_dbh->prepare("select min(timestamp), max(timestamp) from messages${g_suffix}");
		$stmt->execute;
		my $aref = $stmt->fetch;
		$stmt->finish;
		$stmt = $g_dbh->prepare("alter table messages${g_suffix} add constraint timechk${g_suffix} CHECK (timestamp >= ? and timestamp <= ?)");
		$stmt->execute($aref->[0], $aref->[1]);
		$g_dbh->commit;

		$stmt = $g_dbh->prepare("update imported set succeeded = ?, finished = now() where filename = ?");
		$stmt->execute(1, $filename);

		$stmt = $g_dbh->prepare("select min(id), max(id) from bitcoin_messages${g_suffix}");
		$stmt->execute;
		my $min_max = $stmt->fetch;

		$stmt = $g_dbh->prepare("alter table bitcoin_messages${g_suffix} add constraint bm_idchk${g_suffix} CHECK (id >= ? and id <= ?)");
		$stmt->execute($min_max->[0], $min_max->[1]);

		$stmt = $g_dbh->prepare("alter table bitcoin_message_payloads${g_suffix} add constraint bmp_bidchk${g_suffix} CHECK (bitcoin_msg_id >= ? and bitcoin_msg_id <= ?)");
		$stmt->execute($min_max->[0], $min_max->[1]);

		$stmt = $g_dbh->prepare("select min(source_id), max(source_id) from messages${g_suffix}");
		$stmt->execute;
		$min_max = $stmt->fetch;

		$stmt = $g_dbh->prepare("alter table messages${g_suffix} add constraint m_sidchk${g_suffix} CHECK (source_id >= ? and source_id <= ?)");
		$stmt->execute($min_max->[0], $min_max->[1]);


		$g_dbh->do("analyze messages${g_suffix}");
		$g_dbh->do("analyze bitcoin_messages${g_suffix}");
		$g_dbh->do("analyze text_messages${g_suffix}");
		$g_dbh->do("analyze bitcoin_cxn_messages${g_suffix}");
		$g_dbh->do("analyze bitcoin_message_payloads${g_suffix}");

		$g_dbh->commit;

		$0 = "$rootfn doing final insert as select\0";
		eval {
			$g_dbh->do(qq|
insert into address_mapping${g_suffix} (source_id, handle_id, remote_id)
(SELECT m.source_id, cxnm.handle_id, cxnm.remote_id
FROM messages${g_suffix} m
JOIN bitcoin_cxn_messages${g_suffix} cxnm ON m.id = cxnm.message_id
WHERE cxn_type_id in (1,2))|);

			$0 = "$rootfn applying constraints to mapping\0";

			$stmt = $g_dbh->prepare("select min(source_id), max(source_id) from address_mapping${g_suffix}");
			$stmt->execute;
			$min_max = $stmt->fetch;

			$stmt = $g_dbh->prepare("alter table address_mapping${g_suffix} add constraint am_sidchk${g_suffix} CHECK (source_id >= ? and source_id <= ?)");
			$stmt->execute($min_max->[0], $min_max->[1]);
			my $sids = $min_max->[1] - $min_max->[0];

			$stmt = $g_dbh->prepare("select min(handle_id), max(handle_id) from address_mapping${g_suffix}");
			$stmt->execute;
			$min_max = $stmt->fetch;

			$stmt = $g_dbh->prepare("alter table address_mapping${g_suffix} add constraint am_hidchk${g_suffix} CHECK (handle_id >= ? and handle_id <= ?)");
			$stmt->execute($min_max->[0], $min_max->[1]);

			$g_dbh->do("CREATE INDEX am_handle_id${g_suffix} ON address_mapping${g_suffix}(handle_id)");
			if ($sids > 1) {
				$g_dbh->do("CREATE INDEX am_source_id${g_suffix} ON address_mapping${g_suffix}(source_id)");
			}

			$g_dbh->commit;
		}; print $@ if $@;

	}

	close($IN);
	$g_dbh->commit;
	foreach (qw/messages bitcoin_cxn_messages cxn_text_map bitcoin_messages bitcoin_message_payloads text_messages/) {
		close($g_pass_data[2]{$_});
		unlink("$_.$$.copy");
	}
}


foreach my $filename (@ARGV) {
	my $pid = fork();
	if ($pid > 0) {
		$g_pids{$pid} = $filename;
	} elsif ($pid == 0) {
		child_main($filename);
		exit(0);
	} else {
		die "Bad pid $pid: $!\n";
	}
}

my $pname = $0;
my $succeeded = 0;
$0 = "$pname Waiting on " . scalar(keys %g_pids) . " children\0";

while (scalar(keys %g_pids)) {
	my $cpid = wait;
	if ($cpid > 0) {
		delete $g_pids{$cpid};
		if ($? != 0) {
			print { my_out() } "Wait gave non-zero status for $cpid: $?\n";
		} else {
			$succeeded++;
		}
		$0 = "$pname Waiting on " . scalar(keys %g_pids) . " children\0";
		unlock(4);
	} elsif ($cpid == -1) {
		print STDERR "No children to wait on\n";
	}
}


$g_sem->remove();

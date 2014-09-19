#!/usr/bin/env perl

use strict;
use warnings;

use DBI qw(:sql_types);
use Term::ProgressBar;
use Data::Dumper;

# This reads a verbatim stream and writes it to a database.  Right now
# it's just SQLite, but I did it via perl and DBI so it would be very
# easy to switch and/or make behaviors more elaborate

# Also, this does NOt try to prevent duplicates. If you run this on
# the same verbatim record twice, you'll get duplicates. This is
# harder to solve than it sounds, since duplicate records in a log are
# not necessarily incorrect. Probably just adapt it to only insert
# stuff from a range of timestamps

# NOTE: This will run faster if you disable foreign key support. This
# should be fine (foreign key violation is a bug), but it is nice to
# have the debugging

my $g_dbh;

my %g_textid_cache;
my %g_addrid_cache;

sub insert_prolog {
	my $sth = $g_dbh->prepare_cached('insert into messages (type_id, timestamp) values (?, ?)') or die $g_dbh->errstr;
	$sth->execute(@_);
	return $g_dbh->func('last_insert_rowid');
}

sub get_text_id {
	unless (defined $g_textid_cache{$_[0]}) {
		my $text_sth = $g_dbh->prepare_cached(q{
insert or ignore into text_strings (txt) values (?)});
		$text_sth->execute($_[0]);
		my $text_ret = $g_dbh->prepare_cached(q{
select id from text_strings where txt = ?
});
		$text_ret->execute($_[0]);
		$g_textid_cache{$_[0]} = $text_ret->fetch()->[0];
		$text_ret->finish;
	}
	return $g_textid_cache{$_[0]}
}

sub as_text {
	my ($type, $timestamp, $rest) = @_;
	my $mid = insert_prolog($type, $timestamp);
	if (length($rest)) {
		my $text_id = get_text_id($rest);
		my $sth = $g_dbh->prepare_cached('insert into text_messages (message_id, text_id) values (?,?)');
		$sth->execute($mid, $text_id);
	}
}

sub get_addr_id {
	my $key = $_[0] . $_[1] . $_[2];
	unless (defined $g_addrid_cache{$key}) {
		my $addr_sth = $g_dbh->prepare_cached(q{
INSERT OR IGNORE into addresses (family, address, port) values (?,?,?)});
		$addr_sth->execute($_[0], $_[1], $_[2]);

		my $getaddr_sth = $g_dbh->prepare_cached(q{
select id from addresses where family = ? and address = ? and port = ?});
		$getaddr_sth->execute($_[0], $_[1], $_[2]);

		$g_addrid_cache{$key} = $getaddr_sth->fetch()->[0];
		$getaddr_sth->finish;
	}
	return $g_addrid_cache{$key}
}

sub bitcoin_handler {
	my ($type, $timestamp, $rest) = @_;
	my $mid = insert_prolog($type, $timestamp);
	my $addr_pack = "vnNa[8]"; # network in db in little-endian
	my ($handle_id, $update_type, 
	    $remote_family, $remote_port, $remote_addr, $rzeros,
	    $local_family, $local_port, $local_addr, $lzeros,
	    $text_len, $text) = unpack("NN${addr_pack}${addr_pack}NZ*", $rest);

	my $remote_id = get_addr_id($remote_family, $remote_addr, $remote_port);
	my $local_id = get_addr_id($local_family, $local_addr, $local_port);

	my $sth = $g_dbh->prepare_cached(q{
insert into bitcoin_cxn_messages 
(message_id, cxn_type_id, handle_id, remote_id, local_id)
values
(?,?,?,?,?)
});
	$sth->execute($mid, $update_type, $handle_id, $remote_id, $local_id);
	if (length($text)) {
		my $b_id = $g_dbh->func('last_insert_rowid');
		my $text_id = get_text_id($text);
		my $sth = $g_dbh->prepare_cached(q{
insert into cxn_text_map (bitcoin_cxn_msg_id, txt_id) values (?,?)
});
		$sth->execute($b_id, $text_id);
	}
}

sub bitcoin_msg_handler {
	my ($type, $timestamp, $rest) = @_;
	my $mid = insert_prolog($type, $timestamp);
	my ($handle_id, $is_sender,
	    $magic, $command, $length,
	    $checksum, $payload) = unpack("NCVZ[12]VVa*", $rest);

	my $sth = $g_dbh->prepare_cached(q{
insert into bitcoin_messages (message_id, handle_id, is_sender, command)
values
(?, ?, ?, ?)});
	$sth->execute($mid, $handle_id, $is_sender, $command);
	if (length($payload)) {
		my $bt_mid = $g_dbh->func('last_insert_rowid');
		my $sth = $g_dbh->prepare_cached('insert into bitcoin_message_payloads (bitcoin_msg_id, payload) values (?,?)');
		$sth->bind_param(1, $bt_mid);
		$sth->bind_param(2, $payload, SQL_BLOB);
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
	if (!defined $handlers{$type}) {
		print STDERR "Unhandled type : type\n";
	} else {
		$handlers{$type}->($type, $timestamp, $rest);
	}
}

sub get_handle { # init db (if necessary and return a database
                 # handle). Try to not be a total knob and use db
                 # specific things in using it, because it probably
                 # won't always be sqlite

	my $init_stmts = q{
CREATE TABLE IF NOT EXISTS types (
   id INTEGER PRIMARY KEY,
   type TEXT NOT NULL
);

INSERT OR IGNORE INTO types (id, type) VALUES
(2, "DEBUG"), (4, "CTRL"), (8, "ERROR"),
(16, "BITCOIN"), (32, "BITCOIN_MSG"), (64, "CONNECTOR");

CREATE TABLE IF NOT EXISTS messages (
   id INTEGER PRIMARY KEY,
   type_id INTEGER REFERENCES types NOT NULL,
   timestamp INTEGER NOT NULL
);


CREATE TABLE IF NOT EXISTS text_strings (
   id INTEGER PRIMARY KEY,
   txt TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS text_messages (
   message_id INTEGER NOT NULL REFERENCES messages,
   text_id INTEGER NOT NULL REFERENCES text_strings
);

CREATE TABLE IF NOT EXISTS bitcoin_cxn_types (
   id INTEGER PRIMARY KEY,
   type TEXT NOT NULL
);

INSERT OR IGNORE INTO bitcoin_cxn_types (id, type) VALUES
(1, "CONNECT_SUCCESS"),
(2, "ACCEPT_SUCCESS"),
(4, "ORDERLY_DISCONNECT"),
(8, "WRITE_DISCONNECT"),
(16, "UNEXPECTED_ERROR"),
(32, "CONNECT_FAILURE"),
(64, "PEER_RESET");


CREATE TABLE IF NOT EXISTS addr_families (
   id INTEGER PRIMARY KEY,
   family TEXT NOT NULL UNIQUE
);

INSERT OR IGNORE INTO addr_families (id, family) VALUES
(2, "AF_INET");

CREATE TABLE IF NOT EXISTS addresses (
   id INTEGER PRIMARY KEY,
   family INTEGER NOT NULL REFERENCES addr_families,
   address INTEGER NOT NULL,
   port INTEGER NOT NULL,
   UNIQUE(family, address, port)
);

CREATE TABLE IF NOT EXISTS bitcoin_cxn_messages (
   id INTEGER PRIMARY KEY,
   message_id INTEGER NOT NULL UNIQUE REFERENCES messages,
   cxn_type_id INTEGER NOT NULL REFERENCES bitcoin_cxn_types,
   handle_id INTEGER,
   remote_id INTEGER NOT NULL REFERENCES addresses,
   local_id INTEGER NOT NULL REFERENCES addresses
);


CREATE TABLE IF NOT EXISTS cxn_text_map (
  bitcoin_cxn_msg_id INTEGER NOT NULL UNIQUE REFERENCES bitcoin_cxn_messages,
  txt_id INTEGER NOT NULL REFERENCES text_strings
);

CREATE TABLE IF NOT EXISTS bitcoin_messages (
   id INTEGER PRIMARY KEY,
   message_id INTEGER NOT NULL UNIQUE REFERENCES messages,
   handle_id INTEGER NOT NULL,
   is_sender INTEGER NOT NULL,
   command TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS bitcoin_message_payloads (
  bitcoin_msg_id INTEGER NOT NULL UNIQUE REFERENCES bitcoin_messages,
  payload BLOB NOT NULL
);

CREATE VIEW IF NOT EXISTS bitcoin_message_v AS
SELECT m.timestamp, bt.handle_id, bt.is_sender, bt.command, p.payload
FROM messages m, bitcoin_messages bt
LEFT JOIN bitcoin_message_payloads p ON bt.id = p.bitcoin_msg_id
WHERE m.id = bt.message_id and m.type_id = 32
;

CREATE VIEW IF NOT EXISTS bitcoin_cxn_v AS
SELECT m.timestamp, btc.handle_id, cxn_t.type, btc.remote_id, btc.local_id
FROM messages m, bitcoin_cxn_types cxn_t, bitcoin_cxn_messages btc
LEFT JOIN cxn_text_map m ON btc.id = m.bitcoin_cxn_msg_id
WHERE m.id = btc.message_id and m.type_id = 16 and cxn_t.id = cxn_type_id
;

CREATE VIEW IF NOT EXISTS bitcoin_txt_v AS
SELECT m.timestamp, t.type, ts.txt 
FROM messages m, types t, text_strings ts, text_messages tm
WHERE t.id = m.type_id and tm.text_id = ts.id AND
(m.type_id = 2 OR m.type_id = 4 OR m.type_id = 8)

};

	my $dbh = DBI->connect("dbi:SQLite:dbname=verbatim.db", "", "",
	                       { RaiseError => 1,
	                         AutoCommit => 0})
	  or die $DBI::errstr;
	foreach my $s (split/;/, $init_stmts) {
		$dbh->do($s);
	};

	$dbh->commit;

	return $dbh;
};

sub main {
	my $IN = shift;
	binmode($IN) || die "Cannot binmode IN";

	my $filesize = (stat($IN))[7];
	my $progress = Term::ProgressBar->new({count => $filesize, ETA => 'linear', name => 'data insertion'});
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
				$next_update = $progress->update($total_read) if $total_read > $next_update;
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

my $indices = q{PRAGMA foreign_keys = ON;
CREATE INDEX IF NOT EXISTS message_tid ON messages(type_id);
CREATE INDEX IF NOT EXISTS bc_tid ON bitcoin_cxn_messages(cxn_type_id);
CREATE INDEX IF NOT EXISTS bm_command on bitcoin_messages(command);
CREATE UNIQUE INDEX IF NOT EXISTS u_types ON types(type);
CREATE UNIQUE INDEX IF NOT EXISTS u_tstrings ON text_strings(txt);
CREATE UNIQUE INDEX IF NOT EXISTS u_tmsg ON text_messages(message_id);
CREATE UNIQUE INDEX IF NOT EXISTS u_bcxnt ON bitcoin_cxn_types(type);
};
foreach my $s (split/;/, $indices) {
	print "$s";
	$g_dbh->do($s);
};
print "committing\n";
$g_dbh->commit;




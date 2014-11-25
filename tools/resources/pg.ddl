BEGIN TRANSACTION;

CREATE TABLE imported (
   id serial PRIMARY KEY,
   filename varchar NOT NULL UNIQUE,
   started TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT now(),
   finished TIMESTAMP WITH TIME ZONE,
   size int8 NOT NULL,
   succeeded BOOLEAN NOT NULL DEFAULT false
);

CREATE TYPE experiment AS ENUM ('getaddr', 'txprobe');
CREATE TABLE experiments (
   experiment experiment NOT NULL,
   start_time timestamp with time zone NOT NULL,
   end_time timestamp with time zone,
   PRIMARY KEY(experiment, start_time)
);

CREATE RULE experiments_ignore_dupes AS
   ON INSERT TO experiments
   WHERE 
   (EXISTS (select 1 from experiments where experiments.start_time = NEW.start_time and experiments.experiment = NEW.experiment)) DO INSTEAD NOTHING;


CREATE TABLE msg_types (
   id INTEGER PRIMARY KEY,
   type varchar(25) NOT NULL UNIQUE
);

INSERT INTO msg_types (id, type) VALUES
(2, 'DEBUG'), (4, 'CTRL'), (8, 'ERROR'),
(16, 'BITCOIN'), (32, 'BITCOIN_MSG'), (64, 'CONTROL');

CREATE TABLE messages (
   id bigserial PRIMARY KEY,
   source_id INTEGER NOT NULL,
   type_id INTEGER REFERENCES msg_types(id) NOT NULL,
   timestamp timestamp with time zone NOT NULL
);

CREATE INDEX message_tid ON messages(type_id);


CREATE TABLE text_strings (
   id serial PRIMARY KEY,
   txt TEXT NOT NULL UNIQUE
);

CREATE RULE text_strings_ignore_dupes AS
   ON INSERT TO text_strings
   WHERE 
   (EXISTS (select 1 from text_strings where text_strings.txt = NEW.txt)) DO INSTEAD NOTHING;

CREATE TABLE text_messages (
   message_id BIGINT NOT NULL UNIQUE REFERENCES messages(id),
   text_id INTEGER NOT NULL REFERENCES text_strings(id)
);

CREATE TABLE bitcoin_cxn_types (
   id INTEGER PRIMARY KEY,
   type varchar(25) NOT NULL UNIQUE
);

INSERT INTO bitcoin_cxn_types (id, type) VALUES
(1, 'CONNECT_SUCCESS'),
(2, 'ACCEPT_SUCCESS'),
(4, 'ORDERLY_DISCONNECT'),
(8, 'WRITE_DISCONNECT'),
(16, 'UNEXPECTED_ERROR'),
(32, 'CONNECT_FAILURE'),
(64, 'PEER_RESET'),
(128, 'CONNECTOR_DISCONNECT');


CREATE TABLE addr_families (
   id INTEGER PRIMARY KEY,
   family varchar(10) NOT NULL UNIQUE
);

INSERT INTO addr_families (id, family) VALUES
(2, 'AF_INET');

CREATE TABLE addresses (
   id int8 PRIMARY KEY, -- first two bytes is family, second 4 address, last 2 is port. Doesn't matter as long as importer is consistent
   family INTEGER NOT NULL REFERENCES addr_families(id),
   address inet NOT NULL,
   port INTEGER NOT NULL,
   UNIQUE(family, address, port)
);

CREATE OR REPLACE FUNCTION expand_addrid(IN arg int8, OUT family integer, OUT address inet, OUT port integer) 
AS $$
SELECT 
foo.family,
'0.0.0.0'::inet + ((bin & x'ff'::int8) << 24) + (((bin >> 8) & x'ff'::int8) << 16) + + (((bin >> 16) & x'ff'::int8) << 8) +  (((bin >> 24) & x'ff'::int8) << 0),
foo.port
FROM 
   (SELECT ((arg & (x'ffff'::int))::bit(16))::int as port,
     ( (   (arg >> 48) & (x'ffff'::int) )::bit(32) )::int as family,
     ( (   (arg >> 16) & (x'ffffffff'::int) )::bit(32) )::int8 bin) as FOO
$$ LANGUAGE SQL;


CREATE RULE addresses_ignore_dupes AS
   ON INSERT TO addresses
   WHERE 
   (EXISTS (select 1 from addresses a where 
      a.family = NEW.family and a.address = NEW.address and a.port = NEW.port)) DO INSTEAD NOTHING;


CREATE TABLE bitcoin_cxn_messages (
   id bigserial PRIMARY KEY,
   message_id BIGINT NOT NULL UNIQUE REFERENCES messages(id),
   handle_id INTEGER NOT NULL,
   cxn_type_id INTEGER NOT NULL REFERENCES bitcoin_cxn_types(id),
   remote_id int8 NOT NULL REFERENCES addresses(id),
   local_id int8 NOT NULL REFERENCES addresses(id)
);

CREATE INDEX bcm_handle_id ON bitcoin_cxn_messages(handle_id);
CREATE INDEX bc_tid ON bitcoin_cxn_messages(cxn_type_id);

CREATE TABLE cxn_text_map (
  bitcoin_cxn_msg_id bigint NOT NULL UNIQUE REFERENCES bitcoin_cxn_messages,
  txt_id INTEGER NOT NULL REFERENCES text_strings,
  PRIMARY KEY(bitcoin_cxn_msg_id, txt_id)
);

CREATE TABLE commands (
   id serial PRIMARY KEY,
   command varchar(12) NOT NULL UNIQUE
);

INSERT INTO commands (command) VALUES 
('addr'),
('getaddr'),
('getblocks'),
('getdata'),
('inv'),
('notfound'),
('ping'),
('pong'),
('reject'),
('tx'),
('verack'),
('version');



CREATE RULE commands_ignoredupes AS
   ON INSERT TO commands
   WHERE 
   (EXISTS (select 1 from commands c where 
      c.command = NEW.command)) DO INSTEAD NOTHING;


CREATE TABLE bitcoin_messages (
   id bigserial PRIMARY KEY,
   message_id bigint NOT NULL UNIQUE REFERENCES messages(id),
   handle_id INTEGER NOT NULL,
   is_sender BOOLEAN NOT NULL,
   command_id INTEGER NOT NULL REFERENCES commands(id)
);

CREATE INDEX bm_command on bitcoin_messages(command_id);

CREATE TABLE bitcoin_message_payloads (
  bitcoin_msg_id bigint NOT NULL UNIQUE REFERENCES bitcoin_messages,
  payload bytea NOT NULL
);

CREATE TABLE address_mapping (
   source_id INTEGER NOT NULL,
   handle_id INTEGER NOT NULL,
   remote_id int8 NOT NULL REFERENCES addresses(id)
);


CREATE VIEW bitcoin_message_v AS
SELECT m.source_id, m.timestamp, bt.handle_id, bt.is_sender, c.command, p.payload
FROM messages m
JOIN bitcoin_messages bt ON m.id = bt.message_id
JOIN commands c ON c.id = bt.command_id
LEFT JOIN bitcoin_message_payloads p ON bt.id = p.bitcoin_msg_id
WHERE m.type_id = 32
;

CREATE VIEW bitcoin_cxn_v AS
SELECT m.source_id, m.timestamp, btc.handle_id, cxn_t.type, remote.address AS remote_address, 
   remote.port AS remote_port, local.address AS local_address, local.port AS local_port,
   ts.txt AS text
FROM messages m
JOIN bitcoin_cxn_messages btc ON m.id = btc.message_id 
JOIN bitcoin_cxn_types cxn_t ON cxn_t.id = btc.cxn_type_id
JOIN addresses remote ON btc.remote_id = remote.id
JOIN addresses local ON btc.local_id = local.id
LEFT JOIN cxn_text_map cmap ON btc.id = cmap.bitcoin_cxn_msg_id
LEFT JOIN text_strings ts ON cmap.txt_id = ts.id
WHERE m.type_id = 16;

CREATE VIEW bitcoin_txt_v AS
SELECT m.source_id, m.timestamp, t.type, ts.txt 
FROM messages m
JOIN msg_types t ON t.id = m.type_id 
JOIN text_messages tm ON tm.message_id = m.id
JOIN text_strings ts ON tm.text_id = ts.id
WHERE m.type_id in (2,4,8,64);

CREATE VIEW address_mapping_live_v AS
SELECT m.source_id, cxnm.handle_id, cxnm.remote_id, a.family, a.address, a.port 
FROM messages m 
JOIN bitcoin_cxn_messages cxnm  ON m.id = cxnm.message_id
JOIN addresses a ON a.id = cxnm.remote_id
WHERE cxn_type_id in (1,2);

CREATE VIEW bitcoin_message_extended_v AS
SELECT m.source_id, m.timestamp, bt.handle_id, bt.is_sender, bt.command_id, c.command, p.payload, am.remote_id, a.address, a.port
FROM messages m
JOIN bitcoin_messages bt ON m.id = bt.message_id
JOIN commands c ON c.id = bt.command_id
LEFT JOIN bitcoin_message_payloads p ON bt.id = p.bitcoin_msg_id
JOIN address_mapping am ON m.source_id = am.source_id and bt.handle_id = am.handle_id
JOIN addresses a on a.id = am.remote_id
WHERE m.type_id = 32;


END TRANSACTION;


BEGIN TRANSACTION;

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

-- map unique two-tuples to a one-tuple
CREATE TABLE unique_hid ( 
   hid bigserial PRIMARY KEY,
   source_id INTEGER NOT NULL,
   handle_id INTEGER NOT NULL,
   UNIQUE(source_id, handle_id)
);

CREATE RULE unique_hid_ignore_dupes AS
   ON INSERT TO unique_hid
   WHERE 
   (EXISTS (select 1 from unique_hid h where h.source_id = NEW.source_id and h.handle_id = NEW.handle_id)) DO INSTEAD NOTHING;


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
   id serial PRIMARY KEY,
   family INTEGER NOT NULL REFERENCES addr_families(id),
   address inet NOT NULL,
   port INTEGER NOT NULL,
   UNIQUE(family, address, port)
);

CREATE RULE addresses_ignore_dupes AS
   ON INSERT TO addresses
   WHERE 
   (EXISTS (select 1 from addresses a where 
      a.family = NEW.family and a.address = NEW.address and a.port = NEW.port)) DO INSTEAD NOTHING;


CREATE TABLE bitcoin_cxn_messages (
   id bigserial PRIMARY KEY,
   message_id bigint NOT NULL UNIQUE REFERENCES messages(id),
   cxn_type_id INTEGER NOT NULL REFERENCES bitcoin_cxn_types(id),
   hid bigint NOT NULL REFERENCES unique_hid(hid),
   remote_id INTEGER NOT NULL REFERENCES addresses(id),
   local_id INTEGER NOT NULL REFERENCES addresses(id)
);

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

CREATE RULE commands_ignoredupes AS
   ON INSERT TO commands
   WHERE 
   (EXISTS (select 1 from commands c where 
      c.command = NEW.command)) DO INSTEAD NOTHING;


CREATE TABLE bitcoin_messages (
   id bigserial PRIMARY KEY,
   message_id INTEGER NOT NULL UNIQUE REFERENCES messages(id),
   hid bigint NOT NULL REFERENCES unique_hid(hid),
   is_sender BOOLEAN NOT NULL,
   command_id INTEGER NOT NULL REFERENCES commands(id)
);

CREATE INDEX bm_command on bitcoin_messages(command_id);

CREATE TABLE bitcoin_message_payloads (
  bitcoin_msg_id bigint NOT NULL UNIQUE REFERENCES bitcoin_messages,
  payload bytea NOT NULL
);

CREATE VIEW bitcoin_message_v AS
SELECT m.timestamp, bt.hid, bt.is_sender, c.command, p.payload
FROM messages m
JOIN bitcoin_messages bt ON m.id = bt.message_id
JOIN commands c ON c.id = bt.command_id
LEFT JOIN bitcoin_message_payloads p ON bt.id = p.bitcoin_msg_id
WHERE m.type_id = 32
;

CREATE VIEW bitcoin_cxn_v AS
SELECT m.timestamp, btc.hid, cxn_t.type, remote.address AS remote_address, 
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
SELECT m.timestamp, t.type, ts.txt 
FROM messages m
JOIN msg_types t ON t.id = m.type_id 
JOIN text_messages tm ON tm.message_id = m.id
JOIN text_strings ts ON tm.text_id = ts.id
WHERE (m.type_id = 2 OR m.type_id = 4 OR m.type_id = 8);


END TRANSACTION;

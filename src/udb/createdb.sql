--
-- Database creation script for the Keepit NG user database
--
-- $Id: createdb.sql,v 1.36 2013/10/07 08:16:32 joe Exp $
--

CREATE LANGUAGE PLPGSQL;

--
-- We need a method for creating random strings
--
-- Each character has 36 possibilities (a-z plus 0-9)
--
CREATE OR REPLACE FUNCTION rstr(int)
       RETURNS text AS
$$
   SELECT array_to_string(ARRAY(
          SELECT substring('abcdefghijklmnopqrstuvwxyz0123456789'
                           FROM (random() * 35)::int + 1 FOR 1)
                 FROM generate_series(1,$1)), '')
$$ LANGUAGE SQL;

--
-- If we want 1 billion accounts and low probability of collission of
-- user names when generated randomly, we need a 10^18 space to hold
-- our 10^9 accounts.
--
-- We thus need log(10^18) / log(36) = 11.57 or ~ 12 characters of the
-- 36-base random string
--
CREATE OR REPLACE FUNCTION guidstr()
       RETURNS text AS
$$
   SELECT rstr(6) || '-' || rstr(6) || '-' || rstr(6)
$$ LANGUAGE SQL;



--
-- UD/API servers need access to the db
--
CREATE ROLE udapi
       WITH LOGIN ENCRYPTED PASSWORD 'GYOqNB1fw6bVQ';

--
-- User accounts. These support a hierarchy; for example, a partner
-- will have an account. Whenever the partner sells an account, they
-- will access the API using their account credentials to provision a
-- new user account. That user account will have the partner account
-- as its parent.
--
CREATE TABLE account (
       id    BIGSERIAL, -- Internal id of account
       guid  TEXT NOT NULL DEFAULT guidstr(), -- Unique textual account id
       enabled BOOLEAN NOT NULL DEFAULT TRUE, -- Allow access to account?
       parent BIGINT, -- NULL or parent account
       sysusr BOOLEAN NOT NULL DEFAULT FALSE, -- Specially privileged
                                              -- system user that can
                                              -- do impersonation. We
                                              -- should *not* have
                                              -- many of these - for
                                              -- now only the o365
                                              -- agent needs one.
       created TIMESTAMP WITH TIME ZONE DEFAULT now(),
       PRIMARY KEY (id),
       CONSTRAINT unique_account_guid UNIQUE (guid),
       FOREIGN KEY (parent) REFERENCES account(id)
               ON DELETE CASCADE
);

GRANT SELECT, INSERT, DELETE ON account TO udapi;
GRANT SELECT, UPDATE ON account_id_seq TO udapi;

--
-- We can associate contact, billing and other name/address recoreds
-- to an account
--
CREATE TABLE contact (
       id        BIGSERIAL, -- Internal id of contact
       account   BIGINT NOT NULL, -- Account to which we belong
       ctype     CHAR NOT NULL, -- 'p':primary, (later: billing, ...)
       email     TEXT, -- e-mail address of contact
       companyname TEXT, -- company name
       fullname TEXT, -- full name of contact
       phone TEXT, -- phone number
       street1 TEXT, -- street address line 1
       street2 TEXT, -- street address line 2
       city    TEXT, -- city
       state   TEXT, -- state
       zipcode TEXT, -- zip code
       country TEXT, -- country code after ISO3166-1 alpha-2
       PRIMARY KEY (id),
       -- Only allow one primary, one billing, ... per account
       CONSTRAINT contact_type_per_account UNIQUE (ctype,account),
       -- Restrict to known contact types
       CHECK (ctype IN ('p')),
       -- null or two upper-case letters
       CHECK (country IS NULL
              OR char_length(country) = 2 AND upper(country) = country),
       -- Reference our account. When it goes, we go.
       FOREIGN KEY (account) REFERENCES account(id)
               ON DELETE CASCADE
);

GRANT SELECT, INSERT, UPDATE, DELETE ON contact TO udapi;
GRANT SELECT, UPDATE ON contact_id_seq TO udapi;

--
-- We need to be able to tie an external ID to an account
--
CREATE TABLE account_extern_id (
       -- This is the account to which the extern id is associated
       account   BIGINT NOT NULL,
       -- This is the partner account; the extern ids must be unique
       -- within each partner account name space. So, it is the
       -- namespace identifier, not necessarily the direct parent of
       -- the account; usually it will point to the partner/reseller
       -- account.
       parent    BIGINT NOT NULL,
       -- This is the extern id
       extern    TEXT NOT NULL,
       -- We reference an account - when it goes, we go
       FOREIGN KEY (account) REFERENCES account(id)
               ON DELETE CASCADE,
       -- Our parent is an account too
       FOREIGN KEY (parent) REFERENCES account(id)
               ON DELETE CASCADE,
       -- For each partner, the extern id must be unique
       UNIQUE (parent,extern)
);

GRANT SELECT, INSERT, DELETE ON account_extern_id TO udapi;


--
-- To grant access to an account, we use access tokens. They are
-- either user selected usernames and passwords, or machine-generated
-- GUIDs and passwords.
--
-- In order to not expose user chosen passwords, we will do a hash of
-- a user supplied password and then store the lower-case hex encoded
-- digest as the "clear text" password in this table instead. In case
-- our database is leaked, it will not leak the real clear text
-- passwords the users supplied, which are most likely also used in
-- other systems (since people re-use their passwords).
--
-- We will also use access tokens to grant access to shares and other
-- objects; such tokens may be temporary or permanent. In any case,
-- tokens are "cheap", so a single token will only be used to grant
-- access to one object (be it an account or a share or ...). This
-- keeps the tokens simple and it keeps token management simple -
-- there is no list of objects that a token grants access to. A single
-- token grants access to a single object.
--
-- It would be nice to set an ACL on the access token so that we could
-- resolve whether any given access token grans access to CRUD on a
-- given URI target.  Alternatively we could define some basic types
-- of access tokens and code the access restrictions in the API
-- server. Some restrictions are there already...
--
-- Types of access token:
--  AnonymousParent    (can only create accounts nothing else)
--  PartnerParent      (can manage accounts but not their data)
--  User               (primary credentials for a flesh&blood user)
--  Device             (device token for backup access across other devices too,
--                      cannot for example create accounts)
--
--
CREATE TABLE access (
       id    BIGSERIAL, -- Internal id of access token
       descr TEXT NOT NULL, -- User visible/supplied description of access token
       atype CHAR NOT NULL, -- Access token type (a:AnonymousParent,p:PartnerParent,
                            -- u:User, d:Device)
       aname  TEXT NOT NULL, -- GUID or user name
       apass  TEXT NOT NULL, -- clear text password
       created TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT now(), -- time of creation
       created_by BIGINT, -- access token used to generate this (or null if none)
       expires TIMESTAMP WITH TIME ZONE, -- If not null, expiration time of token
       ttype CHAR NOT NULL, -- type of target: 'a' = account
       target BIGINT NOT NULL, -- id of target
       PRIMARY KEY (id),
       CONSTRAINT unique_token_name
                  UNIQUE (aname),
       FOREIGN KEY (created_by) REFERENCES access(id)
               ON DELETE CASCADE,
       CONSTRAINT known_target_type
                  CHECK (ttype = 'a'),
       CONSTRAINT known_token_type
                  CHECK (atype IN ('a', 'p', 'u', 'd'))
);

GRANT SELECT, INSERT, DELETE ON access TO udapi;
GRANT SELECT, UPDATE ON access_id_seq TO udapi;


--
-- A 'device' is a source of data. So, the backup clients will have to
-- register the computer on which they run as a device associated with
-- an account.
--
CREATE TABLE device (
       id    BIGSERIAL,         -- Internal id of device
       guid  TEXT NOT NULL DEFAULT guidstr(), -- Unique textual device id
       kind  CHAR NOT NULL,     -- p=pc, c=cloud
       account BIGINT NOT NULL, -- Id of account to which this belongs
       descr TEXT NOT NULL,     -- Short user visible/supplied name of device
       ctype TEXT,              -- If cloud service, type of cloud
                                -- service: o365, ftp, gdrive, skydrive
       uri TEXT,                -- Cloud device URI
       login TEXT,              -- Cloud device login
       password TEXT,           -- Cloud device password
       attributes HSTORE NOT NULL DEFAULT '',           -- KV store of device attributes
       PRIMARY KEY (id),
       FOREIGN KEY (account) REFERENCES account(id)
               ON DELETE CASCADE,
       UNIQUE(account,descr),     -- Device names must be unique on the account
       UNIQUE(guid),              -- We prefer "globally" unique ids on devices even
                                  -- though they strctly only need be unique on the
                                  -- account... Globally unique is much nicer.
       CHECK (LENGTH(descr) > 0), -- Empty names not allowed
       CHECK (kind IN ('p', 'c')) -- PC, Cloud
);

GRANT SELECT, INSERT, UPDATE, DELETE ON device TO udapi;
GRANT SELECT, UPDATE ON device_id_seq TO udapi;

--
-- Each device has a backup history. Whenver a new backup is
-- completed, an entry will be added to this table.
--
CREATE TABLE backup (
       id      BIGSERIAL,       -- Internal id of backup job
       device  BIGINT NOT NULL, -- Device on which this backup was done
       root    TEXT NOT NULL,   -- 64-character hex string; backup root object id
       tstamp  TIMESTAMP WITH TIME ZONE NOT NULL, -- Client time of backup initiation
       ctime   TIMESTAMP WITH TIME ZONE  -- Server time of snapshot registration
               NOT NULL DEFAULT now(),   -- used for snapshot cleanup purposes
       kind    CHAR NOT NULL,   -- p=partial, c=complete
       PRIMARY KEY (id),
       FOREIGN KEY (device) REFERENCES device(id)
               ON DELETE CASCADE,
       UNIQUE (device,tstamp),
       CHECK (kind IN ('p', 'c')) -- partial or complete
);
               
GRANT SELECT, INSERT, DELETE ON backup TO udapi;
GRANT SELECT, UPDATE ON backup_id_seq TO udapi;



--
-- We trim the backup history.
--
-- Right now, the resolution is globally defined - we need a more
-- flexible scheme in the future most likely.
--

--
-- This function returns the next-valid-item timestamp given a
-- timestamp of some item
--
CREATE OR REPLACE FUNCTION bht_next_valid_before(i TIMESTAMP WITH TIME ZONE)
       RETURNS TIMESTAMP WITH TIME ZONE
AS $$
DECLARE
   age INTERVAL := now() - i;
BEGIN
   -- First 15 minutes we have 1 minute resolution
   IF (age < '15 minutes'::INTERVAL) THEN
      RETURN i + '1 minute'::INTERVAL;
   END IF;
   -- First 8 hours have 15 minute resolution
   IF (age < '8 hours'::INTERVAL) THEN
      RETURN i + '15 minutes'::INTERVAL;
   END IF;
   -- First 24 hours have 1 hour resolution
   IF (age < '24 hours'::INTERVAL) THEN
      RETURN i + '1 hour'::INTERVAL;
   END IF;
   -- The first week has 1 day resolution
   IF (age < '1 week'::INTERVAL) THEN
      RETURN i + '1 day'::INTERVAL;
   END IF;
   -- The first year has 1 week resolution
   IF (age < '1 year') THEN
      RETURN i + '1 week'::INTERVAL;
   END IF;
   -- After the first year, we have 1 month resolution
   RETURN i + '1 month'::INTERVAL;
END
$$ LANGUAGE PLPGSQL;


CREATE OR REPLACE FUNCTION backup_history_trim()
       RETURNS TRIGGER
AS $$
DECLARE
   nvi TIMESTAMP WITH TIME ZONE;
   r backup%rowtype;
BEGIN
   --
   -- Go through the entire history for this device from the most
   -- oldest entry to the newest
   --
   -- We maintain a 'next-valid-item' timestamp, and any entry older
   -- than this will be deleted.
   --
   -- Whenever we meet an entry that is younger than next-valid-item,
   -- we update the next-valid-item depending on the age of the item,
   -- so that the resolution we allow depends on the age of the items.
   --
   nvi := NULL;
   FOR r IN SELECT * FROM backup WHERE device = NEW.device AND ctime < NEW.ctime
            AND kind = 'c'
            ORDER BY ctime ASC
   LOOP
     -- See if this entry is allowed. It is allowed if it is younger
     -- (or same age as) the next-valid-item timestamp
     IF nvi IS NULL OR r.ctime >= nvi THEN
       -- Valid. Update nvi depending on age.
       SELECT INTO nvi bht_next_valid_before(r.ctime);
     ELSE
       -- Not valid. Delete item.
       DELETE FROM backup WHERE id = r.id;
     END IF;
   END LOOP;
   -- Done
   RETURN NEW;
END
$$ LANGUAGE PLPGSQL;

CREATE TRIGGER backup_history_trim_trg
       AFTER INSERT ON backup
       FOR EACH ROW EXECUTE PROCEDURE backup_history_trim();


CREATE OR REPLACE FUNCTION delete_partial_snapshots()
       RETURNS TRIGGER
AS $$
BEGIN
   -- 
   -- Delete all partial backups for given device before insertion
   -- of new ether partial or complete backup.
   -- I.e. we will never have more than one partial snapshot per device
   --
   DELETE FROM backup WHERE device = NEW.device AND kind = 'p';
   -- Done
   RETURN NEW;
END
$$ LANGUAGE PLPGSQL;

CREATE TRIGGER delete_partial_snapshots_trg
       BEFORE INSERT ON backup
       FOR EACH ROW EXECUTE PROCEDURE delete_partial_snapshots(); 

--
-- A device will upload its most recent status whenever its status
-- changes significantly. For example, it will upload status when:
-- 1: The backup agent is started
-- 2: A backup is scheduled
-- 3: A backup is initiated
-- 4: A backup is ended (stopped or completed)
--
CREATE TABLE devstatus (
       id     BIGSERIAL,       -- Internal id of status entry
       device BIGINT NOT NULL, -- Device that reported this
       tstamp TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(), -- Time of report
       status TEXT NOT NULL,   -- Actual status content
       PRIMARY KEY (id),
       FOREIGN KEY (device) REFERENCES device (id)
               ON DELETE CASCADE
);

GRANT SELECT, INSERT, DELETE ON devstatus TO udapi;
GRANT SELECT, UPDATE ON devstatus_id_seq TO udapi;

CREATE INDEX devstatus_device_idx ON devstatus(device);

-- Procedure to clean up device status - delete older status entries
CREATE OR REPLACE FUNCTION devstatus_clean_dev()
       RETURNS TRIGGER
AS $$
BEGIN
   DELETE FROM devstatus
          WHERE device = NEW.device
            AND id NOT IN
            (SELECT id FROM devstatus
                    WHERE device = NEW.device
                 ORDER BY tstamp DESC
                    LIMIT 3);
   RETURN NEW;
END
$$ LANGUAGE PLPGSQL;

-- Clean up device status
CREATE TRIGGER devstatus_history_cleanup_trg
       AFTER INSERT ON devstatus
       FOR EACH ROW EXECUTE PROCEDURE devstatus_clean_dev();


--
-- Our event scheduling API needs a store for recurring and one-shot
-- events
--
CREATE TABLE schedule (
       id BIGSERIAL, -- Internal id of event
       account BIGINT NOT NULL, -- Account to which this belongs
       queue TEXT NOT NULL, -- Name of queue
       expire TIMESTAMP WITH TIME ZONE NOT NULL, -- Next expiry of event
       period INTERVAL,     -- If null, event is one-shot. Otherwise,
                            -- period of recurrence
       identifier TEXT NOT NULL, -- Event identifier (typically URI of
                                 -- object to treat upon event expiry,
                                 -- but the identifier could be
                                 -- anything really)
       PRIMARY KEY (id),
       FOREIGN KEY (account) REFERENCES account(id)
               ON DELETE CASCADE,
       UNIQUE (account,identifier), -- Identifier is unique per account
       CHECK (period IS NULL OR period >= '1 second'::INTERVAL)
);

-- We need to quickly be able to fetch the first event to expire
CREATE INDEX schedule_expiry_ndx
       ON schedule (queue,expire);

GRANT SELECT, INSERT, UPDATE, DELETE ON schedule TO udapi;
GRANT SELECT, UPDATE ON schedule_id_seq TO udapi;

--
-- Favourites
--
-- Note; the API will expose favourite paths as
--  /{devname}/{path-to-object}
-- but in the API we convert this so that the database
-- holds the device id and the {path-to-object}.
--
-- This way, a device rename will not break the favourites.
--
CREATE TABLE favourites (
       id BIGSERIAL, -- Internal id of favourite
       account BIGINT NOT NULL, -- Account to which this belongs
       label TEXT NOT NULL DEFAULT guidstr(), -- Label of favourite
       device BIGINT NOT NULL, -- Device
       path TEXT NOT NULL, -- relative path of favourite
       PRIMARY KEY (id),
       FOREIGN KEY (account) REFERENCES account(id)
               ON DELETE CASCADE,
       FOREIGN KEY (device) REFERENCES device(id)
               ON DELETE CASCADE,
       UNIQUE (account,label) -- Labels are unique per account
);

GRANT SELECT, INSERT, DELETE ON favourites TO udapi;
GRANT SELECT, UPDATE ON favourites_id_seq TO udapi;



--
-- Sharing
--
-- Time-limited sharing of a file tree.
--
-- A shares entry will map the /shares/{guid} URI on to the most
-- recent backup from some users device.
--
-- It maps a {guid} (which will be removed after a while), to device
-- and a path to the shared file or directory.
--
CREATE TABLE share (
       id BIGSERIAL, -- Internal id of share
       account BIGSERIAL NOT NULL, -- Account that shares the data
       access BIGSERIAL NOT NULL, -- Access token used to add share
       guid TEXT NOT NULL, -- we are addressed as: /share/{guid}
       device BIGSERIAL NOT NULL, -- Device we refer to
       path TEXT NOT NULL, -- /{directory-or-file-path}
       expires TIMESTAMP WITH TIME ZONE NOT NULL, -- Expire time of share
       --
       PRIMARY KEY (id),
       FOREIGN KEY (account) REFERENCES account (id)
               ON DELETE CASCADE,
       FOREIGN KEY (access) REFERENCES access (id)
               ON DELETE CASCADE,
       UNIQUE (guid),
       FOREIGN KEY (device) REFERENCES device (id)
               ON DELETE CASCADE
);

GRANT SELECT, INSERT, DELETE ON share TO udapi;
GRANT SELECT, UPDATE ON share_id_seq TO udapi;



---
--- Some test data..
---
INSERT INTO account (id,guid) VALUES (1,'add227e7-7f01-4641-803a-864a920aa816');
INSERT INTO access (id,descr,aname,apass,ttype,target)
       VALUES (1,'Joe''s telnet login', 'joe', '*****', 'a', 1);
INSERT INTO device (id,account,descr) VALUES (1,1,'Joe''s computer');
SELECT setval('account_id_seq', 100);
SELECT setval('access_id_seq', 100);
SELECT setval('device_id_seq', 100);

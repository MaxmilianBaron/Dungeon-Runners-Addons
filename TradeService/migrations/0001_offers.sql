CREATE TABLE offers (
    id TEXT PRIMARY KEY,
    owner TEXT NOT NULL,
    character TEXT NOT NULL,
    item TEXT NOT NULL,
    wanted TEXT NOT NULL,
    wanted_definition TEXT NOT NULL DEFAULT '',
    wanted_signature TEXT NOT NULL DEFAULT '',
    revision INTEGER NOT NULL DEFAULT 1,
    closed INTEGER NOT NULL DEFAULT 0 CHECK (closed IN (0, 1)),
    updated_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL
);

CREATE INDEX offers_public ON offers (closed, updated_at DESC, id DESC);
CREATE INDEX offers_owner ON offers (owner, updated_at DESC, id DESC);
CREATE INDEX offers_expiry ON offers (expires_at);
CREATE INDEX offers_wanted ON offers (wanted_signature, closed, expires_at);

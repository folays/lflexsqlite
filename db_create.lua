pragma(db, "journal_mode", "WAL")

db_query_row(db, [[CREATE TABLE configuration (
key_id INTEGER PRIMARY KEY,
key TEXT NOT NULL UNIQUE,
value TEXT
)]])
db_query_row(db, [[CREATE INDEX i_configuration_key ON configuration(key)]])

-- APP

pragma(db, "application_id", APP_PRAGMA_APPLICATION_ID, true)
pragma(db, "user_version", APP_PRAGMA_USER_VERSION, true)

db_query_row(db, [[CREATE TABLE test (
id INTEGER PRIMARY KEY,
name TEXT NOT NULL UNIQUE,
nb INTEGER NOT NULL
)]])
--db_query_row(db, [[CREATE INDEX i_test_name ON test(name)]])

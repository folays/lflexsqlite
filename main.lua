sqlite = require("lflexsqlite")

--- globals

APP_VERBOSE_LEVEL = 0
APP_DBPATH = "./test.db"
APP_PRAGMA_APPLICATION_ID = 0x77665544
APP_PRAGMA_USER_VERSION = 0x00000001

--- MAIN THREAD

dofile("db_config_global.lua")
dofile("global.lua")

db = db_open(APP_DBPATH)
db_create_if_needed(db)
db:close()

--- OTHERDS THREADS

dofile("global.lua")

db = db_open(APP_DBPATH)

dofile("thread.lua")


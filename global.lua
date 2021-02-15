function pragma(db, name, value, check)
  local stmt = db:prepare("PRAGMA "..name.." = "..value..";")
  stmt:execute()
  local t = stmt:fetch_row()
  stmt:close()
  if (check) then -- if check, then re-do PRAGMA to get new value
      local stmt = db:prepare("PRAGMA "..name)
      stmt:execute()
      t = stmt:fetch_row()
      stmt:close()
  end
  if (check ~= false) then
      --print("---> PRAGMA "..name.." = "..(t and t[1] or "?"))
      -- if check is a string/number, take it as the new expected value
      local expected_value = (check == nil or check == true) and value or check
      if (string.upper(t[1]) ~= string.upper(expected_value)) then error("not equal") end
  end
end

function db_execute(db, query)
  db_query_rows(db, query, 0)
end

function wall_hook(db, dbname, nb_pages)
  if (nb_pages >= 100000) then
      local ret, pnLog, pnCkpt = db:wal_checkpoint(dbname)
      print("\27[31mWAL_AUTOPCHECKPOINT\27[0m [ret "..ret.."] ("..pnLog.."/"..pnCkpt.." frames checkpointed/total)")
      return ret
  end
  return true
end

function db_open(dbpath)
  local db = sqlite.open(dbpath)

--[[
  // PRAGMA read_uncommitted = <boolean>; // https://www.sqlite.org/sharedcache.html

  // http://stackoverflow.com/questions/4358838/sqlite-insert-performance

  /* https://www.sqlite.org/wal.html
   * Note that with PRAGMA synchronous set to NORMAL, the checkpoint is the only operation to issue an I/O barrier or sync operation (fsync() on unix or FlushFileBuffers() on windows). If an application therefore runs checkpoint in a separate thread or process, the main thread or process that is doing database queries and updates will never block on a sync operation. This helps to prevent "latch-up" in applications running on a busy disk drive. The downside to this configuration is that transactions are no longer durable and might rollback following a power failure or hard reset.
   * To maximize the read performance, one wants to keep the WAL as small as possible and hence run checkpoints frequently, perhaps as often as every COMMIT. To maximize write performance, one wants to amortize the cost of each checkpoint over as many writes as possible, meaning that one wants to run checkpoints infrequently and let the WAL grow as large as possible before each checkpoint.
   */
--]]
  pragma(db, "journal_mode", "WAL") -- could try memory of even OFF
--[[
    {
	/* Truncate the WAL to the specified file size after each WAL checkpoint */
	//_sqlite_pragma(fbs->db, "PRAGMA journal_size_limit = 0;"); // default -1
    }
    {
      // PRAGMA auto_vacuum = 1/FULL;		// the database file is truncated at every transaction commit.
      // PRAGMA auto_vacuum = 2/INCREMENTAL;	// the separate PGRAMA incremental_vacuum=([N_pages]) must be invoked
      /* can be changed between full<->incremental anytime. Between "none", first set auto_vacuum, then invoke VACUUM();
       */
    }
--]]
--[[
  {
    /* https://www.sqlite.org/pragma.html#pragma_wal_autocheckpoint
     * a checkpoint will be run automatically whenever the write-ahead log equals or exceeds N pages in length.
     * Autocheckpointing is enabled by default with an interval of 1000 or SQLITE_DEFAULT_WAL_AUTOCHECKPOINT.
     * All automatic checkpoints are PASSIVE.
     */
    /* The automatic checkpoint mechanism is implemented as a simple wrapper around sqlite3_wal_hook().
     * - https://www.sqlite.org/c3ref/wal_hook.html
     * - https://www.sqlite.org/c3ref/wal_autocheckpoint.html
     * Likewise, registering a callback using sqlite3_wal_hook() disables the automatic checkpoint mechanism configured by this function [sqlite3_wal_autocheckpoint()].
     */
    /* In normal cases, new content is appended to the WAL file until the WAL file accumulates about 1000 pages (and is thus about 4MB in size)
     */
  }
--]]
  pragma(db, "wal_autocheckpoint", "100000")
  db:wal_hook(function(dbname, nb_pages) return wall_hook(db, dbname, nb_pages) end)
--[[
  {
    /* The sqlite3_commit_hook() interface registers a callback function to be invoked whenever a transaction is committed. 
     * void *sqlite3_commit_hook(sqlite3*, int(*)(void*), void*);
     * void *sqlite3_rollback_hook(sqlite3*, void(*)(void *), void*);
     * void *sqlite3_update_hook(...);
     * sqlite3_trace_v2() SQLITE_TRACE_ROW
     */
  }
--]]

--[[
  {
    // http://www.sqlite.org/faq.html#q19 - https://www.sqlite.org/pragma.html#pragma_synchronous
    // TODO : could try OFF(0)
    _sqlite_pragma(fbs->db, "PRAGMA synchronous = 1;"); // default : FULL(2) -> NORMAL(1);
    /* folays : _with_ WAL:
     * - OFF(0) : data safe upon crash, but corrupt upon power loss
     * - NORMAL(1) : no fsyncdata() after most transactions/commit in the WAL.
     *   sometimes a WAL checkpoint occurs, which fsyncdata() the WAL, commit the WAL back to the database,
     *    fsyncdata() the database, and then reset and  fsnycdata() the WAL header.
     *   it seems that there is a very small chance that a power loss would corrup the database.
     *    (maybe when the WAL is commited back to the database is not an idempotent operation)
     * - FULL(2) : a fsyncdata() occurs after each transaction/commit in the WAL.
     * - EXTRA(3) : seems not relevant in journal mode (not WAL)
     */
  }
--]]
  pragma(db, "synchronous", "NORMAL", false);
  pragma(db, "mmap_size", "268435456");
  pragma(db, "temp_store", "memory", "2");
  return db
end

function db_query_rows(db, query, index, ...)
  local t = {}
  local stmt = db:prepare(query)
  stmt:bind_all(...)
  stmt:execute()
  repeat
    local row = stmt:fetch_assoc()
    if (row) then t[row[index]] = row end
  until row == nil
  stmt:close()
  return t
end

function db_query_row(db, query, ...)
  local stmt = db:prepare(query)
  stmt:bind_all(...)
  stmt:execute()
  local t = stmt:fetch_row()
  stmt:close()
  return t
end

function db_query_assoc(db, query, ...)
  local stmt = db:prepare(query)
  stmt:bind_all(...)
  stmt:execute()
  local t = stmt:fetch_assoc()
  local n = stmt:last_insert_rowid()
  if (not n) then n = stmt:rows_changed() end
  stmt:close()
  return t, n
end

--[[
if (APP_PRAGMA_APPLICATION_ID) then
    APP_PRAGMA_APPLICATION_ID = APP_PRAGMA_APPLICATION_ID - 0xffffffff -- convert to 32-bit signed integer
end
--]]

function db_is_empty(db)
  local t = db_query_row(db, "SELECT count(*) FROM sqlite_master WHERE type = 'table';")
  if (t[1] > 0) then return false end
  t = db_query_row(db, "PRAGMA application_id;")
  if (t[1] ~= 0) then return false end
  return true
end

function db_is_app(db)
  local t = db_query_row(db, "SELECT count(*) FROM sqlite_master WHERE type = 'table';")
  if (t[1] < 1) then return false end
  t = db_query_row(db, "PRAGMA application_id;")
  if (t[1] ~= APP_PRAGMA_APPLICATION_ID) then return false end
  return true
end

function db_create(db)
  dofile("db_create.lua")
end

function db_create_if_needed(db)
  if (db_is_empty(db)) then db_create(db) end
  if (not db_is_app(db)) then error("not good db") end
end

function db_conf_set(db, key, value)
  db_query_row(db, "INSERT INTO configuration (key, value) VALUES(?, ?)", key, value)
end

function db_conf_get(db, key)
  local t = db_query_row(db, "SELECT value FROM configuration WHERE key = ?", key)
  if (not t) then error("configuration key not found") end
  return t[1]
end

-- figured a way to have an upvalue for this function, instead of accessing _G. everytime.
local APP_VERBOSE_LEVEL = APP_VERBOSE_LEVEL
local function xlvl_print(lvl, format, ...)
   if (lvl > APP_VERBOSE_LEVEL) then return end
   str = string.format(format, ...)
   lvl_print(lvl, str)
end

function xprint(format, ...)
   return xlvl_print(0, format, ...)
end

function xinfo(format, ...)
   return xlvl_print(1, format, ...)
end

function xdebug(format, ...)
   return xlvl_print(2, format, ...)
end

function file_line_get(key)
   local filename = "CONF-"..key..".conf"
   local myfile = io.open(filename, "r")
   if (not myfile) then return end
   local myline = myfile:read("l")
   myfile:close()
   return myline
end

function file_line_set(name, myline)
   local filename = "CONF-"..key..".conf"
   local myfile = io.open(filename, "w+")
   if (not myfile) then return end
   myfile:write(myline)
   myfile:close()
end

function dump(d, level)
   level = level or 0
   local s = string.rep(" ", level)
   if (type(d) == "table") then
       for k, v in pairs(d) do
          if (type(v) == "table") then
              print(s .. k .. " : ")
              dump(v, level + 1)
          else
              print(s .. k .. " : " .. v)
          end
       end
   elseif (d == nil) then
      print(s .. "nil")
   else
      print(s .. d)
   end
end

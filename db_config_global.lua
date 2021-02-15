--[[
  /* - SQLITE_CONFIG_SINGLETHREAD (only one DB connection, usable by only one thread)
   *   There are no arguments to this option. This option sets the threading mode to Single-thread. In other words, it disables all mutexing and puts SQLite into a mode where it can only be used by a single thread. If SQLite is compiled with the SQLITE_THREADSAFE=0 compile-time option then it is not possible to change the threading mode from its default value of Single-thread and so sqlite3_config() will return SQLITE_ERROR if called with the SQLITE_CONFIG_SINGLETHREAD configuration option.
   *
   * - SQLITE_CONFIG_MULTITHREAD (multiple DB connection, each usable by only one thread)
   *   There are no arguments to this option. This option sets the threading mode to Multi-thread. In other words, it disables mutexing on database connection and prepared statement objects. The application is responsible for serializing access to database connections and prepared statements. But other mutexes are enabled so that SQLite will be safe to use in a multi-threaded environment as long as no two threads attempt to use the same database connection at the same time. If SQLite is compiled with the SQLITE_THREADSAFE=0 compile-time option then it is not possible to set the Multi-thread threading mode and sqlite3_config() will return SQLITE_ERROR if called with the SQLITE_CONFIG_MULTITHREAD configuration option.
   * - SQLITE_CONFIG_SERIALIZED (multiple DB connection, each usable by multiple thread) - default
   *   There are no arguments to this option. This option sets the threading mode to Serialized. In other words, this option enables all mutexes including the recursive mutexes on database connection and prepared statement objects. In this mode (which is the default when SQLite is compiled with SQLITE_THREADSAFE=1) the SQLite library will itself serialize access to database connections and prepared statements so that the application is free to use the same database connection or the same prepared statement in different threads at the same time. If SQLite is compiled with the SQLITE_THREADSAFE=0 compile-time option then it is not possible to set the Serialized threading mode and sqlite3_config() will return SQLITE_ERROR if called with the SQLITE_CONFIG_SERIALIZED configuration option.
   *
   *
   * - PRAGMA read_uncommitted = boolean;
   *   Query, set, or clear READ UNCOMMITTED isolation. The default isolation level for SQLite is SERIALIZABLE. Any process or thread can select READ UNCOMMITTED isolation, but SERIALIZABLE will still be used except between connections that share a common page and schema cache. Cache sharing is enabled using the sqlite3_enable_shared_cache() API. Cache sharing is disabled by default.
   *   A database connection in read-uncommitted mode does not attempt to obtain read-locks before reading from database tables as described above. This can lead to inconsistent query results if another database connection modifies a table while it is being read, but it also means that a read-transaction opened by a connection in read-uncommitted mode can neither block nor be blocked by any other connection.
   *   Read-uncommitted mode has no effect on the locks required to write to database tables (i.e. read-uncommitted connections must still obtain write-locks and hence database writes may still block or be blocked). Also, read-uncommitted mode has no effect on the sqlite_master locks required by the rules enumerated below (see section "Schema (sqlite_master) Level Locking").
   *
   * - PRAGMA cell_size_check = boolean;
   *   The cell_size_check pragma enables or disables additional sanity checking on database b-tree pages as they are initially read from disk. With cell size checking enabled, database corruption is detected earlier and is less likely to "spread". However, there is a small performance hit for doing the extra checks and so cell size checking is turned off by default.
   */
--]]

sqlite.config("multithread")
--[[
  // Applications that wish to avoid leaving forensic traces after content is deleted or updated should enable the secure_delete pragma prior to performing the delete or update, or else run VACUUM after the delete or update. - https://www.sqlite.org/pragma.html#pragma_secure_delete

  // This routine enables or disables the sharing of the database cache and schema data structures between connections to the same database. Sharing is enabled if the argument is true and disabled if the argument is false. - https://www.sqlite.org/c3ref/enable_shared_cache.html
--]]

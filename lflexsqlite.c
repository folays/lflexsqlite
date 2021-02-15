#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include <pthread.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <sqlite3.h>

#include "lflexsqlite.h"

#define LFSQ_FETCH_NONE		(0)
#define LFSQ_FETCH_ROW		(1 << 0)
#define LFSQ_FETCH_ASSOC	(1 << 1)
#define LFSQ_FETCH_ARRAY	(LFSQ_FETCH_ROW | LFSQ_FETCH_ASSOC)

/* https://stackoverflow.com/questions/11100435/how-do-i-create-a-class-object-in-lua-c-api-5-2 */

struct lfsq_db
{
  int finalized;
  lua_State *L;
  sqlite3 *db;
  int ref_wal_hook;
  int ref_table_stmt;
};

struct lfsq_stmt
{
  int finalized;
  struct lfsq_db *lfsq_db;
  sqlite3_stmt *stmt;
  int step;
  unsigned char is_readonly;
  unsigned int num_binds;
  unsigned int num_columns;
  int *ref_name_columns;
  int rows_changed;
  int64_t last_insert_rowid;
  struct {
    uint64_t nb_execute;
  } counters;
  struct {
    pthread_mutex_t _mtx;
    pthread_cond_t _cond;
    unsigned char fired;
  } unlock_notify;
};

static void _lfsq_config(int option)
{
  int rc = sqlite3_config(option);
  if (rc != SQLITE_OK)
    errx(1, "SQL error config %d", option);
}

static int l_lfsq_config(lua_State *L)
{
  int rc;

  const char *str = luaL_checkstring(L, 1);
  if (!strcmp(str, "multithread"))
    {
      _lfsq_config(SQLITE_CONFIG_MULTITHREAD);
      sqlite3_enable_shared_cache(1);
    }
  return 0;
}

static int l_lfsq_open(lua_State *L)
{
  struct lfsq_db *o;
  int rc;

  {
    o = lua_newuserdata(L, sizeof(*o));
    {
      memset(o, '\0', sizeof(*o));
      o->ref_wal_hook = LUA_NOREF;
    }
    luaL_getmetatable(L, "lflexsqlite.db");
    lua_setmetatable(L, -2);
  }
  const char *dbpath = luaL_checkstring(L, 1);
  o->L = L;
  rc = sqlite3_open_v2(dbpath, &o->db, SQLITE_OPEN_READWRITE | (/* flag_create */ 1 ? SQLITE_OPEN_CREATE : 0) |
		       SQLITE_OPEN_WAL |
		       0,
		       NULL);
  /* The sqlite3_extended_result_codes() routine enables or disables the extended result codes feature of SQLite.
   * The extended result codes are disabled by default for historical compatibility.
   * - https://sqlite.org/rescode.html
   * - https://sqlite.org/c3ref/extended_result_codes.html
   * - https://sqlite.org/c3ref/errcode.html
   */
  sqlite3_extended_result_codes(o->db, 1);
  { /* benefits of a "Lua weak-keyed table" (stmt -> boolean) instead of a C-table:
     *  upon close() (or __gc collection) of stmt, no need to manually remove them from the C-table.
     */
    {
      lua_newtable(L);
    }
    {
      lua_newtable(L);
      lua_pushliteral(L, "__mode");
      lua_pushliteral(L, "k");
      lua_rawset(L, -3);
    }
    lua_setmetatable(L, -2);
    o->ref_table_stmt = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  return 1;
}

static int l_lfsq_db_close(lua_State *L)
{
  struct lfsq_db *o;
  int rc;

  o = luaL_checkudata(L, 1, "lflexsqlite.db");
  if (o->finalized)
    return 0;

  if (o->ref_table_stmt != LUA_NOREF)
    {
      {
	lua_rawgeti(L, LUA_REGISTRYINDEX, o->ref_table_stmt);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0)
	  {
	    if (luaL_callmeta(L, -2, "close"))
	      lua_pop(L, 1);
	    lua_pop(L, 1);
	  }
      }
      luaL_unref(L, LUA_REGISTRYINDEX, o->ref_table_stmt);
      o->ref_table_stmt = LUA_NOREF;
    }

  if (o->ref_wal_hook != LUA_NOREF)
    {
      luaL_unref(L, LUA_REGISTRYINDEX, o->ref_wal_hook);
      o->ref_wal_hook = LUA_NOREF;
    }

  rc = sqlite3_close(o->db);
  if (rc != SQLITE_OK)
    {

    }
  o->db = NULL;
  o->finalized = 1;
  return 0;
}

static int l_lfsq_db_obj_get(lua_State *L)
{
  struct lfsq_db *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.db");
  if (o->finalized)
    return 0;

  lua_pushlightuserdata(L, o->db);
  return 1;
}

static int l_lfsq_db_obj_put(lua_State *L)
{
  struct lfsq_db *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.db");
  if (o->finalized)
    return 0;

  luaL_checktype(L, 2, LUA_TLIGHTUSERDATA);
  void *ptr = lua_touserdata(L, 2);
  if (o->db != ptr)
    errx(1, "%s : %s error", __FILE__, __func__);
  return 0;
}

static int l_lfsq_db_total_changes(lua_State *L)
{
  struct lfsq_db *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.db");
  if (o->finalized)
    return 0;

  lua_pushinteger(L, sqlite3_total_changes(o->db));
  return 1;
}

static int _lfsq_db_wal_hook(void *data, sqlite3 *db, const char *dbname, int nb_pages)
{
  struct lfsq_db *o = data;
  lua_State *L = o->L;

  lua_rawgeti(L, LUA_REGISTRYINDEX, o->ref_wal_hook);
  lua_pushstring(L, dbname);
  lua_pushinteger(L, nb_pages);
  if (lua_pcall(L, 2, 1, 0))
    {
      const char *errmsg = lua_tostring(L, -1);
      errx(1, "%s : %s error : %s", __FILE__, __func__, errmsg);
    }

  int ret = SQLITE_ERROR;
  switch (lua_type(L, -1))
    {
    case LUA_TBOOLEAN:
      ret = lua_toboolean(L, -1) ? SQLITE_OK : SQLITE_ERROR;
      break;
    case LUA_TNUMBER:
      ret = lua_tonumber(L, -1);
      break;
    default:
      errx(1, "%s : %s error : user wal hook returned an incorrect value", __FILE__, __func__);
    }
  lua_pop(L, 1);
  return ret;
}

static int l_lfsq_db_wal_hook(lua_State *L)
{
  struct lfsq_db *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.db");
  if (o->finalized)
    return 0;

  luaL_checktype(L, 2, LUA_TFUNCTION);
  o->ref_wal_hook = luaL_ref(L, LUA_REGISTRYINDEX);
  sqlite3_wal_hook(o->db, _lfsq_db_wal_hook, o);
  return 0;
}

static int l_lfsq_db_wal_checkpoint(lua_State *L)
{
  struct lfsq_db *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.db");
  if (o->finalized)
    return 0;

  const char *dbname = luaL_checkstring(L, 2);
  int pnLog = -1, pnCkpt = -1;
  int ret = sqlite3_wal_checkpoint_v2(o->db, dbname, SQLITE_CHECKPOINT_PASSIVE, &pnLog, &pnCkpt);
  switch (ret)
    {
    case SQLITE_OK:
    case SQLITE_BUSY:
      lua_pushinteger(L, ret);
      lua_pushinteger(L, pnLog);
      lua_pushinteger(L, pnCkpt);
      return 3;
    default:
      return luaL_error(L, "sqlite3_wal_checkpoint_v2 error : %s\n", sqlite3_errstr(ret));
    }
  return 0;
}

static int l_lfsq_db_prepare(lua_State *L)
{
  struct lfsq_db *o;
  struct lfsq_stmt *o_stmt;
  const char *str;
  size_t len;
  int rc;
  int i;

  o = luaL_checkudata(L, 1, "lflexsqlite.db");
  if (o->finalized)
    {
      lua_pushnil(L);
      return 1;
    }
  str = luaL_checklstring(L, 2, &len);
  {
    o_stmt = lua_newuserdata(L, sizeof(*o_stmt));
    memset(o_stmt, '\0', sizeof(*o_stmt));
    luaL_getmetatable(L, "lflexsqlite.stmt");
    lua_setmetatable(L, -2);
    {
      lua_rawgeti(L, LUA_REGISTRYINDEX, o->ref_table_stmt);
      lua_pushvalue(L, -2);
      lua_pushboolean(L, -1);
      lua_rawset(L, -3);
      lua_pop(L, 1);
    }
  }
  o_stmt->lfsq_db = o;
  rc = sqlite3_prepare_v2(o->db, str, len, &o_stmt->stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return luaL_error(L, "sqlite3_prepare_* error : %s", sqlite3_errstr(rc));
    }
  o_stmt->is_readonly = sqlite3_stmt_readonly(o_stmt->stmt) ? 1 : 0;
  o_stmt->num_binds = sqlite3_bind_parameter_count(o_stmt->stmt);
  o_stmt->num_columns = sqlite3_column_count(o_stmt->stmt);
  {
    int i;

    o_stmt->ref_name_columns = calloc(o_stmt->num_columns, sizeof(*o_stmt->ref_name_columns));
    for (i = 0; i < o_stmt->num_columns; ++i)
      {
	lua_pushstring(L, sqlite3_column_name(o_stmt->stmt, i));
	o_stmt->ref_name_columns[i] = luaL_ref(L, LUA_REGISTRYINDEX);
      }
  }
  {
    pthread_mutex_init(&o_stmt->unlock_notify._mtx, NULL);
    pthread_cond_init(&o_stmt->unlock_notify._cond, NULL);
  }
  return 1;
}

static int l_lfsq_stmt_close(lua_State *L)
{
  struct lfsq_stmt *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  {
    pthread_mutex_destroy(&o->unlock_notify._mtx);
    pthread_cond_destroy(&o->unlock_notify._cond);
  }
  {
    int i;

    for (i = 0; i < o->num_columns; ++i)
      {
	luaL_unref(L, LUA_REGISTRYINDEX, o->ref_name_columns[i]);
	o->ref_name_columns[i] = LUA_NOREF;
      }
    if (o->ref_name_columns)
      {
	free(o->ref_name_columns);
	o->ref_name_columns = NULL;
      }
  }

  sqlite3_finalize(o->stmt);
  o->stmt = NULL;
  o->finalized = 1;
  return 0;
}

static int l_lfsq_stmt_num_binds(lua_State *L)
{
  struct lfsq_stmt *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  lua_pushinteger(L, o->num_binds);
  return 1;
}

static int _lfsq_stmt_bind(struct lfsq_stmt *o, lua_State *L, int n, int index)
{
  if (!(n >= 0 && n < o->num_binds))
    return luaL_error(L, "invalid bind parameted #%d", n + 1);

  int ret = SQLITE_ERROR;

  luaL_checkany(L, index);
  switch (lua_type(L, index))
    {
    case LUA_TNUMBER:
      {
	if (!lua_isinteger(L, index))
	  {
	    double val_n;

	    val_n = luaL_checknumber(L, index);
	    ret = sqlite3_bind_double(o->stmt, n + 1, val_n);
	    /* printf("BIND double [%d] to %lf\n", n+1, val_n); */
	  }
	else
	  {
	    int64_t val_n;

	    val_n = luaL_checkinteger(L, index);
	    ret = sqlite3_bind_int64(o->stmt, n + 1, val_n);
	    /* printf("BIND int64_t [%d] to %ld\n", n+1, val_n); */
	  }
      }
      break;
    case LUA_TSTRING:
      {
	const char *str;
	size_t len;

	str = luaL_checklstring(L, index, &len);
	ret = sqlite3_bind_text(o->stmt, n + 1, str, len, SQLITE_TRANSIENT);
      }
      break;
    /* case LUA_TLIGHTUSERDATA: */
    /*   { */
    /*   } */
    default:
      return luaL_error(L, "invalid bind parameter #%d, Lua type %s not yet handled",
			n + 1, lua_typename(L, lua_type(L, index)));
    }

  if (ret != SQLITE_OK)
    {
      return luaL_error(L, "sqlite3_bind_* error : %s", sqlite3_errstr(ret));
    }

  lua_pushboolean(L, 1);
  return 1;
}

static int l_lfsq_stmt_bind(lua_State *L)
{
  struct lfsq_stmt *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  int n = luaL_checkinteger(L, 2);
  return _lfsq_stmt_bind(o, L, n - 1, 3);
}

static int l_lfsq_stmt_bind_all(lua_State *L)
{
  struct lfsq_stmt *o;
  int i;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  int n = lua_gettop(L) - 1;
  if (n != o->num_binds)
    return luaL_error(L, "invalid number of bind parameter #%d (expected %d)",
		       n, o->num_binds);

  for (i = 0; i < o->num_binds; ++i)
    {
      int ret = _lfsq_stmt_bind(o, L, i, i + 2);
      if (!(lua_isboolean(L, -1) && lua_toboolean(L, -1)))
	return ret;
      lua_pop(L, ret);
    }

  lua_pushboolean(L, 1);
  return 1;
}

/* - https://www.sqlite.org/c3ref/unlock_notify.html
 * There may be more than one blocked connection that has registered for an unlock-notify callback.
 * [...] the callback is invoked once with the set of void* context pointers specified by the blocked
 *  connections bundled together into an array.
 */
static void _lfsq__unlock_notify(void **apArg, int nArg)
{
  int i;

  for (i = 0; i < nArg; ++i)
    {
      struct lfsq_stmt *o = apArg[i];

      pthread_mutex_lock(&o->unlock_notify._mtx);
      o->unlock_notify.fired = 1;
      pthread_cond_signal(&o->unlock_notify._cond);
      pthread_mutex_unlock(&o->unlock_notify._mtx);
    }

}

static int _lfsq_sqlite3_step(struct lfsq_stmt *o, lua_State *L)
{
  int rc;

  while ((rc = sqlite3_step(o->stmt)) != SQLITE_OK)
    {
      if (rc != SQLITE_LOCKED_SHAREDCACHE)
	break;

      /* - https://www.sqlite.org/c3ref/unlock_notify.html
       * > an SQLITE_LOCKED error may only occur on the first call to sqlite3_step()
       */
      o->unlock_notify.fired = 0;

      if (sqlite3_unlock_notify(o->lfsq_db->db, _lfsq__unlock_notify, o) != SQLITE_OK)
	{
	  return luaL_error(L, "sqlite3_unlock_notify error");
	}
      pthread_mutex_lock(&o->unlock_notify._mtx);
      while (o->unlock_notify.fired != 1)
	{
	  pthread_cond_wait(&o->unlock_notify._cond, &o->unlock_notify._mtx);
	}
      pthread_mutex_unlock(&o->unlock_notify._mtx);

      sqlite3_reset(o->stmt);
    }
  if (rc == SQLITE_DONE && !o->is_readonly)
    {
      o->rows_changed = sqlite3_changes(o->lfsq_db->db);
      /* try to catch "ON CONFLICT IGNORE" */
      if (o->rows_changed != 0)
	o->last_insert_rowid = sqlite3_last_insert_rowid(o->lfsq_db->db);
      else
	o->last_insert_rowid = 0;
    }
  return rc;
}

static int _lfsq_stmt_reset(struct lfsq_stmt *o, lua_State *L)
{
  int rc = sqlite3_reset(o->stmt);
  sqlite3_clear_bindings(o->stmt);
  switch (rc)
    {
    case SQLITE_OK:
      break;
    case SQLITE_CONSTRAINT:
    case SQLITE_CONSTRAINT_UNIQUE:
      break;
    default:
      return luaL_error(L, "sqlite3_reset error : %s", sqlite3_errstr(rc));
    }
  return 0;
}

static int l_lfsq_stmt_execute(lua_State *L)
{
  struct lfsq_stmt *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  //_lfsq_stmt_reset(o, L);

  if (!o->is_readonly)
    {
      /* needs sqlite3 >= 3.18 ... to catch "ON CONFLICT IGNORE"
       * otherwise, last_insert_rowid() is indistinguishable between a
       *  SQLITE_DONE and a (ignored) conflit...
       */
      //sqlite3_set_last_insert_rowid(o->lfsq_db, 0);
    }
  o->rows_changed = 0;
  o->last_insert_rowid = 0;

  o->step = _lfsq_sqlite3_step(o, L);
  switch (o->step)
    {
    case SQLITE_DONE:
      lua_pushinteger(L, 0);
      break;
    case SQLITE_ROW:
      lua_pushinteger(L, 1);
      break;
    default:
      lua_pushnil(L);
    }

  ++o->counters.nb_execute;

  return 1;
}

static int l_lfsq_stmt_reset(lua_State *L)
{
  struct lfsq_stmt *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  return _lfsq_stmt_reset(o, L);
}

static int _l_lfsq_stmt_fetch_column(struct lfsq_stmt *o, lua_State *L, int n)
{
  switch (sqlite3_column_type(o->stmt, n))
    {
      /* sqlite3_column_value(o->stmt, n + 0) -> The column result as an unprotected sqlite3_value object. */
    case SQLITE_INTEGER:
      lua_pushinteger(L, sqlite3_column_int64(o->stmt, n + 0));
      break;
    case SQLITE_FLOAT:
      lua_pushnumber(L, sqlite3_column_double(o->stmt, n + 0));
      break;
    case SQLITE_BLOB:
      lua_pushnil(L);
      break;
    case SQLITE_NULL:
      lua_pushnil(L);
      break;
    case SQLITE3_TEXT:
      lua_pushstring(L, sqlite3_column_text(o->stmt, n + 0));
      break;
    default:
      errx(1, "fail switch column type");
    }
}

static int _l_lfsq_stmt_fetch(lua_State *L, int result_type)
{
  struct lfsq_stmt *o;
  int i;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  if (o->step != SQLITE_ROW)
    {
      lua_pushnil(L);
      return 1;
    }

  if (result_type != LFSQ_FETCH_NONE)
    {
      unsigned int n = lua_gettop(L);
      if (n < 2)
	lua_newtable(L);
      else
	{
	  luaL_checktype(L, 2, LUA_TTABLE);
	  lua_pushvalue(L, 2);
	}
    }

  for (i = 0; i < o->num_columns; ++i)
    {
      _l_lfsq_stmt_fetch_column(o, L, i);
      if (result_type & LFSQ_FETCH_ROW)
	{
	  lua_pushinteger(L, i + 1);
	  lua_pushvalue(L, -2);
	  lua_rawset(L, -4);
	}
      if (result_type & LFSQ_FETCH_ASSOC)
	{
	  lua_rawgeti(L, LUA_REGISTRYINDEX, o->ref_name_columns[i]);
	  lua_pushvalue(L, -2);
	  lua_rawset(L, -4);
	}
      if (result_type != LFSQ_FETCH_NONE)
	lua_pop(L, 1);
    }

  o->step = _lfsq_sqlite3_step(o, L);

  return result_type == LFSQ_FETCH_NONE ? o->num_columns : 1;
}

static int l_lfsq_stmt_fetch(lua_State *L)
{
  return _l_lfsq_stmt_fetch(L, 0);
}

static int l_lfsq_stmt_fetch_row(lua_State *L)
{
  return _l_lfsq_stmt_fetch(L, LFSQ_FETCH_ROW);
}

static int l_lfsq_stmt_fetch_assoc(lua_State *L)
{
  return _l_lfsq_stmt_fetch(L, LFSQ_FETCH_ASSOC);
}

static int l_lfsq_stmt_fetch_array(lua_State *L)
{
  return _l_lfsq_stmt_fetch(L, LFSQ_FETCH_ARRAY);
}

static int l_lfsq_stmt_rows_changed(lua_State *L)
{
  struct lfsq_stmt *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  lua_pushinteger(L, o->rows_changed);
  return 1;
}

static int l_lfsq_stmt_last_insert_rowid(lua_State *L)
{
  struct lfsq_stmt *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  if (o->last_insert_rowid == 0)
    lua_pushnil(L);
  else
    lua_pushinteger(L, o->last_insert_rowid);
  return 1;
}

static int l_lfsq_stmt_counters_get(lua_State *L)
{
  struct lfsq_stmt *o;

  o = luaL_checkudata(L, 1, "lflexsqlite.stmt");
  if (o->finalized)
    return 0;

  lua_newtable(L);

  {
    lua_pushliteral(L, "fullscan_step");
    lua_pushinteger(L, sqlite3_stmt_status(o->stmt, SQLITE_STMTSTATUS_FULLSCAN_STEP, 0));
    lua_rawset(L, -3);
  }
  {
    lua_pushliteral(L, "sort");
    lua_pushinteger(L, sqlite3_stmt_status(o->stmt, SQLITE_STMTSTATUS_SORT, 0));
    lua_rawset(L, -3);
  }
  {
    lua_pushliteral(L, "autoindex");
    lua_pushinteger(L, sqlite3_stmt_status(o->stmt, SQLITE_STMTSTATUS_AUTOINDEX, 0));
    lua_rawset(L, -3);
  }
  {
    lua_pushliteral(L, "vm_step");
    lua_pushinteger(L, sqlite3_stmt_status(o->stmt, SQLITE_STMTSTATUS_VM_STEP, 0));
    lua_rawset(L, -3);
  }
  {
    lua_pushliteral(L, "nb_execute");
    lua_pushinteger(L, o->counters.nb_execute);
    lua_rawset(L, -3);
  }

  return 1;
}

static const struct luaL_Reg lflexsqlite_mylib[] =
  {
    {"config", l_lfsq_config},
    {"open", l_lfsq_open},
    {NULL, NULL} /* sentinel */
  };

static const struct luaL_Reg lflexsqlite_mylib_db[] =
  {
    {"close", l_lfsq_db_close},
    {"__gc", l_lfsq_db_close},
    {"total_changes", l_lfsq_db_total_changes},
    {"wal_hook", l_lfsq_db_wal_hook},
    {"wal_checkpoint", l_lfsq_db_wal_checkpoint},
    {"prepare", l_lfsq_db_prepare},
    {"obj_get", l_lfsq_db_obj_get},
    {"obj_put", l_lfsq_db_obj_put},
    {NULL, NULL} /* sentinel */
  };

static const struct luaL_Reg lflexsqlite_mylib_stmt[] =
  {
    {"close", l_lfsq_stmt_close},
    {"__gc", l_lfsq_stmt_close},
    {"num_binds", l_lfsq_stmt_num_binds},
    {"bind", l_lfsq_stmt_bind},
    {"bind_all", l_lfsq_stmt_bind_all},
    {"execute", l_lfsq_stmt_execute},
    {"reset", l_lfsq_stmt_reset},
    {"fetch", l_lfsq_stmt_fetch},
    {"fetch_row", l_lfsq_stmt_fetch_row},
    {"fetch_assoc", l_lfsq_stmt_fetch_assoc},
    {"fetch_array", l_lfsq_stmt_fetch_array},
    {"rows_changed", l_lfsq_stmt_rows_changed},
    {"last_insert_rowid", l_lfsq_stmt_last_insert_rowid},
    {"counters_get", l_lfsq_stmt_counters_get},
    {NULL, NULL} /* sentinel */
  };

int flex_register_lflexsqlite_cclosures(lua_State *L)
{
  luaL_checkversion(L);
  if (*lua_version(L) < 503.0f)
    errx(1, "%s : Lua < 5.3", __func__);

  luaL_newlib(L, lflexsqlite_mylib);
  {
    /* luaL_openlib(L, NULL, lflexsqlite_mylib_db, 0); */
    luaL_newmetatable(L, "lflexsqlite.db");
    luaL_setfuncs(L, lflexsqlite_mylib_db, 0);
    lua_setfield(L, -1, "__index");
  }
  {
  /* luaL_openlib(L, NULL, lflexsqlite_mylib_stmt, 0); */
    luaL_newmetatable(L, "lflexsqlite.stmt");
    luaL_setfuncs(L, lflexsqlite_mylib_stmt, 0);
    lua_setfield(L, -1, "__index");
  }
  return 1;
}

int luaopen_lflexsqlite(lua_State *L)
{
  return flex_register_lflexsqlite_cclosures(L);
}


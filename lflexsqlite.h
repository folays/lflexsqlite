#ifndef __LFLEXSQLITE_H__
# define __LFLEXSQLITE_H__

#include <lua.h>

int flex_register_lflexsqlite_cclosures(lua_State *L);
int luaopen_lflexsqlite(lua_State *L);

#endif /* !__LFLEXSQLITE_H__ */

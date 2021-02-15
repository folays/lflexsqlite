Lua Flex SQLite, a quality implementation of a Lua binding for prepared SQLite statements.

This library could been thread-safe, provided that you initialize it correctly.

This work is an abandonned state, but the main component (the `lflexsqlite.c`) should be very clean code.

This file implements the whole Lua<->SQLite binding.

All others file are example of usage, and getters/setters helpers.

Use it at your own risk.

```
$ apt-get install -y bmake lua5.3 liblua5.3-dev libsqlite3-dev
```

```
$ bmake
gcc -o lflexsqlite.so -shared lflexsqlite.o -rdynamic -lbacktrace -llua5.3 -lsqlite3 -lpthread -Wl,-rpath .
```

100k INSERTs below (with 1 transaction / per insert):
```
$ rm -f test.db  ; time lua main.lua

WAL_AUTOPCHECKPOINT [ret 0] (100001/100001 frames checkpointed/total)
WAL_AUTOPCHECKPOINT [ret 0] (100001/100001 frames checkpointed/total)
 nb : 3
 name : bonjour
 nb : 7
 name : salut
 nb : 20000
 name : bli_20000
 nb : 40000
 name : bli_40000
 nb : 60000
 name : bli_60000
 nb : 80000
 name : bli_80000
 nb : 100000
 name : bli_100000
 nil

real	0m4,284s
```

100k INSERTs below (with `BEGIN TRANSACTION` enabled around the whole insertions):
```
real	0m0,411s
```

1M INSERTs below (with `BEGIN TRANSACTION` enabled around the whole insertions)::
```
real	0m3,552s
```

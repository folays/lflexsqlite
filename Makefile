NAME	= lflexsqlite.so
CC	= gcc
RM	= rm -f

debug		= 0

DEBUG_CFLAGS	= -O0
DEBUG_LFLAGS	= -rdynamic -lbacktrace

LUA_CFLAGS	!= pkg-config --cflags lua5.3
LUA_LIBS	!= pkg-config --libs lua5.3

SQLITE_CFLAGS	!= pkg-config --cflags sqlite3
SQLITE_LIBS	!= pkg-config --libs sqlite3

CFLAGS	= $(DEBUG_CFLAGS) $($CFLAGS)
CFLAGS	+= $(CURL_CFLAGS) $(LUA_CFLAGS) -g -ggdb -Werror -fPIC -DFLEX_VERBOSE=$(debug)
CFLAGS	+= -Wmissing-prototypes -Wimplicit-function-declaration -Wuninitialized -Wmaybe-uninitialized

LFLAGS	= $(DEBUG_LFLAGS)
LFLAGS	+= $(LUA_LIBS) $(SQLITE_LIBS) -lpthread

SRC	= lflexsqlite.c

OBJ	= $(SRC:.c=.o)

all	: $(NAME)

re	: fclean all

clean	:
	-$(RM) $(OBJ) *.o *~

fclean	: clean
	-$(RM) $(NAME)

$(NAME)	: $(OBJ)
	$(CC) -o $(NAME) -shared $(.ALLSRC) $(LFLAGS) -Wl,-rpath .

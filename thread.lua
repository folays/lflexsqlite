
function db_prepare(query)
   query = string.match(query, "^(.*[^\n])\n?$") -- remove final \n
   query = string.gsub(query, "\n", " ") -- replace \n by space
   local stmt = db:prepare(query)
   return stmt
end

stmt_insert = db_prepare([[INSERT INTO test (name, nb) VALUES(?,?)]])

stmt_insert:bind_all("bonjour", 3)
stmt_insert:execute()
stmt_insert:reset()

stmt_insert:bind_all("salut", 7)
stmt_insert:execute()
stmt_insert:reset()

db_execute(db, "BEGIN TRANSACTION")

for i = 100,1000000 do
    stmt_insert:bind_all("bli_"..i, i)
    stmt_insert:execute()
    stmt_insert:reset()
end

db_execute(db, "COMMIT")

stmt_select = db_prepare([[SELECT name, nb FROM test WHERE nb <= ? OR (nb % ?) == 0]])

stmt_select:bind_all(7,20000)
stmt_select:execute()
repeat
   local row = stmt_select:fetch_assoc()
--   print(row)
   dump(row, 1)
until row == nil
stmt_select:close()

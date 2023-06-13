# SQLite DB schema

 * Three tables:

   - "users" with only one column, "ID", which is the primary key (we may
     want to add more columns later, such as name etc.)

   - "doors" with only one column, "ID", which is the primary key (we may
     want to add more columns later, such as office name, building, floor
     etc.). In particular, we might add a boolean column "anybody" which
     means "any valid user can enter", so that we do not need to add one
     line for each user to the "auth" table in this case

   - "auth" with two columns: "userID" and "doorID", which are foreign keys
     (we may want to add more columns later, such as allowed hours etc)

 * To create:
   ```
   create table users(ID int primary key);
   create table doors(ID int primary key);
   create table auth(userID int not null, doorID int not null, foreign key (userID) references users(ID), foreign key (doorID) references doors(ID));
   create index useridx on auth(userID, doorID);
   ```

 * To query:
   ```
   select exists(select * from auth where userID=? and doorID=?);
   ```

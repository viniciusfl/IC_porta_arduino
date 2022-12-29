#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <sqlite3.h>
#include <common.h>
#include <RTClib.h>
#include <dbmanager.h>

#define RETRY_DOWNLOAD_TIME 60000

#define DOWNLOAD_INTERVAL 1200000

#include <sqlite3.h>

namespace DBNS
{
    // This is a wrapper around SQLite which allows us
    // to query whether a user is authorized to enter.
    class Authorizer
    {
    public:
        void init();
        int openDB(const char *filename);
        void closeDB();
        bool userAuthorized(int readerID, unsigned long cardID);

    private:
        sqlite3 *sqlitedb;
        sqlite3_stmt *dbquery;
        void generateLog(unsigned long cardID, int readerID, bool authorized);
    };

    // This is an auxiliary class to UpdateDBManager. It receives one byte
    // at a time to prevent blocking and writes what is received to disk,
    // minus the HTTP headers.
    class FileWriter
    {
    public:
        void open(const char *);
        void write(const byte);
        void close();

    private:
        File file;
        const static int netLineBufferSize = 40;
        byte netLineBuffer[netLineBufferSize];
        int position = 0;
        char previous;
        bool headerDone = false;
        bool beginningOfLine = true;
    };

    // This class periodically downloads a new version of the database
    // and sets this new version as the "active" db file in the Authorizer
    // when downloading is successful. It alternates between two filenames
    // to do its thing and, during startup, identifies which of them is
    // the current one by means of the "timestamp" file.
    class UpdateDBManager
    {
    public:
        void init(Authorizer *);
        void update();

    private:
        const char *currentFile;
        const char *otherFile;
        const char *currentTimestampFile;
        const char *otherTimestampFile;

        void chooseInitialFile();
        void swapFiles();
        bool checkFileFreshness(const char *);

        const char *SERVER = "10.0.2.106";
        unsigned long lastDownloadTime = 0;
        bool downloading = false; // Is there an ongoing DB update?
        void startDownload();
        void finishDownload();

        Authorizer *authorizer;
        WiFiClient client;
        FileWriter writer;
    };

    // This should be called from setup()
    void UpdateDBManager::init(Authorizer *authorizer)
    {
        if (!SD.begin())
        {
            Serial.println("Card Mount Failed, aborting");
            Serial.flush();
            while (true)
                delay(10);
#ifdef DEBUG
        }
        else
        {
            Serial.println("SD connected.");
#endif
        }

        this->authorizer = authorizer;

        chooseInitialFile();
    }

    // This should be called from loop()
    // At each call, we determine the current state we are in, perform
    // a small chunk of work, and return. This means we do not hog the
    // processor and can pursue other tasks while updating the DB.
    void UpdateDBManager::update()
    {
        //FIXME: what if download stops working in the middle and never ends?

        // We start a download only if we are not already downloading
        if (!downloading)
        {
            // millis() wraps every ~49 days, but
            // wrapping does not cause problems here
            // TODO: we might prefer to use dates,
            //       such as "at midnight"
            //
            // ANWSER: we can use an alarm with RTC 1307
            // https://robojax.com/learn/arduino/?vid=robojax_DS1307-clock-alarm
            // I believe the nodeMCU RTC might also have this feature

            if (currentMillis - lastDownloadTime > DOWNLOAD_INTERVAL)
                startDownload();
            return;
        }

        // If we did not return above, we are downloading
        if (!client.available() && !client.connected())
        {
            finishDownload();
            return;
        }

        // If we did not disconnect above, we are connected
        if (client.available())
        {
            char c = client.read();
            writer.write(c);
        }
        return;
    }

    void UpdateDBManager::startDownload()
    {
        Serial.println("started download...");
        // If WiFI is disconnected, pretend nothing
        // ever happened and try again later
/*         if (WiFi.status() != WL_CONNECTION_LOST){
            Serial.println("No internet available, canceling db update.");
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            return;
        }  */

        // TODO: use HTTPS, check certificates etc.
        client.connect(SERVER, 80);

#ifdef DEBUG
        if (client.connected())
        {
            Serial.println("Connected to server.");
        }
        else
        {
            Serial.println("Connection to server failed.");
        }
#endif

        // If connection failed, pretend nothing
        // ever happened and try again later
        if (!client.connected())
        {
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            client.stop();
            return;
        }

        downloading = true;

        client.println("GET /dataBaseIME.db HTTP/1.1");
        client.println(((String) "Host: ") + SERVER);
        client.println("Connection: close");
        client.println();

        // remove old DB files
        SD.remove(otherTimestampFile);
        SD.remove(otherFile);

        writer.open(otherFile);
    }

    void UpdateDBManager::finishDownload()
    {
        client.flush();
        client.stop();
        writer.close();
        downloading = false;
        lastDownloadTime = currentMillis;

        // FIXME: we should only save the timestamp etc.
        //        if the download was successful
        // QUESTION: how can i know that?

        swapFiles();

#ifdef DEBUG
        Serial.println("Disconnecting from server and finishing db update.");
#endif

        authorizer->closeDB();
        if (authorizer->openDB(currentFile) != SQLITE_OK)
        {
#ifdef DEBUG
            Serial.println("Error opening the updated DB, reverting to old one");
#endif
            swapFiles();
            // FIXME: in the unlikely event that this fails too, we are doomed
            authorizer->openDB(currentFile);
        }
    }

    void UpdateDBManager::swapFiles()
    {
        const char *tmp = currentFile;
        currentFile = otherFile;
        otherFile = tmp;
        tmp = currentTimestampFile;
        currentTimestampFile = otherTimestampFile;
        otherTimestampFile = tmp;

        // If we crash before these 5 lines, on restart we will continue
        // using the old version of the DB; if we crash after, we will
        // use the new version of the DB. If we crash in the middle (with
        // both files containing "1"), on restart we will use "bancoA.db",
        // which may be either.
        File f = SD.open(currentTimestampFile, FILE_WRITE);
        f.print(1);
        f.close();

        SD.remove(otherTimestampFile);
    }

    bool UpdateDBManager::checkFileFreshness(const char *tsfile)
    {
        // In some exceptional circumstances, we might end up writing
        // "1" to the file more than once; that's ok, 11 > 0 too :) .
        File f = SD.open(tsfile);
        int t = 0;
        if (f)
        {
            int t = f.parseInt(); // If reading fails, this returns 0
        }
        f.close();
        return t > 0;
    }

    void UpdateDBManager::chooseInitialFile()
    {
        currentFile = "/bancoA.db";
        otherFile = "/bancoB.db";
        currentTimestampFile = "/TSA.TXT";
        otherTimestampFile = "/TSB.TXT";

        if (!checkFileFreshness(currentTimestampFile))
        {
            if (!checkFileFreshness(otherTimestampFile))
            {
#ifdef DEBUG
                Serial.printf("Downloading DB for the first time...");
#endif
                startDownload();
            }
            else
            {
                swapFiles();
            }
        }

#ifdef DEBUG
        Serial.printf("Choosing %s as current DB.\n", currentFile);
#endif
        authorizer->openDB(currentFile);
    }

    void FileWriter::open(const char *filename)
    {
        file = SD.open(filename, FILE_WRITE);
        if(!file)
        { 
        Serial.println("Error openning DB file.");
        }
  
        headerDone = false;
        beginningOfLine = true;
        netLineBuffer[0] = 0;
        position = 0;
        previous = 0;

#ifdef DEBUG
        Serial.print("Writing to ");
        Serial.println(filename);
#endif
    }

    // TODO: String is probably slow and might cause problems
    //       with binary data; we should use a ring buffer.
    void FileWriter::write(const byte c)
    {
        if (headerDone)
        {
            netLineBuffer[position] = c;
            position++;
            if (position >= netLineBufferSize)
            {
#ifdef DEBUG
                Serial.println((String) "Writing " + position + " bytes to db....");
                for (int i = 0; i < netLineBufferSize; i++)
                {
                    Serial.print((char) netLineBuffer[i]);
                    file.print((char) netLineBuffer[i]);
                }
#endif
                for (int i = 0; i < netLineBufferSize; i++){
                    file.print((char) netLineBuffer[i]);
                }
                position = 0;
            }
        }
        else
        {
            if (c == '\n')
            {
                if (beginningOfLine && previous == '\r')
                {
                    headerDone = true;
#ifdef DEBUG
                    Serial.println("Header done!");
#endif
                }
                else
                {
                    previous = 0;
                }
                beginningOfLine = true;
            }
            else
            {
                previous = c;
                if (c != '\r')
                    beginningOfLine = false;
            }
        }
    }

    void FileWriter::close()
    {
        for (int i = 0; i < position; i++)
        {
            file.print((char) netLineBuffer[i]);
        }
#ifdef DEBUG
        Serial.println((String) "Writing " + position + " bytes to db....");
#endif
        file.close();
    }

    // This should be called from setup()
    void Authorizer::init()
    {
        sqlitedb = NULL; // check the comment near Authorizer::closeDB()
        dbquery = NULL;
        sqlite3_initialize();
    }

    int Authorizer::openDB(const char *filename)
    {
        closeDB();
        String name = (String) "/sd" + filename; // FIXME: this is bad

        char buff[sizeof(name)];
        name.toCharArray(buff, sizeof(name));

        int rc = sqlite3_open(buff, &sqlitedb);
        if (rc != SQLITE_OK)
        {
            Serial.printf("Can't open database: %s\n", sqlite3_errmsg(sqlitedb));
        }
        else
        {

 
#ifdef DEBUG
            Serial.printf("Opened database successfully %s\n", filename);
#endif
            rc = sqlite3_prepare_v2(sqlitedb,
                                    "SELECT EXISTS(SELECT * FROM auth WHERE userID=? AND doorID=?)",
                                    -1, &dbquery, NULL);

            if (rc != SQLITE_OK)
            {
                Serial.printf("Can't generate prepared statement: \n");
                Serial.printf("%s: %s\n", sqlite3_errstr(sqlite3_extended_errcode(sqlitedb)), sqlite3_errmsg(sqlitedb));
#ifdef DEBUG
            }
            else
            {
                Serial.printf("Prepared statement created\n");
#endif
            }
        }

        return rc;
    }

    // search element through current database
    bool Authorizer::userAuthorized(int readerID, unsigned long cardID)
    {
        //FIXME: reading card during DB arrises error: unsopported message format
        if (sqlitedb == NULL)
            return false;

#ifdef DEBUG
        Serial.print("Card reader ");
        Serial.print(readerID);
        Serial.println(" was used.");
        Serial.print("We received -> ");
        Serial.println(cardID);
#endif
        sqlite3_int64 card = cardID;
        sqlite3_reset(dbquery);
        sqlite3_bind_int64(dbquery, 1, card);
        sqlite3_bind_int(dbquery, 2, doorID); //FIXME
        // should i verify errors while binding?

        bool authorized = false;
        int rc = sqlite3_step(dbquery);
        while (rc == SQLITE_ROW)
        {
            if (1 == sqlite3_column_int(dbquery, 0))
                authorized = true;
            rc = sqlite3_step(dbquery);
        }

        if (rc != SQLITE_DONE)
        {
            Serial.printf("Error querying DB: %s\n", sqlite3_errmsg(sqlitedb));
        }

        //generateLog(cardID, readerID, authorized);

        if (authorized)
        {
            authorized = false;
            return true;
        }
        else
        {
            return false;
        }
    }

    void Authorizer::generateLog(unsigned long cardID, int readerID, bool authorized)
    {
        //FIXME: organize and optmize

        // get unix time
        time_t now;
        time(&now);
        unsigned long systemtime = now;

        const char* filename = "/sd/log"; // FIXME: this is bad
        int rc = sqlite3_open(filename, &sqlitedb);
        if (rc != SQLITE_OK)
        {
            Serial.printf("Can't open database: %s\n", sqlite3_errmsg(sqlitedb));
        }
        else
        {
#ifdef DEBUG
            Serial.printf("Opened database successfully\n");
#endif

            rc = sqlite3_prepare_v2(sqlitedb,
                                    "INSERT INTO log(cardID, doorID, readerID, unixTimestamp, authorized) VALUES(?, ?, ?, ?, ?)",
                                    -1, &dbquery, NULL);

            if (rc != SQLITE_OK)
            {
                Serial.printf("Can't generate prepared statement: %s\n",
                              sqlite3_errmsg(sqlitedb));
#ifdef DEBUG
            }
            else
            {
                Serial.printf("Prepared statement created\n");
#endif
            }
        }

        // should i verify errors while binding?
        sqlite3_reset(dbquery);
        sqlite3_bind_int(dbquery, 1, cardID);
        sqlite3_bind_int(dbquery, 2, doorID); 
        sqlite3_bind_int(dbquery, 3, readerID); 
        sqlite3_bind_int(dbquery, 4, systemtime); 
        sqlite3_bind_int(dbquery, 5, authorized); 

        while (rc == SQLITE_ROW)
        {
            rc = sqlite3_step(dbquery);
        }

        if (rc != SQLITE_DONE)
        {
            Serial.printf("Error querying DB: %s\n", sqlite3_errmsg(sqlitedb));
        }


#ifdef  DEBUG
        Serial.println("generating log");
#endif

        File log = SD.open("/log.txt", FILE_APPEND);
        if (!log)
        {
            Serial.println(" couldnt open log file...");
        }

        closeDB();
    }

    // The sqlite3 docs say "The C parameter to sqlite3_close(C) and
    // sqlite3_close_v2(C) must be either a NULL pointer or an sqlite3
    // object pointer [...] and not previously closed". So, we always
    // make it NULL here to avoid closing a pointer previously closed.
    void Authorizer::closeDB()
    {
        sqlite3_finalize(dbquery);
        dbquery = NULL;
        sqlite3_close_v2(sqlitedb);
        sqlitedb = NULL;
    }

    Authorizer authorizer;
    UpdateDBManager updateDBManager;
}

void initDB()
{
    DBNS::authorizer.init();
    DBNS::updateDBManager.init(&DBNS::authorizer);
}

void updateDB()
{
    DBNS::updateDBManager.update();
}

bool userAuthorized(int readerID, unsigned long cardID)
{
    return DBNS::authorizer.userAuthorized(readerID, cardID);
}

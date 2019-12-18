#if _AUTH_SERVER_ // defined in cmake files

#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/user_preferences.pb.h>
#include <google/protobuf/map.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/value.h>
#include <fstream>
#include <iostream>
#include <sstream>

#define _XOPEN_SOURCE_
#include <bson.h>
#include <json-c/json.h>
#include <mongoc.h>
#include <stdio.h>
#include <unistd.h>
#include "Carta.h"
#include "EventHeader.h"

void ConnectToMongoDB() {
    mongoc_client_t* client;
    mongoc_collection_t* collection;
    mongoc_cursor_t* cursor;
    const bson_t* doc;
    bson_t* query;
    char* str;
    char user[50];
    std::string start_string;
    Json::Value json_config;
    Json::Reader reader;
    std::ostringstream stringStream;

    mongoc_init();

    cuserid(user);

    stringStream << CARTA::mongo_db_contact_string << "?appname=carta_backend-" << user;
    start_string = stringStream.str();
    client = mongoc_client_new(start_string.c_str());
    collection = mongoc_client_get_collection(client, "CARTA", "userconf");

    query = bson_new();
    BSON_APPEND_UTF8(query, "username", user);
    cursor = mongoc_collection_find_with_opts(collection, query, nullptr, nullptr);

    while (mongoc_cursor_next(cursor, &doc)) {
        str = bson_as_canonical_extended_json(doc, NULL);
        reader.parse(str, json_config);
        CARTA::token = json_config["token"].asString();
        bson_free(str);
    }

    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client);
    mongoc_cleanup();
}

bool SaveLayoutToDB(const std::string& name, const std::string& json_string) {
    const char* uri_string = "mongodb://localhost:27017";
    mongoc_uri_t* uri;
    mongoc_client_t* client;
    mongoc_database_t* database;
    mongoc_collection_t* collection;
    bson_t *command, reply, *insert, layout, *query;
    bson_error_t error;
    bool retval;
    char* str;
    char user[50];

    mongoc_init();

    std::cerr << " SaveLayoutToDB " << std::endl;

    uri = mongoc_uri_new_with_error(uri_string, &error);
    if (!uri) {
        fprintf(stderr,
            "failed to parse URI: %s\n"
            "error message:       %s\n",
            uri_string, error.message);
        return EXIT_FAILURE;
    }

    client = mongoc_client_new_from_uri(uri);
    if (!client) {
        return EXIT_FAILURE;
    }

    mongoc_client_set_appname(client, "carta_backend");

    database = mongoc_client_get_database(client, "CARTA");
    collection = mongoc_client_get_collection(client, "CARTA", "layouts");

    command = BCON_NEW("ping", BCON_INT32(1));
    retval = mongoc_client_command_simple(client, "admin", command, NULL, &reply, &error);
    if (!retval) {
        fprintf(stderr, "%s\n", error.message);
        return EXIT_FAILURE;
    }

    str = bson_as_json(&reply, NULL);

    cuserid(user);

    insert = bson_new();

    bson_init(&layout);
    BSON_APPEND_UTF8(&layout, "username", user);
    BSON_APPEND_UTF8(&layout, "name", name.c_str());
    BSON_APPEND_UTF8(&layout, "json_string", json_string.c_str());

    if (!mongoc_collection_insert_one(collection, &layout, NULL, NULL, &error)) {
        fprintf(stderr, "%s\n", error.message);
    }

    bson_destroy(insert);
    bson_destroy(&reply);
    bson_destroy(command);
    //  bson_free (str);

    mongoc_collection_destroy(collection);
    mongoc_database_destroy(database);
    mongoc_uri_destroy(uri);
    mongoc_client_destroy(client);
    mongoc_cleanup();

    return true;
}

//
bool GetLayoutsAndProfilesFromDB(
    std::vector<std::tuple<std::string, std::string> >* layouts, std::vector<std::tuple<std::string, std::string> >* profiles) {
    //  std::vector< std::tuple<std::string, std::string> > * vec = new std::vector< std::tuple<std::string, std::string> >;
    const char* uri_string = "mongodb://localhost:27017";
    mongoc_uri_t* uri;
    mongoc_client_t* client;
    mongoc_database_t* database;
    mongoc_collection_t* collection;
    mongoc_cursor_t* cursor;
    bson_t *command, reply, *insert, userconf, *query;
    const bson_t* doc;
    bson_error_t error;
    char* str;
    char* uvalue = NULL;
    bool retval;
    char user[50];

    std::cerr << "\n ******** GetLayoutsAndProfilesFromDB  *********\n" << std::endl;

    mongoc_init();

    uri = mongoc_uri_new_with_error(uri_string, &error);
    if (!uri) {
        fprintf(stderr,
            "failed to parse URI: %s\n"
            "error message:       %s\n",
            uri_string, error.message);
        return false;
    }

    client = mongoc_client_new_from_uri(uri);
    if (!client) {
        return false;
    }

    mongoc_client_set_appname(client, "carta_backend");

    query = bson_new();
    database = mongoc_client_get_database(client, "CARTA");
    collection = mongoc_client_get_collection(client, "CARTA", "layouts");

    command = BCON_NEW("ping", BCON_INT32(1));
    retval = mongoc_client_command_simple(client, "admin", command, NULL, &reply, &error);
    if (!retval) {
        fprintf(stderr, "%s\n", error.message);
        return false;
    }
    str = bson_as_json(&reply, NULL);

    cuserid(user);

    fprintf(stderr, " User is %s\n", user);

    query = bson_new();
    BSON_APPEND_UTF8(query, "username", user);
    cursor = mongoc_collection_find_with_opts(collection, query, NULL, NULL);

    CARTA::RegisterViewerAck message;
    struct json_object* parsed_json;
    struct json_object* layout_name;
    struct json_object* layout_str;

    const char* char_layout_name;
    const char* char_layout_str;
    while (mongoc_cursor_next(cursor, &doc)) {
        str = bson_as_canonical_extended_json(doc, NULL);
        parsed_json = json_tokener_parse(str);
        json_object_object_get_ex(parsed_json, "name", &layout_name);
        json_object_object_get_ex(parsed_json, "json_string", &layout_str);
        char_layout_name = json_object_get_string(layout_name);
        char_layout_str = json_object_get_string(layout_str);

        printf("LAYOUT:\n\tNAME: %s\n\tSTR %s\n", char_layout_name, char_layout_str);

        (*(message.mutable_user_layouts()))[char_layout_name] = char_layout_str;
    }
    bson_destroy(query);

    collection = mongoc_client_get_collection(client, "CARTA", "preferences");

    query = bson_new();
    BSON_APPEND_UTF8(query, "username", user);
    cursor = mongoc_collection_find_with_opts(collection, query, NULL, NULL);

    if (mongoc_cursor_next(cursor, &doc)) {
        str = bson_as_canonical_extended_json(doc, NULL);
        printf("GET layouts %s\n", str);

        // need to ad this to list. XXXXX
        //    message.user_preferences ...
    }
    bson_destroy(query);

    return true;
}

bool SaveUserPreferencesToDB(const CARTA::SetUserPreferences& request) {
    const char* uri_string = "mongodb://localhost:27017";
    mongoc_uri_t* uri;
    mongoc_client_t* client;
    mongoc_database_t* database;
    mongoc_collection_t* collection;
    bson_t *command, reply, *insert, prefs, *query;
    bson_error_t error;
    bool retval;
    char* str;
    char user[50];

    std::cerr << " **** SaveUserPreferencesToDB  ****\n" << std::endl;

    mongoc_init();

    uri = mongoc_uri_new_with_error(uri_string, &error);
    if (!uri) {
        fprintf(stderr,
            "failed to parse URI: %s\n"
            "error message:       %s\n",
            uri_string, error.message);
        return EXIT_FAILURE;
    }

    client = mongoc_client_new_from_uri(uri);
    if (!client) {
        return EXIT_FAILURE;
    }

    mongoc_client_set_appname(client, "carta_backend");

    database = mongoc_client_get_database(client, "CARTA");
    collection = mongoc_client_get_collection(client, "CARTA", "preferences");

    command = BCON_NEW("ping", BCON_INT32(1));
    retval = mongoc_client_command_simple(client, "admin", command, NULL, &reply, &error);
    if (!retval) {
        fprintf(stderr, "%s\n", error.message);
        return EXIT_FAILURE;
    }

    str = bson_as_json(&reply, NULL);

    cuserid(user);

    for (auto& pair : request.preference_map()) {
        std::cerr << pair.first << " " << pair.second << std::endl;
        if (pair.second.empty()) {
            // Remove this pair from the DB;
            /*
            doc = bson_new();
            //	BSON_APPEND_OID (doc, "_id", &oid);
            //     BSON_APPEND_UTF8 (doc,  pair.first, "");;

            if (!mongoc_collection_delete_one (collection, prefs, doc, NULL, NULL, &error)) {
              fprintf (stderr, "Delete failed: %s\n", error.message);
            }
            bson_destroy (doc);
            */
        } else {
            // Add this pair to the DB.
            insert = bson_new();
            BSON_APPEND_UTF8(&prefs, "username", user);
            BSON_APPEND_UTF8(&prefs, pair.first.c_str(), pair.second.c_str());
            if (!mongoc_collection_insert_one(collection, &prefs, NULL, NULL, &error)) {
                fprintf(stderr, "%s\n", error.message);
            }
        }
    }

    return true;
}

#endif // _AUTH_SERVER_

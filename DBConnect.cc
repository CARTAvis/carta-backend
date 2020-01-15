#if _AUTH_SERVER_ // defined in cmake files

#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/user_preferences.pb.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
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
#include "DBConnect.h"
#include "EventHeader.h"

bool initMongoDB(mongoc_database_t** database, mongoc_client_t** client, mongoc_collection_t** collection, const char* collname) {
    mongoc_uri_t* uri;
    bson_error_t error;
    std::string uri_string;

    GetMongoURIString(uri_string);

    mongoc_init();

    uri = mongoc_uri_new_with_error(uri_string.c_str(), &error);
    if (!uri) {
        fmt::print(
            "failed to parse URI: {}\n"
            "error message:       {}\n",
            uri_string, error.message);
        return false;
    }

    *client = mongoc_client_new_from_uri(uri);
    if (!*client) {
        return false;
    }

    mongoc_uri_destroy(uri);

    mongoc_client_set_appname(*client, "carta_backend");
    *database = mongoc_client_get_database(*client, "CARTA");
    *collection = mongoc_client_get_collection(*client, "CARTA", collname);

    return true;
}

void ConnectToMongoDB() {
    mongoc_database_t* database;
    mongoc_client_t* client;
    mongoc_collection_t* collection;
    mongoc_cursor_t* cursor;
    const bson_t* doc;
    bson_t* query;
    char* str;
    char user[16];
    std::string start_string;
    Json::Value json_config;
    Json::Reader reader;
    std::ostringstream stringStream;

    initMongoDB(&database, &client, &collection, "layouts");

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
    mongoc_client_t* client;
    mongoc_database_t* database;
    mongoc_collection_t* collection;
    bson_t layout;
    char user[16];
    bson_error_t error;
    bool result = true;

    initMongoDB(&database, &client, &collection, "layouts");

    cuserid(user);

    bson_init(&layout);
    BSON_APPEND_UTF8(&layout, "username", user);
    BSON_APPEND_UTF8(&layout, "name", name.c_str());

    if (!json_string.empty()) {
        if (!mongoc_collection_delete_one(collection, &layout, NULL, NULL, &error)) {
            fmt::print("Delete failed: {}", error.message);
            result = false;
        }
    } else {
        BSON_APPEND_UTF8(&layout, "json_string", json_string.c_str());
        if (!mongoc_collection_insert_one(collection, &layout, NULL, NULL, &error)) {
            fmt::print("{}", error.message);
            result = false;
        }
    }

    mongoc_collection_destroy(collection);
    mongoc_database_destroy(database);
    mongoc_client_destroy(client);
    mongoc_cleanup();

    return result;
}

bool GetLayoutsFromDB(CARTA::RegisterViewerAck* ack_message_ptr) {
    mongoc_client_t* client;
    mongoc_database_t* database;
    mongoc_collection_t* collection;
    mongoc_cursor_t* cursor;
    bson_t* query;
    const bson_t* doc;
    char* str;
    char user[16];

    initMongoDB(&database, &client, &collection, "layouts");

    database = mongoc_client_get_database(client, "CARTA");
    collection = mongoc_client_get_collection(client, "CARTA", "layouts");

    cuserid(user);

    query = bson_new();
    BSON_APPEND_UTF8(query, "username", user);
    cursor = mongoc_collection_find_with_opts(collection, query, NULL, NULL);

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
        (*(ack_message_ptr->mutable_user_layouts()))[char_layout_name] = char_layout_str;
    }
    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    mongoc_database_destroy(database);
    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client);
    mongoc_cleanup();

    return true;
}

bool GetPreferencesFromDB(CARTA::RegisterViewerAck* ack_message_ptr) {
    mongoc_client_t* client;
    mongoc_database_t* database;
    mongoc_collection_t* collection;
    mongoc_cursor_t* cursor;
    bson_t* query;
    const bson_t* doc;
    char* str;
    char user[16];

    initMongoDB(&database, &client, &collection, "preferences");

    database = mongoc_client_get_database(client, "CARTA");

    collection = mongoc_client_get_collection(client, "CARTA", "preferences");

    cuserid(user);

    query = bson_new();
    BSON_APPEND_UTF8(query, "username", user);
    cursor = mongoc_collection_find_with_opts(collection, query, NULL, NULL);

    struct json_object* parsed_json;
    struct json_object* pref_str;

    const char* char_pref_name;
    const char* char_pref_str;
    const char* cstr;

    bson_iter_t iter;
    while (mongoc_cursor_next(cursor, &doc)) {
        str = bson_as_canonical_extended_json(doc, NULL);
        parsed_json = json_tokener_parse(str);
        if (bson_iter_init(&iter, doc)) {
            while (bson_iter_next(&iter)) {
                cstr = bson_iter_key(&iter);
                if (((strcmp(cstr, "_id")) && strcmp(cstr, "username"))) {
                    json_object_object_get_ex(parsed_json, cstr, &pref_str);
                    char_pref_name = cstr;
                    char_pref_str = json_object_get_string(pref_str);
                    (*(ack_message_ptr->mutable_user_preferences()))[char_pref_name] = char_pref_str;
                }
            }
        }
    }

    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client);
    mongoc_database_destroy(database);
    mongoc_cleanup();

    return true;
}

bool SaveUserPreferencesToDB(const CARTA::SetUserPreferences& request) {
    mongoc_client_t* client;
    mongoc_database_t* database;
    mongoc_collection_t* collection;
    bson_error_t error;
    char user[16];
    bool result = true;

    initMongoDB(&database, &client, &collection, "preferences");

    cuserid(user);

    for (auto& pair : request.preference_map()) {
        bson_t* doc;

        if (pair.second.empty()) {
            // Remove this pair from the DB;
            doc = bson_new();
            BSON_APPEND_UTF8(doc, pair.first.c_str(), pair.second.c_str());
            const bson_t* doc1 = (const bson_t*)doc;
            if (!mongoc_collection_delete_one(collection, doc1, NULL, NULL, &error)) {
                fmt::print("Delete failed: {}", error.message);
                result = false;
            }
            bson_destroy(doc);
        } else {
            // Add this pair to the DB.
            doc = bson_new();
            BSON_APPEND_UTF8(doc, "username", user);
            BSON_APPEND_UTF8(doc, pair.first.c_str(), pair.second.c_str());
            if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error)) {
                fmt::print("{}", error.message);
                result = false;
            }
            bson_free(doc);
        }
    }
    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client);
    mongoc_database_destroy(database);
    mongoc_cleanup();

    return result;
}

#endif // _AUTH_SERVER_

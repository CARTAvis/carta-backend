

#if _AUTH_SERVER_ // defined in cmake files

#include <jsoncpp/json/json.h>
#include <jsoncpp/json/value.h>
#include <fstream>
#include <iostream>
#include <sstream>

#include <bson.h>
#include <mongoc.h>
#include <stdio.h>

#include "Carta.h"

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

#endif // _AUTH_SERVER_

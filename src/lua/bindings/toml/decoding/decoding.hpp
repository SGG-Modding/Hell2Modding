#ifndef DECODING_H
#define DECODING_H

#include <cstddef>
#include <lua/bindings/toml/DataTypes/DateAndTime/dateAndTime.hpp>
#include <lua/bindings/toml/Options.hpp>
#include <toml.hpp>

/// Convert `luaTable` into a `toml::table`.
void tomlToLuaTable(toml::table* table, sol::table& lTable, Options options);

void insertNodeInTable(sol::table& luaTable, std::string* key, size_t* index, toml::node* value, Options options);

void tomlArrayToLuaArray(toml::array& tomlArray, sol::table& luaTable, Options options);

#endif /* DECODING_H */

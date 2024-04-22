#ifndef ENCODING_H
#define ENCODING_H

#include <cstddef>
#include <lua/bindings/toml/utilities/utilities.hpp>
#include <toml.hpp>

/// Convert `LuaTable` into a `toml::table`.
toml::table* tomlTableFromLuaTable(sol::table luaTable);
toml::array* tomlArrayFromLuaArray(sol::table luaArray);
toml::node* luaValueToTomlNode(sol::object& luaValue, toml::value_flags* flags);

#endif /* ENCODING_H */

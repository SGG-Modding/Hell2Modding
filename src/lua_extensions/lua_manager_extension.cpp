#include "lua_manager_extension.hpp"

#include "bindings/gui_ext.hpp"
#include "bindings/hades/audio.hpp"
#include "bindings/hades/data.hpp"
#include "bindings/hades/inputs.hpp"
#include "bindings/hades/lz4.hpp"
#include "bindings/lpeg.hpp"
#include "bindings/luasocket/luasocket.hpp"
#include "bindings/paths_ext.hpp"
#include "bindings/tolk/tolk.hpp"
#include "lua_module_ext.hpp"
#include <hooks/hooking.hpp>
#include <hades2/pdb_symbol_map.hpp>

std::wstring utf8_to_wstring(const std::string &utf8_str);

namespace big::lua_manager_extension
{
	static void delete_everything()
	{
		std::scoped_lock l(g_manager_mutex);

		lua::hades::inputs::vanilla_key_callbacks.clear();

		g_is_lua_state_valid = false;

		g_lua_manager_instance.reset();

		LOG(INFO) << "state is no longer valid!";
	}

	static int the_state_is_going_down(lua_State* L)
	{
		delete_everything();

		return 0;
	}

	void init_lua_manager(sol::state_view& state, sol::table& lua_ext)
	{
		init_lua_state(state, lua_ext);
		init_lua_api(state, lua_ext);
	}

	static int open_debug_lib(lua_State* L)
	{
		luaL_requiref(L, "_rom_debug", luaopen_debug, 1 /*Leaves a copy of the module on the stack.*/);

		// Number of elements on the stack.
		return 1;
	}

	typedef luaL_Stream LStream;

	static LStream *newprefile(lua_State *L)
	{
		LStream *p = (LStream *)lua_newuserdata(L, sizeof(LStream));
		p->closef  = NULL; /* mark file handle as 'closed' */
		luaL_setmetatable(L, LUA_FILEHANDLE);
		return p;
	}

#define tolstream(L) ((LStream *)luaL_checkudata(L, 1, LUA_FILEHANDLE))

	static int io_fclose(lua_State *L)
	{
		LStream *p = tolstream(L);
		int res    = fclose(p->f);
		return luaL_fileresult(L, (res == 0), NULL);
	}

	static LStream *newfile(lua_State *L)
	{
		LStream *p = newprefile(L);
		p->f       = NULL;
		p->closef  = &io_fclose;
		return p;
	}

#if !defined(lua_checkmode)

	/*
** Check whether 'mode' matches '[rwa]%+?b?'.
** Change this macro to accept other modes for 'fopen' besides
** the standard ones.
*/
	#define lua_checkmode(mode)                                                                                     \
		(*mode != '\0' && strchr("rwa", *(mode++)) != NULL && (*mode != '+' || ++mode) && /* skip if char is '+' */ \
		 (*mode != 'b' || ++mode) &&                                                      /* skip if char is 'b' */ \
		 (*mode == '\0'))

#endif

	static int io_open_utf8(lua_State* L)
	{
		MessageBoxA(0, "sdf", "sdfdsf", 0);

		const char *filename = luaL_checkstring(L, 1);
		const char *mode     = luaL_optstring(L, 2, "r");
		LStream *p           = newfile(L);
		const char *md       = mode; /* to traverse/check mode */
		luaL_argcheck(L, lua_checkmode(md), 2, "invalid mode");
		p->f = _wfopen(utf8_to_wstring(filename).c_str(), utf8_to_wstring(mode).c_str());
		return (p->f == NULL) ? luaL_fileresult(L, 0, filename) : 1;
	}

	typedef struct LoadF
	{
		int n;                      /* number of pre-read characters */
		FILE *f;                    /* file being read */
		char buff[LUAL_BUFFERSIZE]; /* area for reading file */
	} LoadF;

	static int errfile(lua_State *L, const char *what, int fnameindex)
	{
		const char *serr     = strerror(errno);
		const char *filename = lua_tostring(L, fnameindex) + 1;
		lua_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
		lua_remove(L, fnameindex);
		return LUA_ERRFILE;
	}

	static int skipBOM(LoadF *lf)
	{
		const char *p = "\xEF\xBB\xBF"; /* Utf8 BOM mark */
		int c;
		lf->n = 0;
		do
		{
			c = getc(lf->f);
			if (c == EOF || c != *(const unsigned char *)p++)
			{
				return c;
			}
			lf->buff[lf->n++] = c; /* to be read by the parser */
		} while (*p != '\0');
		lf->n = 0;          /* prefix matched; discard it */
		return getc(lf->f); /* return next character */
	}

	static int skipcomment(LoadF *lf, int *cp)
	{
		int c = *cp = skipBOM(lf);
		if (c == '#')
		{ /* first line is a comment (Unix exec. file)? */
			do
			{ /* skip first line */
				c = getc(lf->f);
			} while (c != EOF && c != '\n');
			*cp = getc(lf->f); /* skip end-of-line, if present */
			return 1;          /* there was a comment */
		}
		else
		{
			return 0; /* no comment */
		}
	}

	static const char *getF(lua_State *L, void *ud, size_t *size)
	{
		LoadF *lf = (LoadF *)ud;
		(void)L; /* not used */
		if (lf->n > 0)
		{                  /* are there pre-read characters to be read? */
			*size = lf->n; /* return them (chars already in buffer) */
			lf->n = 0;     /* no more pre-read characters */
		}
		else
		{ /* read a block from file */
			/* 'fread' can return > 0 *and* set the EOF flag. If next call to
       'getF' called 'fread', it might still wait for user input.
       The next check avoids this problem. */
			if (feof(lf->f))
			{
				return NULL;
			}
			*size = fread(lf->buff, 1, sizeof(lf->buff), lf->f); /* read block */
		}
		return lf->buff;
	}

	static int hook_luaL_loadfilex(lua_State *L, const char *filename, const char *mode)
	{
		LoadF lf;
		int status, readstatus;
		int c;
		int fnameindex = lua_gettop(L) + 1; /* index of filename on the stack */
		if (filename == NULL)
		{
			lua_pushliteral(L, "=stdin");
			lf.f = stdin;
		}
		else
		{
			lua_pushfstring(L, "@%s", filename);
			lf.f = _wfopen(utf8_to_wstring(filename).c_str(), L"r");
			if (lf.f == NULL)
			{
				return errfile(L, "open", fnameindex);
			}
		}
		if (skipcomment(&lf, &c)) /* read initial portion */
		{
			lf.buff[lf.n++] = '\n'; /* add line to correct line numbers */
		}
		if (c == LUA_SIGNATURE[0] && filename)
		{                                         /* binary file? */
			lf.f = freopen(filename, "rb", lf.f); /* reopen in binary mode */
			if (lf.f == NULL)
			{
				return errfile(L, "reopen", fnameindex);
			}
			skipcomment(&lf, &c); /* re-read initial portion */
		}
		if (c != EOF)
		{
			lf.buff[lf.n++] = c; /* 'c' is the first character of the stream */
		}
		status     = lua_load(L, getF, &lf, lua_tostring(L, -1), mode);
		readstatus = ferror(lf.f);
		if (filename)
		{
			fclose(lf.f); /* close file (even in case of errors) */
		}
		if (readstatus)
		{
			lua_settop(L, fnameindex); /* ignore results from `lua_load' */
			return errfile(L, "read", fnameindex);
		}
		lua_remove(L, fnameindex);
		return status;
	}

	void init_lua_state(sol::state_view& state, sol::table& lua_ext)
	{
		// Register our cleanup functions when the state get destroyed.
		{
			sol::table my_takedown_metatable                           = state.create_table_with();
			my_takedown_metatable[sol::meta_function::garbage_collect] = the_state_is_going_down;
			sol::table my_takedown_table                               = lua_ext.create_named(
                std::format("..{}\xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4", rom::g_project_name),
                sol::metatable_key,
                my_takedown_metatable);
		}

		// clang-format off
		state.open_libraries(
			sol::lib::package,
			sol::lib::os,
			sol::lib::debug,
			sol::lib::io);
		// clang-format on

		lua_pushcfunction(state.lua_state(), open_debug_lib);
		lua_setglobal(state.lua_state(), "_rom_open_debug");

		auto L = state.lua_state();

		// Push io table onto the stack
		lua_getglobal(L, "io");      // stack: io
		lua_getfield(L, -1, "open"); // stack: io, io.open

		// Now the function is at the top of the stack (-1)
		if (!lua_isfunction(L, -1))
		{
			LOG(ERROR) << "io.open not a function";
		}

		lua_CFunction ptr = lua_tocfunction(L, -1);
		if (ptr)
		{
			LOG(INFO) << "Hooking some fopen usage";

			static auto hook_ = big::hooking::detour_hook_helper::add_now<io_open_utf8>("io_open_utf8", ptr);
			static auto hook2 =
			    big::hooking::detour_hook_helper::add_now<hook_luaL_loadfilex>("hook_luaL_loadfilex for utf8", big::hades2_symbol_to_address["luaL_loadfilex"]);
		}
		else
		{
			LOG(INFO) << "io.open is a Lua function/closure, no C pointer available\n";
		}
	}

	void init_lua_api(sol::state_view& state, sol::table& lua_ext)
	{
		auto on_import_table = lua_ext.create_named("on_import");

		// Lua API: Function
		// Table: on_import
		// Name: pre
		// Param: function: function: signature (string file_name, current_ENV_for_this_import) return nil or _ENV
		// The passed function will be called before the game loads a .lua script from the game's Content/Scripts folder.
		// The _ENV returned (if not nil) by the passed function gives you a way to define the _ENV of this lua script.
		on_import_table.set_function("pre",
		                             [](sol::protected_function f, sol::this_environment env)
		                             {
			                             auto mod = (lua_module_ext*)lua_module::this_from(env);
			                             if (mod)
			                             {
				                             mod->m_data_ext.m_on_pre_import.push_back(f);
			                             }
		                             });

		// Lua API: Function
		// Table: on_import
		// Name: post
		// Param: function: function: signature (string file_name)
		// The passed function will be called after the game loads a .lua script from the game's Content/Scripts folder.
		on_import_table.set_function("post",
		                             [](sol::protected_function f, sol::this_environment env)
		                             {
			                             auto mod = (lua_module_ext*)lua_module::this_from(env);
			                             if (mod)
			                             {
				                             mod->m_data_ext.m_on_post_import.push_back(f);
			                             }
		                             });

		// Let's keep that list sorted the same as the solution file explorer
		lua::hades::audio::bind(lua_ext);
		lua::hades::data::bind(state, lua_ext);
		lua::hades::inputs::bind(state, lua_ext);
		lua::hades::lz4::bind(lua_ext);
		lua::luasocket::bind(lua_ext);
		lua::tolk::bind(lua_ext);
		lua::gui_ext::bind(lua_ext);
		lua::lpeg::bind(lua_ext);
		lua::paths_ext::bind(lua_ext);
	}
} // namespace big::lua_manager_extension

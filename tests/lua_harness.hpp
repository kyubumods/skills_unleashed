// A LuaJIT with SkillsUnleashed's sources on its require path and a recording stub of `kyubu`, for the unit tests of
// mods-gameplay/skills_unleashed. No game is needed.
//
// Modules are found the way the bundler names them: "skills_unleashed.lib.fx" is <src>/lib/fx.lua. The Lua state is a plain
// LuaJIT (with the FFI), so the tests run the same code the game's LuaJIT runs; the game's calls are what `kyubu` fakes:
//   kyubu.logs        { {level =, text =}, ... }          from kyubu.info / kyubu.warn / kyubu.error
//   kyubu.defs        id -> the table given to kyubu.ability.define
//   kyubu.handlers    event -> { fn, ... }                from kyubu.on (which returns an id); kyubu.fire(event, ...) calls them
//   kyubu.interfaces  name -> a cdata pointer the test puts there (kyubu.interface returns it)
//   kyubu.symbols     name -> an address                  (kyubu.symbol)
//   kyubu.hooks       name -> fn                          (kyubu.hook_mid)
//   kyubu.fhooks      name -> { ctype =, fn = }           (kyubu.hook: a function hook, fn(next, ...)); kyubu.unhook(id) takes a
//                     function hook out again and notes the id in kyubu.unhooked
//   kyubu.channel     register (kept in .handlers), send (recorded in .sent), SERVER
//   kyubu.model       add (recorded in .added), slot (reads .slots)
//   kyubu.animation   bind, bind_for (in .bound)     kyubu.game   ability_end (recorded in .ended); light_spawn, light_set,
//                     light_remove (the live lights in .lights, by id; .light_calls counts the calls); creature_find gives a
//                     handle { id } for any id, whose get() reports the flags of .unstoppable and .flags; creature_set_flags
//                     records { id, flags } in .flag_calls and keeps the flags in .flags (by id)
#pragma once

#include <lua.hpp>

#include <stdexcept>
#include <string>

#ifndef SU_MOD_DIR
#error "SU_MOD_DIR must be the mods-gameplay/skills_unleashed folder (forward slashes, no trailing slash)"
#endif
#ifndef SU_TEST_DIR
#error "SU_TEST_DIR must be the mods-gameplay/tests/skills_unleashed folder"
#endif

namespace su_test {

inline const char* const kPrelude = R"LUA(
local MOD = ...
package.path = ""
package.cpath = ""
local searchers = package.loaders or package.searchers
table.insert(searchers, 2, function(name)
  local rest = name:match("^skills_unleashed%.(.+)$")
  if not rest then return "\n\tnot a skills_unleashed module" end
  local path = MOD .. "/src/" .. rest:gsub("%.", "/") .. ".lua"
  local fn, err = loadfile(path)
  if not fn then return "\n\t" .. tostring(err) end
  return fn
end)

kyubu = { logs = {}, defs = {}, handlers = {}, interfaces = {}, symbols = {}, hooks = {}, fhooks = {}, MOD_DIR = MOD }
local function log(level) return function(text) kyubu.logs[#kyubu.logs + 1] = { level = level, text = tostring(text) } end end
kyubu.info, kyubu.warn, kyubu.error = log("info"), log("warn"), log("error")
kyubu.next_on_id = 1
function kyubu.on(event, fn)
  kyubu.handlers[event] = kyubu.handlers[event] or {}
  table.insert(kyubu.handlers[event], fn)
  local id = kyubu.next_on_id
  kyubu.next_on_id = id + 1
  return id
end
function kyubu.fire(event, ...)
  for _, fn in ipairs(kyubu.handlers[event] or {}) do fn(...) end
end
kyubu.ability = {
  define = function(id, def) kyubu.defs[id] = def; return true end,
}
kyubu.animation = {                                -- kyubu.animation.bind records the clips bound, in order, in .bound
  bound = {},                                        -- bind_for too, as { id, clip or false, creature id }
  bind = function(id, clip) kyubu.animation.bound[#kyubu.animation.bound + 1] = { id, clip }; return true end,
  bind_for = function(id, creature, clip)
    kyubu.animation.bound[#kyubu.animation.bound + 1] = { id, clip or false, creature }
    return true
  end,
}
kyubu.game = {                                     -- kyubu.game.ability_end records the creatures it was asked to end in .ended
  ended = {},
  ability_end = function(c) kyubu.game.ended[#kyubu.game.ended + 1] = c; return true end,
  cooldowns = {},                                    -- ability_cooldown_set records { creature, ability, ms } here
  ability_cooldown_set = function(c, a, ms) kyubu.game.cooldowns[#kyubu.game.cooldowns + 1] = { c, a, ms }; return true end,
  unstoppable = {},                                  -- game id -> true: creature_find(id):get().flags.unstoppable (loader API 1.3)
  flags = {}, flag_calls = {},                       -- game id -> the table creature_set_flags was last given (API 1.4: rooted)
  creature_find = function(id)
    return { id = id, get = function()
      local f = kyubu.game.flags[id] or {}
      return { flags = { unstoppable = kyubu.game.unstoppable[id] == true or f.unstoppable == true,
                         invulnerable = f.invulnerable == true, rooted = f.rooted == true } }
    end }
  end,
  addr_of_id = {},                                   -- game id -> address: creature() and world_of fill it, creature_state_* use it
  creature_state_get = function(h)                   -- API 1.4: pos (units), velocity, knock, stun_ms and (10.7) the hitbox, read
    local a = kyubu.game.addr_of_id[h.id]            -- from the fake creature at the game's offsets
    if not a then return nil, "gone" end
    local p, f = ffi.cast("int64_t*", a + 0x10), ffi.cast("float*", a + 0x80)
    local v, k = ffi.cast("float*", a + 0x34), ffi.cast("float*", a + 0x4C)
    return { pos = { x = tonumber(p[0]), y = tonumber(p[1]), z = tonumber(p[2]) }, box = { x = f[0], y = f[1], z = f[2] },
             velocity = { x = v[0], y = v[1], z = v[2] }, knock = { x = k[0], y = k[1], z = k[2] },
             stun_ms = ffi.cast("int32_t*", a + 0x12C)[0] }
  end,
  -- creature_state_set writes velocity, knock (each part given) and stun_ms into the fake creature, and records { id, t } in
  -- .state_calls. With .client_owns set (an address) it acts as the client side of Cube.exe: any other creature is refused, as the
  -- loader's KY_E_SIDE.
  state_calls = {},
  creature_state_set = function(h, t)
    local g = kyubu.game
    local a = g.addr_of_id[h.id]
    if not a then return nil, "gone" end
    if g.client_owns and a ~= g.client_owns then return nil, "the server side owns this creature" end
    g.state_calls[#g.state_calls + 1] = { id = h.id, t = t }
    for key, off in pairs({ velocity = 0x34, knock = 0x4C }) do
      local vec = t[key]
      if vec then
        local q = ffi.cast("float*", a + off)
        if vec.x then q[0] = vec.x end
        if vec.y then q[1] = vec.y end
        if vec.z then q[2] = vec.z end
      end
    end
    if t.stun_ms then ffi.cast("int32_t*", a + 0x12C)[0] = t.stun_ms end
    return true
  end,
  creature_set_flags = function(h, t)
    local g = kyubu.game
    g.flag_calls[#g.flag_calls + 1] = { id = h.id, flags = t }
    g.flags[h.id] = t
    return true
  end,
  lights = {}, light_calls = 0, next_light = 1,      -- the loader's lights: id -> the table last given
  light_spawn = function(t)
    local g = kyubu.game
    g.light_calls = g.light_calls + 1
    local id = g.next_light; g.next_light = id + 1
    g.lights[id] = t
    return id
  end,
  light_set = function(id, t)
    local g = kyubu.game
    g.light_calls = g.light_calls + 1
    if not g.lights[id] then return nil, "no light " .. id .. " of this mod" end
    g.lights[id] = t
    return true
  end,
  light_remove = function(id)
    local g = kyubu.game
    g.light_calls = g.light_calls + 1
    if not g.lights[id] then return nil, "no light " .. id .. " of this mod" end
    g.lights[id] = nil
    return true
  end,
}
kyubu.model = {                                    -- kyubu.model.add records, slot answers what a test put in `slots`
  added = {}, slots = {},
  add = function(id, file) kyubu.model.added[#kyubu.model.added + 1] = { id, file }; return 1 end,
  slot = function(id) return kyubu.model.slots[id] end,
}
function kyubu.interface(name, version) return kyubu.interfaces[name] end
function kyubu.symbol(name) return kyubu.symbols[name] end
function kyubu.hook_mid(name, fn) kyubu.hooks[name] = fn; return 1 end
kyubu.unhooked, kyubu.fhook_ids, kyubu.next_hook_id = {}, {}, 2
function kyubu.hook(name, ctype, fn)
  local id = kyubu.next_hook_id
  kyubu.next_hook_id = id + 1
  kyubu.fhooks[name] = { ctype = ctype, fn = fn, id = id }
  kyubu.fhook_ids[id] = name
  return id
end
function kyubu.unhook(id)
  kyubu.unhooked[#kyubu.unhooked + 1] = id
  local name = kyubu.fhook_ids[id]
  if name and kyubu.fhooks[name] and kyubu.fhooks[name].id == id then kyubu.fhooks[name] = nil end
  return true
end
kyubu.channel = {
  SERVER = "server", LOCAL_CLIENT = 1, ALL_CLIENTS = "all", handlers = {}, sent = {},
  register = function(name, fn) kyubu.channel.handlers[name] = fn; return true end,
  -- .loopback = true: a message to SERVER is also handed at once to this VM's handler of the channel, from LOCAL_CLIENT (a
  -- single-player game with the client and the server module in one test VM)
  send = function(name, to, msg)
    local ch = kyubu.channel
    ch.sent[#ch.sent + 1] = { name = name, to = to, msg = msg }
    if ch.loopback and to == ch.SERVER and ch.handlers[name] then ch.handlers[name](ch.LOCAL_CLIENT, msg) end
    return true
  end,
}
function kyubu.read_file(path)                -- a file of the mod's folder, as the loader's read_package_file (nil when missing)
  local f = io.open(MOD .. "/" .. path, "rb")
  if not f then return nil end
  local data = f:read("*a")
  f:close()
  return data
end
function kyubu.log_count(level)
  local n = 0
  for _, l in ipairs(kyubu.logs) do if level == nil or l.level == level then n = n + 1 end end
  return n
end
)LUA";

class Vm {
public:
    Vm() : L_(luaL_newstate()) {
        if (!L_) throw std::runtime_error("cannot create a Lua state");
        luaL_openlibs(L_);
        if (luaL_loadstring(L_, kPrelude) != 0) fail("prelude");
        lua_pushstring(L_, SU_MOD_DIR);
        if (lua_pcall(L_, 1, 0, 0) != 0) fail("prelude");
    }
    ~Vm() { lua_close(L_); }
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;

    // Runs a chunk; throws std::runtime_error with Lua's message when it fails.
    void run(const std::string& code, const char* name = "test") {
        if (luaL_loadbuffer(L_, code.data(), code.size(), (std::string("=") + name).c_str()) != 0) fail(name);
        if (lua_pcall(L_, 0, 0, 0) != 0) fail(name);
    }

    // Runs a file of the tests folder (a Lua helper such as check_definition.lua).
    void run_file(const std::string& relative) {
        const std::string path = std::string(SU_TEST_DIR) + "/" + relative;
        if (luaL_loadfile(L_, path.c_str()) != 0) fail(relative.c_str());
        if (lua_pcall(L_, 0, 0, 0) != 0) fail(relative.c_str());
    }

    // The value of a Lua expression, as a number, a string or a truth value.
    double num(const std::string& expr) {
        push_expr(expr);
        if (!lua_isnumber(L_, -1)) { pop(); throw std::runtime_error("not a number: " + expr); }
        double v = lua_tonumber(L_, -1);
        pop();
        return v;
    }
    std::string str(const std::string& expr) {
        push_expr(expr);
        std::string s = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "";
        pop();
        return s;
    }
    bool truthy(const std::string& expr) {
        push_expr(expr);
        bool b = lua_toboolean(L_, -1) != 0;
        pop();
        return b;
    }

    lua_State* state() { return L_; }

private:
    void push_expr(const std::string& expr) {
        const std::string code = "return " + expr;
        if (luaL_loadbuffer(L_, code.data(), code.size(), "=expr") != 0) fail("expression");
        if (lua_pcall(L_, 0, 1, 0) != 0) fail("expression");
    }
    void pop() { lua_settop(L_, 0); }
    [[noreturn]] void fail(const char* what) {
        std::string msg = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_settop(L_, 0);
        throw std::runtime_error(std::string(what) + ": " + msg);
    }

    lua_State* L_;
};

}  // namespace su_test

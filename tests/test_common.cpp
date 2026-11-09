// scripts/common.lua on a server stub. The common script runs on every side the
// mod declares, the server included, so its modules (src/logic/*, lib/tuning, lib/units, lib/volume; src/common.txt) must run in a
// Lua state that has no `kyubu`, no FFI and no other global of the game. The stub below is that state: every read of an unknown
// global raises an error. The same cases then run on the source tree in the usual harness, and both answers must be equal text.
#include "doctest.h"

#include <lua.hpp>

#include <stdexcept>
#include <string>

#include "lua_harness.hpp"

namespace {

// Defines cases(): a text that holds the shape of every logic module and some values. It uses only `require`.
const char* kCases = R"LUA(
local function ser(v, depth)
  depth = depth or 0
  if depth > 12 then return "<deep>" end
  local t = type(v)
  if t == "number" then return string.format("%.17g", v) end
  if t == "string" then return string.format("%q", v) end
  if t == "table" then
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local out = {}
    for _, k in ipairs(keys) do out[#out + 1] = tostring(k) .. "=" .. ser(v[k], depth + 1) end
    return "{" .. table.concat(out, ",") .. "}"
  end
  if t == "function" then return "fn" end
  return tostring(v)
end

function cases()
  local out = {}
  local tuning = require("skills_unleashed.lib.tuning")
  out[#out + 1] = "tuning " .. ser(tuning.blocks)
  for _, name in ipairs({ "assassin", "berserker", "fire_mage", "guardian", "ninja", "scout", "sniper", "water_mage" }) do
    out[#out + 1] = name .. " " .. ser(require("skills_unleashed.logic." .. name))
  end
  local T = tuning.blocks.berserker
  local b = require("skills_unleashed.logic.berserker")
  out[#out + 1] = "berserker.damage " .. ser({ b.damage(T, 2) })
  out[#out + 1] = "berserker.hold " .. ser({ b.holds_player(T, 0), b.holds_player(T, 100000) })
  local a = require("skills_unleashed.logic.assassin")
  local AT = tuning.blocks.assassin
  out[#out + 1] = "assassin.clip " .. ser({ a.clip_for(AT, 3), a.clip_for(AT, 4), a.clip_for(AT, 5), a.clip_for(AT, nil) })
  out[#out + 1] = "units " .. ser(require("skills_unleashed.lib.units"))
  out[#out + 1] = "volume " .. ser(require("skills_unleashed.lib.volume"))
  return table.concat(out, "\n")
end
)LUA";

// A Lua state with no game: no `kyubu`, no `ffi`, and a global table that refuses unknown names.
struct ServerStub {
    lua_State* L = luaL_newstate();
    ServerStub() {
        luaL_openlibs(L);
        run(R"LUA(
          package.path = ""; package.cpath = ""
          package.loaded.ffi = nil; package.preload.ffi = nil
          for _, k in ipairs({ "ffi", "jit", "bit" }) do _G[k] = nil end
          kyubu = nil
        )LUA", "stub");
    }
    ~ServerStub() { lua_close(L); }
    void run(const std::string& code, const char* name) {
        if (luaL_loadbuffer(L, code.data(), code.size(), (std::string("=") + name).c_str()) != 0 || lua_pcall(L, 0, 0, 0) != 0) {
            std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
            lua_settop(L, 0);
            throw std::runtime_error(std::string(name) + ": " + msg);
        }
    }
    void load_file(const std::string& path) {
        if (luaL_loadfile(L, path.c_str()) != 0 || lua_pcall(L, 0, 0, 0) != 0) {
            std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
            lua_settop(L, 0);
            throw std::runtime_error(path + ": " + msg);
        }
    }
    void strict() {
        run(R"LUA(setmetatable(_G, { __index = function(_, k) error("the common script read the global '" .. tostring(k) .. "'", 2) end,
                                       __newindex = function(_, k, v) rawset(_, k, v) end }))LUA", "strict");
    }
    std::string result(const std::string& expr) {
        const std::string code = "return " + expr;
        if (luaL_loadbuffer(L, code.data(), code.size(), "=expr") != 0 || lua_pcall(L, 0, 1, 0) != 0) {
            std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
            lua_settop(L, 0);
            throw std::runtime_error("expr: " + msg);
        }
        std::string s = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_settop(L, 0);
        return s;
    }
};

}  // namespace

TEST_SUITE("common script") {
    TEST_CASE("common.lua runs on a state with no game and gives what the sources give") {
        ServerStub server;
        server.load_file(std::string(SU_MOD_DIR) + "/scripts/common.lua");
        server.run(kCases, "cases");
        server.strict();                                   // from here a read of a missing global is an error
        const std::string on_server = server.result("cases()");
        CHECK(on_server.find("tuning {") == 0);
        CHECK(on_server.find("berserker.damage") != std::string::npos);

        su_test::Vm vm;
        vm.run(kCases, "cases");
        const std::string on_sources = vm.str("cases()");
        CHECK(on_server == on_sources);
    }

    TEST_CASE("a client-only module is not in common.lua") {
        ServerStub server;
        server.load_file(std::string(SU_MOD_DIR) + "/scripts/common.lua");
        server.run("ok = pcall(require, 'skills_unleashed.lib.fx'); ok2 = pcall(require, 'skills_unleashed.lib.combat')", "client-only");
        CHECK(server.result("ok") == "");
        CHECK(server.result("ok2") == "");
        CHECK(server.result("tostring(ok)") == "false");
        CHECK(server.result("tostring(ok2)") == "false");
    }
}

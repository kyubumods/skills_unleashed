// lib/link: a joined game without channels (no join gate in force) runs no SkillsUnleashed ability, on the client or the server.
// The loader's answer is faked by kyubu.channel.send; what stays unproved until a join replay: the loader's real KY_E_NO_REMOTE.
#include "doctest.h"

#include "lua_harness.hpp"

using su_test::Vm;

namespace {

// `fail` set: every send answers the loader's KY_E_NO_REMOTE text (or `fail` itself, when it is another text).
const char* kSetup = R"LUA(
link = require("skills_unleashed.lib.link")
HOST = { host = "client", mode = "remote_client" }
kyubu.host = function() return HOST end
fail = nil
local send = kyubu.channel.send
kyubu.channel.send = function(name, to, msg)
  if fail then
    kyubu.channel.sent[#kyubu.channel.sent + 1] = { name = name, to = to, msg = msg, failed = true }
    return nil, fail == true and link.NO_REMOTE or fail
  end
  return send(name, to, msg)
end
function new_ctx()
  local c = { state = {}, rank = 1, elapsed_ms = 0, cancelled = 0 }
  function c:cancel() self.cancelled = self.cancelled + 1 end
  function c:marker() return {} end
  return c
end
calls = {}
def = link.guard("test:a", {
  on_start = function(ctx) calls[#calls + 1] = "start" end,
  on_step = function(ctx) calls[#calls + 1] = "step" end,
  on_end = function(ctx) calls[#calls + 1] = "end" end,
})
function run_once()
  local c = new_ctx()
  def.on_start(c); def.on_step(c); def.on_end(c)
  return c
end
-- a stand-in for kyubu.ui: records the text drawn; `close` makes the window's X report closed
drawn = {}
close = false
kyubu.ui = {
  begin = function(title, open) return true, not close end,
  text = function(t) drawn[#drawn + 1] = t end,
  finish = function() end,
}
)LUA";

}  // namespace

TEST_CASE("link: single-player sends no probe and runs the ability") {
    Vm vm;
    vm.run(kSetup);
    vm.run("HOST = { host = 'client', mode = 'local' }; fail = true; c = run_once()");
    CHECK(vm.num("#kyubu.channel.sent") == 0.0);
    CHECK(vm.num("c.cancelled") == 0.0);
    CHECK(vm.str("table.concat(calls, ',')") == "start,step,end");
}

TEST_CASE("link: a joined client with channels probes once and runs every start") {
    Vm vm;
    vm.run(kSetup);
    vm.run("a = run_once(); b = run_once()");
    CHECK(vm.num("#kyubu.channel.sent") == 1.0);
    CHECK(vm.str("kyubu.channel.sent[1].name") == "skills_unleashed:ability");
    CHECK(vm.str("kyubu.channel.sent[1].msg.kind") == "hello");
    CHECK(vm.num("a.cancelled + b.cancelled") == 0.0);
    CHECK(vm.str("table.concat(calls, ',')") == "start,step,end,start,step,end");
}

TEST_CASE("link: a joined client without channels cancels at the start, skips the run and tells the player") {
    Vm vm;
    vm.run(kSetup);
    vm.run("fail = true; a = run_once(); b = run_once()");
    CHECK(vm.num("a.cancelled") == 1.0);
    CHECK(vm.num("b.cancelled") == 1.0);
    CHECK(vm.num("#calls") == 0.0);
    CHECK(vm.num("#kyubu.channel.sent") == 1.0);          // the answer holds: one probe for the process
    CHECK(vm.num("kyubu.log_count('error')") == 1.0);     // one error line, then one info line per refused start
    CHECK(vm.truthy("link._notice()"));
    CHECK(vm.num("#kyubu.handlers['render.overlay']") == 1.0);
    vm.run("kyubu.fire('render.overlay')");
    CHECK(vm.str("drawn[1]") == "SkillsUnleashed abilities are off on this server.");
    vm.run("close = true; kyubu.fire('render.overlay')");
    CHECK_FALSE(vm.truthy("link._notice()"));
    vm.run("c = run_once()");                              // a later refusal opens it again, with no second handler
    CHECK(vm.truthy("link._notice()"));
    CHECK(vm.num("#kyubu.handlers['render.overlay']") == 1.0);
}

TEST_CASE("link: an error other than KY_E_NO_REMOTE says nothing about the link") {
    Vm vm;
    vm.run(kSetup);
    vm.run("fail = 'too large'; a = run_once(); fail = nil; b = run_once()");
    CHECK(vm.num("a.cancelled + b.cancelled") == 0.0);
    CHECK(vm.num("#kyubu.channel.sent") == 2.0);          // not cached: the second start probed again
}

TEST_CASE("link: Server.exe without channels applies no ability and draws nothing") {
    Vm vm;
    vm.run(kSetup);
    vm.run("HOST = { host = 'server', mode = 'dedicated' }; fail = true; a = run_once()");
    CHECK(vm.num("a.cancelled") == 1.0);
    CHECK(vm.num("#calls") == 0.0);
    CHECK(vm.str("kyubu.channel.sent[1].name") == "skills_unleashed:world");
    CHECK_FALSE(vm.truthy("link._notice()"));
    CHECK(vm.truthy("kyubu.handlers['render.overlay'] == nil"));
}

TEST_CASE("link: the server ignores the client's probe") {
    Vm vm;
    vm.run(kSetup);
    vm.run("ch = require('skills_unleashed.lib.ability_channel'); ch._on_message(8, { kind = 'hello' }); ch._on_message(99, { kind = 'hello' })");
    CHECK(vm.num("kyubu.log_count()") == 0.0);
}

TEST_CASE("link: every ability of the client and of the server is guarded") {
    Vm vm;
    vm.run(kSetup);
    vm.run("fail = true; require('skills_unleashed.main'); kyubu.fire('lifecycle.register')");
    vm.run("n, cancelled = 0, 0; for id, d in pairs(kyubu.defs) do if d.claim then n = n + 1; local c = new_ctx(); d.on_start(c); cancelled = cancelled + c.cancelled end end");
    CHECK(vm.num("n") == 8.0);
    CHECK(vm.num("cancelled") == 8.0);

    Vm sv;
    sv.run(kSetup);
    sv.run("kyubu.side = 'server'; HOST = { host = 'server', mode = 'dedicated' }; fail = true");
    sv.run("require('skills_unleashed.server.main'); kyubu.fire('lifecycle.register')");
    sv.run("n, cancelled = 0, 0; for id, d in pairs(kyubu.defs) do if d.claim and d.on_start then n = n + 1; local c = new_ctx(); d.on_start(c); cancelled = cancelled + c.cancelled end end");
    CHECK(sv.num("n") == 8.0);
    CHECK(sv.num("cancelled") == 8.0);
}

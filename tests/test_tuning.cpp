// lib/tuning.lua and the numeric-literal scan. No game is needed.
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "literal_scan.hpp"
#include "lua_harness.hpp"

using su_test::Vm;
namespace fs = std::filesystem;

namespace {

const char* kGoodBlock =
    "local t = require('skills_unleashed.lib.tuning'); "
    "good = { wip = true, duration_ms = 800, cooldown_ms = { 9000, 8000, 7000 }, mp_cost = 0.1, max_rank = 3, range = 6 }; ";

std::string reason(Vm& vm, const std::string& block_edit) {
    vm.run(std::string(kGoodBlock) + "local b = {}; for k, v in pairs(good) do b[k] = v end; " + block_edit +
           " ok, why = t.check(b, 'test')");
    return vm.truthy("ok") ? "" : vm.str("why");
}

}  // namespace

TEST_CASE("tuning: a complete block passes and the ability's own numbers are allowed") {
    Vm vm;
    CHECK(reason(vm, "") == "");
}

TEST_CASE("tuning: every shape error is named") {
    Vm vm;
    CHECK(reason(vm, "b.wip = nil;").find("wip") != std::string::npos);
    CHECK(reason(vm, "b.duration_ms = nil;").find("duration_ms is missing") != std::string::npos);
    CHECK(reason(vm, "b.duration_ms = 0;").find("duration_ms") != std::string::npos);
    CHECK(reason(vm, "b.duration_ms = 60001;").find("duration_ms") != std::string::npos);
    CHECK(reason(vm, "b.duration_ms = 800.5;").find("duration_ms") != std::string::npos);
    CHECK(reason(vm, "b.max_rank = 0;").find("max_rank") != std::string::npos);
    CHECK(reason(vm, "b.max_rank = 256;").find("max_rank") != std::string::npos);
    CHECK(reason(vm, "b.mp_cost = 1.5;").find("mp_cost") != std::string::npos);
    CHECK(reason(vm, "b.mp_cost = -0.1;").find("mp_cost") != std::string::npos);
    CHECK(reason(vm, "b.cooldown_ms = { 9000 };").find("one value per rank") != std::string::npos);
    CHECK(reason(vm, "b.cooldown_ms = 9000;").find("one value per rank") != std::string::npos);
    CHECK(reason(vm, "b.cooldown_ms[2] = -1;").find("cooldown_ms[2]") != std::string::npos);
    CHECK(reason(vm, "b.cooldown_ms[3] = 3600001;").find("cooldown_ms[3]") != std::string::npos);
}

TEST_CASE("tuning: the cooldown function covers ranks 0 to max_rank, as the loader calls it") {
    Vm vm;
    vm.run(std::string(kGoodBlock) + "cd = t.cooldown_fn(good)");
    CHECK(vm.num("cd(0)") == 9000.0);   // rank 0 reads the first value
    CHECK(vm.num("cd(1)") == 9000.0);
    CHECK(vm.num("cd(2)") == 8000.0);
    CHECK(vm.num("cd(3)") == 7000.0);
    CHECK(vm.num("cd(4)") == 7000.0);   // above max_rank reads the last
}

TEST_CASE("tuning: a cooldown curve follows the game's saturating strength and has no last rank") {
    Vm vm;
    vm.run(std::string(kGoodBlock) + R"LUA(
      curved = { wip = true, duration_ms = 800, cooldown_curve = { base_ms = 60000, cut_ms = 30000 }, rank_scale = 0.1,
                 mp_cost = 0, max_rank = 255 }
      ok_curve, why_curve = t.check(curved, "curved")
      cd = t.cooldown_fn(curved)
      p0, p10, p1000 = t.power(0, 0.1), t.power(10, 0.1), t.power(1000, 0.1)
      bad_cut = { wip = true, duration_ms = 800, cooldown_curve = { base_ms = 1000, cut_ms = 2000 }, rank_scale = 0.1,
                  mp_cost = 0, max_rank = 3 }
      ok_cut = t.check(bad_cut, "bad_cut")
      bad_scale = { wip = true, duration_ms = 800, cooldown_curve = { base_ms = 1000, cut_ms = 500 }, mp_cost = 0, max_rank = 3 }
      ok_scale = t.check(bad_scale, "bad_scale")
    )LUA");
    CHECK(vm.truthy("ok_curve"));
    CHECK(vm.truthy("p0 == 0 and math.abs(p10 - 0.5) < 1e-9 and p1000 < 1 and p1000 > 0.99"));   // 1 - 1/(0.1*10 + 1)
    CHECK(vm.num("cd(0)") == 60000.0);    // rank 0 reads rank 1
    CHECK(vm.num("cd(1)") == 60000.0);    // 60 s at rank 1
    CHECK(vm.num("cd(2)") == 57273.0);    // 60000 - 30000 * (1 - 1/1.1), rounded
    CHECK(vm.num("cd(11)") == 45000.0);   // power(10) = 0.5
    CHECK(vm.truthy("cd(50) < cd(11) and cd(255) < cd(50) and cd(255) > 30000"));   // always falling, never below the floor
    CHECK(vm.truthy("not ok_cut and not ok_scale"));
}

TEST_CASE("tuning: get returns a registered block and names a missing one") {
    Vm vm;
    vm.run(std::string(kGoodBlock) + "t.blocks.berserker = good; a = t.get('berserker'); b, why = t.get('nobody')");
    CHECK(vm.truthy("a == good"));
    CHECK(vm.truthy("b == nil"));
    CHECK(vm.str("why").find("nobody") != std::string::npos);
}

TEST_CASE("tuning: at_rank clamps both ways") {
    Vm vm;
    vm.run("local t = require('skills_unleashed.lib.tuning'); l = { 10, 20, 30 }; at = t.at_rank");
    CHECK(vm.num("at(l, 0)") == 10.0);
    CHECK(vm.num("at(l, 2)") == 20.0);
    CHECK(vm.num("at(l, 9)") == 30.0);
}

TEST_CASE("tuning: every block registered in lib/tuning.lua has the shape") {
    // covers: skills-unleashed-mod/Every number in one table: Retiming an ability (the shape half)
    Vm vm;
    vm.run("t = require('skills_unleashed.lib.tuning'); bad = {}; "
           "for name, b in pairs(t.blocks) do local ok, why = t.check(b, name); if not ok then bad[#bad + 1] = why end end");
    CHECK(vm.str("table.concat(bad, '; ')") == "");
}

// --- the numeric-literal scan -----------------------------------------------------------------------------------------

TEST_CASE("literal scan: 0, 1 and 2 pass, in any spelling that has the same value") {
    // covers: skills-unleashed-mod/Every number in one table: A literal in an ability file (the passing side)
    CHECK(su_test::numeric_literals_beyond_012("local a = 0\nlocal b = 1\nlocal c = 2\nlocal d = 1.0 + 2.0 + 0x2 - 0.0\n").empty());
}

TEST_CASE("literal scan: any other number is reported with its line") {
    // covers: skills-unleashed-mod/Every number in one table: A literal in an ability file
    const auto r = su_test::numeric_literals_beyond_012("local a = 0\nlocal range = 6\nlocal r2 = 0.5\nlocal h = 0x10\nlocal e = 1e3\n");
    REQUIRE(r.size() == 4);
    CHECK(r[0].line == 2);
    CHECK(r[0].text == "6");
    CHECK(r[1].text == "0.5");
    CHECK(r[2].text == "0x10");
    CHECK(r[3].text == "1e3");
}

TEST_CASE("literal scan: comments, strings and identifiers with digits are not literals") {
    const std::string s =
        "-- a range of 6 blocks, 1500 ms\n"
        "--[[ a long comment with 99\n and 100 ]]\n"
        "--[==[ also 7 ]==]\n"
        "local msg = \"after 500 ms\" .. 'or 600' .. [[ and 700 ]]\n"
        "local vec3, x2, item10 = nil, 1, 2\n"
        "t.x10 = 1\n";
    CHECK(su_test::numeric_literals_beyond_012(s).empty());
}

TEST_CASE("literal scan: a number after a comment on the same line and a negative number are still seen") {
    const auto r = su_test::numeric_literals_beyond_012("local a = -3 -- one\nlocal b = a * 4\nf(...) local c = 1 .. 5\n");
    REQUIRE(r.size() == 3);
    CHECK(r[0].text == "3");
    CHECK(r[1].text == "4");
    CHECK(r[2].text == "5");
}

TEST_CASE("literal scan: src/abilities and src/logic hold no number but 0, 1 and 2") {
    // covers: skills-unleashed-mod/Every number in one table: A literal in an ability file
    std::ostringstream found;
    for (const char* dir : {"abilities", "logic"}) {
        const fs::path p = fs::path(SU_MOD_DIR) / "src" / dir;
        if (!fs::exists(p)) continue;
        for (const auto& e : fs::recursive_directory_iterator(p)) {
            if (e.path().extension() != ".lua") continue;
            std::ifstream f(e.path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            for (const auto& lit : su_test::numeric_literals_beyond_012(ss.str()))
                found << e.path().filename().string() << ":" << lit.line << " " << lit.text << "\n";
        }
    }
    CHECK_MESSAGE(found.str().empty(), "move these numbers to lib/tuning.lua:\n" << found.str());
}

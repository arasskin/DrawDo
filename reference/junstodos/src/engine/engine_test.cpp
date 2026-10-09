// Engine port tests: EDN value roundtrips, transact index maintenance
// (including the falsy-retraction and ave-cleanup subtleties), and query
// semantics — including one remaining faithfully-ported quirk (injective
// bindings), asserted as-is and marked KNOWN QUIRK so changing it later
// breaks a test on purpose.

#include <cstdio>
#include <string>

#include "db.h"
#include "value.h"

using edn::Value;
using engine::Db;
using engine::Diff;

#define REQUIRE(cond, msg) do { \
    if (!(cond)) { printf("ENGINE FAIL: %s\n", msg); return 1; } \
} while (0)

using edn::kw;
using edn::sym;
using edn::M;

static Value str(const char* s) { return Value::string(s); }

static Value q(const Db& db, const char* pattern_edn) {
    return engine::query(db, pattern_edn);  // the string-pattern overload
}

int engine_test_main(const char*) {
    // ---- EDN roundtrips
    {
        const char* forms[] = {
            "nil", "true", "false", "42", "-7", "3.5", "\"hi \\\"there\\\"\\n\"",
            ":done", "my-sym", "[1 2 [3 :x]]", "{:a 1 :b [2 3]}",
        };
        for (const char* f : forms) {
            Value v = edn::read_string(f);
            Value again = edn::read_string(edn::pr_str(v));
            REQUIRE(v == again, (std::string("roundtrip: ") + f).c_str());
        }
        REQUIRE(edn::read_string("{:a 1 :b 2}") == edn::read_string("{:b 2, :a 1}"),
                "map equality is order-insensitive");
        REQUIRE(edn::read_string("1") != edn::read_string("1.0"), "int != double");
        bool threw = false;
        try { edn::read_string("[1 2"); } catch (...) { threw = true; }
        REQUIRE(threw, "unterminated vector throws");
    }

    // ---- literal construction (implicit ctors, M builder, get)
    {
        REQUIRE(Value(5) == edn::read_string("5"), "implicit int");
        REQUIRE(Value(true) == edn::read_string("true"), "implicit bool");
        REQUIRE(Value(1.5) == edn::read_string("1.5"), "implicit double");
        REQUIRE(Value("hi") == edn::read_string("\"hi\""), "implicit c-string");
        REQUIRE(Value(std::string("hi")) == edn::read_string("\"hi\""), "implicit std::string");

        Value m = M{{kw("done"), false}, {kw("text"), "hi"}};
        REQUIRE(m == edn::read_string("{:done false :text \"hi\"}"), "M builds a canonical map");
        REQUIRE(m.get(kw("text")) == Value("hi"), "get finds a key");
        REQUIRE(m.get(kw("zzz")).is_nil(), "get on absent key -> nil");
        REQUIRE(Value(5).get(kw("x")).is_nil(), "get on non-map -> nil");

        Value nested = M{{"e1", M{{kw("a"), 1}}}};
        REQUIRE(nested.get(Value("e1")).get(kw("a")) == Value(1), "nested M, chained get");
    }

    // ---- transact
    Db db;
    {
        Diff d;
        d.adds = {{str("jun"), kw("name"), str("Jun")},
                  {str("jun"), kw("done"), Value::boolean(false)},
                  {str("mia"), kw("name"), str("Mia")},
                  {str("mia"), kw("likes"), str("plants")},
                  {str("kim"), kw("likes"), str("plants")}};
        db = transact(db, d);
    }
    REQUIRE(q(db, "{\"jun\" {:name ?n}}") == edn::read_string("{\"jun\" {:name \"Jun\"}}"),
            "eav lookup");
    REQUIRE(q(db, "{?e {:likes \"plants\"}}") ==
                edn::read_string("{\"mia\" {:likes \"plants\"} \"kim\" {:likes \"plants\"}}"),
            "ave lookup finds both entities");

    // overwrite: old value's ave entry must lose the entity
    {
        Diff d;
        d.adds = {{str("mia"), kw("likes"), str("rocks")}};
        db = transact(db, d);
    }
    REQUIRE(q(db, "{?e {:likes \"plants\"}}") == edn::read_string("{\"kim\" {:likes \"plants\"}}"),
            "ave cleaned up on overwrite");

    // falsy values retract correctly (the proprietary-nothing sentinel)
    {
        Diff d;
        d.retracts = {{str("jun"), kw("done")}};
        db = transact(db, d);
    }
    REQUIRE(q(db, "{\"jun\" {:done ?x}}") == edn::read_string("{}"),
            "stored false was retracted");

    // retract before add within one transaction
    {
        Diff d;
        d.retracts = {{str("kim"), kw("likes")}};
        d.adds = {{str("kim"), kw("likes"), str("moss")}};
        db = transact(db, d);
    }
    REQUIRE(q(db, "{\"kim\" {:likes ?x}}") == edn::read_string("{\"kim\" {:likes \"moss\"}}"),
            "retract-then-add lands on add");

    // collection values work as ave keys
    {
        Diff d;
        d.adds = {{str("jun"), kw("tags"), edn::read_string("[:home :urgent]")}};
        db = transact(db, d);
    }
    REQUIRE(q(db, "{?e {:tags [:home :urgent]}}") ==
                edn::read_string("{\"jun\" {:tags [:home :urgent]}}"),
            "vector value as ave key");

    // ---- query joins
    {
        Diff d;
        d.adds = {{str("jun"), kw("age"), Value::integer(30)}};
        db = transact(db, d);
    }
    REQUIRE(q(db, "{?e {:name \"Jun\" :age ?a}}") ==
                edn::read_string("{\"jun\" {:name \"Jun\" :age 30}}"),
            "join binds entity then reads age");

    REQUIRE(q(db, "{?e {:name \"Nobody\"}}") == edn::read_string("{}"), "no match -> {}");

    // constructor-form pattern: runtime values place directly, no parsing
    {
        Value entity = str("jun");  // stands in for a value known only at runtime
        REQUIRE(engine::query(db, M{{entity, M{{kw("age"), sym("?a")}}}}) ==
                    q(db, "{\"jun\" {:age ?a}}"),
                "constructor pattern == equivalent string pattern");
    }

    // ---- fixed quirk (2026-07-11): a triple that becomes fully ground via
    // substitution is now VERIFIED against the db. (The original recorded it
    // into results unchecked, dependent on Clojure map insertion order —
    // :name sorted before :zzz, bound ?e, and the nonexistent :zzz pair was
    // asserted into the result.)
    REQUIRE(q(db, "{?e {:name \"Jun\" :zzz 999}}") == edn::read_string("{}"),
            "ground-after-substitution triple with absent attr fails the match");
    REQUIRE(q(db, "{?e {:name \"Jun\" :age 30}}") ==
                edn::read_string("{\"jun\" {:name \"Jun\" :age 30}}"),
            "ground-after-substitution triple that holds still matches");
    // an attr that sorts FIRST goes through the (empty) ave index and fails
    // the query the same way — order no longer changes the answer.
    REQUIRE(q(db, "{?e {:done false :name \"Jun\"}}") == edn::read_string("{}"),
            "ground triple processed first fails via empty ave");

    // ---- fully ground patterns (2026-07-11): literal entity + literal value
    // is no longer dropped at parse time — it's verified, so a query with a
    // full entity is an existence check.
    REQUIRE(q(db, "{\"jun\" {:name \"Jun\"}}") == edn::read_string("{\"jun\" {:name \"Jun\"}}"),
            "ground pattern that holds matches itself");
    REQUIRE(q(db, "{\"jun\" {:name \"Mia\"}}") == edn::read_string("{}"),
            "ground pattern with wrong value fails");
    REQUIRE(q(db, "{\"jun\" {:zzz 1}}") == edn::read_string("{}"),
            "ground pattern with absent attr fails");
    // a ground triple constrains the rest of the pattern (conjunction)
    REQUIRE(q(db, "{?e {:likes \"moss\"} \"jun\" {:age 30}}") ==
                edn::read_string("{\"kim\" {:likes \"moss\"} \"jun\" {:age 30}}"),
            "true ground triple gates and joins the result");
    REQUIRE(q(db, "{?e {:likes \"moss\"} \"jun\" {:age 31}}") == edn::read_string("{}"),
            "false ground triple fails the whole query");

    // ---- KNOWN QUIRK (faithful port): injective bindings — two distinct
    // variables can never bind the same value, so same-value cross matches
    // yield nothing.
    {
        Db db2;
        Diff d;
        d.adds = {{str("a"), kw("tag"), str("t")}, {str("b"), kw("tag"), str("t")}};
        db2 = transact(db2, d);
        REQUIRE(q(db2, "{?x {:tag ?t1} ?y {:tag ?t2}}") == edn::read_string("{}"),
                "QUIRK: injectivity forbids ?t1/?t2 sharing a value");
    }

    // variable attributes throw
    {
        bool threw = false;
        try { q(db, "{?e {?attr 1}}"); } catch (const std::runtime_error&) { threw = true; }
        REQUIRE(threw, "variable attribute throws");
    }

    printf("OK: engine test passed\n");
    return 0;
}

#define SLIMR_TEST_MAIN
#include "slimr/test.hpp"
#include "slimr/reflect.hpp"

using namespace slimr;

struct SimpleTestStruct {
    Field<std::string, "The name of the item"> name;
    Desc<std::optional<int>, "Optional count"> count;
    OptField<bool, "Flag indicating active state"> active{true};
};

struct EmptyStruct {};

struct NestedStruct {
    Field<SimpleTestStruct, "Nested item"> item;
    Field<std::optional<std::string>, "Optional label"> label;
};

TEST_SUITE("slimr::reflect") {
    TEST_CASE("Field and Desc serialization/deserialization") {
        SimpleTestStruct obj{"Item1", 42, false};
        std::string s = write_json(obj);
        CHECK(s.find("\"name\":\"Item1\"") != std::string::npos);
        CHECK(s.find("\"count\":42") != std::string::npos);
        CHECK(s.find("\"active\":false") != std::string::npos);

        SimpleTestStruct readBack;
        auto ec = read_json(readBack, s);
        CHECK(!ec);
        CHECK(readBack.name.get() == "Item1");
        CHECK(readBack.count.has_value());
        CHECK(readBack.count.value_or(0) == 42);
        CHECK(readBack.active.get() == false);
    }

    TEST_CASE("Field optional ergonomics: value_or, operator*, operator->") {
        Field<std::optional<std::string>, "Optional string"> optStr;
        CHECK(!optStr.has_value());
        CHECK(optStr.value_or("default") == "default");

        optStr = "hello world";
        CHECK(optStr.has_value());
        CHECK(optStr.value_or("default") == "hello world");
        CHECK(**optStr == "hello world");
        CHECK((*optStr)->length() == 11);

        Field<std::string, "Required string"> reqStr{"regular string"};
        CHECK(reqStr->length() == 14);
        CHECK(*reqStr == "regular string");
    }

    TEST_CASE("MCP Schema Generation") {
        Json sc = schema<SimpleTestStruct>();
        CHECK(sc["type"].get<std::string>() == "object");
        CHECK(sc["additionalProperties"].get<bool>() == false);

        auto props = sc["properties"];
        CHECK(props.contains("name"));
        CHECK(props["name"]["type"].get<std::string>() == "string");
        CHECK(props["name"]["description"].get<std::string>() == "The name of the item");

        CHECK(props.contains("count"));
        CHECK(props["count"]["type"].get<std::string>() == "integer");
        CHECK(props["count"]["description"].get<std::string>() == "Optional count");

        CHECK(props.contains("active"));
        CHECK(props["active"]["type"].get<std::string>() == "boolean");
        CHECK(props["active"]["description"].get<std::string>() == "Flag indicating active state");

        auto req = sc["required"];
        CHECK(req.is_array());
        std::vector<std::string> reqVec;
        for (const auto& item : req.get<Json::array_t>()) {
            reqVec.push_back(item.get<std::string>());
        }
        CHECK(std::find(reqVec.begin(), reqVec.end(), "name") != reqVec.end());
        CHECK(std::find(reqVec.begin(), reqVec.end(), "count") == reqVec.end());
        CHECK(std::find(reqVec.begin(), reqVec.end(), "active") == reqVec.end());
    }

    TEST_CASE("Empty struct schema generation") {
        Json sc = schema<EmptyStruct>();
        CHECK(sc["type"].get<std::string>() == "object");
        CHECK(sc["properties"].is_object());
        CHECK(sc["properties"].get<Json::object_t>().empty());
        CHECK(sc["required"].is_array());
        CHECK(sc["required"].get<Json::array_t>().empty());
    }
}

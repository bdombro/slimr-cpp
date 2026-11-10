#define SLIMR_TEST_MAIN
#include "slimr/test.hpp"
#include "slimr/yaml.hpp"

using namespace slimr;

struct YamlConfig {
    std::string name;
    int port{8080};
    bool enabled{true};
    std::vector<std::string> tags;
};

TEST_SUITE("slimr::yaml") {
    TEST_CASE("parse_yaml basic values and structures") {
        std::string yaml_doc = R"(
name: slimr-yaml
port: 9000
enabled: true
tags:
  - fast
  - cpp26
)";
        auto res = parse_yaml<Json>(yaml_doc);
        REQUIRE(res.has_value());
        const Json& j = *res;
        CHECK(j["name"].get<std::string>() == "slimr-yaml");
        CHECK(j["port"].get<int64_t>() == 9000);
        CHECK(j["enabled"].get<bool>() == true);
        REQUIRE(j["tags"].is_array());
        CHECK(j["tags"].get<Json::array_t>().size() == 2);
    }

    TEST_CASE("to_yaml emission and roundtrip") {
        Json j = Json::object_t{};
        j["title"] = "Test Title";
        j["count"] = 42;

        std::string out = to_yaml(j);
        CHECK(out.find("title: Test Title") != std::string::npos);
        CHECK(out.find("count: 42") != std::string::npos);

        auto roundtrip = parse_yaml<Json>(out);
        REQUIRE(roundtrip.has_value());
        CHECK((*roundtrip)["title"].get<std::string>() == "Test Title");
        CHECK((*roundtrip)["count"].get<int64_t>() == 42);
    }

    TEST_CASE("parse_yaml directly into C++ struct") {
        std::string yaml_doc = R"(
name: my-app
port: 3000
enabled: false
tags:
  - web
  - api
)";
        auto res = parse_yaml<YamlConfig>(yaml_doc);
        REQUIRE(res.has_value());
        CHECK(res->name == "my-app");
        CHECK(res->port == 3000);
        CHECK(res->enabled == false);
        REQUIRE(res->tags.size() == 2);
        CHECK(res->tags[0] == "web");
        CHECK(res->tags[1] == "api");
    }

    TEST_CASE("parse_frontmatter with valid frontmatter") {
        std::string md = "---\ntitle: Note 1\nauthor: Brian\n---\n\n# Body Heading\nThis is content.";
        auto split = parse_frontmatter(md);
        REQUIRE(split.has_value());
        REQUIRE(split->frontmatter.has_value());
        CHECK((*split->frontmatter)["title"].get<std::string>() == "Note 1");
        CHECK((*split->frontmatter)["author"].get<std::string>() == "Brian");
        CHECK(split->body.find("# Body Heading") != std::string::npos);
    }

    TEST_CASE("parse_frontmatter without frontmatter") {
        std::string md = "# Body Heading\nThis is pure markdown.";
        auto split = parse_frontmatter(md);
        REQUIRE(split.has_value());
        CHECK(!split->frontmatter.has_value());
        CHECK(split->body == md);
    }

    TEST_CASE("parse_frontmatter with unclosed frontmatter") {
        std::string md = "---\ntitle: Unclosed\nno end delimiter";
        auto split = parse_frontmatter(md);
        CHECK(!split.has_value());
    }
}

#include "inc/base.hpp"

using std::string;
using std::vector;
using json = nlohmann::json;

/*
This packet should handles conversion between
MCP , CLI and internal Tool/Param representation

conversion MAP
[] cli -> mcp
[] mcp -> cli
[] internal -> cli/mcp
*/

// flag short name
// CLI to_cli provide follow flags for query: --query -q
// MCP do not use flag option
struct Param
{
    string flag = "", name, desc,
           type = "string"; // default string
    std::vector<string> enum_list = {};
};
struct Tool
{
    string name, description;
    std::vector<string> required = {};
    std::vector<Param> params = {};
};

json
to_mcp_json(Tool& tool)
{
    json props = json::object();
    for (auto& p : tool.params) {
        props[p.name] = { { "type", p.type }, { "description", p.desc } };
        if (!p.enum_list.empty())
            props[p.name]["enum"] = p.enum_list;
    }

    return {
        { "name", tool.name },
        { "description", tool.description },
        { "inputSchema",
          {{ "type", "object" },
            { "properties", props },
            { "required",  tool.required }}},
    };
}

// TODO
// json parse_cli_to_mcp(const Tool& tool,int argc,char* argv[]){
//     json parsed_args = json::object();
//     for (int i = 1; i < argc; ++i){
//         string arg = argv[i];
//         for (const auto &p : tool.params)
//         {

//         }
//     }
// }

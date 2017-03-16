/****************************************************************/
/*               DO NOT MODIFY THIS HEADER                      */
/* MOOSE - Multiphysics Object Oriented Simulation Environment  */
/*                                                              */
/*           (c) 2010 Battelle Energy Alliance, LLC             */
/*                   ALL RIGHTS RESERVED                        */
/*                                                              */
/*          Prepared by Battelle Energy Alliance, LLC           */
/*            Under Contract No. DE-AC07-05ID14517              */
/*            With the U. S. Department of Energy               */
/*                                                              */
/*            See COPYRIGHT for full restrictions               */
/****************************************************************/

#include "JsonSyntaxTree.h"

#include "Parser.h"

#include <algorithm>
#include <cctype>

JsonSyntaxTree::JsonSyntaxTree(const std::string & search_string) : _search(search_string) {}

std::vector<std::string>
JsonSyntaxTree::splitPath(const std::string & path)
{
  std::string s;
  std::istringstream f(path);
  std::vector<std::string> paths;
  while (std::getline(f, s, '/'))
    if (s.size() > 0)
      paths.push_back(s);
  return paths;
}

moosecontrib::Json::Value &
JsonSyntaxTree::getJson(const std::string & path)
{
  auto paths = splitPath(path);
  mooseAssert(paths.size() > 0, "path is empty");
  moosecontrib::Json::Value * next = &_root[paths[0]];

  for (auto pit = paths.begin() + 1; pit != paths.end(); ++pit)
  {
    if (*pit == "*")
      // It has an action syntax as a parent
      next = &(*next)["star"];
    else if (*pit == "<type>")
      next = &(*next)["types"];
    else
      next = &(*next)["subblocks"][*pit];
  }
  return *next;
}

moosecontrib::Json::Value &
JsonSyntaxTree::getJson(const std::string & parent, const std::string & path, bool is_type)
{
  if (parent.empty())
  {
    auto & j = getJson(path);
    if (path.back() == '*' && !j.isMember("subblock_types"))
      j["subblock_types"] = moosecontrib::Json::Value();
    else if (path.back() != '*' && !j.isMember("types"))
      j["types"] = moosecontrib::Json::Value();
    return j["actions"];
  }

  auto & parent_json = getJson(parent);
  auto paths = splitPath(path);
  std::string key = "subblock_types";
  if (is_type)
    key = "types";
  auto & val = parent_json[key][paths.back()];
  return val;
}

void
JsonSyntaxTree::addParameters(const std::string & parent, const std::string & path, bool is_type,
                              const std::string & action, const std::string & task_name,
                              bool is_action, InputParameters * params)
{
  moosecontrib::Json::Value all_params;
  if (action == "EmptyAction")
    return;

  size_t count = 0;
  for (auto & iter : *params)
  {
    // Make sure we want to see this parameter
    if (params->isPrivate(iter.first) ||
        (_search != "" && !MooseUtils::wildCardMatch(iter.first, _search) &&
         !MooseUtils::wildCardMatch(path, _search) && !MooseUtils::wildCardMatch(action, _search) &&
         !MooseUtils::wildCardMatch(parent, _search)))
      continue;

    ++count;
    moosecontrib::Json::Value param_json;

    // Block params may be required and will have a doc string
    std::string required = params->isParamRequired(iter.first) ? "Yes" : "No";

    param_json["required"] = required;

    // Only output default if it has one
    if (params->isParamValid(iter.first))
      param_json["default"] = buildOutputString(iter);
    else if (params->hasDefaultCoupledValue(iter.first))
      param_json["default"] = params->defaultCoupledValue(iter.first);

    param_json["options"] = buildOptions(iter);
    param_json["cpp_type"] = params->type(iter.first);
    param_json["group_name"] = params->getGroupName(iter.first);
    param_json["name"] = iter.first;

    std::string doc = params->getDocString(iter.first);
    MooseUtils::escape(doc);
    param_json["description"] = doc;
    all_params[iter.first] = param_json;
  }
  if (_search != "" && count == 0)
    // no parameters that matched the search string
    return;

  moosecontrib::Json::Value & json = getJson(parent, path, is_type);
  if (is_action)
  {
    json[action]["parameters"] = all_params;
    json[action]["tasks"].append(task_name);
    json[action]["action_path"] = path;
  }
  else if (params)
  {
    json["parameters"] = all_params;
    json["syntax_path"] = path;
    json["parent_syntax"] = parent;
    json["description"] = params->getClassDescription();
  }
}

std::string
JsonSyntaxTree::buildOptions(const std::iterator_traits<InputParameters::iterator>::value_type & p)
{
  std::string options;
  {
    InputParameters::Parameter<MooseEnum> * enum_type =
        dynamic_cast<InputParameters::Parameter<MooseEnum> *>(p.second);
    if (enum_type)
      options = enum_type->get().getRawNames();
  }
  {
    InputParameters::Parameter<MultiMooseEnum> * enum_type =
        dynamic_cast<InputParameters::Parameter<MultiMooseEnum> *>(p.second);
    if (enum_type)
      options = enum_type->get().getRawNames();
  }
  {
    InputParameters::Parameter<std::vector<MooseEnum>> * enum_type =
        dynamic_cast<InputParameters::Parameter<std::vector<MooseEnum>> *>(p.second);
    if (enum_type)
      options = (enum_type->get())[0].getRawNames();
  }
  return options;
}

std::string
JsonSyntaxTree::buildOutputString(
    const std::iterator_traits<InputParameters::iterator>::value_type & p)
{
  // Account for Point
  std::stringstream str;
  InputParameters::Parameter<Point> * ptr0 =
      dynamic_cast<InputParameters::Parameter<Point> *>(p.second);

  // Account for RealVectorValues
  InputParameters::Parameter<RealVectorValue> * ptr1 =
      dynamic_cast<InputParameters::Parameter<RealVectorValue> *>(p.second);

  // Output the Point components
  if (ptr0)
    str << ptr0->get().operator()(0) << " " << ptr0->get().operator()(1) << " "
        << ptr0->get().operator()(2);

  // Output the RealVectorValue components
  else if (ptr1)
    str << ptr1->get().operator()(0) << " " << ptr1->get().operator()(1) << " "
        << ptr1->get().operator()(2);

  // General case, call the print operator
  else
    p.second->print(str);

  // remove additional '\n' possibly generated in output (breaks JSON parsing)
  std::string tmp_str = str.str();
  for (auto & ch : tmp_str)
    if (ch == '\n')
      ch = ' ';
  return tmp_str;
}
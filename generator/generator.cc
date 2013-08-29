// Copyright 2013
// Author: Christopher Van Arsdale

#include <set>
#include <vector>
#include <string>
#include "common/log/log.h"
#include "repobuild/env/input.h"
#include "repobuild/generator/generator.h"
#include "nodes/node.h"
#include "repobuild/reader/parser.h"
#include "repobuild/nodes/cc_library.h"  // TODO(cvanarsdale): clunky, remove.

using std::string;
using std::vector;
using std::set;

namespace repobuild {
namespace {

void ExpandNode(const Parser& parser,
                const Node* node,
                set<const Node*>* parents,
                set<const Node*>* seen,
                vector<const Node*>* to_process) {
  if (!seen->insert(node).second) {
    // Already processed.
    return;
  }

  // Check for recursive dependency
  if (!parents->insert(node).second) {
    LOG(FATAL) << "Recursive dependency: " << node->target().full_path();
  }

  // Now expand our sub-node dependencies.
  for (const TargetInfo* target : node->dependencies()) {
    const Node* dep = parser.GetNode(target->full_path());
    if (dep == NULL) {
      LOG(FATAL) << "Could not find dependency " << target->full_path()
                 << " of target " << node->target().full_path();
    }
    ExpandNode(parser, dep, parents, seen, to_process);
  }

  // And record this node as ready to go in the queue.
  parents->erase(node);
  to_process->push_back(node);
}

void GetFullDepList(const Parser& parser,
                    const Node* node,
                    vector<const Node*>* dependencies,
                    set<const Node*>* deps_set) {
  for (const TargetInfo* target : node->dependencies()) {
    const Node* dep = parser.GetNode(target->full_path());
    CHECK(node);  // checked above in ExpandNode.
    if (deps_set->insert(dep).second) {
      GetFullDepList(parser, dep, dependencies, deps_set);
      dependencies->push_back(dep);  // after this nodes dependencies
    }
  }
}

// GetFullDepList
//  This returns dependencies in a DFS-style order. All dependencies of any
//  node in the list will be ahead of it in the list.
void GetFullDepList(const Parser& parser,
                    const Node* node,
                    vector<const Node*>* dependencies) {
  set<const Node*> deps_set;
  GetFullDepList(parser, node, dependencies, &deps_set);
}

}  // namespace

Generator::Generator() {
}

Generator::~Generator() {
}

string Generator::GenerateMakefile(const Input& input) {
  Makefile out;
  out.SetSilent(input.silent_make());
  out.append("# Auto-generated by repobuild, do not modify directly.\n\n");

  repobuild::Parser parser;
  parser.Parse(input);

  // TODO(cvanarsdale): Make this part of the parser's static registry?
  CCLibraryNode::WriteMakeHead(input, &out);

  // Figure out the order we want to write in our Makefile.
  set<const Node*> parents, seen;
  vector<const Node*> process_order;
  for (const Node* node : parser.input_nodes()) {
    ExpandNode(parser, node, &parents, &seen, &process_order);
  }

  // Generate the makefile.
  for (const Node* node : process_order) {
    vector<const Node*> all_deps;
    GetFullDepList(parser, node, &all_deps);
    node->WriteMake(all_deps, &out);
  }

  // Write the make clean rule.
  out.StartRule("clean", "");
  for (const Node* node : process_order) {
    vector<const Node*> all_deps;
    GetFullDepList(parser, node, &all_deps);
    node->WriteMakeClean(all_deps, &out);
  }
  out.WriteCommand("rm -rf " + input.object_dir());
  out.WriteCommand("rm -rf " + input.genfile_dir());
  out.WriteCommand("rm -rf " + input.source_dir());
  out.FinishRule();

  // Write the all rule.
  set<string> outputs;
  for (const Node* node : parser.all_nodes()) {
    if (input.contains_target(node->target().full_path())) {
      vector<string> node_out;
      node->FinalOutputs(&node_out);
      for (const string& output : node_out) {
        outputs.insert(output);
      }
      outputs.insert(node->target().make_path());
    }
  }
  out.WriteRule("all", strings::JoinAll(outputs, " "));

  // Not real files:
  out.WriteRule(".PHONY", "clean all");

  // Default build everything.
  out.append(".DEFAULT_GOAL=all\n\n");

  return out.out();
}

}  // namespace repobuild

// Copyright 2013
// Author: Christopher Van Arsdale

#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include <iterator>
#include "common/log/log.h"
#include "common/strings/path.h"
#include "common/strings/strutil.h"
#include "repobuild/env/input.h"
#include "repobuild/nodes/java_binary.h"
#include "repobuild/reader/buildfile.h"

using std::string;
using std::vector;
using std::set;

namespace repobuild {

void JavaBinaryNode::Parse(BuildFile* file, const BuildFileNode& input) {
  JavaLibraryNode::Parse(file, input);
  ParseRepeatedString(input, "java_manifest", &java_manifest_);
  bool have_main_class = false;
  for (const string& j : java_manifest_) {
    have_main_class |= strings::HasPrefix(j, "Main-Class:");
  }
  if (!have_main_class) {
    if (sources_.size() == 1) {
      string classname = strings::ReplaceAll(sources_[0].path(), "/", ".");
      classname = classname.substr(0, classname.size() - 5);  // remove .java
      java_manifest_.push_back("Main-class: " + classname);
    } else {
      LOG(FATAL) << "java_binary requires Main-class:... in "
                 << "java_binary.java_manifest[].";
    }
  }
}

void JavaBinaryNode::WriteMakefile(const vector<const Node*>& all_deps,
                                 Makefile* out) const {
  JavaLibraryNode::WriteMakefileInternal(all_deps, false, out);

  // Output binary
  Resource bin = Resource::FromLocalPath(input().object_dir(),
                                         target().make_path() + ".jar");
  WriteJar(all_deps, bin, out);

  {  // Output user target
    set<Resource> deps;
    deps.insert(bin);
    WriteBaseUserTarget(deps, out);
  }

  // Symlink to root dir.
  Resource out_bin = OutBinary();
  out->StartRule(out_bin.path(), bin.path());
  out->WriteCommand("pwd > /dev/null");  // hack to work around make issue?
  out->WriteCommand(
      strings::Join(
          "ln -f -s ",
          strings::JoinPath(input().object_dir(), target().make_path()),
          " ", out_bin.path()));
  out->FinishRule();
}

void JavaBinaryNode::WriteJar(
    const vector<const Node*>& all_deps,
    const Resource& file,
    Makefile* out) const {
  // Collect objects, and strip .obj-dir prefix.
  vector<Resource> objects;
  vector<string> class_paths;
  CollectObjects(all_deps, &objects);
  for (int i = 0; i < objects.size(); ++i) {
    // "jar" runs from the object directory.
    if (!strings::HasPrefix(objects[i].path(), input().object_dir() + "/")) {
      LOG(FATAL) << "Jar needs all objects to be in object_dir: "
                 << input().object_dir()
                 << "\", but have \""
                 << objects[i] << "\"";
    }
    class_paths.push_back(objects[i].path().substr(
        input().object_dir().size() + 1));
  }

  // Collect flags.
  set<string> flags;
  CollectLinkFlags(all_deps, &flags);
  for (const string& f : input().flags("-J")) {
    flags.insert(f);
  }

  // Write manifest file.
  Resource manifest = Resource::FromLocalPath(
      strings::JoinPath(input().genfile_dir(),
                        target().dir()),
      target().local_path() + ".manifest");
  string man_cmd = "eval 'rm " + manifest.path() + "; for line in";
  for (const string& str : java_manifest_) {
    man_cmd += " \"" + str + "\"";
  }
  man_cmd += "; do echo \"$$line\" >> " + manifest.path() + "; done; ";
  man_cmd += "touch " + manifest.path() + "'";
  out->StartRule(manifest.path(), strings::JoinAll(objects, " "));
  out->WriteCommand(man_cmd);
  out->FinishRule();

  // Jar rule
  out->StartRule(file.path(),
                 strings::JoinWith(" ",
                                   manifest.path(),
                                   strings::JoinAll(objects, " ")));
  out->WriteCommand("echo Jaring: " + file.path());
  out->WriteCommand(strings::JoinWith(
      " ",
      "cd " + input().object_dir(),
      "; jar cfm",
      "../" + file.path(),
      "../" + manifest.path(),
      strings::JoinAll(flags, " "),
      strings::JoinAll(class_paths, " ")));
  out->FinishRule();
}

void JavaBinaryNode::WriteMakeClean(const vector<const Node*>& all_deps,
                                  Makefile* out) const {
  out->WriteCommand("rm -f " + OutBinary().path());
  out->WriteCommand("rm -f " + JarName().path());
}

void JavaBinaryNode::FinalOutputs(vector<Resource>* outputs) const {
  outputs->push_back(OutBinary());
}

Resource JavaBinaryNode::OutBinary() const {
  return Resource::FromLocalPath(input().root_dir(), target().local_path());
}

Resource JavaBinaryNode::JarName() const {
  return Resource::FromLocalPath(input().root_dir(),
                                 target().local_path() + ".jar");
}

}  // namespace repobuild
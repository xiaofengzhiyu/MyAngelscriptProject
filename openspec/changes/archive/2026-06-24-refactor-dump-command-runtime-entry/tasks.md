## 1. Runtime Command Entry

- [x] 1.1 <!-- TDD --> Add a dump command registration regression under `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`.
- [x] 1.2 <!-- Non-TDD --> Move `as.DumpEngineState` command implementation from `AngelscriptTest` into `AngelscriptRuntime/Dump`.
- [x] 1.3 <!-- Non-TDD --> Remove the obsolete test-module command source from the build.

## 2. Verification

- [x] 2.1 Run the focused dump automation prefix with `Tools\RunTests.ps1`.
- [x] 2.2 Run a plugin build with `Tools\RunBuild.ps1`.
